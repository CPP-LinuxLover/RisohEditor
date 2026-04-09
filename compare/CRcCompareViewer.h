#pragma once

// CRcCompareViewer.h --- PE resource compare viewer (Win32 native)
//
// Layout: TreeView + (dialog preview [primary] / RC text [secondary])
// using MSplitterWnd, with a bottom panel for compare results ListView.
// Languages are auto-detected from the EntrySet.

#include "MWindowBase.hpp"
#include "MSplitterWnd.hpp"
#include "CRcCompareControler.h"
#include "CompareProjectConfig.h"
#include "CompareTypes.h"
#include "DialogRes.hpp"
#include "NavigationTree.h"
#include "ViewRenderer.h"
#include "ResourceManager.h"
#include <commctrl.h>
#include <map>
#include <set>

class CRcCompareViewer : public MWindowBase
{
public:
	explicit CRcCompareViewer(CRcCompareControler* pControler, HWND hParent = NULL);
	virtual ~CRcCompareViewer();

	// Show the viewer window (modal-like: runs own message loop, returns on close)
	INT DoViewerWindow();

	void SetCompareControler(CRcCompareControler* pControler);
	bool NavigateToAddedItem(int nIndex);
	bool NavigateToNextUnmodified();
	bool NavigateToPrevUnmodified();
	int GetCurrentAddedItemIndex() const { return m_nCurrentAddedIndex; }
	void RefreshAddedItemsList();
	bool CanClose() const;
	std::wstring GetProgressText() const;

	virtual LPCTSTR GetWndClassNameDx() const
	{
		return TEXT("RcCompareViewer");
	}

protected:
	virtual LRESULT CALLBACK
	WindowProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	BOOL OnCreate(HWND hwnd, LPCREATESTRUCT lpcs);
	void OnDestroy(HWND hwnd);
	void OnSize(HWND hwnd, int cx, int cy);
	void OnClose(HWND hwnd);
	void OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
	LRESULT OnNotify(HWND hwnd, int idFrom, NMHDR* pNMHDR);
	void OnTimer(UINT_PTR nIDEvent);

	// Button handlers
	void OnBtnNextUnmodified();
	void OnBtnPrevUnmodified();
	void OnBtnExportRc();
	void OnBtnSave();
	void OnBtnAcceptAllPrimary();
	void OnBtnMarkAllDone();
	void OnComboTaskSelChange();
	void OnComboFilterSelChange();
	void OnComboPrimaryLangSelChange();
	void OnComboSecondaryLangSelChange();
	void OnChkShowPrimary();

	enum EViewerCtrlID
	{
		IDC_TREE_RESOURCES = 5100,
		IDC_LIST_ADDED_ITEMS = 5101,
		IDC_BTN_NEXT_UNMOD = 5102,
		IDC_BTN_PREV_UNMOD = 5103,
		IDC_BTN_EXPORT_RC = 5104,
		IDC_BTN_SAVE_RES = 5105,
		IDC_STATIC_PROGRESS = 5106,
		IDC_COMBO_TASK = 5107,
		IDC_COMBO_FILTER = 5108,
		IDC_STATIC_TASK_LABEL = 5109,
		IDC_STATIC_FILTER_LABEL = 5110,
		IDC_BTN_ACCEPT_ALL_PRIMARY = 5111,
		IDC_BTN_MARK_ALL_DONE = 5112,
		IDC_STATIC_PRIMARY_LANG = 5113,
		IDC_COMBO_PRIMARY_LANG = 5114,
		IDC_STATIC_SECONDARY_LANG = 5115,
		IDC_COMBO_SECONDARY_LANG = 5116,
		IDC_CHK_SHOW_PRIMARY = 5117,
		IDC_PREVIEW_HOST = 5118,
		IDC_CODE_EDITOR = 5119,
		IDC_TOOLBAR_HOST = 5120,
	};

	enum ETimerID
	{
		TIMER_UPDATE_PROGRESS = 1001,
	};

	// List columns
	enum EListColumn
	{
		COL_TYPE = 0,
		COL_ID = 1,
		COL_PRIMARY_TEXT = 2,
		COL_SECONDARY_TEXT = 3,
		COL_STATUS = 4,
	};

	// Filter modes
	enum EFilterMode
	{
		FILTER_ALL = 0,
		FILTER_PENDING = 1,
		FILTER_TRANSLATED = 2,
	};

	// Control creation and layout
	void CreateViewerControls();

	// Data display
	void UpdateProgressDisplay();
	void SwitchToTask(int nTaskIndex);
	void RefreshTaskCombo();
	void ApplyFilter();
	void UpdateListItemDisplay(int nAddedIndex);
	void BeginEditSecondaryText(int nListIndex);
	std::wstring ShowTextEditDialog(const std::wstring& wsPrimary,
	                                const std::wstring& wsCurrent,
	                                const std::wstring& wsTitle);

	// Resource tree (delegated to NavigationTree module)
	void BuildResourceTree();
	void ClearResourceTree();
	void OnTreeSelChanged(NMTREEVIEWW* pNMTV);

	// Resource preview (delegated to ViewRenderer module)
	void PreviewEntry(EntryBase* pEntry);

	// Language management
	void PopulateLanguageCombos();
	LANGID GetSelectedPrimaryLang() const;
	LANGID GetSelectedSecondaryLang() const;
	void AutoDetectAndSetLanguages();

	// Navigation from compare result to tree (delegated to NavigationTree)
	void NavigateTreeToCompareItem(const SCompareItem& item);

	CRcCompareControler* m_pControler;
	HWND m_hParent;
	int m_nCurrentAddedIndex;
	int m_nCurrentTaskIndex;
	EFilterMode m_eFilterMode;
	bool m_bUpdatingUI;
	bool m_bShowPrimary;

	HFONT m_hFont;

	// Detected languages for current task
	std::vector<LANGID> m_vecPrimaryLangs;
	std::vector<LANGID> m_vecSecondaryLangs;

	// Splitters (MMainWnd-like layout)
	MSplitterWnd m_splitter_main;   // vertical: top(resource area) / bottom(compare results)
	MSplitterWnd m_splitter_top;    // horizontal: left(tree) / right(code+preview)
	MSplitterWnd m_splitter_inner;  // vertical: top(code editor) / bottom(preview)
	HWND m_hwndToolbarHost;         // toolbar row container

	// Control HWNDs
	HWND m_hwndTreeResources;
	HWND m_hwndCodeEditor;          // multiline EDIT for RC source
	HWND m_hwndListItems;
	HWND m_hwndBtnNextUnmod;
	HWND m_hwndBtnPrevUnmod;
	HWND m_hwndBtnExportRc;
	HWND m_hwndBtnSaveRes;
	HWND m_hwndStaticProgress;
	HWND m_hwndComboTask;
	HWND m_hwndComboFilter;
	HWND m_hwndStaticTaskLabel;
	HWND m_hwndStaticFilterLabel;
	HWND m_hwndBtnAcceptAll;
	HWND m_hwndBtnMarkAllDone;
	HWND m_hwndStaticPrimaryLang;
	HWND m_hwndComboPrimaryLang;
	HWND m_hwndStaticSecondaryLang;
	HWND m_hwndComboSecondaryLang;
	HWND m_hwndChkShowPrimary;
	HWND m_hwndPreviewHost;

	// Container for compare results area (bottom splitter pane)
	HWND m_hwndBottomPanel;

	// Map from ListView row index -> compare result index
	std::vector<int> m_vecFilteredIndices;

	// Modular components (orchestrator pattern)
	NavigationTree m_navTree;
	ViewRenderer m_viewRenderer;
};
