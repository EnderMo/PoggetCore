#include "../framework.h"
#include "PoggetCoreItemManager.hpp"
#include <filesystem>
#include <algorithm>

namespace PoggetCore {

    void PoggetCoreItemManager::AddItem(const PoggetFileItem& item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        PoggetFileItem newItem = item;
        if (newItem.id == 0) {
            newItem.id = static_cast<int>(m_items.size()) + 1;
        }
        m_items.push_back(newItem);
        if (OnItemsChanged) {
            OnItemsChanged();
        }
    }

    void PoggetCoreItemManager::InsertItem(int position, const PoggetFileItem& item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (position < 0 || position > static_cast<int>(m_items.size())) {
            position = static_cast<int>(m_items.size());
        }
        PoggetFileItem newItem = item;
        m_items.insert(m_items.begin() + position, newItem);
        for (size_t i = 0; i < m_items.size(); ++i) {
            m_items[i].id = static_cast<int>(i);
        }
        if (OnItemsChanged) {
            OnItemsChanged();
        }
    }

    bool PoggetCoreItemManager::RemoveItem(int id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::remove_if(m_items.begin(), m_items.end(), [id](const PoggetFileItem& item) {
            return item.id == id;
        });
        if (it != m_items.end()) {
            m_items.erase(it, m_items.end());
            if (OnItemsChanged) {
                OnItemsChanged();
            }
            return true;
        }
        return false;
    }

    bool PoggetCoreItemManager::RemoveItemByPath(const std::wstring& path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::remove_if(m_items.begin(), m_items.end(), [&path](const PoggetFileItem& item) {
            return item.path == path;
        });
        if (it != m_items.end()) {
            m_items.erase(it, m_items.end());
            if (OnItemsChanged) {
                OnItemsChanged();
            }
            return true;
        }
        return false;
    }

    void PoggetCoreItemManager::ClearItems() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_items.clear();
        if (OnItemsChanged) {
            OnItemsChanged();
        }
    }

    void PoggetCoreItemManager::ClearReplicas() {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::remove_if(m_items.begin(), m_items.end(), [](const PoggetFileItem& item) {
            return item.isMergedReplica;
        });
        if (it != m_items.end()) {
            m_items.erase(it, m_items.end());
            for (size_t i = 0; i < m_items.size(); ++i) {
                m_items[i].id = static_cast<int>(i);
            }
            if (OnItemsChanged) {
                OnItemsChanged();
            }
        }
    }

    std::vector<PoggetFileItem> PoggetCoreItemManager::GetItems() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items;
    }

    size_t PoggetCoreItemManager::GetItemCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.size();
    }

    PoggetFileItem* PoggetCoreItemManager::GetItemById(int id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& item : m_items) {
            if (item.id == id) return &item;
        }
        return nullptr;
    }

    void PoggetCoreItemManager::SyncWithPhysicalFolder(const std::wstring& folderPath) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (folderPath.empty() || folderPath == L"vui!NULL") return;
        std::error_code ec;
        if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec)) return;

        std::vector<PoggetFileItem> updatedItems;
        int nextId = 1;
        for (const auto& entry : std::filesystem::directory_iterator(folderPath, ec)) {
            PoggetFileItem item;
            item.id = nextId++;
            item.path = entry.path().wstring();
            item.alias = entry.path().filename().wstring();
            item.cachedName = item.alias;
            item.isDirectory = entry.is_directory(ec);
            if (!item.isDirectory) {
                item.cachedExtension = entry.path().extension().wstring();
                item.cachedSize = entry.file_size(ec);
            }
            updatedItems.push_back(item);
        }
        m_items = std::move(updatedItems);
        if (OnItemsChanged) {
            OnItemsChanged();
        }
    }

} // namespace PoggetCore
