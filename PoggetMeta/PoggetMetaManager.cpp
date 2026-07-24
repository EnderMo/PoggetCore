#include "PoggetMetaManager.hpp"
#include <chrono>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace PoggetMeta {

    static bool RestoreReplacedDestinationWithoutDataLoss(
        AsyncFileTask& task,
        IPoggetMetaListener* listener) {
        if (task.replacedBackupPath.empty()) return true;

        const std::filesystem::path backup = task.replacedBackupPath;
        const std::filesystem::path destination = task.dest;
        if (!PoggetCore::HistoryFileSystem::Exists(backup)) {
            if (listener) {
                listener->OnLog(L"ERROR",
                    L"Replacement backup is missing: " + backup.wstring());
            }
            return false;
        }

        if (PoggetCore::HistoryFileSystem::Exists(destination)) {
            auto recoveryRoot = destination.parent_path();
            if (recoveryRoot.empty()) {
                std::error_code ec;
                recoveryRoot = std::filesystem::current_path(ec);
            }
            const auto recovery = PoggetCore::HistoryFileSystem::MakeUniquePath(
                recoveryRoot, destination, L"pogget-recovered", true);
            if (!recovery.empty()) {
                auto preserveResult = PoggetCore::HistoryFileSystem::MovePath(backup, recovery);
                if (preserveResult) {
                    task.replacedBackupPath = recovery.wstring();
                    if (listener) {
                        listener->OnLog(L"ERROR",
                            L"Destination became occupied during rollback. The replaced item was "
                            L"preserved at: " + recovery.wstring());
                    }
                    return false;
                }
            }
            if (listener) {
                listener->OnLog(L"ERROR",
                    L"Rollback stopped because the destination is occupied. No existing path was "
                    L"deleted; replacement backup remains at: " + backup.wstring());
            }
            return false;
        }

        auto restoreResult = PoggetCore::HistoryFileSystem::MovePath(backup, destination);
        if (restoreResult) {
            task.replacedBackupPath.clear();
            return true;
        }
        if (listener) {
            listener->OnLog(L"ERROR",
                L"Failed to restore overwritten destination. Backup remains at: " +
                backup.wstring());
        }
        return false;
    }

    PoggetMetaManager& PoggetMetaManager::GetInstance() {
        static PoggetMetaManager instance;
        return instance;
    }

    PoggetMetaManager::PoggetMetaManager() {
        m_workerThread = std::thread(&PoggetMetaManager::WorkerLoop, this);
    }

    PoggetMetaManager::~PoggetMetaManager() {
        m_exitFlag = true;
        m_cv.notify_all();
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
    }

#ifdef _WIN32
    static DWORD CALLBACK MetaCopyProgressRoutine(
        LARGE_INTEGER TotalFileSize, LARGE_INTEGER TotalBytesTransferred,
        LARGE_INTEGER StreamSize, LARGE_INTEGER StreamBytesTransferred,
        DWORD dwStreamNumber, DWORD dwCallbackReason, HANDLE hSourceFile,
        HANDLE hDestinationFile, LPVOID lpData) 
    {
        PoggetMetaManager* mgr = static_cast<PoggetMetaManager*>(lpData);
        if (!mgr) return PROGRESS_CONTINUE;

        if (mgr->IsCancelRequested()) return PROGRESS_CANCEL;
        
        if (TotalFileSize.QuadPart > 0) {
            mgr->SetCopyProgress(static_cast<float>(TotalBytesTransferred.QuadPart) / TotalFileSize.QuadPart);
        }

        DWORD currentTick = GetTickCount();
        if (currentTick - mgr->GetLastTick() > 48) {
            mgr->SetLastTick(currentTick);
            if (mgr->GetListener()) {
                mgr->GetListener()->OnRunOnMainThread([mgr]() {
                    if (mgr->GetListener()) mgr->GetListener()->OnRefreshUI();
                });
            }
        }
        return PROGRESS_CONTINUE;
    }
