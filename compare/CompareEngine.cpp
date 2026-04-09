// CompareEngine.cpp --- Core comparison engine implementation
//////////////////////////////////////////////////////////////////////////////

#include "CompareEngine.h"
#include "NavigationTree.h"
#include "MByteStreamEx.hpp"
#include <algorithm>
#include <map>
#include <set>

//////////////////////////////////////////////////////////////////////////////

CompareEngine::CompareEngine()
	: m_pPrimary(NULL)
	, m_pSecondary(NULL)
{
}

CompareEngine::~CompareEngine()
{
}

void CompareEngine::SetPrimary(EntrySet* pRes, WORD lang)
{
	m_pPrimary = pRes;
	m_primaryCtx.SetExplicit(lang);
}

void CompareEngine::SetSecondary(EntrySet* pRes, WORD lang)
{
	m_pSecondary = pRes;
	m_secondaryCtx.SetExplicit(lang);
}

void CompareEngine::SetPrimaryAutoDetect(EntrySet* pRes)
{
	m_pPrimary = pRes;
	m_primaryCtx.Reset();
}

void CompareEngine::SetSecondaryAutoDetect(EntrySet* pRes)
{
	m_pSecondary = pRes;
	m_secondaryCtx.Reset();
}

void CompareEngine::Clear()
{
	m_vecResults.clear();
	m_vecConflicts.clear();
}

//////////////////////////////////////////////////////////////////////////////
// Language detection utilities

// Detect all unique languages present in an EntrySet
std::vector<LANGID> CompareEngine::DetectLanguages(const EntrySet* pRes)
{
	std::set<LANGID> langSet;
	if (pRes)
	{
		for (auto it = pRes->begin(); it != pRes->end(); ++it)
		{
			EntryBase* entry = *it;
			if (entry && entry->m_et == ET_LANG && entry->m_lang != BAD_LANG)
				langSet.insert(entry->m_lang);
		}
	}
	return std::vector<LANGID>(langSet.begin(), langSet.end());
}

// Detect the most common language in an EntrySet (by resource count).
// More robust than picking the first language by set order.
WORD CompareEngine::DetectMostCommonLanguage(const EntrySet* pRes)
{
	if (!pRes)
		return BAD_LANG;

	std::map<LANGID, int> langCount;
	for (auto it = pRes->begin(); it != pRes->end(); ++it)
	{
		EntryBase* entry = *it;
		if (entry && entry->m_et == ET_LANG && entry->m_lang != BAD_LANG)
			langCount[entry->m_lang]++;
	}

	LANGID bestLang = BAD_LANG;
	int bestCount = 0;
	for (auto it = langCount.begin(); it != langCount.end(); ++it)
	{
		if (it->second > bestCount)
		{
			bestCount = it->second;
			bestLang = it->first;
		}
	}
	return bestLang;
}

// Resolve a language context: detect from data if no explicit override
void CompareEngine::ResolveLanguage(SLanguageContext& ctx, const EntrySet* pRes)
{
	if (ctx.wExplicitLang == BAD_LANG)
		ctx.SetDetected(DetectMostCommonLanguage(pRes));
	else
		ctx.Resolve();
}

//////////////////////////////////////////////////////////////////////////////

int CompareEngine::DoCompare()
{
	Clear();

	if (!m_pPrimary || !m_pSecondary)
		return 0;

	// Resolve languages from data if not explicitly set
	ResolveLanguage(m_primaryCtx, m_pPrimary);
	ResolveLanguage(m_secondaryCtx, m_pSecondary);

	if (!m_primaryCtx.IsResolved() || !m_secondaryCtx.IsResolved())
		return 0;

	// Binary comparison of ALL resources by unique ID (type+name)
	CompareAllResources();

	// Structured comparison for known types (adds detailed items)
	CompareDialogs();
	CompareStrings();
	CompareMenus();

	return (int)m_vecResults.size();
}

//////////////////////////////////////////////////////////////////////////////
// Compare ALL resources by unique ID (type+name) with binary data comparison.
// Adds CIT_RESOURCE items for resources that differ or are missing.

