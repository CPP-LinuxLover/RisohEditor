// NavigationTree.cpp --- Resource tree navigation implementation
//////////////////////////////////////////////////////////////////////////////

#include "NavigationTree.h"
#include "MByteStreamEx.hpp"
#include <strsafe.h>

//////////////////////////////////////////////////////////////////////////////

NavigationTree::NavigationTree()
	: m_hwndTreeView(NULL)
{
}

NavigationTree::~NavigationTree()
{
}

void NavigationTree::SetTreeView(HWND hwndTV)
{
	m_hwndTreeView = hwndTV;
}

void NavigationTree::Build(const EntrySet& entries, LANGID langFilter)
{
	Clear();

	if (!m_hwndTreeView)
		return;

	for (auto it = entries.begin(); it != entries.end(); ++it)
	{
		EntryBase* entry = *it;
		if (entry->m_et != ET_LANG)
			continue;

		// Filter by language (0 = show all)
		if (langFilter != 0 && entry->m_lang != langFilter)
			continue;

		HTREEITEM hType = AddTypeNode(entry->m_type);
		HTREEITEM hName = AddNameNode(hType, entry->m_type, entry->m_name, entry);

		// Store EntryBase* in the name node
		TV_ITEM tvi;
		ZeroMemory(&tvi, sizeof(tvi));
		tvi.mask = TVIF_PARAM;
		tvi.hItem = hName;
		tvi.lParam = (LPARAM)entry;
		TreeView_SetItem(m_hwndTreeView, &tvi);
	}
}

void NavigationTree::Clear()
{
	if (m_hwndTreeView)
		TreeView_DeleteAllItems(m_hwndTreeView);
	m_mapTypeItems.clear();
	m_mapNameItems.clear();
}

void NavigationTree::SelectEntry(const MIdOrString& type,
                                 const MIdOrString& name, LANGID lang)
{
	(void)lang; // Currently unused; future: multi-language tree with lang nodes

	if (!m_hwndTreeView)
		return;

	std::wstring typeStr = type.str();
	std::wstring nameStr = typeStr + L"/" + name.str();

	auto itType = m_mapTypeItems.find(typeStr);
	if (itType != m_mapTypeItems.end())
	{
		TreeView_Expand(m_hwndTreeView, itType->second, TVE_EXPAND);

		auto itName = m_mapNameItems.find(nameStr);
		if (itName != m_mapNameItems.end())
		{
			TreeView_SelectItem(m_hwndTreeView, itName->second);
			return;
		}
		TreeView_SelectItem(m_hwndTreeView, itType->second);
	}
}

void NavigationTree::NavigateToCompareItem(const SCompareItem& item)
{
	if (!m_hwndTreeView)
		return;

	MIdOrString typeKey;
	MIdOrString nameKey;

	switch (item.eType)
	{
	case CIT_CONTROL:
	case CIT_DIALOG_CAPTION:
		typeKey = MIdOrString(LOWORD(RT_DIALOG));
		nameKey = MIdOrString((WORD)item.nDialogID);
		break;
	case CIT_MENU:
		typeKey = MIdOrString(LOWORD(RT_MENU));
		nameKey = MIdOrString((WORD)item.nMenuID);
		break;
	case CIT_STRING:
		typeKey = MIdOrString(LOWORD(RT_STRING));
		nameKey = MIdOrString((WORD)((item.nStringID >> 4) + 1));
		break;
	default:
		return;
	}

	SelectEntry(typeKey, nameKey);
}

EntryBase* NavigationTree::OnSelectionChanged(NMTREEVIEWW* pNMTV)
{
	if (!pNMTV || !m_hwndTreeView)
		return NULL;

	HTREEITEM hItem = pNMTV->itemNew.hItem;
	if (!hItem)
		return NULL;

	TV_ITEM tvi;
	ZeroMemory(&tvi, sizeof(tvi));
	tvi.mask = TVIF_PARAM;
	tvi.hItem = hItem;
	TreeView_GetItem(m_hwndTreeView, &tvi);

	EntryBase* pEntry = (EntryBase*)tvi.lParam;

	if (m_pfnOnSelChanged)
		m_pfnOnSelChanged(pEntry);

	return pEntry;
}

