#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include <set>
#include <functional>
#include <filesystem>
#include <memory>
#include "PoggetCoreItemManager.hpp"
#include "PoggetHistoryFileSystem.hpp"

namespace PoggetCore {

    struct CoreFileOp {
        int type = 0; // 0: physical create/copy, 1: delete, 2: rename, 3-5: virtual, 6: move, 7: virtual path
        std::wstring path1;
        std::wstring path2;
        std::wstring alias;
        std::wstring originContainerId;
        std::vector<CoreFileOp> subOps;
        std::wstring path3; // Backup of a destination replaced by the operation.
    };

    class ContainerModel {
    public:
        // 核心标识与路径
        std::wstring id;
        std::wstring alias;
        std::wstring title = L"Unnamed";
        bool IsMinimized = false;
        bool IsIntegrated = false;
        // A merged host owns presentation state only. File items always belong
        // to the member containers referenced by MergedHostId.
        bool IsMergedHost = false;
        std::wstring MergedHostColor;
        std::wstring MergedHostId;
        bool UseTargetFolder = false;
        std::wstring TargetFolder = L"vui!NULL";
        bool IsCustomTarget = false;

        // 逻辑文件项管理器
		// Logic file items are managed by PoggetCoreItemManager

        std::shared_ptr<PoggetCoreItemManager> itemManager = std::make_shared<PoggetCoreItemManager>();

        // 视图与排版逻辑状态

        // SortMode 0: 自由, 1: 名称正序, 2: 名称倒序, 3: 时间正序, 4: 时间倒序
		// SortMode 0: Free, 1: Name Ascending, 2: Name Descending, 3: Time Ascending, 4: Time Descending
        int SortMode = 0; 

        int IconSizeLevel = 3;
        bool ShowDefaultFolderIcon = false;
        bool ShowTextPreview = true;
        bool ShowMediaPreview = true;

		// HideFileExtension 0: 显示, 1: 隐藏扩展名, 2: 隐藏文件名
		// HideFileExtension 0: Show, 1: Hide Extension, 2: Hide Filename
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
        float textSize = 10.0f;
        bool DisableLayoutAnimations = false;

        // 模式与状态标志
		// Mode and state flags

        bool IsLocked = false;
        bool IsEmbeddedLayer = false;
        bool IsUpwardExpand = false;
        int TransparentFrameMode = 0;
        int AutoHideTitleBar = 0;
        int QuickExpandCollapse = 1;
        int AutoFade = 0;
        bool FadeOutSwitch = false;
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
		// Search and inline folder view logic state

        bool IsSearchMode = false;
        std::wstring searchText = L"";
        bool EnableInlineFolderView = true;
        bool IsInInlineFolderView = false;
        std::wstring InlineFolderPath = L"";
        std::vector<std::wstring> InlineFolderHistory;

        // 选中状态管理 (Core 层集中处理) 
		// Selection state management (handled centrally in the Core layer)

        bool IsSelectMode = false;
        std::set<int> SelectedIconIDs;

        // 撤销/重做逻辑状态
		// Undo/Redo logic state

        int maxHistory = 100;
        std::vector<CoreFileOp> undoStack;
        std::vector<CoreFileOp> redoStack;
        bool historyOperationInProgress = false;

        // 回调事件
        // 当数据发生改变，通知外层 UI 刷新
		// Callback event
		// When data changes, notify the outer UI to refresh

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

        static void DisposeHistoryPayload(const CoreFileOp& op) {
            if ((op.type == 0 || op.type == 1) &&
                HistoryFileSystem::IsManagedHistoryPath(op.path2)) {
                HistoryFileSystem::RemovePath(op.path2);
            }
            if (HistoryFileSystem::IsManagedHistoryPath(op.path3)) {
                HistoryFileSystem::RemovePath(op.path3);
            }
            for (const auto& subOp : op.subOps) DisposeHistoryPayload(subOp);
        }

        void PushUndo(const CoreFileOp& op) {
            undoStack.push_back(op);
            const size_t historyLimit = static_cast<size_t>((std::max)(1, maxHistory));
            while (undoStack.size() > historyLimit) {
                DisposeHistoryPayload(undoStack.front());
                undoStack.erase(undoStack.begin());
            }
            for (const auto& redoOp : redoStack) DisposeHistoryPayload(redoOp);
            redoStack.clear();

            if (OnDataChanged) {
                OnDataChanged();
            }
        }

        bool TryBeginHistoryOperation() {
            if (historyOperationInProgress) return false;
            historyOperationInProgress = true;
            return true;
        }

        void EndHistoryOperation() {
            historyOperationInProgress = false;
            if (OnDataChanged) OnDataChanged();
        }

        void Save();
    };

} // namespace PoggetCore