void CompareEngine::CompareAllResources()
{
	const WORD wPriLang = m_primaryCtx.wEffectiveLang;
	const WORD wSecLang = m_secondaryCtx.wEffectiveLang;

	for (auto it = m_pPrimary->begin(); it != m_pPrimary->end(); ++it)
	{
		EntryBase* priEntry = *it;
		if (priEntry->m_et != ET_LANG)
			continue;
		if (priEntry->m_lang != wPriLang)
			continue;

		// Find matching secondary entry by unique ID (type + name)
		EntryBase* secEntry = m_pSecondary->find(
			ET_LANG, priEntry->m_type, priEntry->m_name, wSecLang);

		std::wstring wsTypeDisp = NavigationTree::GetTypeDisplayName(priEntry->m_type);
		std::wstring wsNameDisp = priEntry->m_name.str();

		if (!secEntry)
		{
			// Resource exists in primary but not in secondary
			SCompareItem item;
			item.eType = CIT_RESOURCE;
			item.eStatus = CIS_ADDED;
			item.wsResType = wsTypeDisp;
			item.wsResName = wsNameDisp;
			m_vecResults.push_back(item);

			if (m_pfnOnItemFound)
				m_pfnOnItemFound(item);
		}
		else if (priEntry->m_data != secEntry->m_data)
		{
			// Binary data differs
			SCompareItem item;
			item.eType = CIT_RESOURCE;
			item.eStatus = CIS_CHANGED;
			item.wsResType = wsTypeDisp;
			item.wsResName = wsNameDisp;
			m_vecResults.push_back(item);

			if (m_pfnOnItemFound)
				m_pfnOnItemFound(item);
		}
	}
}

//////////////////////////////////////////////////////////////////////////////
// Compare all dialogs

void CompareEngine::CompareDialogs()
{
	const WORD wPriLang = m_primaryCtx.wEffectiveLang;
	const WORD wSecLang = m_secondaryCtx.wEffectiveLang;

	// Find all dialog entries in primary for the given language
	for (auto it = m_pPrimary->begin(); it != m_pPrimary->end(); ++it)
	{
		EntryBase* priEntry = *it;
		if (priEntry->m_et != ET_LANG)
			continue;
		if (priEntry->m_type != RT_DIALOG)
			continue;
		if (priEntry->m_lang != wPriLang)
			continue;

		// Parse primary dialog
		DialogRes priDlg;
		if (!ParseDialog(priEntry, priDlg))
			continue;

		// Find corresponding dialog in secondary
		EntryBase* secEntry = m_pSecondary->find(
			ET_LANG, RT_DIALOG, priEntry->m_name, wSecLang);

		if (!secEntry)
		{
			// Entire dialog is missing in secondary
			// Add caption as a compare item
			SCompareItem item;
			item.eType = CIT_DIALOG_CAPTION;
			item.eStatus = CIS_ADDED;
			item.nDialogID = priEntry->m_name.m_id;
			item.wsPrimaryText = priDlg.m_title.m_str;
			m_vecResults.push_back(item);

			if (m_pfnOnItemFound)
				m_pfnOnItemFound(item);

			// Add all controls as missing
			for (size_t i = 0; i < priDlg.size(); ++i)
			{
				const DialogItem& priItem = priDlg[i];
				SCompareItem ctrlItem;
				ctrlItem.eType = CIT_CONTROL;
				ctrlItem.eStatus = CIS_ADDED;
				ctrlItem.nDialogID = priEntry->m_name.m_id;
				ctrlItem.nControlID = priItem.m_id;
				ctrlItem.nControlIndex = (int)i;
				ctrlItem.wsPrimaryText = priItem.m_title.m_str;
				m_vecResults.push_back(ctrlItem);

				if (m_pfnOnItemFound)
					m_pfnOnItemFound(ctrlItem);
			}
			continue;
		}

		// Parse secondary dialog
		DialogRes secDlg;
		if (!ParseDialog(secEntry, secDlg))
			continue;

		// Compare dialog captions
		if (priDlg.m_title.m_str != secDlg.m_title.m_str)
		{
			SCompareItem item;
			item.eType = CIT_DIALOG_CAPTION;
			item.eStatus = CIS_CHANGED;
			item.nDialogID = priEntry->m_name.m_id;
			item.wsPrimaryText = priDlg.m_title.m_str;
			item.wsSecondaryText = secDlg.m_title.m_str;
			m_vecResults.push_back(item);

			if (m_pfnOnItemFound)
				m_pfnOnItemFound(item);
		}

		// Compare controls
		CompareDialogControls(priEntry->m_name, priDlg, secDlg);
	}
}

