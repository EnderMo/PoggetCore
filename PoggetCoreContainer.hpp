#pragma once
#include <string>
#include <vector>
#include <set>
#include <functional>
#include <filesystem>
#include <memory>
#include "PoggetCoreItemManager.hpp"

namespace PoggetCore {

    struct CoreFileOp {
        int type = 0; // 0: 物理添加, 1: 物理删除, 2: 物理重命名, 3: 虚拟添加, 4: 虚拟删除, 5: 虚拟别名修改
        std::wstring path1;
        std::wstring path2;
        std::wstring alias;
        std::wstring originContainerId;
        std::vector<CoreFileOp> subOps;
    };

    class ContainerModel {
    public:
        // 核心标识与路径
        std::wstring id;
        std::wstring alias;
        std::wstring title = L"Unnamed";
        bool IsMinimized = false;
        bool IsIntegrated = false;
        bool UseTargetFolder = false;
        std::wstring TargetFolder = L"vui!NULL";
        bool IsCustomTarget = false;

        // 逻辑文件项管理器
        std::shared_ptr<PoggetCoreItemManager> itemManager = std::make_shared<PoggetCoreItemManager>();

        // 视图与排版逻辑状态
        int SortMode = 0; // 0: 自由, 1: 名称正序, 2: 名称倒序, 3: 时间正序, 4: 时间倒序
        int IconSizeLevel = 3;
        bool ShowDefaultFolderIcon = false;
        bool ShowTextPreview = true;
        int HideFileExtension = 0; // 0,1,2
        int IconSpacingMode = 0;
        int IconSpacingType = 0;
        bool IsListView = false;
        int TextRenderMode = 0;
        int FileNameColorMode = 0;
        int TitleType = 0;
        int TagStyle = 0;
        int ListDetailType = 0;
        std::wstring fontFamily = L"Segoe UI";
        bool DisableLayoutAnimations = false;

        // 模式与状态标志
        bool IsLocked = false;
        bool IsEmbeddedLayer = false;
        bool IsUpwardExpand = false;
        int TransparentFrameMode = 0;
        int AutoHideTitleBar = 0;
        int AutoFade = 0;
        bool AutoFadeDragStat = false;
        int FolderOpenMode = 0;
        bool SwipeUpSearchEnabled = true;
        bool IsSectionCollapsed = false;
        float backgroundAlpha = 1.0f;
        float titleAlpha = 1.0f;
        int CompatibleLayer = 0;
        bool EnableStaticMaterialLayerForD3D11 = false;
        bool EnableDynamicMaterialLayerForD3D11 = false;
        float cornerRad = 16.0f;
        float shadowBlur = 16.0f;
        float shadowAlpha = 0.2f;
        float shadowOffsetX = 0.0f;
        float shadowOffsetY = 4.0f;

        // 搜索与文件夹内联视图逻辑状态
        bool IsSearchMode = false;
        std::wstring searchText = L"";
        bool EnableInlineFolderView = true;
        bool IsInInlineFolderView = false;
        std::wstring InlineFolderPath = L"";
        std::vector<std::wstring> InlineFolderHistory;

        // 选中状态管理 (Core 层集中处理) 
        bool IsSelectMode = false;
        std::set<int> SelectedIconIDs;

        // 撤销/重做逻辑状态
        int maxHistory = 5;
        std::vector<CoreFileOp> undoStack;
        std::vector<CoreFileOp> redoStack;

        // 回调事件
        // 当数据发生改变，通知外层 UI 刷新
        std::function<void()> OnDataChanged;

        // 批量选中 API
        void ClearSelection() {
            SelectedIconIDs.clear();
            if (OnDataChanged) OnDataChanged();
        }

        void SetSelection(const std::set<int>& ids) {
            SelectedIconIDs = ids;
            if (OnDataChanged) OnDataChanged();
        }

        void ToggleSelection(int iconId) {
            if (SelectedIconIDs.count(iconId)) {
                SelectedIconIDs.erase(iconId);
            } else {
                SelectedIconIDs.insert(iconId);
            }
            if (OnDataChanged) OnDataChanged();
        }

        void PushUndo(const CoreFileOp& op) {
            undoStack.push_back(op);
            if (undoStack.size() > static_cast<size_t>(maxHistory)) {
                auto& oldest = undoStack.front();
                if (oldest.type == 1 && std::filesystem::exists(oldest.path2)) {
                    std::error_code ec;
                    std::filesystem::remove(oldest.path2, ec);
                }
                for (const auto& subOp : oldest.subOps) {
                    if (subOp.type == 1 && std::filesystem::exists(subOp.path2)) {
                        std::error_code ec;
                        std::filesystem::remove(subOp.path2, ec);
                    }
                }
                undoStack.erase(undoStack.begin());
            }
            for (auto& r : redoStack) {
                if (r.type == 1 && std::filesystem::exists(r.path2)) {
                    std::error_code ec;
                    std::filesystem::remove(r.path2, ec);
                }
                for (const auto& subOp : r.subOps) {
                    if (subOp.type == 1 && std::filesystem::exists(subOp.path2)) {
                        std::error_code ec;
                        std::filesystem::remove(subOp.path2, ec);
                    }
                }
            }
            redoStack.clear();

            if (OnDataChanged) {
                OnDataChanged();
            }
        }

        void Save();
    };

} // namespace PoggetCore
