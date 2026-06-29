#include "PoggetMetaManager.hpp"
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace PoggetMeta {

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

    void PoggetMetaManager::PerformAsyncCopy(const std::vector<AsyncFileTask>& pasteQueue, int batchCollisionChoice) {
        std::vector<AsyncFileTask> adjustedBatch;
        for (auto t : pasteQueue) {
            t.batchCollisionChoice = batchCollisionChoice;
            t.opType = t.isMenuPaste ? MetaOpType::Move : MetaOpType::Copy;
            adjustedBatch.push_back(t);
        }
        SubmitTaskBatch(adjustedBatch);
    }

    void PoggetMetaManager::SubmitTaskBatch(const std::vector<AsyncFileTask>& batch) {
        if (batch.empty()) return;
        
        uint64_t newBatchId = ++m_currentBatchId;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            for (auto t : batch) {
                t.batchId = newBatchId;
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

            if (m_cancelCopy) continue; // Skip remaining tasks of cancelled batch

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
                if (m_listener) {
                    m_listener.load()->OnRunOnMainThread([this]() {
                        if (m_listener) m_listener.load()->OnRefreshUI();
                    });
                }
            }

            std::error_code ec;
            bool success = false;

            try {
                // Execute cross-platform file operation
                if (task.opType == MetaOpType::Move) {
                if (task.batchCollisionChoice == 1 && std::filesystem::exists(task.dest)) {
                    std::filesystem::remove(task.dest, ec);
                }
                std::filesystem::rename(task.src, task.dest, ec);
                if (!ec) {
                    success = true;
                } else {
                    if (std::filesystem::is_directory(task.src, ec)) {
                        SetCopyProgress(0.5f);
                        std::filesystem::copy(task.src, task.dest, std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive, ec);
                        if (!ec) {
                            std::filesystem::remove_all(task.src, ec);
                            success = true;
                        }
                    } else {
#ifdef _WIN32
                        BOOL bCancel = FALSE;
                        if (CopyFileExW(task.src.c_str(), task.dest.c_str(), MetaCopyProgressRoutine, this, &bCancel, 0)) {
                            std::filesystem::remove(task.src, ec);
                            success = true;
                        }
#else
                        std::filesystem::copy(task.src, task.dest, std::filesystem::copy_options::overwrite_existing, ec);
                        if (!ec) {
                            std::filesystem::remove(task.src, ec);
                            success = true;
                        }
#endif
                    }
                }
            } else if (task.opType == MetaOpType::Copy) {
                if (std::filesystem::is_directory(task.src, ec)) {
                    SetCopyProgress(0.5f);
                    std::filesystem::copy(task.src, task.dest, std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive, ec);
                    success = !ec;
                } else {
#ifdef _WIN32
                    BOOL bCancel = FALSE;
                    success = CopyFileExW(task.src.c_str(), task.dest.c_str(), MetaCopyProgressRoutine, this, &bCancel, 0);
#else
                    std::filesystem::copy(task.src, task.dest, std::filesystem::copy_options::overwrite_existing, ec);
                    success = !ec;
#endif
                }
            } else if (task.opType == MetaOpType::Rename) {
                if (task.batchCollisionChoice == 1 && std::filesystem::exists(task.dest)) {
                    std::filesystem::remove(task.dest, ec);
                }
                std::filesystem::rename(task.src, task.dest, ec);
                success = !ec;
            } else if (task.opType == MetaOpType::Delete) {
                if (std::filesystem::is_directory(task.src, ec)) {
                    std::filesystem::remove_all(task.src, ec);
                } else {
                    std::filesystem::remove(task.src, ec);
                }
                success = !ec;
            } else if (task.opType == MetaOpType::Recycle) {
#ifdef _WIN32
                std::wstring doubleNullStr = task.src + L"\0";
                SHFILEOPSTRUCTW fileOp = {0};
                fileOp.wFunc = FO_DELETE;
                fileOp.pFrom = doubleNullStr.c_str();
                fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
                int res = SHFileOperationW(&fileOp);
                success = (res == 0);
#else
                if (std::filesystem::is_directory(task.src, ec)) std::filesystem::remove_all(task.src, ec);
                else std::filesystem::remove(task.src, ec);
                success = !ec;
#endif
            }

                        } catch (const std::exception& e) {
                if (m_listener) {
                    std::string msg = e.what();
                    m_listener.load()->OnLog(L"ERROR", L"Exception during async file task: " + std::wstring(msg.begin(), msg.end()));
                }
                success = false;
            } catch (...) {
                if (m_listener) m_listener.load()->OnLog(L"ERROR", L"Unknown exception during async file task.");
                success = false;
            }

            if (success) {
                if (m_listener) {
                    m_listener.load()->OnRunOnMainThread([this, task]() {
                        if (task.onSuccess) task.onSuccess();
                        if (m_listener) m_listener.load()->OnFilePasteSuccess(task);
                    });
                }
            } else {
                if (m_listener) {
                    m_listener.load()->OnLog(L"ERROR", L"Failed async file task: " + task.src);
                    m_listener.load()->OnRunOnMainThread([this, task]() {
                        if (task.onError) task.onError();
                        if (m_listener) m_listener.load()->OnFilePasteError(task);
                    });
                }
            }

            // Check if queue empty -> End of Batch
            bool isQueueEmpty = false;
            {
                std::lock_guard<std::mutex> qlock(m_queueMutex);
                isQueueEmpty = m_taskQueue.empty();
            }

            if (isQueueEmpty) {
                m_isCopying = false;
                if (m_listener) {
                    m_listener.load()->OnRunOnMainThread([this]() {
                        if (m_listener) m_listener.load()->OnCopyAllComplete();
                    });
                }
            }
        }
    }
}
