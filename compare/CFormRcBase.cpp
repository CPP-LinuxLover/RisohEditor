// CFormRcBase.cpp --- Base dialog implementation
//////////////////////////////////////////////////////////////////////////////

#include "CFormRcBase.h"

CFormRcBase::CFormRcBase()
	: m_pWndBuf(NULL)
{
	m_pWndBuf = new WORD[MAX_BUFFER_SIZE];
}

CFormRcBase::~CFormRcBase()
{
	if (m_pWndBuf)
	{
		delete[] m_pWndBuf;
		m_pWndBuf = NULL;
	}
}

DLGTEMPLATE* CFormRcBase::CreateTemplate(DWORD dwStyle, const RECT& rect,
                                          LPCWSTR caption, DWORD dwStyleEx)
{
	if (m_pWndBuf)
		ZeroMemory(m_pWndBuf, MAX_BUFFER_SIZE * sizeof(WORD));

	DLGTEMPLATE* pTemplate = reinterpret_cast<DLGTEMPLATE*>(m_pWndBuf);
	pTemplate->style = dwStyle;
	pTemplate->dwExtendedStyle = dwStyleEx;
	pTemplate->cdit = 0;
	pTemplate->x = (short)rect.left;
	pTemplate->y = (short)rect.top;
	pTemplate->cx = (short)(rect.right - rect.left);
	pTemplate->cy = (short)(rect.bottom - rect.top);

	WORD* pWord = reinterpret_cast<WORD*>(pTemplate + 1);
	*pWord++ = 0; // menu
	*pWord++ = 0; // class

	if (caption)
	{
		int len = lstrlenW(caption);
		lstrcpyW(reinterpret_cast<wchar_t*>(pWord), caption);
		pWord += len + 1;
	}
	else
	{
		*pWord++ = 0; // empty caption
	}
	return pTemplate;
}
