#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include "PoggetCoreContainer.hpp"

namespace PoggetCore {

    class PoggetContainerManager {
    private:
        std::unordered_map<std::wstring, std::shared_ptr<ContainerModel>> m_containers;
        mutable std::mutex m_mutex;

        PoggetContainerManager() = default;
        ~PoggetContainerManager() = default;

    public:
        PoggetContainerManager(const PoggetContainerManager&) = delete;
        PoggetContainerManager& operator=(const PoggetContainerManager&) = delete;

        static PoggetContainerManager& GetInstance() {
            static PoggetContainerManager instance;
            return instance;
        }

        std::shared_ptr<ContainerModel> CreateContainer(const std::wstring& id);
        void RegisterContainer(const std::wstring& id, std::shared_ptr<ContainerModel> model);
        std::shared_ptr<ContainerModel> GetContainer(const std::wstring& id) const;
        std::shared_ptr<ContainerModel> GetMergedHostByColor(const std::wstring& colorKey) const;
        void RemoveContainer(const std::wstring& id);
        std::vector<std::shared_ptr<ContainerModel>> GetAllContainers() const;
        void Clear();
    };

} // namespace PoggetCore
