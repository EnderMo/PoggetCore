#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <filesystem>
#include "PoggetMetaTypes.hpp"
#include "IPoggetMetaListener.hpp"

namespace PoggetMeta {

    class PoggetMetaManager {
    public:
        static PoggetMetaManager& GetInstance();

        ~PoggetMetaManager();

        void SetListener(IPoggetMetaListener* listener) { m_listener = listener; }
        IPoggetMetaListener* GetListener() const { return m_listener; }

        bool IsCopying() const { return m_isCopying.load(); }
        void CancelCopy() { m_cancelCopy = true; } 
        bool IsCancelRequested() const { return m_cancelCopy.load(); }

        void SetCopyProgress(float p) { m_copyProgress = p; }
        float GetCopyProgress() const { return m_copyProgress.load(); }

        void SetCurrentFileName(const std::wstring& name) { 
            std::lock_guard<std::mutex> lock(m_fileNameMutex);
            m_currentFileName = name; 
        }
        std::wstring GetCurrentFileName() {
            std::lock_guard<std::mutex> lock(m_fileNameMutex);
            return m_currentFileName;
        }

        unsigned long GetLastTick() const { return m_lastTick; }
        void SetLastTick(unsigned long tick) { m_lastTick = tick; }

        // Legacy compatibility
        void PerformAsyncCopy(const std::vector<AsyncFileTask>& pasteQueue, int batchCollisionChoice);
        
        // Unified Queue API
        void SubmitTaskBatch(const std::vector<AsyncFileTask>& batch);

    private:
        PoggetMetaManager();
        void WorkerLoop();

        std::atomic<IPoggetMetaListener*> m_listener = nullptr;
        
        std::queue<AsyncFileTask> m_taskQueue;
        std::mutex m_queueMutex;
        std::condition_variable m_cv;
        std::thread m_workerThread;
        std::atomic<bool> m_exitFlag{false};

        std::atomic<uint64_t> m_currentBatchId{0};
        std::atomic<uint64_t> m_executingBatchId{0};
        
        std::atomic<bool> m_isCopying{ false };
        std::atomic<bool> m_cancelCopy{ false };
        std::atomic<float> m_copyProgress{ 0.0f };
        std::wstring m_currentFileName;
        std::mutex m_fileNameMutex;
        unsigned long m_lastTick = 0;
    };
}
