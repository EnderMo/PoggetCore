#pragma once
#include <variant>
#include <string>
#include <memory>
#include <mutex>
#include "tsl/ordered_map.h"
#include "PoggetCoreContainer.hpp"
#include "Storage/PoggetStorage.hpp"

namespace PoggetCore {

    class PoggetStorageProvider {
    private:
        VinaStorage m_storage;
        std::wstring m_filePath;
        mutable std::mutex m_mutex;
        bool m_isInitialized = false;

        PoggetStorageProvider() = default;
        ~PoggetStorageProvider() = default;

    public:
        PoggetStorageProvider(const PoggetStorageProvider&) = delete;
        PoggetStorageProvider& operator=(const PoggetStorageProvider&) = delete;

        static PoggetStorageProvider& GetInstance() {
            static PoggetStorageProvider instance;
            return instance;
        }

        void Initialize(const std::wstring& filePath);
        bool IsInitialized() const;

        // 统一提交接口 各个 ContainerModel 触发 Save 时将增量/全量属性提交给 Provider
        void SubmitContainerModel(const ContainerModel& model);

        // 统一加载接口 根据 containerId 从 VinaStorage 加载到 ContainerModel
        bool LoadContainerModel(const std::wstring& containerId, ContainerModel& outModel);

        // 移除指定容器配置
        void RemoveContainerModel(const std::wstring& containerId);

		// 保存 Storage，多分辨率兼容
        void SaveStorage();

        // 直接访问全局 VinaStorage 代理 用于过渡兼容
        VinaStorage& GetRawStorage() { return m_storage; }
    };

} // namespace PoggetCore
