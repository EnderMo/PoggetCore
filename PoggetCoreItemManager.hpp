#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <set>
#include <mutex>

namespace PoggetCore {

    struct PoggetFileItem {
        int id = 0;
        std::wstring path;
        std::wstring alias;
        std::wstring originalPath;
        std::wstring cachedName;
        uint64_t cachedTime = 0;
        bool isDirectory = false;
        uint64_t cachedSize = 0;
        std::wstring cachedExtension;
        std::wstring sectionTitle;
        bool isMergedReplica = false;
    };

    class PoggetCoreItemManager {
    private:
        std::vector<PoggetFileItem> m_items;
        std::wstring m_containerId;
        mutable std::mutex m_mutex;

    public:
        PoggetCoreItemManager() = default;
        explicit PoggetCoreItemManager(const std::wstring& containerId) : m_containerId(containerId) {}
        ~PoggetCoreItemManager() = default;

        void SetContainerId(const std::wstring& id) { m_containerId = id; }
        std::wstring GetContainerId() const { return m_containerId; }

        void AddItem(const PoggetFileItem& item);
        void InsertItem(int position, const PoggetFileItem& item);
        bool RemoveItem(int id);
        bool RemoveItemByPath(const std::wstring& path);
        void ClearItems();
        void ClearReplicas();

        std::vector<PoggetFileItem> GetItems() const;
        size_t GetItemCount() const;
        PoggetFileItem* GetItemById(int id);

        // 纯逻辑物理文件夹同步
        void SyncWithPhysicalFolder(const std::wstring& folderPath);

        // 回调通知
        std::function<void()> OnItemsChanged;
    };

} // namespace PoggetCore
