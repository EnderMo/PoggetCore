#pragma once
#include "VinaStorage.hpp"

class PoggetDesktopContainerStorage : public VinaStorage {
public:
	void Save();
};

class PoggetQuickPanelStorage : public VinaStorage {
public:
	void Save();
};

class PoggetMagWinStorage : public VinaStorage {
public:
	void Save();
};

inline VinaStorage PoggetMain;
inline PoggetDesktopContainerStorage DesktopContainerStorage;
inline PoggetMagWinStorage MagWinStorage;
inline PoggetQuickPanelStorage QuickPanelStorage;