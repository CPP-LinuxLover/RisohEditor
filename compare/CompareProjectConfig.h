#pragma once

// CompareProjectConfig.h --- Multi-DLL batch comparison project config
//
// JSON-based project configuration for batch compare tasks.

#ifndef _INC_WINDOWS
	#include <windows.h>
#endif
#include <vector>
#include <string>
#include "nlohmann/json.hpp"

namespace rceditor {

struct SCompareTaskConfig
{
	std::wstring taskName;               // Task name (e.g. "MainApp")

	// Primary language input paths
	std::wstring primaryDllPath;         // Primary DLL/EXE path
	std::wstring primaryRcPath;          // Primary .rc file path
	std::wstring primaryResourceHPath;   // Primary resource.h path

	// Secondary language input paths
	std::wstring secondaryDllPath;       // Secondary DLL/EXE path
	std::wstring secondaryRcPath;        // Secondary .rc file path
	std::wstring secondaryResourceHPath; // Secondary resource.h path

	// Output directory
	std::wstring outputDir;              // Output directory

	// Output paths (legacy compat)
	std::wstring outputDllPath;          // Secondary DLL output path
	std::wstring outputRcPath;           // Secondary .rc output path
	std::wstring outputResourceHPath;    // Secondary resource.h output path

	SCompareTaskConfig() = default;
};

struct SCompareProject
{
	std::wstring projectName;                    // Project name
	std::wstring projectDescription;             // Project description
	std::vector<SCompareTaskConfig> tasks;       // Task list
	WORD primaryLang;                            // Primary language ID
	WORD secondaryLang;                          // Secondary language ID

	SCompareProject()
		: primaryLang(0)
		, secondaryLang(0)
	{
	}
};

class CompareProjectConfigManager
{
public:
	static bool LoadProjectConfig(LPCWSTR path, SCompareProject& outProject);
	static bool SaveProjectConfig(LPCWSTR path, const SCompareProject& project);
	static bool ValidateTaskInputs(const SCompareTaskConfig& task,
	                               std::vector<std::wstring>& outErrors);
	static bool ValidateProject(const SCompareProject& project,
	                            std::vector<std::wstring>& outErrors);
	static bool IsFileAccessible(const std::wstring& filePath);

private:
	static nlohmann::json TaskToJson(const SCompareTaskConfig& task);
	static SCompareTaskConfig TaskFromJson(const nlohmann::json& j);
	static std::string WToUtf8(const std::wstring& w);
	static std::wstring Utf8ToW(const std::string& s);
};

} // namespace rceditor
