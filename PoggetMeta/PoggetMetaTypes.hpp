#pragma once
#include <string>
#include <functional>

namespace PoggetMeta {

    enum class MetaOpType {
        Copy,
        Move,
        Rename,
        Delete,
        Recycle
    };

    struct AsyncFileTask {
        MetaOpType opType = MetaOpType::Copy;
        std::wstring src;
        std::wstring dest;
        std::wstring origSrc;
        int index = -1;
        float scaledX = 0;
        float scaledY = 0;
        void* targetWin = nullptr;
        bool isMenuPaste = false; // Legacy fallback
        int batchCollisionChoice = 0;
        uint64_t batchId = 0;
        
        std::function<void()> onSuccess = nullptr;
        std::function<void()> onError = nullptr;
    };

}
