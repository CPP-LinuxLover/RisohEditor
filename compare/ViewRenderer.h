// ViewRenderer.h --- Resource preview and rendering module
//
// Renders resource entries (dialog, menu, string table, etc.) into
// a code editor (RC source dump) and a preview host (visual preview).
// Extracted from CRcCompareViewer for modular reuse.
//////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _INC_WINDOWS
	#include <windows.h>
#endif
#include "Res.hpp"
#include "DialogRes.hpp"
#include "MenuRes.hpp"
#include "StringRes.hpp"

//////////////////////////////////////////////////////////////////////////////

class ViewRenderer
{
public:
	ViewRenderer();
	~ViewRenderer();

	// Set the host windows for rendering output
	void SetHostWindows(HWND hwndCodeEditor, HWND hwndPreviewHost);

	// Render a resource entry (auto-detects type)
	void RenderEntry(EntryBase* pEntry);

	// Clear all rendered content
	void Clear();

	// Clear only the dialog preview
	void ClearDialogPreview();

	// Get current preview dialog HWND (may be NULL)
	HWND GetPreviewDialog() const { return m_hwndPreviewDialog; }

private:
	// Type-specific rendering
	void RenderDialog(EntryBase* pEntry);
	void RenderMenu(EntryBase* pEntry);
	void RenderStringTable(EntryBase* pEntry);
	void RenderGeneric(EntryBase* pEntry);

	// Visual dialog preview
	void ShowDialogPreview(EntryBase* pEntry);

	HWND m_hwndCodeEditor;
	HWND m_hwndPreviewHost;
	HWND m_hwndPreviewDialog;
};
