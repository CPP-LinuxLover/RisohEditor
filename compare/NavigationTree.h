// NavigationTree.h --- Resource tree navigation module
//
// Manages a TreeView control that displays resources organized by
// Type -> Name hierarchy. Supports language filtering, selection
// tracking, and navigation from compare results to tree nodes.
// Extracted from CRcCompareViewer for modular reuse.
//////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _INC_WINDOWS
	#include <windows.h>
#endif
#include <map>
#include <string>
#include <functional>
#include <commctrl.h>
#include "Res.hpp"
#include "DialogRes.hpp"
#include "MByteStreamEx.hpp"
#include "CompareTypes.h"

//////////////////////////////////////////////////////////////////////////////

class NavigationTree
{
public:
	// Callback when tree selection changes (entry may be NULL for type nodes)
	using OnSelectionChangedCallback = std::function<void(EntryBase* pEntry)>;

	NavigationTree();
	~NavigationTree();

	// Set the TreeView control handle
	void SetTreeView(HWND hwndTV);

	// Build tree from an EntrySet, optionally filtering by language
	void Build(const EntrySet& entries, LANGID langFilter = 0);

	// Clear the tree
	void Clear();

	// Select a specific entry in the tree
	void SelectEntry(const MIdOrString& type, const MIdOrString& name, LANGID lang = 0);

	// Navigate tree to match a compare result item
	void NavigateToCompareItem(const SCompareItem& item);

	// Handle TVN_SELCHANGED notification; returns the selected EntryBase* or NULL
	EntryBase* OnSelectionChanged(NMTREEVIEWW* pNMTV);

	// Set selection changed callback
	void SetOnSelectionChanged(OnSelectionChangedCallback cb) { m_pfnOnSelChanged = cb; }

	// Get display name for a resource type
	static std::wstring GetTypeDisplayName(const MIdOrString& type);

private:
	HTREEITEM AddTypeNode(const MIdOrString& type);
	HTREEITEM AddNameNode(HTREEITEM hParent, const MIdOrString& type,
	                      const MIdOrString& name, EntryBase* pEntry);

	HWND m_hwndTreeView;
	OnSelectionChangedCallback m_pfnOnSelChanged;

	// Maps for fast lookup: typeKey -> HTREEITEM, "type/name" -> HTREEITEM
	std::map<std::wstring, HTREEITEM> m_mapTypeItems;
	std::map<std::wstring, HTREEITEM> m_mapNameItems;
};
