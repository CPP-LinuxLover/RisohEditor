#pragma once

// CFormRcBase.h --- Base dialog class for compare UI dialogs
// Uses MDialogBase from MZC4 framework instead of MFC CDialog.

#include "MWindowBase.hpp"

class CFormRcBase : public MDialogBase
{
public:
	explicit CFormRcBase();
	virtual ~CFormRcBase();

protected:
	// Overridable hooks
	virtual LPCWSTR GetInitialCaption() const { return L"RC Editor"; }
	virtual void OnAfterControlsCreated() {}
	virtual void OnBeforeDialogClose() {}

	// Dialog template buffer for runtime dialog creation
	WORD* m_pWndBuf;
	static const int MAX_BUFFER_SIZE = 10240;

	// Build a DLGTEMPLATE in the internal buffer
	DLGTEMPLATE* CreateTemplate(DWORD dwStyle, const RECT& rect,
	                            LPCWSTR caption, DWORD dwStyleEx = 0);
};