#endif

    void PoggetMetaManager::PerformAsyncCopy(
        const std::vector<AsyncFileTask>& pasteQueue,
        int batchCollisionChoice,
        bool verifyContent) {
        std::vector<AsyncFileTask> adjustedBatch;
        for (auto t : pasteQueue) {
            t.batchCollisionChoice = batchCollisionChoice;
            t.verifyContent = verifyContent;
            t.opType = t.isMenuPaste ? MetaOpType::Move : MetaOpType::Copy;
            adjustedBatch.push_back(t);
        }
        SubmitTaskBatch(adjustedBatch);
    }

    void PoggetMetaManager::SubmitTaskBatch(const std::vector<AsyncFileTask>& batch) {
        if (batch.empty()) return;

        std::vector<AsyncFileTask> prepared = batch;
        std::wstring batchFailure;
        std::unordered_set<std::wstring> sources;
        std::unordered_set<std::wstring> destinations;
        std::vector<std::filesystem::path> transferSources;

        for (const auto& task : prepared) {
            const bool isTransfer = task.opType == MetaOpType::Copy ||
                task.opType == MetaOpType::Move || task.opType == MetaOpType::Rename;
            if (!isTransfer) continue;
            if (task.src.empty() || task.dest.empty()) {
                batchFailure = L"batch contains an empty transfer path";
                break;
            }

            const auto sourceKey = PoggetCore::HistoryFileSystem::ComparablePath(task.src);
            const auto destinationKey = PoggetCore::HistoryFileSystem::ComparablePath(task.dest);
            if (sourceKey == destinationKey) continue;
            if (!sources.insert(sourceKey).second) {
                batchFailure = L"batch contains the same source more than once: " + task.src;
                break;
            }
            if (!destinations.insert(destinationKey).second) {
                batchFailure = L"batch contains multiple items for the same destination: " + task.dest;
                break;
            }
            transferSources.emplace_back(task.src);
        }

        if (batchFailure.empty()) {
            for (size_t left = 0; left < transferSources.size() && batchFailure.empty(); ++left) {
                for (size_t right = left + 1; right < transferSources.size(); ++right) {
                    if (PoggetCore::HistoryFileSystem::IsPathInside(
                            transferSources[left], transferSources[right]) ||
                        PoggetCore::HistoryFileSystem::IsPathInside(
                            transferSources[right], transferSources[left])) {
                        batchFailure = L"batch contains overlapping parent and child sources";
                        break;
                    }
                }
            }
        }

        if (batchFailure.empty()) {
            for (const auto& task : prepared) {
                const bool isTransfer = task.opType == MetaOpType::Copy ||
                    task.opType == MetaOpType::Move || task.opType == MetaOpType::Rename;
                if (!isTransfer) continue;
                const auto sourceKey = PoggetCore::HistoryFileSystem::ComparablePath(task.src);
                const auto destinationKey = PoggetCore::HistoryFileSystem::ComparablePath(task.dest);
                if (sourceKey != destinationKey && sources.count(destinationKey) != 0) {
                    batchFailure = L"batch destination overlaps another source: " + task.dest;
                    break;
                }
            }
        }

        if (!batchFailure.empty()) {
            for (auto& task : prepared) task.preflightError = batchFailure;
        }

        uint64_t newBatchId = ++m_currentBatchId;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            for (auto t : prepared) {
                t.batchId = newBatchId;
                if (!t.listener) t.listener = m_listener.load();
                m_taskQueue.push(t);
            }
        }
        m_cv.notify_one();
    }

    void PoggetMetaManager::WorkerLoop() {
        while (!m_exitFlag) {
            AsyncFileTask task;
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_cv.wait(lock, [this]() { return !m_taskQueue.empty() || m_exitFlag; });

                if (m_exitFlag) break;

                task = m_taskQueue.front();
                m_taskQueue.pop();
            }

            // Detect new batch
            if (task.batchId != m_executingBatchId) {
                m_executingBatchId = task.batchId;
                m_cancelCopy = false; 
                m_isCopying = true;
            }

            IPoggetMetaListener* taskListener = task.listener;
            m_activeListener = taskListener;

            const bool cancelled = m_cancelCopy.load();

            SetCurrentFileName(std::filesystem::path(task.src).filename().wstring());
            SetCopyProgress(0.0f);

#ifdef _WIN32
            DWORD currentTick = GetTickCount();
#else
            auto now = std::chrono::steady_clock::now();
            unsigned long currentTick = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
#endif
            if (currentTick - m_lastTick > 48) {
                m_lastTick = currentTick;
                if (taskListener) {
                    taskListener->OnRunOnMainThread([taskListener]() {
                        taskListener->OnRefreshUI();
                    });
                }
            }

            std::error_code ec;
            bool success = false;

            try {
                if (!task.preflightError.empty()) {
                    if (taskListener) taskListener->OnLog(L"ERROR", task.preflightError);
                    success = false;
                }
                else if (cancelled) {
                    success = false;
                }
                else {
                std::filesystem::path replacedBackup;
                if (task.batchCollisionChoice == 1 &&
                    !task.dest.empty() &&
                    PoggetCore::HistoryFileSystem::ComparablePath(task.src) !=
                        PoggetCore::HistoryFileSystem::ComparablePath(task.dest) &&
                    PoggetCore::HistoryFileSystem::Exists(task.dest)) {
                    std::filesystem::path backupRoot = task.historyBackupRoot;
                    if (backupRoot.empty()) {
                        backupRoot = std::filesystem::temp_directory_path(ec) / L"PoggetUndo";
                    }
                    auto backupResult = PoggetCore::HistoryFileSystem::BackupDestination(
                        task.dest, backupRoot, replacedBackup);
                    if (!backupResult) {
                        throw std::filesystem::filesystem_error(
                            "Unable to back up overwritten destination",
                            task.dest,
                            backupResult.error);
                    }
                    task.replacedBackupPath = replacedBackup.wstring();
                }

                // Execute cross-platform file operation
                if (task.opType == MetaOpType::Move) {
                    auto result = PoggetCore::HistoryFileSystem::MovePath(
                        task.src, task.dest, task.verifyContent);
                    success = static_cast<bool>(result);
                    if (success && !result.context.empty() && taskListener) {
                        taskListener->OnLog(L"WARNING", result.context);
                    }
            } else if (task.opType == MetaOpType::Copy) {
                SetCopyProgress(0.5f);
                auto result = PoggetCore::HistoryFileSystem::CopyPath(
                    task.src, task.dest, task.verifyContent);
                success = static_cast<bool>(result);
            } else if (task.opType == MetaOpType::Rename) {
                auto result = PoggetCore::HistoryFileSystem::MovePath(
                    task.src, task.dest, task.verifyContent);
                success = static_cast<bool>(result);
                if (success && !result.context.empty() && taskListener) {
                    taskListener->OnLog(L"WARNING", result.context);
                }
            } else if (task.opType == MetaOpType::Delete) {
                auto result = PoggetCore::HistoryFileSystem::RemovePath(task.src);
                success = static_cast<bool>(result);
            } else if (task.opType == MetaOpType::Recycle) {
#ifdef _WIN32
                std::wstring doubleNullStr = task.src;
                doubleNullStr.push_back(L'\0');
                doubleNullStr.push_back(L'\0');
                SHFILEOPSTRUCTW fileOp = {0};
                fileOp.wFunc = FO_DELETE;
                fileOp.pFrom = doubleNullStr.c_str();
                fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
                int res = SHFileOperationW(&fileOp);
                success = (res == 0 && !fileOp.fAnyOperationsAborted &&
                    !PoggetCore::HistoryFileSystem::Exists(task.src));
#else
                auto result = PoggetCore::HistoryFileSystem::RemovePath(task.src);
                success = static_cast<bool>(result);
#endif
            }

                if (!success && !task.replacedBackupPath.empty() &&
                    PoggetCore::HistoryFileSystem::Exists(task.replacedBackupPath)) {
                    RestoreReplacedDestinationWithoutDataLoss(task, taskListener);
                }
                }

                        } catch (const std::exception& e) {
                RestoreReplacedDestinationWithoutDataLoss(task, taskListener);
                if (taskListener) {
                    std::string msg = e.what();
                    taskListener->OnLog(L"ERROR", L"Exception during async file task: " + std::wstring(msg.begin(), msg.end()));
                }
                success = false;
            } catch (...) {
                RestoreReplacedDestinationWithoutDataLoss(task, taskListener);
                if (taskListener) taskListener->OnLog(L"ERROR", L"Unknown exception during async file task.");
                success = false;
            }

            if (success) {
                auto successCallback = [taskListener, task]() {
                        if (task.onSuccess) task.onSuccess();
                        if (task.notifyListener && taskListener) {
                            taskListener->OnFilePasteSuccess(task);
                        }
                    };
                if (task.dispatchToMainThread) {
                    task.dispatchToMainThread(std::move(successCallback));
                }
                else if (taskListener) {
                    taskListener->OnRunOnMainThread(std::move(successCallback));
                }
            } else {
                auto errorCallback = [taskListener, task]() {
                    if (task.onError) task.onError();
                    if (task.notifyListener && taskListener) {
                        taskListener->OnFilePasteError(task);
                    }
                };
                if (taskListener) {
                    taskListener->OnLog(L"ERROR", L"Failed async file task: " + task.src);
                }
                if (task.dispatchToMainThread) {
                    task.dispatchToMainThread(std::move(errorCallback));
                }
                else if (taskListener) {
                    taskListener->OnRunOnMainThread(std::move(errorCallback));
                }
            }

            // Complete each batch independently, even if another batch is queued.
            bool isQueueEmpty = false;
            bool isBatchComplete = false;
            {
                std::lock_guard<std::mutex> qlock(m_queueMutex);
                isQueueEmpty = m_taskQueue.empty();
                isBatchComplete = isQueueEmpty ||
                    m_taskQueue.front().batchId != task.batchId;
            }

            if (isBatchComplete) {
                if (isQueueEmpty) m_isCopying = false;
                if (task.notifyListener && taskListener) {
                    taskListener->OnRunOnMainThread([taskListener]() {
                        taskListener->OnCopyAllComplete();
                    });
                }
            }
            m_activeListener = nullptr;
        }
    }
}
