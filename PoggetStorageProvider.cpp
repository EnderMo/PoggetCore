#include "../framework.h"
#include "PoggetStorageProvider.hpp"
#include <iostream>

namespace PoggetCore {

    static std::wstring FormatCompKey(const std::wstring& id) {
        if (id.rfind(L"ComID_", 0) == 0) return id;
        return L"ComID_" + id;
    }

    void PoggetStorageProvider::Initialize(const std::wstring& filePath) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_filePath = filePath;
        try {
            std::filesystem::path p(filePath);
            std::wstring backupPath = (p.parent_path() / L"Backups" / L"BufferBak" / p.filename()).wstring();
            m_storage.SetBackupPath(backupPath);
            m_storage.Load(filePath);
            m_isInitialized = true;
        } catch (const std::exception& e) {
            std::cerr << "PoggetStorageProvider Init Error: " << e.what() << std::endl;
            m_isInitialized = false;
        }
    }

    bool PoggetStorageProvider::IsInitialized() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_isInitialized;
    }

    void PoggetStorageProvider::SubmitContainerModel(const ContainerModel& model) {

        std::lock_guard<std::mutex> lock(m_mutex);
        if (model.id.empty()) return;
        std::wstring key = FormatCompKey(model.id);

        auto proxy = m_storage[L"Components"][key];
        proxy[L"id"] = model.id;
        proxy[L"alias"] = model.alias;
        proxy[L"title"] = model.title;
        proxy[L"IsMinimized"] = model.IsMinimized;
        proxy[L"IsIntegrated"] = model.IsIntegrated;
        proxy[L"UseTargetFolder"] = model.UseTargetFolder;
        proxy[L"TargetFolder"] = model.TargetFolder;
        proxy[L"IsCustomTarget"] = model.IsCustomTarget;
        proxy[L"SortMode"] = model.SortMode;
        proxy[L"IconSizeLevel"] = model.IconSizeLevel;
        proxy[L"ShowDefaultFolderIcon"] = model.ShowDefaultFolderIcon;
        proxy[L"ShowTextPreview"] = model.ShowTextPreview;
        proxy[L"HideFileExtension"] = model.HideFileExtension;
        proxy[L"IconSpacingMode"] = model.IconSpacingMode;
        proxy[L"IconSpacingType"] = model.IconSpacingType;
        proxy[L"IsListView"] = model.IsListView;
        proxy[L"TextRenderMode"] = model.TextRenderMode;
        proxy[L"FileNameColorMode"] = model.FileNameColorMode;
        proxy[L"TitleType"] = model.TitleType;
        proxy[L"TagStyle"] = model.TagStyle;
        proxy[L"ListDetailType"] = model.ListDetailType;
        proxy[L"fontFamily"] = model.fontFamily;
        proxy[L"DisableLayoutAnimations"] = model.DisableLayoutAnimations;
        proxy[L"IsLocked"] = model.IsLocked;
        proxy[L"IsEmbeddedLayer"] = model.IsEmbeddedLayer;
        proxy[L"IsUpwardExpand"] = model.IsUpwardExpand;
        proxy[L"TransparentFrameMode"] = model.TransparentFrameMode;
        proxy[L"AutoHideTitleBar"] = model.AutoHideTitleBar;
        proxy[L"AutoFade"] = model.AutoFade;
        proxy[L"AutoFadeDragStat"] = model.AutoFadeDragStat;
        proxy[L"FolderOpenMode"] = model.FolderOpenMode;
        proxy[L"SwipeUpSearchEnabled"] = model.SwipeUpSearchEnabled;
        proxy[L"bgAlpha"] = static_cast<double>(model.backgroundAlpha);
        proxy[L"titleAlpha"] = static_cast<double>(model.titleAlpha);
        proxy[L"CompatibleLayer"] = model.CompatibleLayer;
        proxy[L"EnableStaticMaterialLayerForD3D11"] = model.EnableStaticMaterialLayerForD3D11;
        proxy[L"EnableDynamicMaterialLayerForD3D11"] = model.EnableDynamicMaterialLayerForD3D11;
        proxy[L"cornerRad"] = static_cast<double>(model.cornerRad);
        proxy[L"shadowBlur"] = static_cast<double>(model.shadowBlur);
        proxy[L"shadowAlpha"] = static_cast<double>(model.shadowAlpha);
        proxy[L"shadowOffsetX"] = static_cast<double>(model.shadowOffsetX);
        proxy[L"shadowOffsetY"] = static_cast<double>(model.shadowOffsetY);
        proxy[L"EnableInlineFolderView"] = model.EnableInlineFolderView;
    }

    bool PoggetStorageProvider::LoadContainerModel(const std::wstring& containerId, ContainerModel& outModel) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (containerId.empty()) return false;
        std::wstring key = FormatCompKey(containerId);

        outModel.id = containerId;
        auto proxy = m_storage[L"Components"][key];
        outModel.alias = proxy[L"alias"].get<std::wstring>(L"");
        outModel.title = proxy[L"title"].get<std::wstring>(L"Unnamed");
        outModel.IsMinimized = proxy[L"IsMinimized"].get<bool>(false);
        outModel.IsIntegrated = proxy[L"IsIntegrated"].get<bool>(false);
        outModel.UseTargetFolder = proxy[L"UseTargetFolder"].get<bool>(false);
        outModel.TargetFolder = proxy[L"TargetFolder"].get<std::wstring>(L"vui!NULL");
        outModel.IsCustomTarget = proxy[L"IsCustomTarget"].get<bool>(false);
        outModel.SortMode = proxy[L"SortMode"].get<int>(0);
        outModel.IconSizeLevel = proxy[L"IconSizeLevel"].get<int>(3);
        outModel.ShowDefaultFolderIcon = proxy[L"ShowDefaultFolderIcon"].get<bool>(false);
        outModel.ShowTextPreview = proxy[L"ShowTextPreview"].get<bool>(true);
        outModel.HideFileExtension = proxy[L"HideFileExtension"].get<int>(0);
        outModel.IconSpacingMode = proxy[L"IconSpacingMode"].get<int>(0);
        outModel.IconSpacingType = proxy[L"IconSpacingType"].get<int>(0);
        outModel.IsListView = proxy[L"IsListView"].get<bool>(false);
        outModel.TextRenderMode = proxy[L"TextRenderMode"].get<int>(0);
        outModel.FileNameColorMode = proxy[L"FileNameColorMode"].get<int>(0);
        outModel.TitleType = proxy[L"TitleType"].get<int>(0);
        outModel.TagStyle = proxy[L"TagStyle"].get<int>(0);
        outModel.ListDetailType = proxy[L"ListDetailType"].get<int>(0);
        outModel.fontFamily = proxy[L"fontFamily"].get<std::wstring>(L"Segoe UI");
        outModel.DisableLayoutAnimations = proxy[L"DisableLayoutAnimations"].get<bool>(false);
        outModel.IsLocked = proxy[L"IsLocked"].get<bool>(false);
        outModel.IsEmbeddedLayer = proxy[L"IsEmbeddedLayer"].get<bool>(false);
        outModel.IsUpwardExpand = proxy[L"IsUpwardExpand"].get<bool>(false);
        outModel.TransparentFrameMode = proxy[L"TransparentFrameMode"].get<int>(0);
        outModel.AutoHideTitleBar = proxy[L"AutoHideTitleBar"].get<int>(0);
        outModel.AutoFade = proxy[L"AutoFade"].get<int>(0);
        outModel.AutoFadeDragStat = proxy[L"AutoFadeDragStat"].get<bool>(false);
        outModel.FolderOpenMode = proxy[L"FolderOpenMode"].get<int>(0);
        outModel.SwipeUpSearchEnabled = proxy[L"SwipeUpSearchEnabled"].get<bool>(true);
        outModel.backgroundAlpha = static_cast<float>(proxy[L"bgAlpha"].get<double>(1.0));
        outModel.titleAlpha = static_cast<float>(proxy[L"titleAlpha"].get<double>(1.0));
        outModel.CompatibleLayer = proxy[L"CompatibleLayer"].get<int>(0);
        outModel.EnableStaticMaterialLayerForD3D11 = proxy[L"EnableStaticMaterialLayerForD3D11"].get<bool>(false);
        outModel.EnableDynamicMaterialLayerForD3D11 = proxy[L"EnableDynamicMaterialLayerForD3D11"].get<bool>(false);
        outModel.cornerRad = static_cast<float>(proxy[L"cornerRad"].get<double>(16.0));
        outModel.shadowBlur = static_cast<float>(proxy[L"shadowBlur"].get<double>(16.0));
        outModel.shadowAlpha = static_cast<float>(proxy[L"shadowAlpha"].get<double>(0.2));
        outModel.shadowOffsetX = static_cast<float>(proxy[L"shadowOffsetX"].get<double>(0.0));
        outModel.shadowOffsetY = static_cast<float>(proxy[L"shadowOffsetY"].get<double>(4.0));
        outModel.EnableInlineFolderView = proxy[L"EnableInlineFolderView"].get<bool>(true);
        return true;
    }

    void PoggetStorageProvider::RemoveContainerModel(const std::wstring& containerId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (containerId.empty()) return;
        std::wstring key = FormatCompKey(containerId);
        m_storage[L"Components"][key].remove();
    }

    void PoggetStorageProvider::SaveStorage() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_filePath.empty() || !m_isInitialized) return;
        try {
            m_storage.Save();
        } catch (const std::exception& e) {
            std::cerr << "PoggetStorageProvider Save Error: " << e.what() << std::endl;
        }
    }

    void ContainerModel::Save() {
        PoggetStorageProvider::GetInstance().SubmitContainerModel(*this);
        PoggetStorageProvider::GetInstance().SaveStorage();
    }

} // namespace PoggetCore
