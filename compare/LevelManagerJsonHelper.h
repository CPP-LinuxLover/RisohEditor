#pragma once

#ifndef _INC_WINDOWS
	#include <windows.h>
#endif
#include <string>
#include "nlohmann/json.hpp"

namespace rceditor {

class LevelManagerJsonHelper
{
public:
	// Convert wide string to UTF-8
	static std::string WToUtf8(const std::wstring& str);

	// Convert UTF-8 to wide string
	static std::wstring Utf8ToW(const std::string& utf8);

	// Load JSON from file path
	static bool LoadJson(LPCWSTR path, nlohmann::json& outRoot);

	// Save JSON to file path
	static bool SaveJson(LPCWSTR path, const nlohmann::json& root);
};

} // namespace rceditor
