#pragma once

// CRcCompareControler.h --- PE resource comparison controller
//
// Orchestrates comparison and merge of two language variants using
// CompareEngine + MergeEngine, operating directly on RisohEditor's EntrySet.

#ifndef _INC_WINDOWS
	#include <windows.h>
#endif
#include <map>
#include <set>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include "CompareEngine.h"
#include "MergeEngine.h"
#include "ResourceManager.h"
#include "CompareProjectConfig.h"
#include "CCompareWorkerThread.h"

//////////////////////////////////////////////////////////////////////////////
// Controller class — orchestrates CompareEngine + MergeEngine

class CRcCompareControler
{
public:
	// Callback types
	using OnItemFoundCallback = std::function<void(const SCompareItem& item)>;
	using OnCompareCompleteCallback = std::function<void(int nTotalDiffs)>;
	using ProgressCallback = std::function<void(int percent)>;

	CRcCompareControler();
	virtual ~CRcCompareControler();

	// Language settings
	void SetPrimaryLanguage(LANGID langID);
	void SetSecondaryLanguage(LANGID langID);
	LANGID GetPrimaryLanguage() const { return m_primaryCtx.wEffectiveLang; }
	LANGID GetSecondaryLanguage() const { return m_secondaryCtx.wEffectiveLang; }
	const SLanguageContext& GetPrimaryContext() const { return m_primaryCtx; }
	const SLanguageContext& GetSecondaryContext() const { return m_secondaryCtx; }

	// Set EntrySet pointers (owned by RisohEditor, not by us)
	void SetPrimaryEntrySet(EntrySet* pRes);
	void SetSecondaryEntrySet(EntrySet* pRes);

	// Load PE file into internal EntrySet
	bool LoadPrimaryFile(LPCWSTR pszPath);
	bool LoadSecondaryFile(LPCWSTR pszPath);

	// Clear all state
	void ClearAll();

	// Execute comparison — returns number of differences
	int DoCompare();

	// Apply all modified items to secondary EntrySet
	SMergeResult ApplyChanges();

	// Apply single item
	bool ApplyItem(int nIndex);

	// Save secondary EntrySet to PE file
	bool SaveSecondary(LPCWSTR pszPath);

	// Access compare results (delegated from CompareEngine)
	const std::vector<SCompareItem>& GetResults() const;
	int GetResultCount() const;
	const SCompareItem* GetResult(int nIndex) const;

	// Conflict access
	const std::vector<SConflictInfo>& GetConflicts() const;

	// Modification tracking (delegated to CompareEngine)
	int GetUnmodifiedCount() const;
	bool IsAllModified() const;
	void SetItemModified(int nIndex, bool bModified = true);
	void UpdateItemText(int nIndex, const std::wstring& wsText);
	int GetNextUnmodifiedIndex(int nStartIndex = -1) const;

	// Control property update (modifies secondary dialog binary directly)
	void UpdateControlProperties(UINT nDialogID, WORD nCtrlID,
	                             const RECT& rect, DWORD dwStyle, DWORD dwExStyle);
	void UpdateDialogRect(UINT nDialogID, const RECT& rect);

	// Mark item modified by control index/ID
	bool MarkControlModifiedByIndex(UINT nDialogID, int nCtrlIndex);
	bool MarkControlModifiedByID(UINT nDialogID, UINT nCtrlID);

	// Set callbacks
	void SetOnItemFoundCallback(OnItemFoundCallback cb);
	void SetOnCompareCompleteCallback(OnCompareCompleteCallback cb);

	// Access engines directly (for advanced usage)
	CompareEngine& GetCompareEngine() { return m_compareEngine; }
	MergeEngine& GetMergeEngine() { return m_mergeEngine; }

	// Access EntrySet
	EntrySet* GetPrimaryEntrySet() { return m_pPrimaryRes; }
	EntrySet* GetSecondaryEntrySet() { return m_pSecondaryRes; }

	// Language auto-detection: delegates to CompareEngine
	static std::vector<LANGID> DetectLanguages(const EntrySet* pRes);

	// ====================================================================
	// Batch project support
	// ====================================================================

	// Per-task data
	struct STaskData
	{
		EntrySet primaryRes;        // Primary PE resources
		EntrySet secondaryRes;      // Secondary PE resources
		rceditor::SCompareTaskConfig config;
		SMergeResult mergeResult;
		int nDiffCount;
		bool bLoaded;
		bool bCompared;
		bool bMerged;
		bool bExported;
		std::wstring wsLastError;

		STaskData()
			: nDiffCount(0)
			, bLoaded(false)
			, bCompared(false)
			, bMerged(false)
			, bExported(false)
		{
		}
	};

	void SetProject(const rceditor::SCompareProject& project);
	const rceditor::SCompareProject& GetProject() const { return m_project; }

	std::vector<STaskData>& GetTaskDataVec() { return m_vecTaskData; }
	const std::vector<STaskData>& GetTaskDataVec() const { return m_vecTaskData; }
	STaskData* GetTaskData(int taskIndex);
	const STaskData* GetTaskData(int taskIndex) const;

	bool LoadTask(int taskIndex, const std::atomic<bool>& cancelFlag,
	              ProgressCallback progressCb = nullptr);
	bool CompareTask(int taskIndex);
	bool MergeTask(int taskIndex);
	bool ExportTask(int taskIndex);
	bool ValidateBeforeExport(int taskIndex, std::vector<std::wstring>& outWarnings);

	// Worker thread entry point
	void ProcessSingleTask(int taskIndex,
	                       const rceditor::SCompareTaskConfig& task,
	                       const std::atomic<bool>& cancelFlag,
	                       ProgressCallback progressCb);

	int GetCurrentTaskIndex() const { return m_nCurrentTaskIndex; }
	void SetCurrentTaskIndex(int idx) { m_nCurrentTaskIndex = idx; }

private:
	// Load PE file resources into EntrySet
	static bool LoadPEResources(LPCWSTR pszPath, EntrySet& res);

	SLanguageContext m_primaryCtx;
	SLanguageContext m_secondaryCtx;

	// EntrySet pointers — may be external (from RisohEditor) or from task data
	EntrySet* m_pPrimaryRes;
	EntrySet* m_pSecondaryRes;
	bool m_bOwnsPrimary;        // true if we loaded it ourselves
	bool m_bOwnsSecondary;

	// Engines
	CompareEngine m_compareEngine;
	MergeEngine m_mergeEngine;

	// Callbacks
	OnItemFoundCallback m_pfnOnItemFound;
	OnCompareCompleteCallback m_pfnOnCompareComplete;

	// Batch project
	rceditor::SCompareProject m_project;
	std::vector<STaskData> m_vecTaskData;
	int m_nCurrentTaskIndex;

	// Owned EntrySet storage (for standalone PE loading)
	EntrySet m_ownedPrimary;
	EntrySet m_ownedSecondary;
};
