#pragma once

// CConflictResolveDialog.h --- ID conflict resolution dialog (Win32 native)

#include "MWindowBase.hpp"
#include "CompareTypes.h"
#include <vector>
#include <commctrl.h>

class CConflictResolveDialog : public MDialogBase
{
public:
	explicit CConflictResolveDialog(const std::vector<SConflictInfo>& conflicts,
	                                HWND hParent = NULL);
	virtual ~CConflictResolveDialog();

	INT_PTR DoResolveDialog();

	const std::vector<SConflictResolutionEntry>& GetResolutions() const { return m_entries; }

protected:
	virtual INT_PTR CALLBACK
	DialogProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	BOOL OnInitDialog(HWND hwnd);
	void OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
	void OnSize(HWND hwnd, int cx, int cy);
	LRESULT OnNotify(HWND hwnd, int idFrom, NMHDR* pNMHDR);

	void OnBtnKeepAll();
	void OnBtnUsePrimaryAll();
	void OnBtnAutoAll();
	void OnComboResolutionChanged();

private:
	enum ECtrlID
	{
		IDC_LIST_CONFLICTS = 6100,
		IDC_STATIC_DETAIL,
		IDC_COMBO_RESOLUTION,
		IDC_EDIT_MANUAL_VALUE,
		IDC_BTN_KEEP_ALL,
		IDC_BTN_PRIMARY_ALL,
		IDC_BTN_AUTO_ALL,
		IDC_STATIC_STATS,
	};

	void CreateControls();
	void LayoutControls();
	void PopulateList();
	void UpdateDetailPanel(int selectedIndex);
	void ApplyResolutionToAll(EConflictResolution res);

	std::vector<SConflictResolutionEntry> m_entries;
	int m_selectedIndex;

	HFONT m_hFont;
	HWND m_hParent;
	HWND m_hwndListConflicts;
	HWND m_hwndStaticDetail;
	HWND m_hwndComboResolution;
	HWND m_hwndEditManualValue;
	HWND m_hwndBtnKeepAll;
	HWND m_hwndBtnPrimaryAll;
	HWND m_hwndBtnAutoAll;
	HWND m_hwndStaticStats;
};
