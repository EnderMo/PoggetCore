#pragma once
#include <string>
#include <functional>
#include "PoggetMetaTypes.hpp"

namespace PoggetMeta {

    class IPoggetMetaListener {
    public:
        virtual ~IPoggetMetaListener() = default;
        virtual void OnRunOnMainThread(std::function<void()> task) = 0;
        virtual void OnLog(const std::wstring& level, const std::wstring& msg) = 0;
        virtual void OnRefreshUI() = 0;
        virtual void OnFilePasteSuccess(const AsyncFileTask& task) = 0;
        virtual void OnFilePasteError(const AsyncFileTask& task) = 0;
        virtual void OnCopyAllComplete() = 0;
    };

}
