// RisohCompare.cpp --- RisohCompare main implementation
//////////////////////////////////////////////////////////////////////////////
// RisohEditor --- Another free Win32 resource editor
// Copyright (C) 2017-2025 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
// License: GPL-3 or later
//
// RisohCompare is a standalone resource comparison tool that re-uses the
// GUI dialog editor (OnGuiEdit) and the dialog test/preview (OnTest)
// infrastructure from RisohEditor.
//////////////////////////////////////////////////////////////////////////////

#include "RisohCompare.hpp"

//////////////////////////////////////////////////////////////////////////////
// Program-wide global variables (USE_GLOBALS=1 is defined by CMake)

#ifdef USE_GLOBALS
	ConstantsDB  g_db;		// constants database
	RisohSettings g_settings;	// editor settings
	EntrySet     g_res;		// the set of resource entries
#endif

// No-GUI mode flag (declared extern in MWindowBase.hpp / MMainWnd.hpp)
INT g_bNoGuiMode = 0;

// Track whether the loaded file has been modified
static BOOL s_bModified = FALSE;

// Implementation of DoSetFileModified (declared in Utils.h, normally defined
// in RisohEditor.cpp which we do not include here)
void DoSetFileModified(BOOL bModified)
{
	s_bModified = bModified;
}

//////////////////////////////////////////////////////////////////////////////
// MCompareWnd --- the main window of RisohCompare

class MCompareWnd : public MWindowBase
{
public:
	// RAD dialog designer window (used by OnGuiEdit for RT_DIALOG)
	MRadWindow	m_rad_window;

	// Path to vcvarsall.bat (empty → TYPELIB editing disabled, which is OK)
	WCHAR		m_szVCBat[MAX_PATH];

	// Stub handle for a "code editor" control (NULL = no editor present)
	HWND		m_hCodeEditor;

	// Optional status bar
	HWND		m_hStatusBar;

	// Status string ID set by recompile paths
	INT			m_nStatusStringID;

	// ---- Nested SelectTV state enum (mirrors MMainWnd::STV) ----
	enum STV
	{
		STV_RESETTEXTANDMODIFIED,
		STV_RESETTEXT,
		STV_DONTRESET
	};

	MCompareWnd()
		: m_hCodeEditor(NULL)
		, m_hStatusBar(NULL)
		, m_nStatusStringID(0)
	{
		m_szVCBat[0] = L'\0';
	}

	// ---- Infrastructure stubs ----

	// No pending text edits to compile — always succeed.
	BOOL CompileIfNecessary(BOOL /*bReopen*/ = FALSE)
	{
		return TRUE;
	}

	// Recompile a resource text fragment back to binary.
	// Returns FALSE in this skeleton (compiler path not configured).
	BOOL CompileParts(MStringA& strOutput,
					  const MIdOrString& /*type*/,
					  const MIdOrString& /*name*/,
					  WORD /*lang*/,
					  const MStringW& /*strWide*/)
	{
		strOutput = "CompileParts: compiler not available in RisohCompare.";
		return FALSE;
	}

	// Update entry selection (tree-view equivalent is not present here;
	// RisohCompare subclasses can override to drive a list control).
	void SelectTV(EntryBase * /*entry*/, BOOL /*bDoubleClick*/,
				  STV /*stv*/ = STV_RESETTEXTANDMODIFIED)
	{
	}

	void SelectTV(EntryType et, const MIdOrString& type,
				  const MIdOrString& name, WORD lang,
				  BOOL bDoubleClick,
				  STV stv = STV_RESETTEXTANDMODIFIED)
	{
		if (auto e = g_res.find(et, type, name, lang))
			SelectTV(e, bDoubleClick, stv);
	}

	// Update the status bar text (no-op when no status bar is present).
	void ChangeStatusText(INT nID)
	{
		ChangeStatusText(LoadStringDx(nID));
	}
	void ChangeStatusText(LPCTSTR pszText)
	{
		if (::IsWindow(m_hStatusBar))
			::SendMessage(m_hStatusBar, SB_SETTEXT, 0, (LPARAM)pszText);
	}

	// Show error output from a failed compile attempt.
	void SetErrorMessage(const MStringA& strOutput)
	{
		MStringW wide(strOutput.begin(), strOutput.end());
		::MessageBoxW(m_hwnd, wide.c_str(), LoadStringDx(IDS_RECOMPILEFAILED),
					  MB_ICONERROR | MB_OK);
	}

