#include "stdafx.h"
#include "LevelManagerJsonHelper.h"

#include <fstream>

namespace rceditor {

std::string LevelManagerJsonHelper::CStringWToUtf8(const CStringW& str)
{
    if (str.IsEmpty()) return {};
    int utf8len = WideCharToMultiByte(CP_UTF8, 0, str.GetString(), str.GetLength(), nullptr, 0, nullptr, nullptr);
    if (utf8len <= 0) return {};
    std::string utf8str(utf8len, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.GetString(), str.GetLength(), &utf8str[0], utf8len, nullptr, nullptr);
    return utf8str;
}

CStringW LevelManagerJsonHelper::Utf8ToCStringW(const std::string& utf8)
{
    if (utf8.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    if (wlen <= 0) return L"";
    CStringW out;
    wchar_t* buf = out.GetBuffer(wlen);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), buf, wlen);
    out.ReleaseBuffer(wlen);
    return out;
}

bool LevelManagerJsonHelper::LoadJson(const CString& path, nlohmann::json& outRoot)
{
    // 从文件读取并解析 JSON，返回解析结果（路径为本地编码 CString）
    std::ifstream in(path.GetString());
    if (!in.is_open())
        return false;
    outRoot = nlohmann::json::parse(in, nullptr, false);
    if (outRoot.is_discarded())
    {
        in.close();
        return false;
    }
    in.close();
    return true;
}

bool LevelManagerJsonHelper::SaveJson(const CString& path, const nlohmann::json& root)
{
    // 将 JSON 写入磁盘（覆盖或新建文件）
    std::ofstream out(path.GetString());
    if (!out.is_open())
        return false;
    out << root.dump(4);
    out.close();
    return true;
}

} // namespace rceditor