//////////////////////////////////////////////////////////////////////////////
// Compare controls within a dialog pair

void CompareEngine::CompareDialogControls(
	const MIdOrString& dlgName,
	const DialogRes& priDlg,
	const DialogRes& secDlg)
{
	UINT nDialogID = dlgName.m_id;

	// Use topology matcher for robust matching
	std::vector<SControlMatch> matches = m_topoMatcher.MatchControls(priDlg, secDlg);

	for (size_t i = 0; i < matches.size(); ++i)
	{
		const SControlMatch& m = matches[i];
		const DialogItem& priItem = priDlg[m.nPrimaryIndex];

		if (m.nSecondaryIndex < 0)
		{
			// Control exists in primary but not in secondary (added)
			SCompareItem item;
			item.eType = CIT_CONTROL;
			item.eStatus = CIS_ADDED;
			item.nDialogID = nDialogID;
			item.nControlID = priItem.m_id;
			item.nControlIndex = m.nPrimaryIndex;
			item.wsPrimaryText = priItem.m_title.m_str;
			item.bNeedsLayoutAdjust = true;
			m_vecResults.push_back(item);

			if (m_pfnOnItemFound)
				m_pfnOnItemFound(item);
		}
		else if (m.bTextDiffers)
		{
			// Text differs between primary and secondary
			const DialogItem& secItem = secDlg[m.nSecondaryIndex];
			SCompareItem item;
			item.eType = CIT_CONTROL;
			item.eStatus = CIS_CHANGED;
			item.nDialogID = nDialogID;
			item.nControlID = priItem.m_id;
			item.nControlIndex = m.nPrimaryIndex;
			item.wsPrimaryText = priItem.m_title.m_str;
			item.wsSecondaryText = secItem.m_title.m_str;
			item.bNeedsLayoutAdjust = m.bPositionDiffers;
			m_vecResults.push_back(item);

			if (m_pfnOnItemFound)
				m_pfnOnItemFound(item);
		}
	}
}

//////////////////////////////////////////////////////////////////////////////
// Compare string tables