	//////////////////////////////////////////////////////////////////////////
	// OnGuiEdit --- open the appropriate GUI editor for the selected entry.
	// Copied verbatim from MMainWnd::OnGuiEdit in RisohEditor.cpp and
	// adapted to use MCompareWnd methods instead of MMainWnd methods.
	//////////////////////////////////////////////////////////////////////////
	void OnGuiEdit(HWND hwnd)
	{
		// get the selected entry
		auto entry = g_res.get_entry();
		if (!entry->is_editable(m_szVCBat))
			return;		// not editable

		if (!entry->can_gui_edit())
			return;		// unable to edit by GUI?

		// compile if necessary
		if (!CompileIfNecessary(FALSE))
			return;

		if (entry->m_type == RT_ACCELERATOR)
		{
			// entry->m_data --> accel_res
			AccelRes accel_res;
			MByteStreamEx stream(entry->m_data);
			if (accel_res.LoadFromStream(stream))
			{
				// editing...
				ChangeStatusText(IDS_EDITINGBYGUI);

				// show the dialog
				MEditAccelDlg dialog(accel_res);
				INT nID = (INT)dialog.DialogBoxDx(hwnd);
				if (nID == IDOK && entry == g_res.get_entry())
				{
					DoSetFileModified(TRUE);

					// update accel_res
					accel_res.Update();

					// accel_res --> entry->m_data
					entry->m_data = accel_res.data();
				}
			}

			// make it non-read-only
			Edit_SetReadOnly(m_hCodeEditor, FALSE);

			// select the entry
			if (entry == g_res.get_entry())
				SelectTV(entry, FALSE);

			// ready
			ChangeStatusText(IDS_READY);
		}
		else if (entry->m_type == RT_MENU)
		{
			// entry->m_data --> menu_res
			MenuRes menu_res;
			MByteStreamEx stream(entry->m_data);
			if (menu_res.LoadFromStream(stream))
			{
				// editing...
				ChangeStatusText(IDS_EDITINGBYGUI);

				// show the dialog
				MEditMenuDlg dialog(menu_res);
				INT nID = (INT)dialog.DialogBoxDx(hwnd);
				if (nID == IDOK && entry == g_res.get_entry())
				{
					DoSetFileModified(TRUE);

					// update menu_res
					menu_res.Update();

					// menu_res --> entry->m_data
					entry->m_data = menu_res.data();
				}
			}

			// make it non-read-only
			Edit_SetReadOnly(m_hCodeEditor, FALSE);

			// select the entry
			if (entry == g_res.get_entry())
				SelectTV(entry, FALSE);

			// ready
			ChangeStatusText(IDS_READY);
		}
		else if (entry->m_type == RT_TOOLBAR)
		{
			// entry->m_data --> toolbar_res
			ToolbarRes toolbar_res;
			MByteStreamEx stream(entry->m_data);
			if (toolbar_res.LoadFromStream(stream))
			{
				// editing...
				ChangeStatusText(IDS_EDITINGBYGUI);

				// show the dialog
				MEditToolbarDlg dialog(toolbar_res);
				INT nID = (INT)dialog.DialogBoxDx(hwnd);
				if (nID == IDOK && entry == g_res.get_entry())
				{
					DoSetFileModified(TRUE);

					// toolbar_res --> entry->m_data
					entry->m_data = toolbar_res.data();
				}
			}

			// make it non-read-only
			Edit_SetReadOnly(m_hCodeEditor, FALSE);

			// select the entry
			if (entry == g_res.get_entry())
				SelectTV(entry, FALSE);

			// ready
			ChangeStatusText(IDS_READY);
		}
		else if (entry->m_type == RT_DIALOG)
		{
			// editing...
			ChangeStatusText(IDS_EDITINGBYGUI);

			// entry->m_data --> m_rad_window.m_dialog_res
			MByteStreamEx stream(entry->m_data);
			m_rad_window.m_dialog_res.LoadFromStream(stream);
			m_rad_window.m_dialog_res.m_lang = entry->m_lang;
			m_rad_window.m_dialog_res.m_name = entry->m_name;
			m_rad_window.clear_maps();
			m_rad_window.create_maps(entry->m_lang);

			// load RT_DLGINIT
			if (auto e = g_res.find(ET_LANG, RT_DLGINIT, entry->m_name, entry->m_lang))
			{
				m_rad_window.m_dialog_res.LoadDlgInitData(e->m_data);
			}
			else if (auto e = g_res.find(ET_LANG, RT_DLGINIT, entry->m_name, BAD_LANG))
			{
				m_rad_window.m_dialog_res.LoadDlgInitData(e->m_data);
			}

			// recreate the RADical dialog
			if (::IsWindow(m_rad_window))
			{
				m_rad_window.ReCreateRadDialog(m_rad_window);
			}
			else
			{
				if (!m_rad_window.CreateDx(m_hwnd))
				{
					ErrorBoxDx(IDS_DLGFAIL);
				}
			}

			// make it non-read-only
			Edit_SetReadOnly(m_hCodeEditor, FALSE);
		}
		else if (entry->m_type == RT_DLGINIT)
		{
			// entry->m_data --> dlginit_res
			DlgInitRes dlginit_res;
			MByteStreamEx stream(entry->m_data);
			if (dlginit_res.LoadFromStream(stream))
			{
				// editing...
				ChangeStatusText(IDS_EDITINGBYGUI);

				// show the dialog
				MDlgInitDlg dialog(dlginit_res);
				INT nID = (INT)dialog.DialogBoxDx(hwnd);
				if (nID == IDOK && entry == g_res.get_entry())
				{
					DoSetFileModified(TRUE);

					// dlginit_res --> entry->m_data
					entry->m_data = dlginit_res.data();
				}
			}

			// make it non-read-only
			Edit_SetReadOnly(m_hCodeEditor, FALSE);

			// select the entry
			if (entry == g_res.get_entry())
				SelectTV(entry, FALSE);

			// ready
			ChangeStatusText(IDS_READY);
		}
		else if (entry->m_type == RT_STRING && entry->m_et == ET_STRING)
		{
			// g_res --> found
			WORD lang = entry->m_lang;
			EntrySet found;
			g_res.search(found, ET_LANG, RT_STRING, BAD_NAME, lang);

			// found --> str_res
			StringRes str_res;
			for (auto e : found)
			{
				MByteStreamEx stream(e->m_data);
				if (!str_res.LoadFromStream(stream, e->m_name.m_id))
				{
					ErrorBoxDx(IDS_CANNOTLOAD);
					return;
				}
			}

			// editing...
			ChangeStatusText(IDS_EDITINGBYGUI);

			// show the dialog
			MStringsDlg dialog(str_res);
			INT nID = (INT)dialog.DialogBoxDx(hwnd);
			if (nID == IDOK)
			{
				DoSetFileModified(TRUE);

				// dialog --> str_res
				str_res = dialog.m_str_res;

				// dump (with disabling macro IDs)
				bool shown = !g_settings.bHideID;
				g_settings.bHideID = false;
				MStringW strWide = str_res.Dump();
				g_settings.bHideID = !shown;

				// compile the dumped result
				MStringA strOutput;
				if (CompileParts(strOutput, RT_STRING, BAD_NAME, lang, strWide))
				{
					m_nStatusStringID = IDS_RECOMPILEOK;

					// select the entry to update the source
					SelectTV(ET_STRING, RT_STRING, BAD_NAME, lang, FALSE);
				}
				else
				{
					m_nStatusStringID = IDS_RECOMPILEFAILED;
					SetErrorMessage(strOutput);
				}
			}

			// make it non-read-only
			Edit_SetReadOnly(m_hCodeEditor, FALSE);

			// ready
			ChangeStatusText(IDS_READY);
		}
		else if (entry->m_type == RT_MESSAGETABLE && entry->m_et == ET_MESSAGE)
		{
			if (g_settings.bUseMSMSGTABLE)
				return;

			// g_res --> found
			WORD lang = entry->m_lang;
			EntrySet found;
			g_res.search(found, ET_LANG, RT_MESSAGETABLE, BAD_NAME, lang);

			// found --> msg_res
			MessageRes msg_res;
			for (auto e : found)
			{
				MByteStreamEx stream(e->m_data);
				if (!msg_res.LoadFromStream(stream, e->m_name.m_id))
				{
					ErrorBoxDx(IDS_CANNOTLOAD);
					return;
				}
			}

			// editing...
			ChangeStatusText(IDS_EDITINGBYGUI);

			// show the dialog
			MMessagesDlg dialog(msg_res);
			INT nID = (INT)dialog.DialogBoxDx(hwnd);
			if (nID == IDOK)
			{
				DoSetFileModified(TRUE);

				// dialog --> msg_res
				msg_res = dialog.m_msg_res;

				// dump (with disabling macro IDs)
				bool shown = !g_settings.bHideID;
				g_settings.bHideID = false;
				MStringW strWide = msg_res.Dump();
				g_settings.bHideID = !shown;

				// compile the dumped result
				MStringA strOutput;
				if (CompileParts(strOutput, RT_MESSAGETABLE, WORD(1), lang, strWide))
				{
					m_nStatusStringID = IDS_RECOMPILEOK;

					// select the entry
					SelectTV(ET_MESSAGE, RT_MESSAGETABLE, BAD_NAME, lang, FALSE);
				}
				else
				{
					m_nStatusStringID = IDS_RECOMPILEFAILED;
					SetErrorMessage(strOutput);
				}
			}

			Edit_SetReadOnly(m_hCodeEditor, g_settings.bUseMSMSGTABLE);

			// ready
			ChangeStatusText(IDS_READY);
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// OnTest --- show a preview / test window for the selected entry.
	// Copied verbatim from MMainWnd::OnTest in RisohEditor.cpp and adapted
	// to use MCompareWnd methods.
	//////////////////////////////////////////////////////////////////////////
	void OnTest(HWND hwnd)
	{
		// compile if necessary
		if (!CompileIfNecessary(::IsWindowVisible(m_rad_window)))
			return;

		// get the selected entry
		auto entry = g_res.get_entry();
		if (!entry)
			return;

		if (entry->m_type == RT_DIALOG)
		{
			// entry->m_data --> stream --> dialog_res
			DialogRes dialog_res;
			MByteStreamEx stream(entry->m_data);
			dialog_res.LoadFromStream(stream);

			if (!dialog_res.m_class.empty())
			{
				// TODO: support the classed dialogs
				ErrorBoxDx(IDS_CANTTESTCLASSDLG);
			}
			else
			{
				// detach the dialog template menu (it will be used after)
				MIdOrString menu = dialog_res.m_menu;
				dialog_res.m_menu.clear();
				stream.clear();

				// fixup for "AtlAxWin*" and/or "{...}" window classes.
				// see also: DialogRes::FixupForTest
				dialog_res.FixupForTest(false);
				dialog_res.SaveToStream(stream);
				dialog_res.FixupForTest(true);

				// load RT_DLGINIT if any
				std::vector<BYTE> dlginit_data;
				if (auto e = g_res.find(ET_LANG, RT_DLGINIT, entry->m_name, entry->m_lang))
				{
					dlginit_data = e->m_data;
				}

				// show test dialog
				if (dialog_res.m_style & WS_CHILD)
				{
					// dialog_res is a child dialog. create its parent (with menu if any)
					auto window = new MTestParentWnd(dialog_res, menu, entry->m_lang,
													 stream, dlginit_data);
					window->CreateWindowDx(hwnd, LoadStringDx(IDS_PARENTWND),
						WS_DLGFRAME | WS_POPUPWINDOW, WS_EX_APPWINDOW);

					// show it
					ShowWindow(*window, g_bNoGuiMode ? SW_HIDE : SW_SHOWNORMAL);
					UpdateWindow(*window);
				}
				else
				{
					// it's a non-child dialog. show the test dialog (with menu if any)
					MTestDialog dialog(dialog_res, menu, entry->m_lang, dlginit_data);
					dialog.DialogBoxIndirectDx(hwnd, stream.ptr());
				}
			}
		}
		else if (entry->m_type == RT_MENU)
		{
			// load a menu from memory
			HMENU hMenu = LoadMenuIndirect(&(*entry)[0]);
			if (hMenu)
			{
				// show the dialog
				MTestMenuDlg dialog(hMenu);
				dialog.DialogBoxDx(hwnd, IDD_MENUTEST);

				// unload the menu
				DestroyMenu(hMenu);
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// Window procedure helpers
	//////////////////////////////////////////////////////////////////////////

	void OnCreate(HWND hwnd)
	{
		// create a status bar
		m_hStatusBar = ::CreateWindowExW(0, STATUSCLASSNAMEW, NULL,
			WS_CHILD | WS_VISIBLE | CCS_BOTTOM | SBARS_SIZEGRIP,
			0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)1, GetModuleHandleW(NULL), NULL);

		ChangeStatusText(IDS_READY);
	}

	void OnSize(HWND hwnd, UINT /*state*/, int /*cx*/, int /*cy*/)
	{
		if (::IsWindow(m_hStatusBar))
		{
			// let the status bar reposition itself
			::SendMessage(m_hStatusBar, WM_SIZE, 0, 0);
		}
	}

	void OnDestroy(HWND /*hwnd*/)
	{
		::PostQuitMessage(0);
	}

	// Main message dispatcher
	virtual LRESULT CALLBACK
	WindowProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		HANDLE_MSG(hwnd, WM_CREATE, OnCreate_);
		HANDLE_MSG(hwnd, WM_SIZE, OnSize);
		HANDLE_MSG(hwnd, WM_DESTROY, OnDestroy);

		default:
			return DefaultProcDx(hwnd, uMsg, wParam, lParam);
		}
	}

private:
	// Adapter: HANDLE_MSG passes CREATESTRUCT* for WM_CREATE; ignore it.
	BOOL OnCreate_(HWND hwnd, LPCREATESTRUCT /*lpcs*/)
	{
		OnCreate(hwnd);
		return TRUE;
	}
};

//////////////////////////////////////////////////////////////////////////////
// WinMain

extern "C" int WINAPI
wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
         LPWSTR /*lpCmdLine*/, int nCmdShow)
{
	// Initialize COM (needed for OLE host / ATL controls in the RAD designer)
	::CoInitialize(NULL);

	// Initialize GDI+ (needed by MBitmapDx for PNG/GIF rendering)
	Gdiplus::GdiplusStartupInput gdiplusInput;
	ULONG_PTR gdiplusToken = 0;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, NULL);

