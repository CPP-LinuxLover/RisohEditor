// CompareProjectConfig.cpp --- JSON project configuration manager
//////////////////////////////////////////////////////////////////////////////

#include "CompareProjectConfig.h"
#include <fstream>

namespace rceditor {

namespace {

std::wstring ExtractDirectory(const std::wstring& filePath)
{
	size_t pos = filePath.find_last_of(L"\\/");
	if (pos == std::wstring::npos)
		return L"";
	return filePath.substr(0, pos);
}

} // namespace

// Encoding helpers

std::string CompareProjectConfigManager::WToUtf8(const std::wstring& w)
{
	if (w.empty()) return {};
	int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
	                              NULL, 0, NULL, NULL);
	if (len <= 0) return {};
	std::string s(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
	                    &s[0], len, NULL, NULL);
	return s;
}

std::wstring CompareProjectConfigManager::Utf8ToW(const std::string& s)
{
	if (s.empty()) return L"";
	int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
	if (len <= 0) return L"";
	std::wstring w(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
	return w;
}

// JSON serialization

nlohmann::json CompareProjectConfigManager::TaskToJson(const SCompareTaskConfig& task)
{
	nlohmann::json j;
	j["taskName"]              = WToUtf8(task.taskName);
	j["primaryDllPath"]        = WToUtf8(task.primaryDllPath);
	j["primaryRcPath"]         = WToUtf8(task.primaryRcPath);
	j["primaryResourceHPath"]  = WToUtf8(task.primaryResourceHPath);
	j["secondaryDllPath"]      = WToUtf8(task.secondaryDllPath);
	j["secondaryRcPath"]       = WToUtf8(task.secondaryRcPath);
	j["secondaryResourceHPath"]= WToUtf8(task.secondaryResourceHPath);
	j["outputDir"]             = WToUtf8(task.outputDir);
	j["outputDllPath"]         = WToUtf8(task.outputDllPath);
	j["outputRcPath"]          = WToUtf8(task.outputRcPath);
	j["outputResourceHPath"]   = WToUtf8(task.outputResourceHPath);
	return j;
}

SCompareTaskConfig CompareProjectConfigManager::TaskFromJson(const nlohmann::json& j)
{
	SCompareTaskConfig task;

	auto readStr = [&](const char* key, std::wstring& out)
	{
		if (j.contains(key) && j[key].is_string())
			out = Utf8ToW(j[key].get<std::string>());
	};

	readStr("taskName",              task.taskName);
	readStr("primaryDllPath",        task.primaryDllPath);
	readStr("primaryRcPath",         task.primaryRcPath);
	readStr("primaryResourceHPath",  task.primaryResourceHPath);
	readStr("secondaryDllPath",      task.secondaryDllPath);
	readStr("secondaryRcPath",       task.secondaryRcPath);
	readStr("secondaryResourceHPath",task.secondaryResourceHPath);
	readStr("outputDir",             task.outputDir);
	readStr("outputDllPath",         task.outputDllPath);
	readStr("outputRcPath",          task.outputRcPath);
	readStr("outputResourceHPath",   task.outputResourceHPath);

	// Legacy compat: derive outputDir from old output paths
	if (task.outputDir.empty())
	{
		if (!task.outputDllPath.empty())
			task.outputDir = ExtractDirectory(task.outputDllPath);
		else if (!task.outputRcPath.empty())
			task.outputDir = ExtractDirectory(task.outputRcPath);
		else if (!task.outputResourceHPath.empty())
			task.outputDir = ExtractDirectory(task.outputResourceHPath);
	}

	return task;
}

// Load/Save

bool CompareProjectConfigManager::LoadProjectConfig(LPCWSTR path, SCompareProject& outProject)
{
	// Convert wide path to narrow for std::ifstream (XP compat)
	char szPath[MAX_PATH * 2];
	WideCharToMultiByte(CP_ACP, 0, path, -1, szPath, sizeof(szPath), NULL, NULL);

	std::ifstream in(szPath);
	if (!in.is_open())
		return false;

	nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
	in.close();

	if (root.is_discarded())
		return false;

	outProject.tasks.clear();

	if (root.contains("projectName") && root["projectName"].is_string())
		outProject.projectName = Utf8ToW(root["projectName"].get<std::string>());
	if (root.contains("projectDescription") && root["projectDescription"].is_string())
		outProject.projectDescription = Utf8ToW(root["projectDescription"].get<std::string>());
	if (root.contains("primaryLang") && root["primaryLang"].is_number_unsigned())
		outProject.primaryLang = (WORD)root["primaryLang"].get<unsigned int>();
	if (root.contains("secondaryLang") && root["secondaryLang"].is_number_unsigned())
		outProject.secondaryLang = (WORD)root["secondaryLang"].get<unsigned int>();

	if (root.contains("tasks") && root["tasks"].is_array())
	{
		for (const auto& jTask : root["tasks"])
		{
			outProject.tasks.push_back(TaskFromJson(jTask));
		}
	}

	return true;
}

bool CompareProjectConfigManager::SaveProjectConfig(LPCWSTR path, const SCompareProject& project)
{
	nlohmann::json root;
	root["projectName"]        = WToUtf8(project.projectName);
	root["projectDescription"] = WToUtf8(project.projectDescription);
	root["primaryLang"]        = (unsigned int)project.primaryLang;
	root["secondaryLang"]      = (unsigned int)project.secondaryLang;

	nlohmann::json arrTasks = nlohmann::json::array();
	for (const auto& task : project.tasks)
	{
		arrTasks.push_back(TaskToJson(task));
	}
	root["tasks"] = arrTasks;

	char szPath[MAX_PATH * 2];
	WideCharToMultiByte(CP_ACP, 0, path, -1, szPath, sizeof(szPath), NULL, NULL);

	std::ofstream out(szPath);
	if (!out.is_open())
		return false;

	out << root.dump(4);
	out.close();
	return true;
}

// Validation

bool CompareProjectConfigManager::ValidateTaskInputs(const SCompareTaskConfig& task,
                                                      std::vector<std::wstring>& outErrors)
{
	bool valid = true;

	auto checkFile = [&](const std::wstring& p, const wchar_t* label)
	{
		if (p.empty())
		{
			std::wstring msg = L"Task '";
			msg += task.taskName;
			msg += L"': ";
			msg += label;
			msg += L" is empty";
			outErrors.push_back(msg);
			valid = false;
		}
		else if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			std::wstring msg = L"Task '";
			msg += task.taskName;
			msg += L"': ";
			msg += label;
			msg += L" not found: ";
			msg += p;
			outErrors.push_back(msg);
			valid = false;
		}
	};

	// Primary binary (DLL/EXE) is required
	checkFile(task.primaryDllPath, L"Primary Binary");
	// Secondary binary (DLL/EXE) is required
	checkFile(task.secondaryDllPath, L"Secondary Binary");

	// RC and resource.h are optional (validate only when provided)
	if (!task.primaryRcPath.empty())
		checkFile(task.primaryRcPath, L"Primary RC");
	if (!task.primaryResourceHPath.empty())
		checkFile(task.primaryResourceHPath, L"Primary resource.h");
	if (!task.secondaryRcPath.empty())
		checkFile(task.secondaryRcPath, L"Secondary RC");
	if (!task.secondaryResourceHPath.empty())
		checkFile(task.secondaryResourceHPath, L"Secondary resource.h");

	return valid;
}

bool CompareProjectConfigManager::ValidateProject(const SCompareProject& project,
                                                   std::vector<std::wstring>& outErrors)
{
	if (project.tasks.empty())
	{
		outErrors.push_back(L"Project has no tasks defined");
		return false;
	}

	bool allValid = true;
	for (const auto& task : project.tasks)
	{
		if (!ValidateTaskInputs(task, outErrors))
			allValid = false;
	}
	return allValid;
}

bool CompareProjectConfigManager::IsFileAccessible(const std::wstring& filePath)
{
	HANDLE hFile = CreateFileW(
		filePath.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	CloseHandle(hFile);
	return true;
}

} // namespace rceditor
