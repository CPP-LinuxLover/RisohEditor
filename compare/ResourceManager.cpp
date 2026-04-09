// ResourceManager.cpp --- Resource file loading and management
//////////////////////////////////////////////////////////////////////////////

#include "ResourceManager.h"
#include "CompareEngine.h"

//////////////////////////////////////////////////////////////////////////////
// PE resource enumeration callback context

struct ResEnumContext
{
	EntrySet* pRes;
	HMODULE hModule;
};

static BOOL CALLBACK ResEnumLangProc(HMODULE hModule, LPCWSTR lpType, LPCWSTR lpName,
                                     WORD wLanguage, LONG_PTR lParam)
{
	ResEnumContext* ctx = (ResEnumContext*)lParam;
	HRSRC hResInfo = FindResourceExW(hModule, lpType, lpName, wLanguage);
	if (!hResInfo)
		return TRUE;

	HGLOBAL hResData = LoadResource(hModule, hResInfo);
	if (!hResData)
		return TRUE;

	DWORD dwSize = SizeofResource(hModule, hResInfo);
	LPVOID pvData = LockResource(hResData);
	if (pvData && dwSize > 0)
	{
		MIdOrString type(lpType);
		MIdOrString name(lpName);
		EntryBase* entry = ctx->pRes->add_lang_entry(type, name, wLanguage);
		if (entry)
		{
			entry->m_data.resize(dwSize);
			memcpy(&entry->m_data[0], pvData, dwSize);
		}
	}
	return TRUE;
}

static BOOL CALLBACK ResEnumNameProc(HMODULE hModule, LPCWSTR lpType, LPWSTR lpName,
                                     LONG_PTR lParam)
{
	EnumResourceLanguagesW(hModule, lpType, lpName, ResEnumLangProc, lParam);
	return TRUE;
}

static BOOL CALLBACK ResEnumTypeProc(HMODULE hModule, LPWSTR lpType, LONG_PTR lParam)
{
	EnumResourceNamesW(hModule, lpType, ResEnumNameProc, lParam);
	return TRUE;
}

//////////////////////////////////////////////////////////////////////////////

ResourceManager::ResourceManager()
	: m_bLoaded(false)
{
}

ResourceManager::~ResourceManager()
{
}

bool ResourceManager::LoadPEFile(LPCWSTR pszPath)
{
	Clear();

	if (!pszPath || !pszPath[0])
		return false;

	if (!LoadPEResources(pszPath, m_entries))
		return false;

	m_wsFilePath = pszPath;
	m_bLoaded = true;
	return true;
}

void ResourceManager::Clear()
{
	m_entries.clear();
	m_wsFilePath.clear();
	m_bLoaded = false;
}

EntryBase* ResourceManager::FindEntry(const MIdOrString& type,
                                      const MIdOrString& name, WORD lang)
{
	return m_entries.find(ET_LANG, type, name, lang);
}

std::vector<LANGID> ResourceManager::GetLanguages() const
{
	return CompareEngine::DetectLanguages(&m_entries);
}

bool ResourceManager::SavePEFile(LPCWSTR pszPath)
{
	if (!pszPath || !m_bLoaded)
		return false;

	return m_entries.update_exe(pszPath) != FALSE;
}

//////////////////////////////////////////////////////////////////////////////
// Static PE loading

bool ResourceManager::LoadPEResources(LPCWSTR pszPath, EntrySet& res)
{
	HMODULE hModule = LoadLibraryExW(pszPath, NULL,
		LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	if (!hModule)
	{
		// Fallback for older OS (Windows XP)
		hModule = LoadLibraryExW(pszPath, NULL, LOAD_LIBRARY_AS_DATAFILE);
	}
	if (!hModule)
		return false;

	ResEnumContext ctx;
	ctx.pRes = &res;
	ctx.hModule = hModule;

	EnumResourceTypesW(hModule, ResEnumTypeProc, (LONG_PTR)&ctx);
	FreeLibrary(hModule);
	return true;
}