	// Initialize common controls
	INITCOMMONCONTROLSEX icc;
	icc.dwSize = sizeof(icc);
	icc.dwICC  = ICC_WIN95_CLASSES | ICC_COOL_CLASSES;
	::InitCommonControlsEx(&icc);

	// Apply default settings
	g_settings.ui_lang         = ::GetThreadUILanguage();
	g_settings.bAlwaysControl  = FALSE;
	g_settings.bHideID         = FALSE;
	g_settings.bUseIDC_STATIC  = FALSE;
	g_settings.bShowDotsOnDialog = TRUE;
	g_settings.nComboHeight    = 300;
	g_settings.nRadLeft        = CW_USEDEFAULT;
	g_settings.nRadTop         = CW_USEDEFAULT;
	g_settings.bResumeWindowPos = TRUE;
	g_settings.bUseBeginEnd    = FALSE;
	g_settings.bUseMSMSGTABLE  = FALSE;
	g_settings.bWrapManifest   = TRUE;
	g_settings.ResetEncoding();

	// Register the window class and create the main window
	MCompareWnd wnd;

	WNDCLASSEXW wcx;
	ZeroMemory(&wcx, sizeof(wcx));
	wcx.cbSize        = sizeof(wcx);
	wcx.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wcx.lpfnWndProc   = MWindowBase::WindowProc;
	wcx.hInstance     = hInstance;
	wcx.hIcon         = ::LoadIconW(NULL, IDI_APPLICATION);
	wcx.hCursor       = ::LoadCursorW(NULL, IDC_ARROW);
	wcx.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
	wcx.lpszClassName = L"RisohCompareWnd";
	wcx.hIconSm       = ::LoadIconW(NULL, IDI_APPLICATION);

	if (!::RegisterClassExW(&wcx))
		return 1;

	HWND hwnd = wnd.CreateWindowDx(NULL, L"RisohCompare",
		WS_OVERLAPPEDWINDOW,
		0,
		CW_USEDEFAULT, CW_USEDEFAULT, 760, 480);
	if (!hwnd)
		return 1;

	::ShowWindow(hwnd, nCmdShow);
	::UpdateWindow(hwnd);

	MSG msg;
	while (::GetMessageW(&msg, NULL, 0, 0))
	{
		::TranslateMessage(&msg);
		::DispatchMessageW(&msg);
	}

	// Shutdown GDI+ and COM
	Gdiplus::GdiplusShutdown(gdiplusToken);
	::CoUninitialize();

	return (int)msg.wParam;
}
