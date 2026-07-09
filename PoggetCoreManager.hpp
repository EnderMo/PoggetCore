#pragma once
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <set>
#include <map>
#include <algorithm>
#include <filesystem>
#include "PoggetMeta/PoggetMeta.hpp"
#include "PoggetMeta/PoggetMetaManager.hpp"
#include "PoggetContainerManager.hpp"
#include "PoggetStorageProvider.hpp"
#include "PoggetCoreItemManager.hpp"

namespace PoggetCore {

    struct CoreContainerConfig {
        bool DisableLayoutAnimations = false;
        bool IsIntegrated = false;
        int SortMode = 0;
        int IconSpacingMode = 0;
        int IconSpacingType = 0;
        bool IsListView = false;
        bool IsInInlineFolderView = false;
        bool IsSearchMode = false;
        std::wstring searchText = L"";
        std::wstring title = L"";
        bool IsSectionCollapsed = false;
        bool ShowTextPreview = true;
    };

    struct CoreIconLayoutData {
        std::wstring path;
        std::wstring alias;
        std::wstring originalPath;
        std::wstring cachedName;
        uint64_t cachedTime = 0;
        bool isDirectory = false;
        uint64_t cachedSize = 0;
        std::wstring cachedExtension;
        std::wstring sectionTitle;
        void* originWindow = nullptr;
        void* originalIconPtr = nullptr; // back reference to IconData

        int originOrder = 0;

        float targetX = -100.0f;
        float targetY = -100.0f;
        float customWidth = -1.0f;
        bool isCollapsed = false;
        bool isVisible = true;
    };

    struct CoreSectionHeaderLayoutData {
        void* originWindow = nullptr;
        std::wstring title;
        int x1 = 0;
        int y1 = 0;
        int x2 = 0;
        int y2 = 0;
    };

    class PoggetCoreManager {
    public:
        PoggetCoreManager() = default;
        virtual ~PoggetCoreManager() = default;

        // 表面便捷接口
        static PoggetContainerManager& Containers() { return PoggetContainerManager::GetInstance(); }
        static PoggetStorageProvider& Storage() { return PoggetStorageProvider::GetInstance(); }

        static bool MatchWildcard(const std::wstring& pattern, const std::wstring& text) {
            std::wstring p = pattern, t = text;
            std::transform(p.begin(), p.end(), p.begin(), ::towlower);
            std::transform(t.begin(), t.end(), t.begin(), ::towlower);

            size_t n = t.size(), m = p.size();
            size_t i = 0, j = 0;
            size_t startIndex = static_cast<size_t>(-1);
            size_t match = 0;

            while (i < n) {
                if (j < m && (p[j] == L'?' || p[j] == t[i])) {
                    i++; j++;
                } else if (j < m && p[j] == L'*') {
                    startIndex = j++;
                    match = i;
                } else if (startIndex != static_cast<size_t>(-1)) {
                    j = startIndex + 1;
                    i = ++match;
                } else {
                    return false;
                }
            }
            while (j < m && p[j] == L'*') j++;
            return j == m;
        }

        static void CalculatePositionsCore(
            std::vector<CoreIconLayoutData>& icons,
            std::vector<CoreSectionHeaderLayoutData>& outHeaders,
            void* containerWin,
            int containerWidth,
            int startX,
            int startY,
            int iconSize,
            int gap,
            bool isSearchManager,
            bool isInlineManager,
            const std::function<CoreContainerConfig(void*)>& getConfig,
            const std::function<bool(void*, const std::wstring&)>& isSectionCollapsedInSearch
        );
    };

}
