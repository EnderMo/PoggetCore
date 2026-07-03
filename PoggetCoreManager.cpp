#include "PoggetCoreManager.hpp"
#include <cmath>

namespace PoggetCore {

    void PoggetCoreManager::CalculatePositionsCore(
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
    ) {
        if (icons.empty()) return;

        CoreContainerConfig mainData = getConfig(containerWin);
        bool needsSort = false;
        bool isIntegrated = mainData.IsIntegrated || isSearchManager;

        if (isIntegrated && !isInlineManager && !isSearchManager) {
            needsSort = true;
        } else if (mainData.SortMode != 0 && !isSearchManager) {
            needsSort = true;
        }

        if (needsSort && !icons.empty()) {
            std::map<void*, std::wstring> sectionTitles;
            std::map<void*, int> originOrder;
            int currentOrder = 0;

            for (auto& ic : icons) {
                void* targetWin = ic.originWindow ? ic.originWindow : containerWin;
                if (!(isSearchManager && mainData.IsInInlineFolderView) && !ic.sectionTitle.empty()) {
                    sectionTitles[targetWin] = ic.sectionTitle;
                    ic.sectionTitle = L"";
                }
                if (originOrder.find(targetWin) == originOrder.end()) {
                    originOrder[targetWin] = currentOrder++;
                }
            }

            std::stable_sort(icons.begin(), icons.end(), [&originOrder, containerWin, &getConfig](const CoreIconLayoutData& a, const CoreIconLayoutData& b) {
                void* aWin = a.originWindow ? a.originWindow : containerWin;
                void* bWin = b.originWindow ? b.originWindow : containerWin;

                if (aWin != bWin) {
                    int orderA = originOrder.count(aWin) ? originOrder.at(aWin) : 0;
                    int orderB = originOrder.count(bWin) ? originOrder.at(bWin) : 0;
                    return orderA < orderB;
                }

                CoreContainerConfig aData = getConfig(aWin);
                int mode = aData.SortMode;

                if (mode == 1) return a.cachedName < b.cachedName;
                if (mode == 2) return a.cachedName > b.cachedName;
                if (mode == 3) return a.cachedTime < b.cachedTime;
                if (mode == 4) return a.cachedTime > b.cachedTime;
                if (mode == 5) {
                    if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
                    if (a.cachedExtension != b.cachedExtension) return a.cachedExtension < b.cachedExtension;
                    return a.cachedName < b.cachedName;
                }
                if (mode == 6) {
                    if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
                    if (a.cachedExtension != b.cachedExtension) return a.cachedExtension > b.cachedExtension;
                    return a.cachedName > b.cachedName;
                }
                if (mode == 7) {
                    if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
                    if (a.cachedSize != b.cachedSize) return a.cachedSize < b.cachedSize;
                    return a.cachedName < b.cachedName;
                }
                if (mode == 8) {
                    if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
                    if (a.cachedSize != b.cachedSize) return a.cachedSize > b.cachedSize;
                    return a.cachedName > b.cachedName;
                }
                return false;
            });

            std::set<void*> processedOrigins;
            for (auto& ic : icons) {
                void* targetWin = ic.originWindow ? ic.originWindow : containerWin;
                if (processedOrigins.find(targetWin) == processedOrigins.end()) {
                    if (!(isSearchManager && mainData.IsInInlineFolderView) && sectionTitles.find(targetWin) != sectionTitles.end()) {
                        ic.sectionTitle = sectionTitles[targetWin];
                    }
                    processedOrigins.insert(targetWin);
                }
            }
        }

        int spacingMode = mainData.IconSpacingMode;
        int actualStartX = startX;
        if (mainData.IconSpacingType == 1) {
            actualStartX = (spacingMode == 0) ? 16 : ((spacingMode == 1) ? 24 : 32);
        }
        int availableWidth = containerWidth - 2 * actualStartX;
        bool isList = mainData.IsListView;

        int gapX = gap + (spacingMode == 1 ? 16 : (spacingMode == 2 ? 36 : 0));
        int gapY = gap + 15 + (spacingMode == 1 ? 20 : (spacingMode == 2 ? 45 : 0));
        int baseListStepY = (iconSize / 2) + 8;
        int listStepY = baseListStepY + (spacingMode == 1 ? 12 : (spacingMode == 2 ? 28 : 0));

        int iconsPerRow = isList ? 1 : (availableWidth / (iconSize + gapX));
        if (iconsPerRow <= 0) iconsPerRow = 1;

        if (mainData.IconSpacingType == 1 && !isList) {
            if (iconsPerRow > 1) {
                gapX = (availableWidth - iconsPerRow * iconSize) / (iconsPerRow - 1);
            }
        }

        int usedWidth = isList ? availableWidth : (iconsPerRow * (iconSize + gapX) - gapX);
        int centeredOffsetX = isList ? 0 : (mainData.IconSpacingType == 1 ? 0 : (availableWidth - usedWidth) / 2);

        float standardRowGapX = static_cast<float>(gapX);
        float standardRowCenteredOffsetX = static_cast<float>(centeredOffsetX);
        if (mainData.IconSpacingType == 1 && !isList) {
            int standardNumItems = iconsPerRow;
            int standardTotalItemsWidth = standardNumItems * iconSize;
            float hoverExpandX = (spacingMode == 1) ? 8.0f : ((spacingMode == 2) ? 18.0f : 0.0f);
            int E = 5 + static_cast<int>(hoverExpandX);
            if (standardNumItems > 1) {
                int totalCollisionWidth = standardTotalItemsWidth + standardNumItems * (2 * E);
                int remainingSpace = availableWidth - totalCollisionWidth;
                if (remainingSpace >= 0) {
                    float hoverGapX = static_cast<float>(remainingSpace) / (standardNumItems - 1);
                    standardRowGapX = hoverGapX + 2.0f * E;
                    standardRowCenteredOffsetX = static_cast<float>(E);
                } else {
                    int spacingSpace = availableWidth - standardTotalItemsWidth;
                    if (spacingSpace < 0) spacingSpace = 0;
                    float unit = static_cast<float>(spacingSpace) / (2.0f * standardNumItems);
                    standardRowCenteredOffsetX = unit;
                    float totalGapsSpace = static_cast<float>(spacingSpace) - 2.0f * standardRowCenteredOffsetX;
                    if (totalGapsSpace < 0.0f) totalGapsSpace = 0.0f;
                    standardRowGapX = totalGapsSpace / (standardNumItems - 1);
                }
            } else {
                standardRowGapX = 0.0f;
                standardRowCenteredOffsetX = static_cast<float>(availableWidth - standardTotalItemsWidth) / 2.0f;
                if (standardRowCenteredOffsetX < 0.0f) standardRowCenteredOffsetX = 0.0f;
            }
        }

        for (size_t i = 0; i < icons.size(); ++i) {
            icons[i].isVisible = true;
            icons[i].isCollapsed = false;
            void* targetWin = icons[i].originWindow ? icons[i].originWindow : containerWin;

            if (!isSearchManager && mainData.IsSearchMode && !mainData.searchText.empty()) {
                std::wstring name = icons[i].alias == L"vui!NULL" ? std::filesystem::path(icons[i].path).filename().wstring() : icons[i].alias;
                bool isOriginalLnk = false;
                std::wstring pathExt = std::filesystem::path(icons[i].path).extension().wstring();
                std::transform(pathExt.begin(), pathExt.end(), pathExt.begin(), ::towlower);
                if (pathExt == L".lnk") {
                    isOriginalLnk = true;
                } else if (icons[i].originalPath != L"vui!NULL" && !icons[i].originalPath.empty()) {
                    std::wstring origExt = std::filesystem::path(icons[i].originalPath).extension().wstring();
                    std::transform(origExt.begin(), origExt.end(), origExt.begin(), ::towlower);
                    if (origExt == L".lnk") isOriginalLnk = true;
                }
                if (isOriginalLnk) {
                    bool aliasContainsLnk = false;
                    if (icons[i].alias != L"vui!NULL") {
                        std::wstring aliasLower = icons[i].alias;
                        std::transform(aliasLower.begin(), aliasLower.end(), aliasLower.begin(), ::towlower);
                        if (aliasLower.find(L".lnk") != std::wstring::npos) aliasContainsLnk = true;
                    }
                    if (!aliasContainsLnk) {
                        size_t lnkPos = name.size() >= 4 ? name.size() - 4 : std::wstring::npos;
                        if (lnkPos != std::wstring::npos) {
                            std::wstring tail = name.substr(lnkPos);
                            std::transform(tail.begin(), tail.end(), tail.begin(), ::towlower);
                            if (tail == L".lnk") name = name.substr(0, lnkPos);
                        }
                    }
                }
                if (!MatchWildcard(mainData.searchText, name)) {
                    icons[i].isVisible = false;
                }
            }
        }

        struct Row {
            std::vector<size_t> iconIndices;
            void* targetWin = nullptr;
            std::wstring sectionTitle = L"";
            bool isFull = false;
        };
        std::vector<Row> layoutRows;
        std::vector<size_t> iconToRowMap(icons.size(), static_cast<size_t>(-1));

        if (mainData.IconSpacingType == 1 && !isList) {
            void* tempLastOrigin = nullptr;
            std::wstring activeSectionTitle = L"";
            Row currentTempRow;
            float hoverExpandX = (spacingMode == 1) ? 8.0f : ((spacingMode == 2) ? 18.0f : 0.0f);
            int E = 5 + static_cast<int>(hoverExpandX);

            for (size_t i = 0; i < icons.size(); ++i) {
                if (!icons[i].isVisible) continue;
                void* targetWin = icons[i].originWindow ? icons[i].originWindow : containerWin;
                
                CoreContainerConfig targetData = getConfig(targetWin);

                bool sectionChanged = (targetWin != tempLastOrigin || (isSearchManager && mainData.IsInInlineFolderView && !icons[i].sectionTitle.empty()));
                if (sectionChanged) {
                    if (!currentTempRow.iconIndices.empty()) {
                        currentTempRow.isFull = false;
                        layoutRows.push_back(currentTempRow);
                        currentTempRow.iconIndices.clear();
                        currentTempRow.isFull = false;
                    }
                    tempLastOrigin = targetWin;
                    activeSectionTitle = icons[i].sectionTitle.empty() ? targetData.title : icons[i].sectionTitle;
                } else if (!icons[i].sectionTitle.empty()) {
                    activeSectionTitle = icons[i].sectionTitle;
                }

                bool sectionCollapsed = false;
                if (isSearchManager) sectionCollapsed = isSectionCollapsedInSearch(targetWin, activeSectionTitle);
                else sectionCollapsed = (isIntegrated && !isInlineManager) ? targetData.IsSectionCollapsed : false;
                if (sectionCollapsed) {
                    icons[i].isCollapsed = true;
                    continue;
                }

                currentTempRow.targetWin = targetWin;
                currentTempRow.sectionTitle = activeSectionTitle;

                std::vector<size_t> testIndices = currentTempRow.iconIndices;
                testIndices.push_back(i);
                int testNumItems = static_cast<int>(testIndices.size());
                int minRequiredWidth = 0;
                for (size_t idx : testIndices) {
                    bool isTxt = (MatchWildcard(L"*.txt", icons[idx].path) || MatchWildcard(L"*.md", icons[idx].path));
                    if (isTxt) {
                        int w_base = iconSize * 2 + 12;
                        int maxShrink = w_base / 6;
                        minRequiredWidth += (w_base - maxShrink);
                    } else {
                        minRequiredWidth += iconSize;
                    }
                }
                int min_padding = 10;
                int min_gap = 16;
                if (spacingMode == 0) { min_padding = 4; min_gap = 8; }
                else if (spacingMode == 2) { min_padding = 18; min_gap = 28; }

                minRequiredWidth += 2 * min_padding;
                if (testNumItems > 1) {
                    minRequiredWidth += (testNumItems - 1) * min_gap;
                }

                if (minRequiredWidth > availableWidth && !currentTempRow.iconIndices.empty()) {
                    currentTempRow.isFull = true;
                    layoutRows.push_back(currentTempRow);
                    currentTempRow.iconIndices.clear();
                    currentTempRow.isFull = false;
                }
                currentTempRow.iconIndices.push_back(i);
                iconToRowMap[i] = layoutRows.size();
            }
            if (!currentTempRow.iconIndices.empty()) {
                currentTempRow.isFull = false;
                layoutRows.push_back(currentTempRow);
            }
        } else {
            int tempCursorX = 0;
            void* tempLastOrigin = nullptr;
            std::wstring activeSectionTitle = L"";
            Row currentTempRow;

            for (size_t i = 0; i < icons.size(); ++i) {
                if (!icons[i].isVisible) continue;
                void* targetWin = icons[i].originWindow ? icons[i].originWindow : containerWin;

                CoreContainerConfig targetData = getConfig(targetWin);

                bool sectionChanged = (targetWin != tempLastOrigin || (isSearchManager && mainData.IsInInlineFolderView && !icons[i].sectionTitle.empty()));
                if (sectionChanged) {
                    if (!currentTempRow.iconIndices.empty()) {
                        layoutRows.push_back(currentTempRow);
                        currentTempRow.iconIndices.clear();
                    }
                    tempCursorX = 0;
                    tempLastOrigin = targetWin;
                    activeSectionTitle = icons[i].sectionTitle.empty() ? targetData.title : icons[i].sectionTitle;
                } else if (!icons[i].sectionTitle.empty()) {
                    activeSectionTitle = icons[i].sectionTitle;
                }

                bool sectionCollapsed = false;
                if (isSearchManager) sectionCollapsed = isSectionCollapsedInSearch(targetWin, activeSectionTitle);
                else sectionCollapsed = (isIntegrated && !isInlineManager) ? targetData.IsSectionCollapsed : false;
                if (sectionCollapsed) {
                    icons[i].isCollapsed = true;
                    continue;
                }

                bool isTxt = (!isList && (MatchWildcard(L"*.txt", icons[i].path) || MatchWildcard(L"*.md", icons[i].path)));
                int itemSpan = isTxt ? 2 : 1;
                if (tempCursorX + itemSpan > iconsPerRow && tempCursorX > 0) {
                    if (!currentTempRow.iconIndices.empty()) {
                        layoutRows.push_back(currentTempRow);
                        currentTempRow.iconIndices.clear();
                    }
                    tempCursorX = 0;
                }
                currentTempRow.iconIndices.push_back(i);
                iconToRowMap[i] = layoutRows.size();
                tempCursorX += itemSpan;
                if (tempCursorX >= iconsPerRow) {
                    layoutRows.push_back(currentTempRow);
                    currentTempRow.iconIndices.clear();
                    tempCursorX = 0;
                }
            }
            if (!currentTempRow.iconIndices.empty()) {
                currentTempRow.isFull = false;
                layoutRows.push_back(currentTempRow);
            }
        }

        int cursorX = 0;
        int cursorYBase = startY;
        void* lastOrigin = nullptr;
        std::wstring currentSectionTitle = L"";

        float rowGapX = static_cast<float>(gapX);
        float rowCenteredOffsetX = static_cast<float>(centeredOffsetX);
        float rowAccumulatedX = static_cast<float>(actualStartX) + rowCenteredOffsetX;

        for (size_t i = 0; i < icons.size(); ++i) {
            icons[i].customWidth = -1.0f;
        }

        for (size_t i = 0; i < icons.size(); ++i) {
            if (!icons[i].isVisible) {
                icons[i].targetX = -100.0f;
                icons[i].targetY = -100.0f;
                icons[i].isCollapsed = true; // Treating hidden as collapsed
                continue;
            }

            void* targetWin = icons[i].originWindow ? icons[i].originWindow : containerWin;
            CoreContainerConfig targetData = getConfig(targetWin);

            if (targetWin != lastOrigin || (isSearchManager && mainData.IsInInlineFolderView && !icons[i].sectionTitle.empty())) {
                if (cursorX > 0) {
                    cursorX = 0;
                    cursorYBase += isList ? listStepY : (iconSize + gapY);
                }
                currentSectionTitle = icons[i].sectionTitle.empty() ? targetData.title : icons[i].sectionTitle;

                if (isIntegrated && !isInlineManager) {
                    CoreSectionHeaderLayoutData header;
                    header.originWindow = targetWin;
                    header.x1 = actualStartX + centeredOffsetX;
                    header.y1 = cursorYBase;
                    header.y2 = cursorYBase + 35;
                    header.x2 = containerWidth - (actualStartX + centeredOffsetX);
                    header.title = currentSectionTitle;
                    outHeaders.push_back(header);
                    cursorYBase += 50; // 35px height + 10px spacing
                }
                lastOrigin = targetWin;
            }

            if (icons[i].isCollapsed) {
                icons[i].targetX = -100.0f;
                icons[i].targetY = -100.0f;
                continue;
            }

            icons[i].isCollapsed = false;
            bool isTxt = (!isList && (MatchWildcard(L"*.txt", icons[i].path) || MatchWildcard(L"*.md", icons[i].path)));
            int itemSpan = isTxt ? 2 : 1;

            if (mainData.IconSpacingType == 1 && !isList) {
                bool needsWrap = (i > 0 && iconToRowMap[i] != iconToRowMap[i - 1]);
                if (needsWrap && cursorX > 0) {
                    cursorX = 0;
                    cursorYBase += (iconSize + gapY);
                }
            } else {
                if (cursorX + itemSpan > iconsPerRow && cursorX > 0) {
                    cursorX = 0;
                    cursorYBase += isList ? listStepY : (iconSize + gapY);
                }
            }

            if (cursorX == 0) {
                Row row;
                int numItems = 0;
                if (iconToRowMap[i] != static_cast<size_t>(-1)) {
                    row = layoutRows[iconToRowMap[i]];
                    numItems = static_cast<int>(row.iconIndices.size());
                }

                rowGapX = static_cast<float>(gapX);
                rowCenteredOffsetX = static_cast<float>(centeredOffsetX);

                if (mainData.IconSpacingType == 1 && !isList && numItems > 0) {
                    int totalSpan = 0;
                    for (size_t idx : row.iconIndices) {
                        bool isTxt = (!isList && (MatchWildcard(L"*.txt", icons[idx].path) || MatchWildcard(L"*.md", icons[idx].path)));
                        totalSpan += isTxt ? 2 : 1;
                    }
                    bool isRowFull = row.isFull || (totalSpan >= iconsPerRow);

                    int totalUnshrunkWidth = 0;
                    int numWidgets = 0;
                    for (size_t idx : row.iconIndices) {
                        bool isTxt = (!isList && (MatchWildcard(L"*.txt", icons[idx].path) || MatchWildcard(L"*.md", icons[idx].path)));
                        if (isTxt) {
                            numWidgets++;
                            totalUnshrunkWidth += (iconSize * 2 + 12);
                        } else {
                            totalUnshrunkWidth += iconSize;
                        }
                    }

                    float spaceNeededWithStandard = static_cast<float>(totalUnshrunkWidth) + (numItems > 1 ? static_cast<float>(numItems - 1) * standardRowGapX : 0.0f) + 2.0f * standardRowCenteredOffsetX;
                    bool shouldShrinkAndFit = isRowFull || (spaceNeededWithStandard > static_cast<float>(availableWidth));

                    if (shouldShrinkAndFit) {
                        int totalItemsWidth = totalUnshrunkWidth;
                        float hoverExpandX = (spacingMode == 1) ? 8.0f : ((spacingMode == 2) ? 18.0f : 0.0f);
                        int E = 5 + static_cast<int>(hoverExpandX);

                        int stretchPerWidget = 0;
                        if (numWidgets > 0) {
                            int requiredSpace = availableWidth - numItems * (2 * E);
                            if (numItems > 1) {
                                float standardHoverGapX = standardRowGapX - 2.0f * E;
                                if (standardHoverGapX < 0.0f) standardHoverGapX = 0.0f;
                                requiredSpace -= static_cast<int>(std::round((numItems - 1) * standardHoverGapX));
                            }
                            int diff = requiredSpace - totalItemsWidth;
                            if (diff > 0) {
                                int maxTotalStretch = numWidgets * ((iconSize * 2 + 12) / 4);
                                int actualTotalStretch = (diff < maxTotalStretch) ? diff : maxTotalStretch;
                                stretchPerWidget = actualTotalStretch / numWidgets;
                                totalItemsWidth += actualTotalStretch;
                            } else if (diff < 0) {
                                int maxTotalShrink = numWidgets * ((iconSize * 2 + 12) / 6);
                                int actualTotalShrink = (-diff < maxTotalShrink) ? -diff : maxTotalShrink;
                                stretchPerWidget = -actualTotalShrink / numWidgets;
                                totalItemsWidth -= actualTotalShrink;
                            }
                        }

                        if (numItems > 1) {
                            int totalCollisionWidth = totalItemsWidth + numItems * (2 * E);
                            int remainingSpace = availableWidth - totalCollisionWidth;
                            if (remainingSpace >= 0) {
                                float hoverGapX = static_cast<float>(remainingSpace) / (numItems - 1);
                                float targetGapX = hoverGapX + 2.0f * E;
                                int maxGapX = gapX + (spacingMode == 0 ? 20 : (spacingMode == 1 ? 40 : 60));
                                if (targetGapX > static_cast<float>(maxGapX)) {
                                    rowGapX = static_cast<float>(maxGapX);
                                    float totalWidth = static_cast<float>(totalItemsWidth) + static_cast<float>(numItems - 1) * rowGapX;
                                    rowCenteredOffsetX = (static_cast<float>(availableWidth) - totalWidth) / 2.0f;
                                    if (rowCenteredOffsetX < 0.0f) rowCenteredOffsetX = 0.0f;
                                } else {
                                    rowGapX = targetGapX;
                                    rowCenteredOffsetX = static_cast<float>(E);
                                }
                            } else {
                                int spacingSpace = availableWidth - totalItemsWidth;
                                if (spacingSpace < 0) spacingSpace = 0;
                                float unit = static_cast<float>(spacingSpace) / (2.0f * numItems);
                                rowCenteredOffsetX = unit;
                                float totalGapsSpace = static_cast<float>(spacingSpace) - 2.0f * rowCenteredOffsetX;
                                if (totalGapsSpace < 0.0f) totalGapsSpace = 0.0f;
                                rowGapX = totalGapsSpace / (numItems - 1);
                            }
                        } else {
                            rowGapX = 0.0f;
                            rowCenteredOffsetX = static_cast<float>(availableWidth - totalItemsWidth) / 2.0f;
                            if (rowCenteredOffsetX < 0.0f) rowCenteredOffsetX = 0.0f;
                        }

                        for (size_t idx : row.iconIndices) {
                            bool isTxt = (!isList && (MatchWildcard(L"*.txt", icons[idx].path) || MatchWildcard(L"*.md", icons[idx].path)));
                            if (isTxt) icons[idx].customWidth = static_cast<float>((iconSize * 2 + 12) + stretchPerWidget);
                            else icons[idx].customWidth = -1.0f;
                        }
                    } else {
                        rowGapX = standardRowGapX;
                        rowCenteredOffsetX = standardRowCenteredOffsetX;
                        for (size_t idx : row.iconIndices) icons[idx].customWidth = -1.0f;
                    }
                }
                rowAccumulatedX = static_cast<float>(actualStartX) + rowCenteredOffsetX;
            }

            int newX;
            if (mainData.IconSpacingType == 1 && !isList) {
                newX = static_cast<int>(std::round(rowAccumulatedX));
                rowAccumulatedX += (icons[i].customWidth > 0.0f ? icons[i].customWidth : static_cast<float>(isTxt ? (iconSize * 2 + 12) : iconSize)) + rowGapX;
            } else {
                newX = actualStartX + centeredOffsetX + (isList ? 0 : cursorX * (iconSize + gapX));
            }

            icons[i].targetX = static_cast<float>(newX);
            icons[i].targetY = static_cast<float>(cursorYBase);

            if (mainData.IconSpacingType == 1 && !isList) cursorX += itemSpan;
            else {
                cursorX += itemSpan;
                if (cursorX >= iconsPerRow) {
                    cursorX = 0;
                    cursorYBase += isList ? listStepY : (iconSize + gapY);
                }
            }
        }
    }
}
