#include "stdafx.h"
#include "CompareProjectConfig.h"
#include <fstream>

namespace rceditor {

namespace {

CStringW ExtractDirectory(const CStringW& filePath)
{
    int pos = filePath.ReverseFind(L'\\');
    if (pos < 0)
        pos = filePath.ReverseFind(L'/');
    if (pos < 0)
        return L"";
    return filePath.Left(pos);
}

} // namespace

// ---- 编码转换辅助 ----

std::string CompareProjectConfigManager::WToUtf8(const CStringW& w)
{
    if (w.IsEmpty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.GetString(), w.GetLength(),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.GetString(), w.GetLength(),
                        &s[0], len, nullptr, nullptr);
    return s;
}

CStringW CompareProjectConfigManager::Utf8ToW(const std::string& s)
{
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return L"";
    CStringW w;
    wchar_t* buf = w.GetBuffer(len);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), buf, len);
    w.ReleaseBuffer(len);
    return w;
}

// ---- JSON 序列化 ----

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
    if (j.contains("taskName") && j["taskName"].is_string())
        task.taskName = Utf8ToW(j["taskName"].get<std::string>());
    if (j.contains("primaryDllPath") && j["primaryDllPath"].is_string())
        task.primaryDllPath = Utf8ToW(j["primaryDllPath"].get<std::string>());
    if (j.contains("primaryRcPath") && j["primaryRcPath"].is_string())
        task.primaryRcPath = Utf8ToW(j["primaryRcPath"].get<std::string>());
    if (j.contains("primaryResourceHPath") && j["primaryResourceHPath"].is_string())
        task.primaryResourceHPath = Utf8ToW(j["primaryResourceHPath"].get<std::string>());
    if (j.contains("secondaryDllPath") && j["secondaryDllPath"].is_string())
        task.secondaryDllPath = Utf8ToW(j["secondaryDllPath"].get<std::string>());
    if (j.contains("secondaryRcPath") && j["secondaryRcPath"].is_string())
        task.secondaryRcPath = Utf8ToW(j["secondaryRcPath"].get<std::string>());
    if (j.contains("secondaryResourceHPath") && j["secondaryResourceHPath"].is_string())
        task.secondaryResourceHPath = Utf8ToW(j["secondaryResourceHPath"].get<std::string>());
    if (j.contains("outputDir") && j["outputDir"].is_string())
        task.outputDir = Utf8ToW(j["outputDir"].get<std::string>());
    if (j.contains("outputDllPath") && j["outputDllPath"].is_string())
        task.outputDllPath = Utf8ToW(j["outputDllPath"].get<std::string>());
    if (j.contains("outputRcPath") && j["outputRcPath"].is_string())
        task.outputRcPath = Utf8ToW(j["outputRcPath"].get<std::string>());
    if (j.contains("outputResourceHPath") && j["outputResourceHPath"].is_string())
        task.outputResourceHPath = Utf8ToW(j["outputResourceHPath"].get<std::string>());

    // 兼容旧配置：若未显式配置 outputDir，则尝试从旧输出路径中提取目录
    if (task.outputDir.IsEmpty())
    {
        if (!task.outputDllPath.IsEmpty())
            task.outputDir = ExtractDirectory(task.outputDllPath);
        else if (!task.outputRcPath.IsEmpty())
            task.outputDir = ExtractDirectory(task.outputRcPath);
        else if (!task.outputResourceHPath.IsEmpty())
            task.outputDir = ExtractDirectory(task.outputResourceHPath);
    }

    return task;
}

// ---- 加载/保存 ----

bool CompareProjectConfigManager::LoadProjectConfig(const CString& path, SCompareProject& outProject)
{
    std::ifstream in(path.GetString());
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

    if (root.contains("tasks") && root["tasks"].is_array())
    {
        for (const auto& jTask : root["tasks"])
        {
            outProject.tasks.push_back(TaskFromJson(jTask));
        }
    }

    return true;
}

bool CompareProjectConfigManager::SaveProjectConfig(const CString& path, const SCompareProject& project)
{
    nlohmann::json root;
    root["projectName"]        = WToUtf8(project.projectName);
    root["projectDescription"] = WToUtf8(project.projectDescription);

    nlohmann::json arrTasks = nlohmann::json::array();
    for (const auto& task : project.tasks)
    {
        arrTasks.push_back(TaskToJson(task));
    }
    root["tasks"] = arrTasks;

    std::ofstream out(path.GetString());
    if (!out.is_open())
        return false;

    out << root.dump(4);
    out.close();
    return true;
}

// ---- 验证 ----

bool CompareProjectConfigManager::ValidateTaskInputs(const SCompareTaskConfig& task,
                                                      std::vector<CStringW>& outErrors)
{
    bool valid = true;

    auto checkFile = [&](const CStringW& p, const wchar_t* label)
    {
        if (p.IsEmpty())
        {
            CStringW msg;
            msg.Format(L"Task '%s': %s is empty", (LPCWSTR)task.taskName, label);
            outErrors.push_back(msg);
            valid = false;
        }
        else if (GetFileAttributesW(p.GetString()) == INVALID_FILE_ATTRIBUTES)
        {
            CStringW msg;
            msg.Format(L"Task '%s': %s not found: %s", (LPCWSTR)task.taskName, label, (LPCWSTR)p);
            outErrors.push_back(msg);
            valid = false;
        }
    };

    // 主语言二进制(DLL/EXE)是必需的
    checkFile(task.primaryDllPath, L"Primary Binary");
    // 次语言二进制(DLL/EXE)是必需的
    checkFile(task.secondaryDllPath, L"Secondary Binary");

    // RC 和 resource.h 是可选的（仅在提供时验证）
    if (!task.primaryRcPath.IsEmpty())
        checkFile(task.primaryRcPath, L"Primary RC");
    if (!task.primaryResourceHPath.IsEmpty())
        checkFile(task.primaryResourceHPath, L"Primary resource.h");
    if (!task.secondaryRcPath.IsEmpty())
        checkFile(task.secondaryRcPath, L"Secondary RC");
    if (!task.secondaryResourceHPath.IsEmpty())
        checkFile(task.secondaryResourceHPath, L"Secondary resource.h");

    return valid;
}

bool CompareProjectConfigManager::ValidateProject(const SCompareProject& project,
                                                   std::vector<CStringW>& outErrors)
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

bool CompareProjectConfigManager::IsFileAccessible(const CStringW& filePath)
{
    HANDLE hFile = CreateFileW(
        filePath.GetString(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    CloseHandle(hFile);
    return true;
}

} // namespace rceditor
