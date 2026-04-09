// CRcCompareControler.cpp --- Controller implementation
//////////////////////////////////////////////////////////////////////////////

#include "CRcCompareControler.h"
#include "MByteStreamEx.hpp"
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

//////////////////////////////////////////////////////////////////////////////
// PE resource enumeration callback context

struct EnumResContext
{
	EntrySet* pRes;
	HMODULE hModule;
};

static BOOL CALLBACK EnumResLangProc(HMODULE hModule, LPCWSTR lpType, LPCWSTR lpName,
                                     WORD wLanguage, LONG_PTR lParam)
{
	EnumResContext* ctx = (EnumResContext*)lParam;
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

static BOOL CALLBACK EnumResNameProc(HMODULE hModule, LPCWSTR lpType, LPWSTR lpName,
                                     LONG_PTR lParam)
{
	EnumResourceLanguagesW(hModule, lpType, lpName, EnumResLangProc, lParam);
	return TRUE;
}

static BOOL CALLBACK EnumResTypeProc(HMODULE hModule, LPWSTR lpType, LONG_PTR lParam)
{
	EnumResourceNamesW(hModule, lpType, EnumResNameProc, lParam);
	return TRUE;
}

//////////////////////////////////////////////////////////////////////////////

CRcCompareControler::CRcCompareControler()
	: m_pPrimaryRes(NULL)
	, m_pSecondaryRes(NULL)
	, m_bOwnsPrimary(false)
	, m_bOwnsSecondary(false)
	, m_nCurrentTaskIndex(-1)
{
}

CRcCompareControler::~CRcCompareControler()
{
}

void CRcCompareControler::SetPrimaryLanguage(LANGID langID)
{
	m_primaryCtx.SetExplicit(langID);
}

void CRcCompareControler::SetSecondaryLanguage(LANGID langID)
{
	m_secondaryCtx.SetExplicit(langID);
}

void CRcCompareControler::SetPrimaryEntrySet(EntrySet* pRes)
{
	m_pPrimaryRes = pRes;
	m_bOwnsPrimary = false;
}

void CRcCompareControler::SetSecondaryEntrySet(EntrySet* pRes)
{
	m_pSecondaryRes = pRes;
	m_bOwnsSecondary = false;
}

bool CRcCompareControler::LoadPrimaryFile(LPCWSTR pszPath)
{
	m_ownedPrimary.clear();
	if (!LoadPEResources(pszPath, m_ownedPrimary))
		return false;
	m_pPrimaryRes = &m_ownedPrimary;
	m_bOwnsPrimary = true;
	return true;
}

bool CRcCompareControler::LoadSecondaryFile(LPCWSTR pszPath)
{
	m_ownedSecondary.clear();
	if (!LoadPEResources(pszPath, m_ownedSecondary))
		return false;
	m_pSecondaryRes = &m_ownedSecondary;
	m_bOwnsSecondary = true;
	return true;
}

void CRcCompareControler::ClearAll()
{
	m_compareEngine.Clear();
	m_ownedPrimary.clear();
	m_ownedSecondary.clear();
	m_pPrimaryRes = NULL;
	m_pSecondaryRes = NULL;
	m_bOwnsPrimary = false;
	m_bOwnsSecondary = false;
	m_vecTaskData.clear();
	m_nCurrentTaskIndex = -1;
}

//////////////////////////////////////////////////////////////////////////////
// Core operations

int CRcCompareControler::DoCompare()
{
	if (!m_pPrimaryRes || !m_pSecondaryRes)
		return 0;

	// Wire up the compare engine with language contexts
	if (m_primaryCtx.wExplicitLang != BAD_LANG)
		m_compareEngine.SetPrimary(m_pPrimaryRes, m_primaryCtx.wExplicitLang);
	else
		m_compareEngine.SetPrimaryAutoDetect(m_pPrimaryRes);

	if (m_secondaryCtx.wExplicitLang != BAD_LANG)
		m_compareEngine.SetSecondary(m_pSecondaryRes, m_secondaryCtx.wExplicitLang);
	else
		m_compareEngine.SetSecondaryAutoDetect(m_pSecondaryRes);

	if (m_pfnOnItemFound)
	{
		m_compareEngine.SetOnItemFoundCallback(m_pfnOnItemFound);
	}

	int nCount = m_compareEngine.DoCompare();

	// Sync resolved languages back to controller context
	m_primaryCtx = m_compareEngine.GetPrimaryContext();
	m_secondaryCtx = m_compareEngine.GetSecondaryContext();

	if (m_pfnOnCompareComplete)
		m_pfnOnCompareComplete(nCount);

	return nCount;
}

SMergeResult CRcCompareControler::ApplyChanges()
{
	m_mergeEngine.SetCompareEngine(&m_compareEngine);
	m_mergeEngine.SetPrimary(m_pPrimaryRes, m_primaryCtx.wEffectiveLang);
	m_mergeEngine.SetSecondary(m_pSecondaryRes, m_secondaryCtx.wEffectiveLang);
	return m_mergeEngine.ApplyAll();
}

bool CRcCompareControler::ApplyItem(int nIndex)
{
	m_mergeEngine.SetCompareEngine(&m_compareEngine);
	m_mergeEngine.SetPrimary(m_pPrimaryRes, m_primaryCtx.wEffectiveLang);
	m_mergeEngine.SetSecondary(m_pSecondaryRes, m_secondaryCtx.wEffectiveLang);
	SMergeResult result;
	return m_mergeEngine.ApplyItem(nIndex, result);
}

bool CRcCompareControler::SaveSecondary(LPCWSTR pszPath)
{
	if (!m_pSecondaryRes || !pszPath)
		return false;

	return m_pSecondaryRes->update_exe(pszPath) != FALSE;
}

//////////////////////////////////////////////////////////////////////////////
// Result accessors (delegate to CompareEngine)

const std::vector<SCompareItem>& CRcCompareControler::GetResults() const
{
	return m_compareEngine.GetResults();
}

int CRcCompareControler::GetResultCount() const
{
	return m_compareEngine.GetResultCount();
}

const SCompareItem* CRcCompareControler::GetResult(int nIndex) const
{
	const std::vector<SCompareItem>& results = m_compareEngine.GetResults();
	if (nIndex < 0 || nIndex >= (int)results.size())
		return NULL;
	return &results[nIndex];
}

const std::vector<SConflictInfo>& CRcCompareControler::GetConflicts() const
{
	return m_compareEngine.GetConflicts();
}

int CRcCompareControler::GetUnmodifiedCount() const
{
	return m_compareEngine.GetUnmodifiedCount();
}

bool CRcCompareControler::IsAllModified() const
{
	return m_compareEngine.IsAllModified();
}

void CRcCompareControler::SetItemModified(int nIndex, bool bModified)
{
	m_compareEngine.SetItemModified(nIndex, bModified);
}

void CRcCompareControler::UpdateItemText(int nIndex, const std::wstring& wsText)
{
	m_compareEngine.UpdateItemText(nIndex, wsText);
}

int CRcCompareControler::GetNextUnmodifiedIndex(int nStartIndex) const
{
	return m_compareEngine.GetNextUnmodifiedIndex(nStartIndex);
}

//////////////////////////////////////////////////////////////////////////////
// Control property updates (direct binary modification)

void CRcCompareControler::UpdateControlProperties(
	UINT nDialogID, WORD nCtrlID,
	const RECT& rect, DWORD dwStyle, DWORD dwExStyle)
{
	if (!m_pSecondaryRes)
		return;

	MIdOrString dlgName(nDialogID);
	EntryBase* entry = m_pSecondaryRes->find(
		ET_LANG, RT_DIALOG, dlgName, m_secondaryCtx.wEffectiveLang);
	if (!entry)
		return;

	DialogRes dlg;
	MByteStreamEx stream(entry->m_data);
	if (!dlg.LoadFromStream(stream))
		return;

	for (size_t i = 0; i < dlg.size(); ++i)
	{
		if (dlg[i].m_id == nCtrlID)
		{
			dlg[i].m_pt.x = (short)rect.left;
			dlg[i].m_pt.y = (short)rect.top;
			dlg[i].m_siz.cx = (short)(rect.right - rect.left);
			dlg[i].m_siz.cy = (short)(rect.bottom - rect.top);
			dlg[i].m_style = dwStyle;
			dlg[i].m_ex_style = dwExStyle;

			MByteStreamEx outStream;
			if (dlg.SaveToStream(outStream))
				entry->m_data = outStream.data();
			return;
		}
	}
}

void CRcCompareControler::UpdateDialogRect(UINT nDialogID, const RECT& rect)
{
	if (!m_pSecondaryRes)
		return;

	MIdOrString dlgName(nDialogID);
	EntryBase* entry = m_pSecondaryRes->find(
		ET_LANG, RT_DIALOG, dlgName, m_secondaryCtx.wEffectiveLang);
	if (!entry)
		return;

	DialogRes dlg;
	MByteStreamEx stream(entry->m_data);
	if (!dlg.LoadFromStream(stream))
		return;

	dlg.m_pt.x = (short)rect.left;
	dlg.m_pt.y = (short)rect.top;
	dlg.m_siz.cx = (short)(rect.right - rect.left);
	dlg.m_siz.cy = (short)(rect.bottom - rect.top);

	MByteStreamEx outStream;
	if (dlg.SaveToStream(outStream))
		entry->m_data = outStream.data();
}

//////////////////////////////////////////////////////////////////////////////
// Mark control modified by index/ID

bool CRcCompareControler::MarkControlModifiedByIndex(UINT nDialogID, int nCtrlIndex)
{
	const std::vector<SCompareItem>& results = m_compareEngine.GetResults();
	for (int i = 0; i < (int)results.size(); ++i)
	{
		const SCompareItem& item = results[i];
		if (item.eType == CIT_CONTROL &&
		    item.nDialogID == nDialogID &&
		    item.nControlIndex == nCtrlIndex)
		{
			m_compareEngine.SetItemModified(i, true);
			return true;
		}
	}
	return false;
}

bool CRcCompareControler::MarkControlModifiedByID(UINT nDialogID, UINT nCtrlID)
{
	const std::vector<SCompareItem>& results = m_compareEngine.GetResults();
	for (int i = 0; i < (int)results.size(); ++i)
	{
		const SCompareItem& item = results[i];
		if (item.eType == CIT_CONTROL &&
		    item.nDialogID == nDialogID &&
		    item.nControlID == (WORD)nCtrlID)
		{
			m_compareEngine.SetItemModified(i, true);
			return true;
		}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////////
// Callbacks

void CRcCompareControler::SetOnItemFoundCallback(OnItemFoundCallback cb)
{
	m_pfnOnItemFound = cb;
}

void CRcCompareControler::SetOnCompareCompleteCallback(OnCompareCompleteCallback cb)
{
	m_pfnOnCompareComplete = cb;
}

//////////////////////////////////////////////////////////////////////////////
// Language auto-detection

std::vector<LANGID> CRcCompareControler::DetectLanguages(const EntrySet* pRes)
{
	return CompareEngine::DetectLanguages(pRes);
}

//////////////////////////////////////////////////////////////////////////////
// PE loading

bool CRcCompareControler::LoadPEResources(LPCWSTR pszPath, EntrySet& res)
{
	HMODULE hModule = LoadLibraryExW(pszPath, NULL,
		LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	if (!hModule)
	{
		// Fallback for older OS
		hModule = LoadLibraryExW(pszPath, NULL, LOAD_LIBRARY_AS_DATAFILE);
	}
	if (!hModule)
		return false;

	EnumResContext ctx;
	ctx.pRes = &res;
	ctx.hModule = hModule;

	EnumResourceTypesW(hModule, EnumResTypeProc, (LONG_PTR)&ctx);
	FreeLibrary(hModule);
	return true;
}

//////////////////////////////////////////////////////////////////////////////
// Batch project support

void CRcCompareControler::SetProject(const rceditor::SCompareProject& project)
{
	m_project = project;
	m_vecTaskData.clear();
	m_vecTaskData.resize(project.tasks.size());
	for (size_t i = 0; i < project.tasks.size(); ++i)
	{
		m_vecTaskData[i].config = project.tasks[i];
	}
	m_nCurrentTaskIndex = -1;
}

CRcCompareControler::STaskData* CRcCompareControler::GetTaskData(int taskIndex)
{
	if (taskIndex < 0 || taskIndex >= (int)m_vecTaskData.size())
		return NULL;
	return &m_vecTaskData[taskIndex];
}

const CRcCompareControler::STaskData* CRcCompareControler::GetTaskData(int taskIndex) const
{
	if (taskIndex < 0 || taskIndex >= (int)m_vecTaskData.size())
		return NULL;
	return &m_vecTaskData[taskIndex];
}

bool CRcCompareControler::LoadTask(int taskIndex, const std::atomic<bool>& cancelFlag,
                                   ProgressCallback progressCb)
{
	STaskData* pData = GetTaskData(taskIndex);
	if (!pData)
		return false;

	const rceditor::SCompareTaskConfig& config = pData->config;

	if (progressCb)
		progressCb(10);

	// Load primary PE
	if (!config.primaryDllPath.empty())
	{
		if (!LoadPEResources(config.primaryDllPath.c_str(), pData->primaryRes))
		{
			pData->wsLastError = L"Failed to load primary DLL: ";
			pData->wsLastError += config.primaryDllPath;
			return false;
		}
	}

	if (cancelFlag.load())
		return false;

	if (progressCb)
		progressCb(50);

	// Load secondary PE
	if (!config.secondaryDllPath.empty())
	{
		if (!LoadPEResources(config.secondaryDllPath.c_str(), pData->secondaryRes))
		{
			pData->wsLastError = L"Failed to load secondary DLL: ";
			pData->wsLastError += config.secondaryDllPath;
			return false;
		}
	}

	if (progressCb)
		progressCb(100);

	pData->bLoaded = true;
	return true;
}

bool CRcCompareControler::CompareTask(int taskIndex)
{
	STaskData* pData = GetTaskData(taskIndex);
	if (!pData || !pData->bLoaded)
		return false;

	CompareEngine engine;
	if (m_primaryCtx.wExplicitLang != BAD_LANG)
		engine.SetPrimary(&pData->primaryRes, m_primaryCtx.wExplicitLang);
	else
		engine.SetPrimaryAutoDetect(&pData->primaryRes);
	if (m_secondaryCtx.wExplicitLang != BAD_LANG)
		engine.SetSecondary(&pData->secondaryRes, m_secondaryCtx.wExplicitLang);
	else
		engine.SetSecondaryAutoDetect(&pData->secondaryRes);
	pData->nDiffCount = engine.DoCompare();
	pData->bCompared = true;
	return true;
}

bool CRcCompareControler::MergeTask(int taskIndex)
{
	STaskData* pData = GetTaskData(taskIndex);
	if (!pData || !pData->bCompared)
		return false;

	MergeEngine merger;
	CompareEngine engine;
	if (m_primaryCtx.wExplicitLang != BAD_LANG)
		engine.SetPrimary(&pData->primaryRes, m_primaryCtx.wExplicitLang);
	else
		engine.SetPrimaryAutoDetect(&pData->primaryRes);
	if (m_secondaryCtx.wExplicitLang != BAD_LANG)
		engine.SetSecondary(&pData->secondaryRes, m_secondaryCtx.wExplicitLang);
	else
		engine.SetSecondaryAutoDetect(&pData->secondaryRes);
	engine.DoCompare();

	// Mark all items as modified for auto-merge
	for (int i = 0; i < engine.GetResultCount(); ++i)
		engine.SetItemModified(i, true);

	merger.SetCompareEngine(&engine);
	merger.SetPrimary(&pData->primaryRes, engine.GetPrimaryLang());
	merger.SetSecondary(&pData->secondaryRes, engine.GetSecondaryLang());
	pData->mergeResult = merger.ApplyAll();
	pData->bMerged = true;
	return true;
}

bool CRcCompareControler::ExportTask(int taskIndex)
{
	STaskData* pData = GetTaskData(taskIndex);
	if (!pData || !pData->bMerged)
		return false;

	// Determine output path
	std::wstring wsOutputPath;
	if (!pData->config.outputDllPath.empty())
	{
		wsOutputPath = pData->config.outputDllPath;
	}
	else if (!pData->config.outputDir.empty())
	{
		// Build output path from output dir + secondary file name
		wsOutputPath = pData->config.outputDir;
		if (wsOutputPath.back() != L'\\')
			wsOutputPath += L'\\';

		std::wstring secPath = pData->config.secondaryDllPath;
		size_t pos = secPath.find_last_of(L"\\/");
		if (pos != std::wstring::npos)
			wsOutputPath += secPath.substr(pos + 1);
		else
			wsOutputPath += secPath;
	}
	else
	{
		pData->wsLastError = L"No output path specified";
		return false;
	}

	BOOL bRet = pData->secondaryRes.update_exe(wsOutputPath.c_str());
	pData->bExported = (bRet != FALSE);
	if (!pData->bExported)
	{
		pData->wsLastError = L"Failed to write output: ";
		pData->wsLastError += wsOutputPath;
	}
	return pData->bExported;
}

bool CRcCompareControler::ValidateBeforeExport(int taskIndex,
                                                std::vector<std::wstring>& outWarnings)
{
	STaskData* pData = GetTaskData(taskIndex);
	if (!pData)
		return false;

	// Basic validation â€?check that task is loaded and compared
	if (!pData->bLoaded)
	{
		outWarnings.push_back(L"Task not loaded");
		return false;
	}
	if (!pData->bCompared)
	{
		outWarnings.push_back(L"Task not compared");
		return false;
	}
	return true;
}

void CRcCompareControler::ProcessSingleTask(
	int taskIndex,
	const rceditor::SCompareTaskConfig& task,
	const std::atomic<bool>& cancelFlag,
	ProgressCallback progressCb)
{
	if (!LoadTask(taskIndex, cancelFlag, progressCb))
		return;

	if (cancelFlag.load())
		return;

	CompareTask(taskIndex);

	if (cancelFlag.load())
		return;

	MergeTask(taskIndex);
}