void CompareEngine::CompareStrings()
{
	const WORD wPriLang = m_primaryCtx.wEffectiveLang;
	const WORD wSecLang = m_secondaryCtx.wEffectiveLang;

	// Collect all primary string table entries
	for (auto it = m_pPrimary->begin(); it != m_pPrimary->end(); ++it)
	{
		EntryBase* priEntry = *it;
		if (priEntry->m_et != ET_LANG)
			continue;
		if (priEntry->m_type != RT_STRING)
			continue;
		if (priEntry->m_lang != wPriLang)
			continue;

		WORD wName = priEntry->m_name.m_id;
		StringRes priStr;
		if (!ParseStringTable(priEntry, wName, priStr))
			continue;

		// Find corresponding string table in secondary
		EntryBase* secEntry = m_pSecondary->find(
			ET_LANG, RT_STRING, priEntry->m_name, wSecLang);

		StringRes secStr;
		if (secEntry)
			ParseStringTable(secEntry, wName, secStr);

		// Compare each string in this block
		for (auto& pair : priStr.map())
		{
			WORD wID = pair.first;
			const MStringW& priText = pair.second;

			if (priText.empty())
				continue;

			auto secIt = secStr.map().find(wID);
			if (secIt == secStr.map().end() || secIt->second.empty())
			{
				// String exists in primary but not in secondary
				SCompareItem item;
				item.eType = CIT_STRING;
				item.eStatus = CIS_ADDED;
				item.nStringID = wID;
				item.wsPrimaryText = priText;
				m_vecResults.push_back(item);

				if (m_pfnOnItemFound)
					m_pfnOnItemFound(item);
			}
			else if (priText != secIt->second)
			{
				// Text differs
				SCompareItem item;
				item.eType = CIT_STRING;
				item.eStatus = CIS_CHANGED;
				item.nStringID = wID;
				item.wsPrimaryText = priText;
				item.wsSecondaryText = secIt->second;
				m_vecResults.push_back(item);

				if (m_pfnOnItemFound)
					m_pfnOnItemFound(item);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////
// Compare menus

void CompareEngine::CompareMenus()
{
	const WORD wPriLang = m_primaryCtx.wEffectiveLang;
	const WORD wSecLang = m_secondaryCtx.wEffectiveLang;

	for (auto it = m_pPrimary->begin(); it != m_pPrimary->end(); ++it)
	{
		EntryBase* priEntry = *it;
		if (priEntry->m_et != ET_LANG)
			continue;
		if (priEntry->m_type != RT_MENU)
			continue;
		if (priEntry->m_lang != wPriLang)
			continue;

		MenuRes priMenu;
		if (!ParseMenu(priEntry, priMenu))
			continue;

		EntryBase* secEntry = m_pSecondary->find(
			ET_LANG, RT_MENU, priEntry->m_name, wSecLang);

		if (!secEntry)
		{
			// Entire menu missing in secondary â€?add all items
			if (priMenu.IsExtended())
			{
				for (size_t i = 0; i < priMenu.exitems().size(); ++i)
				{
					const MenuRes::ExMenuItem& mi = priMenu.exitems()[i];
					SCompareItem item;
					item.eType = CIT_MENU;
					item.eStatus = CIS_ADDED;
					item.nMenuID = mi.menuId;
					item.nMenuItemIndex = (int)i;
					item.wsPrimaryText = mi.text;
					m_vecResults.push_back(item);

					if (m_pfnOnItemFound)
						m_pfnOnItemFound(item);
				}
			}
			else
			{
				for (size_t i = 0; i < priMenu.items().size(); ++i)
				{
					const MenuRes::MenuItem& mi = priMenu.items()[i];
					SCompareItem item;
					item.eType = CIT_MENU;
					item.eStatus = CIS_ADDED;
					item.nMenuID = mi.wMenuID;
					item.nMenuItemIndex = (int)i;
					item.wsPrimaryText = mi.text;
					m_vecResults.push_back(item);

					if (m_pfnOnItemFound)
						m_pfnOnItemFound(item);
				}
			}
			continue;
		}

		MenuRes secMenu;
		if (!ParseMenu(secEntry, secMenu))
			continue;

		CompareMenuItems(priEntry->m_name, priMenu, secMenu);
	}
}

//////////////////////////////////////////////////////////////////////////////
// Compare menu items between primary and secondary

void CompareEngine::CompareMenuItems(
	const MIdOrString& menuName,
	const MenuRes& priMenu,
	const MenuRes& secMenu)
{
	if (priMenu.IsExtended())
	{
		// Build ID-to-index map for secondary
		std::map<UINT, size_t> secIdMap;
		for (size_t i = 0; i < secMenu.exitems().size(); ++i)
		{
			UINT id = secMenu.exitems()[i].menuId;
			if (id != 0)
				secIdMap[id] = i;
		}

		for (size_t i = 0; i < priMenu.exitems().size(); ++i)
		{
			const MenuRes::ExMenuItem& priItem = priMenu.exitems()[i];
			if (priItem.text.empty())
				continue;

			auto secIt = secIdMap.find(priItem.menuId);
			if (secIt == secIdMap.end())
			{
				SCompareItem item;
				item.eType = CIT_MENU;
				item.eStatus = CIS_ADDED;
				item.nMenuID = priItem.menuId;
				item.nMenuItemIndex = (int)i;
				item.wsPrimaryText = priItem.text;
				m_vecResults.push_back(item);

				if (m_pfnOnItemFound)
					m_pfnOnItemFound(item);
			}
			else
			{
				const MStringW& secText = secMenu.exitems()[secIt->second].text;
				if (priItem.text != secText)
				{
					SCompareItem item;
					item.eType = CIT_MENU;
					item.eStatus = CIS_CHANGED;
					item.nMenuID = priItem.menuId;
					item.nMenuItemIndex = (int)i;
					item.wsPrimaryText = priItem.text;
					item.wsSecondaryText = secText;
					m_vecResults.push_back(item);

					if (m_pfnOnItemFound)
						m_pfnOnItemFound(item);
				}
			}
		}
	}
	else
	{
		// Standard menu: match by wMenuID
		std::map<WORD, size_t> secIdMap;
		for (size_t i = 0; i < secMenu.items().size(); ++i)
		{
			WORD id = secMenu.items()[i].wMenuID;
			if (id != 0)
				secIdMap[id] = i;
		}

		for (size_t i = 0; i < priMenu.items().size(); ++i)
		{
			const MenuRes::MenuItem& priItem = priMenu.items()[i];
			if (priItem.text.empty())
				continue;

			auto secIt = secIdMap.find(priItem.wMenuID);
			if (secIt == secIdMap.end())
			{
				SCompareItem item;
				item.eType = CIT_MENU;
				item.eStatus = CIS_ADDED;
				item.nMenuID = priItem.wMenuID;
				item.nMenuItemIndex = (int)i;
				item.wsPrimaryText = priItem.text;
				m_vecResults.push_back(item);

				if (m_pfnOnItemFound)
					m_pfnOnItemFound(item);
			}
			else
			{
				const MStringW& secText = secMenu.items()[secIt->second].text;
				if (priItem.text != secText)
				{
					SCompareItem item;
					item.eType = CIT_MENU;
					item.eStatus = CIS_CHANGED;
					item.nMenuID = priItem.wMenuID;
					item.nMenuItemIndex = (int)i;
					item.wsPrimaryText = priItem.text;
					item.wsSecondaryText = secText;
					m_vecResults.push_back(item);

					if (m_pfnOnItemFound)
						m_pfnOnItemFound(item);
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////
// Binary parsing helpers

bool CompareEngine::ParseDialog(EntryBase* entry, DialogRes& dlg)
{
	if (!entry || entry->empty())
		return false;

	MByteStreamEx stream(entry->m_data);
	return dlg.LoadFromStream(stream);
}

bool CompareEngine::ParseMenu(EntryBase* entry, MenuRes& menu)
{
	if (!entry || entry->empty())
		return false;

	// Detect extended menu format by peeking at version field in header
	bool bExtended = false;
	if (entry->m_data.size() >= sizeof(WORD))
	{
		WORD wVersion = *(const WORD*)&entry->m_data[0];
		bExtended = (wVersion == 1);
	}

	MByteStreamEx stream(entry->m_data);
	if (bExtended)
		return menu.LoadFromStreamEx(stream);
	return menu.LoadFromStream(stream);
}

bool CompareEngine::ParseStringTable(EntryBase* entry, WORD wName, StringRes& str)
{
	if (!entry || entry->empty())
		return false;

	MByteStreamEx stream(entry->m_data);
	return str.LoadFromStream(stream, wName);
}

//////////////////////////////////////////////////////////////////////////////
// Result state management

int CompareEngine::GetUnmodifiedCount() const
{
	int count = 0;
	for (size_t i = 0; i < m_vecResults.size(); ++i)
	{
		if (!m_vecResults[i].bModified)
			++count;
	}
	return count;
}

bool CompareEngine::IsAllModified() const
{
	for (size_t i = 0; i < m_vecResults.size(); ++i)
	{
		if (!m_vecResults[i].bModified)
			return false;
	}
	return true;
}

void CompareEngine::SetItemModified(int nIndex, bool bModified)
{
	if (nIndex >= 0 && nIndex < (int)m_vecResults.size())
		m_vecResults[nIndex].bModified = bModified;
}

void CompareEngine::UpdateItemText(int nIndex, const std::wstring& wsText)
{
	if (nIndex >= 0 && nIndex < (int)m_vecResults.size())
	{
		m_vecResults[nIndex].wsSecondaryText = wsText;
		m_vecResults[nIndex].bModified = true;
	}
}

int CompareEngine::GetNextUnmodifiedIndex(int nStartIndex) const
{
	int start = (nStartIndex < 0) ? 0 : nStartIndex + 1;
	for (int i = start; i < (int)m_vecResults.size(); ++i)
	{
		if (!m_vecResults[i].bModified)
			return i;
	}
	return -1;
}
