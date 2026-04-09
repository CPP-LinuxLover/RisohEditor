// LevelManagerJsonHelper.cpp --- JSON helper utilities
//////////////////////////////////////////////////////////////////////////////

#include "LevelManagerJsonHelper.h"
#include <fstream>

namespace rceditor {

std::string LevelManagerJsonHelper::WToUtf8(const std::wstring& str)
{
	if (str.empty()) return {};
	int utf8len = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(),
	                                  NULL, 0, NULL, NULL);
	if (utf8len <= 0) return {};
	std::string utf8str(utf8len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(),
	                    &utf8str[0], utf8len, NULL, NULL);
	return utf8str;
}

std::wstring LevelManagerJsonHelper::Utf8ToW(const std::string& utf8)
{
	if (utf8.empty()) return L"";
	int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
	if (wlen <= 0) return L"";
	std::wstring out(wlen, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &out[0], wlen);
	return out;
}

bool LevelManagerJsonHelper::LoadJson(LPCWSTR path, nlohmann::json& outRoot)
{
	// Convert wide path to narrow for std::ifstream (XP compat)
	char szPath[MAX_PATH * 2];
	WideCharToMultiByte(CP_ACP, 0, path, -1, szPath, sizeof(szPath), NULL, NULL);

	std::ifstream in(szPath);
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

bool LevelManagerJsonHelper::SaveJson(LPCWSTR path, const nlohmann::json& root)
{
	char szPath[MAX_PATH * 2];
	WideCharToMultiByte(CP_ACP, 0, path, -1, szPath, sizeof(szPath), NULL, NULL);

	std::ofstream out(szPath);
	if (!out.is_open())
		return false;
	out << root.dump(4);
	out.close();
	return true;
}

} // namespace rceditor
