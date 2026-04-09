// CompareEngine.h --- Core comparison engine for primary/secondary resources
// Operates directly on RisohEditor's EntrySet, DialogRes, MenuRes, StringRes.
// Single responsibility: find differences between two language variants.
//////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _INC_WINDOWS
	#include <windows.h>
#endif
#include <vector>
#include <set>
#include <functional>
#include "Res.hpp"
#include "DialogRes.hpp"
#include "MenuRes.hpp"
#include "StringRes.hpp"
#include "CompareTypes.h"
#include "TopologyMatcher.h"

//////////////////////////////////////////////////////////////////////////////

class CompareEngine
{
public:
	// Callback: fires when a compare item is found
	using OnItemFoundCallback = std::function<void(const SCompareItem& item)>;

	CompareEngine();
	~CompareEngine();

	// Set source data with explicit language (backward-compatible)
	void SetPrimary(EntrySet* pRes, WORD lang);
	void SetSecondary(EntrySet* pRes, WORD lang);

	// Set source data with auto-detection (lang = BAD_LANG)
	void SetPrimaryAutoDetect(EntrySet* pRes);
	void SetSecondaryAutoDetect(EntrySet* pRes);

	// Direct access to language contexts
	SLanguageContext& GetPrimaryContext() { return m_primaryCtx; }
	SLanguageContext& GetSecondaryContext() { return m_secondaryCtx; }
	const SLanguageContext& GetPrimaryContext() const { return m_primaryCtx; }
	const SLanguageContext& GetSecondaryContext() const { return m_secondaryCtx; }

	// Resolved language accessors (for callers that need the final LANGID)
	WORD GetPrimaryLang() const { return m_primaryCtx.wEffectiveLang; }
	WORD GetSecondaryLang() const { return m_secondaryCtx.wEffectiveLang; }

	// Execute comparison. Returns number of differences found.
	int DoCompare();

	// Clear all results and reset
	void Clear();

	// Results access
	const std::vector<SCompareItem>& GetResults() const { return m_vecResults; }
	const std::vector<SConflictInfo>& GetConflicts() const { return m_vecConflicts; }
	int GetResultCount() const { return (int)m_vecResults.size(); }
	int GetUnmodifiedCount() const;
	bool IsAllModified() const;

	// Mark item as modified
	void SetItemModified(int nIndex, bool bModified = true);

	// Update item text (secondary)
	void UpdateItemText(int nIndex, const std::wstring& wsText);

	// Get next unmodified item index
	int GetNextUnmodifiedIndex(int nStartIndex = -1) const;

	// Set callback
	void SetOnItemFoundCallback(OnItemFoundCallback cb) { m_pfnOnItemFound = cb; }

	// --- Language detection utilities (static) ---

	// Detect all unique languages in an EntrySet
	static std::vector<LANGID> DetectLanguages(const EntrySet* pRes);

	// Detect the most common language in an EntrySet (by resource count)
	static WORD DetectMostCommonLanguage(const EntrySet* pRes);

	// Resolve language context: apply detection to an EntrySet, respecting explicit override
	static void ResolveLanguage(SLanguageContext& ctx, const EntrySet* pRes);

private:
	// Compare all resources by unique ID (type+name) with binary data comparison
	void CompareAllResources();

	// Compare dialogs between primary and secondary
	void CompareDialogs();

	// Compare string tables
	void CompareStrings();

	// Compare menus
	void CompareMenus();

	// Compare individual dialog controls using topology matcher
	void CompareDialogControls(
		const MIdOrString& dlgName,
		const DialogRes& priDlg,
		const DialogRes& secDlg);

	// Compare menu items (recursive for standard and extended)
	void CompareMenuItems(
		const MIdOrString& menuName,
		const MenuRes& priMenu,
		const MenuRes& secMenu);

	// Parse binary resource data into typed structure
	bool ParseDialog(EntryBase* entry, DialogRes& dlg);
	bool ParseMenu(EntryBase* entry, MenuRes& menu);
	bool ParseStringTable(EntryBase* entry, WORD wName, StringRes& str);

private:
	EntrySet* m_pPrimary;
	EntrySet* m_pSecondary;
	SLanguageContext m_primaryCtx;
	SLanguageContext m_secondaryCtx;

	std::vector<SCompareItem> m_vecResults;
	std::vector<SConflictInfo> m_vecConflicts;
	TopologyMatcher m_topoMatcher;
	OnItemFoundCallback m_pfnOnItemFound;
};
