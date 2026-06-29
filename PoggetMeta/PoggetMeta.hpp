#pragma once
#include <string>
#include <memory>

class VinaWindow;

namespace PoggetMeta {

	struct PoggetMetaIconData {
		int id;
		std::wstring alias = L"vui!NULL";
		std::wstring path;
		int x, y, size;
		float targetX, targetY;
		bool isMergedReplica = false;
		std::wstring sectionTitle = L"";
		std::shared_ptr<VinaWindow> originWindow = nullptr;

		std::wstring originalPath = L"vui!NULL";

		long long cachedTime = 0;
		std::wstring cachedName = L"";

		long long cachedSize = 0;
		std::wstring cachedExtension = L"";

		bool isDirectory = false;
		bool isCollapsed = false;
		float customWidth = -1.0f;
	};

}
