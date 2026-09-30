#include "PoggetMetaManager.hpp"
#include <chrono>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace PoggetMeta {

    static const wchar_t* MetaOperationName(MetaOpType operation) noexcept {
        switch (operation) {
        case MetaOpType::Copy: return L"Copy";
        case MetaOpType::Move: return L"Move";
        case MetaOpType::MoveByCopy: return L"MoveByCopy";
        case MetaOpType::Rename: return L"Rename";
        case MetaOpType::Delete: return L"Delete";
        case MetaOpType::Recycle: return L"Recycle";
        case MetaOpType::PrivateDelete: return L"PrivateDelete";
        case MetaOpType::RecycleWithUndoBackup: return L"RecycleWithUndoBackup";
        }
        return L"Unknown";
    }

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

    void PoggetMetaManager::SubmitTransferBatch(
        const std::vector<AsyncFileTask>& transferQueue,
        int batchCollisionChoice,
        bool verifyContent) {
        std::vector<AsyncFileTask> adjustedBatch;
        adjustedBatch.reserve(transferQueue.size());
        for (auto t : transferQueue) {
            t.batchCollisionChoice = batchCollisionChoice;
            t.verifyContent = verifyContent;
            adjustedBatch.push_back(std::move(t));
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
        std::vector<PoggetCore::HistoryFileSystem::DestructiveRequest> destructiveRequests;

        for (const auto& task : prepared) {
            const bool isTransfer = OperationRequiresDestination(task.opType);
            const bool isDestructive = IsDestructiveOperation(task.opType);
            if (isDestructive) {
                destructiveRequests.push_back({ task.src, task.requiredSourceParent });
            }
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

        if (batchFailure.empty() && !destructiveRequests.empty()) {
            const auto validation =
                PoggetCore::HistoryFileSystem::ValidateDestructiveBatch(destructiveRequests);
            if (!validation) batchFailure = validation.context;
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
                const bool isTransfer = OperationRequiresDestination(task.opType);
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
                if (!t.listener && t.notifyListener) t.listener = m_listener.load();
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
            bool retryRequested = false;
            do {
                success = false;
                retryRequested = false;
                task.failureReason.clear();
                task.failureError.clear();

                auto acceptResult = [&](const PoggetCore::HistoryFileSystem::Result& result) {
                    success = static_cast<bool>(result);
                    if (success) {
                        if (!result.context.empty() && taskListener)
                            taskListener->OnLog(L"WARNING", result.context);
                        return;
                    }
                    task.failureReason = result.context.empty()
                        ? L"file-system operation failed" : result.context;
                    task.failureError = result.error;
                    if (result.error) {
                        const auto message = result.error.message();
                        if (!message.empty()) {
                            task.failureReason += L" (" +
                                std::wstring(message.begin(), message.end()) + L")";
                        }
                    }
                };

                try {
                    if (!task.preflightError.empty()) {
                        task.failureReason = task.preflightError;
                        if (taskListener) taskListener->OnLog(L"ERROR", task.preflightError);
                    }
                    else if (cancelled) {
                        task.failureReason = L"operation was cancelled";
                    }
                    else {
                        const bool isDestructive = IsDestructiveOperation(task.opType);
                        if (isDestructive) {
                            const auto validation =
                                PoggetCore::HistoryFileSystem::ValidateDestructiveBatch(
                                    { { task.src, task.requiredSourceParent } });
                            if (!validation) {
                                task.failureReason = validation.context;
                                if (taskListener)
                                    taskListener->OnLog(L"ERROR", validation.context);
                            }
                        }

                        if (task.failureReason.empty() && task.batchCollisionChoice == 1 &&
                            !task.dest.empty() &&
                            PoggetCore::HistoryFileSystem::ComparablePath(task.src) !=
                                PoggetCore::HistoryFileSystem::ComparablePath(task.dest) &&
                            PoggetCore::HistoryFileSystem::Exists(task.dest)) {
                            std::filesystem::path backupRoot = task.historyBackupRoot;
                            if (backupRoot.empty()) {
                                backupRoot = std::filesystem::temp_directory_path(ec) / L"PoggetUndo";
                            }
                            std::filesystem::path replacedBackup;
                            const auto backupResult =
                                PoggetCore::HistoryFileSystem::BackupDestination(
                                    task.dest, backupRoot, replacedBackup);
                            if (!backupResult) {
                                acceptResult(backupResult);
                            }
                            else {
                                task.replacedBackupPath = replacedBackup.wstring();
                            }
                        }

                        if (task.failureReason.empty()) {
                            PoggetLogger::Log(L"PoggetMeta", L"INFO",
                                L"Executing async file task: operation=" +
                                std::wstring(MetaOperationName(task.opType)) +
                                L" source=\"" + task.src + L"\" destination=\"" +
                                task.dest + L"\"");
                            if (task.opType == MetaOpType::Move) {
                                acceptResult(PoggetCore::HistoryFileSystem::MovePath(
                                    task.src, task.dest, task.verifyContent));
                            }
                            else if (task.opType == MetaOpType::MoveByCopy) {
                                const auto result =
                                    PoggetCore::HistoryFileSystem::MovePathWithSharedProfileFallback(
                                        task.src, task.dest, task.verifyContent);
                                task.sourceDeleted = !result.sourceRetained;
                                acceptResult(result);
                            }
                            else if (task.opType == MetaOpType::Copy) {
                                SetCopyProgress(0.5f);
                                acceptResult(PoggetCore::HistoryFileSystem::CopyPath(
                                    task.src, task.dest, task.verifyContent));
                            }
                            else if (task.opType == MetaOpType::Rename) {
                                acceptResult(PoggetCore::HistoryFileSystem::MovePath(
                                    task.src, task.dest, task.verifyContent));
                            }
                            else if (task.opType == MetaOpType::Delete) {
                                acceptResult(PoggetCore::HistoryFileSystem::RemovePath(task.src));
                            }
                            else if (task.opType == MetaOpType::Recycle) {
                                acceptResult(PoggetCore::HistoryFileSystem::RecyclePath(task.src));
                            }
                            else if (task.opType == MetaOpType::PrivateDelete) {
                                acceptResult(PoggetCore::HistoryFileSystem::MovePath(
                                    task.src, task.dest, task.verifyContent));
                            }
                            else if (task.opType == MetaOpType::RecycleWithUndoBackup) {
                                acceptResult(
                                    PoggetCore::HistoryFileSystem::RecyclePathWithUndoBackup(
                                        task.src, task.dest, task.verifyContent));
                            }
                        }

                        if (!success && !task.replacedBackupPath.empty() &&
                            PoggetCore::HistoryFileSystem::Exists(task.replacedBackupPath)) {
                            if (!RestoreReplacedDestinationWithoutDataLoss(task, taskListener)) {
                                task.failureReason +=
                                    L"; automatic rollback needs attention before retrying";
                            }
                        }
                    }
                }
                catch (const std::exception& e) {
                    RestoreReplacedDestinationWithoutDataLoss(task, taskListener);
                    const std::string message = e.what();
                    task.failureReason = L"Exception during async file task: " +
                        std::wstring(message.begin(), message.end());
                    if (taskListener) taskListener->OnLog(L"ERROR", task.failureReason);
                    success = false;
                }
                catch (...) {
                    RestoreReplacedDestinationWithoutDataLoss(task, taskListener);
                    task.failureReason = L"Unknown exception during async file task";
                    if (taskListener) taskListener->OnLog(L"ERROR", task.failureReason);
                    success = false;
                }

                const bool retryIsSafe = task.replacedBackupPath.empty();
                if (!success && !cancelled && retryIsSafe && task.requestRetry) {
                    try {
                        retryRequested = task.requestRetry(task.failureReason, task.failureError);
                    }
                    catch (...) {
                        retryRequested = false;
                        if (taskListener)
                            taskListener->OnLog(L"ERROR", L"Retry decision callback failed");
                    }
                }
            } while (retryRequested);

            if (success) {
                auto successCallback = [taskListener, task]() {
                        if (task.sourceDeleted == false && task.onSourceRetained)
                            task.onSourceRetained();
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