//////////////////////////////////////////////////////////////////////////////
// Tree node helpers

HTREEITEM NavigationTree::AddTypeNode(const MIdOrString& type)
{
	std::wstring key = type.str();
	auto it = m_mapTypeItems.find(key);
	if (it != m_mapTypeItems.end())
		return it->second;

	std::wstring display = GetTypeDisplayName(type);

	TV_INSERTSTRUCTW tvis;
	ZeroMemory(&tvis, sizeof(tvis));
	tvis.hParent = TVI_ROOT;
	tvis.hInsertAfter = TVI_SORT;
	tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
	tvis.item.pszText = const_cast<LPWSTR>(display.c_str());
	tvis.item.lParam = 0;

	HTREEITEM hItem = TreeView_InsertItem(m_hwndTreeView, &tvis);
	m_mapTypeItems[key] = hItem;
	return hItem;
}

HTREEITEM NavigationTree::AddNameNode(HTREEITEM hParent,
                                      const MIdOrString& type,
                                      const MIdOrString& name,
                                      EntryBase* pEntry)
{
	std::wstring key = type.str() + L"/" + name.str();
	auto it = m_mapNameItems.find(key);
	if (it != m_mapNameItems.end())
		return it->second;

	std::wstring display = name.str();

	// For dialog resources, append the dialog caption
	if (pEntry && type.m_id == LOWORD(RT_DIALOG) && !pEntry->m_data.empty())
	{
		MByteStreamEx stream(pEntry->m_data);
		DialogRes dlgRes;
		if (dlgRes.LoadFromStream(stream) && !dlgRes.m_title.empty())
		{
			display += L" - ";
			display += dlgRes.m_title.str();
		}
	}

	TV_INSERTSTRUCTW tvis;
	ZeroMemory(&tvis, sizeof(tvis));
	tvis.hParent = hParent;
	tvis.hInsertAfter = TVI_SORT;
	tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
	tvis.item.pszText = const_cast<LPWSTR>(display.c_str());
	tvis.item.lParam = 0;

	HTREEITEM hItem = TreeView_InsertItem(m_hwndTreeView, &tvis);
	m_mapNameItems[key] = hItem;
	return hItem;
}

//////////////////////////////////////////////////////////////////////////////
// Resource type display names

std::wstring NavigationTree::GetTypeDisplayName(const MIdOrString& type)
{
	if (type.m_id)
	{
		switch (type.m_id)
		{
		case 1:  return L"RT_CURSOR (1)";
		case 2:  return L"RT_BITMAP (2)";
		case 3:  return L"RT_ICON (3)";
		case 4:  return L"RT_MENU (4)";
		case 5:  return L"RT_DIALOG (5)";
		case 6:  return L"RT_STRING (6)";
		case 7:  return L"RT_FONTDIR (7)";
		case 8:  return L"RT_FONT (8)";
		case 9:  return L"RT_ACCELERATOR (9)";
		case 10: return L"RT_RCDATA (10)";
		case 11: return L"RT_MESSAGETABLE (11)";
		case 12: return L"RT_GROUP_CURSOR (12)";
		case 14: return L"RT_GROUP_ICON (14)";
		case 16: return L"RT_VERSION (16)";
		case 17: return L"RT_DLGINCLUDE (17)";
		case 19: return L"RT_PLUGPLAY (19)";
		case 20: return L"RT_VXD (20)";
		case 21: return L"RT_ANICURSOR (21)";
		case 22: return L"RT_ANIICON (22)";
		case 23: return L"RT_HTML (23)";
		case 24: return L"RT_MANIFEST (24)";
		default:
			{
				WCHAR buf[32];
				StringCchPrintfW(buf, _countof(buf), L"#%u", type.m_id);
				return buf;
			}
		}
	}
	return std::wstring(type.m_str.c_str());
}
