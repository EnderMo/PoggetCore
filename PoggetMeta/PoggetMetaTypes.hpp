#pragma once
#include <string>
#include <functional>
#include <system_error>

namespace PoggetMeta {

    class IPoggetMetaListener;

    enum class MetaOpType {
        Copy,
        Move,
        Rename,
        Delete,
        Recycle,
        PrivateDelete,
        RecycleWithUndoBackup,
        // Public/Default profile items cannot always be renamed out of their
        // protected source directory.  This operation copies the item first
        // and removes the source only after the copy has completed.
        MoveByCopy
    };

    enum class DeleteDisposition {
        RecycleBin,
        Permanent
    };

    inline constexpr MetaOpType ResolveDeleteOperation(
        DeleteDisposition disposition,
        bool recordUndo) noexcept {
        if (disposition == DeleteDisposition::RecycleBin) {
            return recordUndo ? MetaOpType::RecycleWithUndoBackup : MetaOpType::Recycle;
        }
        return recordUndo ? MetaOpType::PrivateDelete : MetaOpType::Delete;
    }

    inline constexpr bool OperationRequiresDestination(MetaOpType operation) noexcept {
        return operation == MetaOpType::Copy || operation == MetaOpType::Move ||
            operation == MetaOpType::MoveByCopy ||
            operation == MetaOpType::Rename || operation == MetaOpType::PrivateDelete ||
            operation == MetaOpType::RecycleWithUndoBackup;
    }

    inline constexpr bool IsDestructiveOperation(MetaOpType operation) noexcept {
        return operation == MetaOpType::Delete || operation == MetaOpType::Recycle ||
            operation == MetaOpType::PrivateDelete ||
            operation == MetaOpType::RecycleWithUndoBackup;
    }

    struct AsyncFileTask {
        MetaOpType opType = MetaOpType::Copy;
        std::wstring src;
        std::wstring dest;
        std::wstring origSrc;
        int index = -1;
        float scaledX = 0;
        float scaledY = 0;
        void* targetWin = nullptr;
        int batchCollisionChoice = 0;
        uint64_t batchId = 0;
        std::wstring historyBackupRoot;
        std::wstring replacedBackupPath;
        std::wstring preflightError;
        // Optional deletion boundary. A destructive task is rejected unless src
        // is an immediate child of this exact directory.
        std::wstring requiredSourceParent;
        bool verifyContent = false;
        IPoggetMetaListener* listener = nullptr;
        bool notifyListener = true;
        std::function<void(std::function<void()>)> dispatchToMainThread = nullptr;
        // Optional UI-layer decision hook. It may marshal to the UI thread and
        // block this worker until the modal retry/cancel decision is complete.
        // PoggetMeta itself never creates or references GUI objects.
        std::function<bool(const std::wstring&, const std::error_code&)> requestRetry = nullptr;
        std::wstring failureReason;
        std::error_code failureError;
        // MoveByCopy reports a successful copy even when the final source
        // deletion is rejected, so the UI can keep the copied item and record
        // the operation as a copy instead of losing the destination.
        bool sourceDeleted = true;
        
        std::function<void()> onSuccess = nullptr;
        std::function<void()> onError = nullptr;
        std::function<void()> onSourceRetained = nullptr;
    };

}
