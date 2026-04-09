#pragma once

// CFormRcCompare.h --- PE resource compare main window (Win32 native)

#include "CFormRcBase.h"
#include "CRcCompareControler.h"
#include "CompareProjectConfig.h"
#include "CCompareWorkerThread.h"

class CFormRcCompare : public CFormRcBase
{
public:
	explicit CFormRcCompare(HWND hParent = NULL);
	virtual ~CFormRcCompare();

	INT_PTR DoCompareDialog();

protected:
	virtual LPCWSTR GetInitialCaption() const override { return L"PE \x8D44\x6E90\x6BD4\x5BF9\x5DE5\x5177"; }
	virtual void OnBeforeDialogClose() override;

	virtual INT_PTR CALLBACK
	DialogProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	BOOL OnInitDialog(HWND hwnd);
	void OnSize(HWND hwnd, int cx, int cy);
	void OnGetMinMaxInfo(MINMAXINFO* pMMI);
	void OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);

	// Project management handlers
	void OnBtnLoadProject();
	void OnBtnSaveProject();
	void OnBtnAddTask();
	void OnBtnEditTask();
	void OnBtnRemoveTask();
	void OnBtnBatchCompare();
	void OnBtnBatchExport();
	void OnBtnOpenViewer();

	// Worker thread message handlers
	LRESULT OnCompareProgress(WPARAM wParam, LPARAM lParam);
	LRESULT OnCompareTaskDone(WPARAM wParam, LPARAM lParam);
	LRESULT OnCompareAllDone(WPARAM wParam, LPARAM lParam);
	LRESULT OnCompareError(WPARAM wParam, LPARAM lParam);
	LRESULT OnCompareStatus(WPARAM wParam, LPARAM lParam);

	enum ECtrlID
	{
		IDC_STATIC_PROJECT_GROUP = 5220,
		IDC_LIST_TASKS = 5221,
		IDC_BTN_LOAD_PROJECT = 5222,
		IDC_BTN_SAVE_PROJECT = 5223,
		IDC_BTN_ADD_TASK = 5224,
		IDC_BTN_EDIT_TASK = 5225,
		IDC_BTN_REMOVE_TASK = 5226,
		IDC_BTN_BATCH_COMPARE = 5227,
		IDC_BTN_BATCH_EXPORT = 5228,
		IDC_STATIC_STATUS = 5229,
		IDC_PROGRESS_BAR = 5230,
		IDC_BTN_OPEN_VIEWER = 5231,
	};

	void CreateProjectControls();
	void LayoutProjectControls();
	void RefreshTaskList();
	void UpdateBatchButtonState();
	void SetProcessingUI(bool bProcessing);

	HFONT m_hFont;

	// Compare controller
	CRcCompareControler m_controler;

	bool m_bInitialized;

	// Control HWNDs
	HWND m_hwndProjectGroup;
	HWND m_hwndListTasks;
	HWND m_hwndBtnLoadProject;
	HWND m_hwndBtnSaveProject;
	HWND m_hwndBtnAddTask;
	HWND m_hwndBtnEditTask;
	HWND m_hwndBtnRemoveTask;
	HWND m_hwndBtnBatchCompare;
	HWND m_hwndBtnBatchExport;
	HWND m_hwndStaticStatus;
	HWND m_hwndProgressBar;
	HWND m_hwndBtnOpenViewer;

	// Project config
	rceditor::SCompareProject m_project;
	rceditor::CCompareWorkerThread m_worker;
	bool m_bProcessing;
	HWND m_hParent;
};
