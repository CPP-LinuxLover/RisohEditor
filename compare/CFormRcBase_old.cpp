/*!
 * \file CFormRcBase.cpp
 * \brief CFormRcBase 的实现
 */
#include "stdafx.h"
#include "CFormRcBase.h"

IMPLEMENT_DYNAMIC(CFormRcBase, CDialog)

/*! \brief 构造函数：分配对话模板缓冲区 */
CFormRcBase::CFormRcBase(CWnd* pParent)
    : CDialog()
{
    m_pWndBuf = new WORD[MAX_BUFFER_SIZE];
}

/*! \brief 析构函数：释放对话模板缓冲区 */
CFormRcBase::~CFormRcBase()
{
    if (m_pWndBuf)
    {
        delete[] m_pWndBuf;
        m_pWndBuf = nullptr;
    }
}

/*! \brief 在内部缓冲区构造 DLGTEMPLATE 并返回指针 */
// 在内部缓冲区构造对话模板（DLGTEMPLATE）并返回指针。
// 注意：返回的指针指向内部缓冲区，调用者不得释放或在析构后继续使用。
DLGTEMPLATE* CFormRcBase::CreateTemplate(DWORD dwStyle, CRect& rect, CStringW caption, DWORD dwStyleEx)
{
    if (m_pWndBuf)
        ZeroMemory(m_pWndBuf, MAX_BUFFER_SIZE * sizeof(WORD));

    DLGTEMPLATE* pTemplate = reinterpret_cast<DLGTEMPLATE*>(m_pWndBuf);
    pTemplate->style = dwStyle;
    pTemplate->dwExtendedStyle = dwStyleEx;
    pTemplate->cdit = 0;
    pTemplate->x = (short)rect.left;
    pTemplate->y = (short)rect.top;
    pTemplate->cx = (short)rect.Width();
    pTemplate->cy = (short)rect.Height();

    WORD* pWord = reinterpret_cast<WORD*>(pTemplate + 1);
    *pWord++ = 0; // menu
    *pWord++ = 0; // class

    int len = caption.GetLength();
    wcscpy_s(reinterpret_cast<wchar_t*>(pWord), len + 1, caption.GetString());
    pWord += len + 1;
    return pTemplate;
}
