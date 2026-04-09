// ViewRenderer.cpp --- Resource preview and rendering implementation
//////////////////////////////////////////////////////////////////////////////

#include "ViewRenderer.h"
#include "MByteStreamEx.hpp"
#include <strsafe.h>

//////////////////////////////////////////////////////////////////////////////
// Minimal dialog proc for read-only preview

static INT_PTR CALLBACK PreviewDlgProc(
	HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
		// Disable all child controls for read-only preview
		for (HWND h = GetWindow(hwnd, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
			EnableWindow(h, FALSE);
		return TRUE;
	case WM_COMMAND:
		return TRUE; // eat all commands
	case WM_CLOSE:
		return TRUE; // prevent closing
	case WM_ERASEBKGND:
		{
			HDC hdc = (HDC)wParam;
			RECT rc;
			GetClientRect(hwnd, &rc);
			FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
		}
		return TRUE;
	}
	return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

ViewRenderer::ViewRenderer()
	: m_hwndCodeEditor(NULL)
	, m_hwndPreviewHost(NULL)
	, m_hwndPreviewDialog(NULL)
{
}

ViewRenderer::~ViewRenderer()
{
	ClearDialogPreview();
}

void ViewRenderer::SetHostWindows(HWND hwndCodeEditor, HWND hwndPreviewHost)
{
	m_hwndCodeEditor = hwndCodeEditor;
	m_hwndPreviewHost = hwndPreviewHost;
}

void ViewRenderer::RenderEntry(EntryBase* pEntry)
{
	if (!pEntry || pEntry->m_data.empty())
	{
		Clear();
		return;
	}

	WORD wType = pEntry->m_type.m_id;

	if (wType == LOWORD(RT_DIALOG))
		RenderDialog(pEntry);
	else if (wType == LOWORD(RT_MENU))
		RenderMenu(pEntry);
	else if (wType == LOWORD(RT_STRING))
		RenderStringTable(pEntry);
	else
		RenderGeneric(pEntry);
}

void ViewRenderer::Clear()
{
	if (m_hwndCodeEditor)
		SetWindowTextW(m_hwndCodeEditor, L"");
	ClearDialogPreview();
}

void ViewRenderer::ClearDialogPreview()
{
	if (m_hwndPreviewDialog)
	{
		DestroyWindow(m_hwndPreviewDialog);
		m_hwndPreviewDialog = NULL;
	}
}

//////////////////////////////////////////////////////////////////////////////
// Type-specific rendering

void ViewRenderer::RenderDialog(EntryBase* pEntry)
{
	MByteStreamEx stream(pEntry->m_data);
	DialogRes dlgRes;
	if (dlgRes.LoadFromStream(stream))
	{
		MStringW dump = dlgRes.Dump(pEntry->m_name);
		if (m_hwndCodeEditor)
			SetWindowTextW(m_hwndCodeEditor, dump.c_str());
	}
	else
	{
		if (m_hwndCodeEditor)
			SetWindowTextW(m_hwndCodeEditor, L"// Failed to parse dialog");
	}
	ShowDialogPreview(pEntry);
}

void ViewRenderer::RenderMenu(EntryBase* pEntry)
{
	// Detect extended menu format by peeking at version field
	bool bExtended = false;
	if (pEntry->m_data.size() >= sizeof(WORD))
	{
		WORD wVersion = *(const WORD*)&pEntry->m_data[0];
		bExtended = (wVersion == 1);
	}

	MByteStreamEx stream(pEntry->m_data);
	MenuRes menuRes;
	bool bLoaded = bExtended
		? menuRes.LoadFromStreamEx(stream)
		: menuRes.LoadFromStream(stream);

	if (bLoaded)
	{
		MStringW dump = bExtended
			? menuRes.DumpEx(pEntry->m_name)
			: menuRes.Dump(pEntry->m_name);
		if (m_hwndCodeEditor)
			SetWindowTextW(m_hwndCodeEditor, dump.c_str());
	}
	else
	{
		if (m_hwndCodeEditor)
			SetWindowTextW(m_hwndCodeEditor, L"// Failed to parse menu");
	}
	ClearDialogPreview();
}

void ViewRenderer::RenderStringTable(EntryBase* pEntry)
{
	MByteStreamEx stream(pEntry->m_data);
	StringRes strRes;
	WORD wName = pEntry->m_name.m_id;
	if (strRes.LoadFromStream(stream, wName))
	{
		MStringW dump = strRes.Dump(wName);
		if (m_hwndCodeEditor)
			SetWindowTextW(m_hwndCodeEditor, dump.c_str());
	}
	else
	{
		if (m_hwndCodeEditor)
			SetWindowTextW(m_hwndCodeEditor, L"// Failed to parse string table");
	}
	ClearDialogPreview();
}

void ViewRenderer::RenderGeneric(EntryBase* pEntry)
{
	WCHAR szInfo[256];
	StringCchPrintfW(szInfo, _countof(szInfo),
		L"// Resource type: %u, size: %u bytes",
		pEntry->m_type.m_id, (UINT)pEntry->m_data.size());
	if (m_hwndCodeEditor)
		SetWindowTextW(m_hwndCodeEditor, szInfo);
	ClearDialogPreview();
}

//////////////////////////////////////////////////////////////////////////////
// Visual dialog preview

void ViewRenderer::ShowDialogPreview(EntryBase* pEntry)
{
	ClearDialogPreview();

	if (!pEntry || pEntry->m_data.empty() || !m_hwndPreviewHost)
		return;

	MByteStreamEx stream(pEntry->m_data);
	DialogRes dlgRes;
	if (!dlgRes.LoadFromStream(stream))
		return;

	// Modify dialog style for embedded child preview (same approach as
	// DialogRes::FixupForRad in RisohEditor core)
	dlgRes.m_style &= ~(WS_POPUP | DS_SYSMODAL | WS_DISABLED | WS_SYSMENU |
	                     WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
	dlgRes.m_style |= WS_VISIBLE | WS_CHILD | DS_NOIDLEMSG;
	dlgRes.m_ex_style &= ~(WS_EX_ACCEPTFILES | WS_EX_TOPMOST |
	                        WS_EX_LAYERED | WS_EX_TRANSPARENT);
	dlgRes.m_ex_style |= WS_EX_NOACTIVATE;
	dlgRes.m_menu.clear();
	dlgRes.m_class.clear();

	std::vector<BYTE> templateData = dlgRes.data();
	if (templateData.empty())
		return;

	m_hwndPreviewDialog = ::CreateDialogIndirectParamW(
		GetModuleHandle(NULL),
		(LPCDLGTEMPLATE)&templateData[0],
		m_hwndPreviewHost,
		PreviewDlgProc,
		0);

	if (m_hwndPreviewDialog)
	{
		// Position the dialog centered inside the preview host
		RECT rcHost, rcDlg;
		GetClientRect(m_hwndPreviewHost, &rcHost);
		GetWindowRect(m_hwndPreviewDialog, &rcDlg);
		int dlgW = rcDlg.right - rcDlg.left;
		int dlgH = rcDlg.bottom - rcDlg.top;
		int x = (rcHost.right - dlgW) / 2;
		int y = (rcHost.bottom - dlgH) / 2;
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		SetWindowPos(m_hwndPreviewDialog, NULL, x, y, 0, 0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
	}
}
