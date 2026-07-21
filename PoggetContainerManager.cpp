#include "../framework.h"
#include "PoggetContainerManager.hpp"

namespace PoggetCore {

    std::shared_ptr<ContainerModel> PoggetContainerManager::CreateContainer(const std::wstring& id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_containers.find(id);
        if (it != m_containers.end()) {
            return it->second;
        }
        auto model = std::make_shared<ContainerModel>();
        model->id = id;
        m_containers[id] = model;
        return model;
    }

    void PoggetContainerManager::RegisterContainer(const std::wstring& id, std::shared_ptr<ContainerModel> model) {
        if (id.empty() || !model) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        model->id = id;
        m_containers[id] = model;
    }

    std::shared_ptr<ContainerModel> PoggetContainerManager::GetContainer(const std::wstring& id) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_containers.find(id);
        if (it != m_containers.end()) {
            return it->second;
        }
        return nullptr;
    }

    std::shared_ptr<ContainerModel> PoggetContainerManager::GetMergedHostByColor(const std::wstring& colorKey) const {
        if (colorKey.empty()) return nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& pair : m_containers) {
            const auto& model = pair.second;
            if (model && model->IsMergedHost && model->MergedHostColor == colorKey) {
                return model;
            }
        }
        return nullptr;
    }

    void PoggetContainerManager::RemoveContainer(const std::wstring& id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_containers.erase(id);
    }

    std::vector<std::shared_ptr<ContainerModel>> PoggetContainerManager::GetAllContainers() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::shared_ptr<ContainerModel>> result;
        result.reserve(m_containers.size());
        for (const auto& pair : m_containers) {
            result.push_back(pair.second);
        }
        return result;
    }

    void PoggetContainerManager::Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_containers.clear();
    }

} // namespace PoggetCore
