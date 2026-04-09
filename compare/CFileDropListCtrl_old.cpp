#include "stdafx.h"
#include "CFileDropListCtrl.h"
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

IMPLEMENT_DYNAMIC(CFileDropListCtrl, CListCtrl)

BEGIN_MESSAGE_MAP(CFileDropListCtrl, CListCtrl)
    ON_WM_DROPFILES()
    ON_WM_CONTEXTMENU()
    ON_WM_SIZE()
    ON_WM_KEYDOWN()
END_MESSAGE_MAP()

CFileDropListCtrl::CFileDropListCtrl()
    : m_bAllowDuplicates(false)
    , m_strFileFilter(_T("可执行文件(*.exe;*.dll)|*.exe;*.dll|所有文件(*.*)|*.*||"))
{
}

CFileDropListCtrl::~CFileDropListCtrl()
{
}

void CFileDropListCtrl::InitColumns(const CString& strIndexHeader,
                                     const CString& strFileNameHeader,
                                     const CString& strFilePathHeader)
{
    // 设置扩展样式：全行选中、网格线
    SetExtendedStyle(GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // 删除已有列
    while (DeleteColumn(0)) {}

    // 添加三列：序号、文件名、路径
    InsertColumn(0, strIndexHeader, LVCFMT_LEFT, 50);
    InsertColumn(1, strFileNameHeader, LVCFMT_LEFT, 150);
    InsertColumn(2, strFilePathHeader, LVCFMT_LEFT, 300);

    // 启用拖放支持
    DragAcceptFiles(TRUE);

    AdjustColumnWidths();
}

void CFileDropListCtrl::RemoveAllFiles()
{
    DeleteAllItems();
    m_vecItems.clear();

    if (m_pfnListChanged)
    {
        m_pfnListChanged();
    }
}

int CFileDropListCtrl::AddFile(const CString& strFilePath, DWORD_PTR dwData)
{
    // 检查文件是否存在
    if (!PathFileExists(strFilePath))
    {
        return -1;
    }

    // 检查是否允许重复
    if (!m_bAllowDuplicates && IsFileExists(strFilePath))
    {
        return -1;
    }

    SFileItem item;
    item.strFilePath = strFilePath;
    item.strFileName = ExtractFileName(strFilePath);
    item.dwData = dwData;

    int nIndex = GetItemCount();
    
    // 插入行
    CString strIndex;
    strIndex.Format(_T("%d"), nIndex + 1);
    InsertItem(nIndex, strIndex);
    SetItemText(nIndex, 1, item.strFileName);
    SetItemText(nIndex, 2, item.strFilePath);
    SetItemData(nIndex, (DWORD_PTR)m_vecItems.size());

    m_vecItems.push_back(item);

    if (m_pfnListChanged)
    {
        m_pfnListChanged();
    }

    return nIndex;
}

bool CFileDropListCtrl::RemoveFile(int nIndex)
{
    if (nIndex < 0 || nIndex >= GetItemCount())
        return false;

    DWORD_PTR idx = GetItemData(nIndex);
    if (idx >= m_vecItems.size())
        return false;

    // 从vector中删除
    m_vecItems.erase(m_vecItems.begin() + idx);

    // 从列表中删除
    DeleteItem(nIndex);

    // 更新所有行的序号和ItemData
    UpdateRowIndices();

    if (m_pfnListChanged)
    {
        m_pfnListChanged();
    }

    return true;
}

int CFileDropListCtrl::RemoveSelectedFiles()
{
    int nDeleted = 0;

    // 从后往前删除，避免索引变化问题
    for (int i = GetItemCount() - 1; i >= 0; --i)
    {
        if (GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED)
        {
            DWORD_PTR idx = GetItemData(i);
            if (idx < m_vecItems.size())
            {
                m_vecItems.erase(m_vecItems.begin() + idx);
                DeleteItem(i);
                ++nDeleted;
            }
        }
    }

    if (nDeleted > 0)
    {
        // 更新所有行的序号和ItemData
        UpdateRowIndices();

        if (m_pfnListChanged)
        {
            m_pfnListChanged();
        }
    }

    return nDeleted;
}

int CFileDropListCtrl::BrowseAndAddFiles()
{
    CFileDialog dlg(TRUE, NULL, NULL,
        OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
        m_strFileFilter, this);

    // 为多选分配足够大的缓冲区
    const int BUFFER_SIZE = 65535;
    TCHAR* pszBuffer = new TCHAR[BUFFER_SIZE];
    memset(pszBuffer, 0, BUFFER_SIZE * sizeof(TCHAR));
    dlg.m_ofn.lpstrFile = pszBuffer;
    dlg.m_ofn.nMaxFile = BUFFER_SIZE;

    int nAdded = 0;

    if (dlg.DoModal() == IDOK)
    {
        POSITION pos = dlg.GetStartPosition();
        while (pos != NULL)
        {
            CString strFilePath = dlg.GetNextPathName(pos);
            if (AddFile(strFilePath) >= 0)
            {
                ++nAdded;
            }
        }
    }

    delete[] pszBuffer;

    return nAdded;
}

bool CFileDropListCtrl::GetFileItem(int nIndex, SFileItem& outItem) const
{
    if (nIndex < 0 || nIndex >= GetItemCount())
        return false;

    DWORD_PTR idx = GetItemData(nIndex);
    if (idx >= m_vecItems.size())
        return false;

    outItem = m_vecItems[idx];
    return true;
}

void CFileDropListCtrl::GetAllFilePaths(std::vector<CString>& vecPaths) const
{
    vecPaths.clear();
    vecPaths.reserve(m_vecItems.size());

    for (const auto& item : m_vecItems)
    {
        vecPaths.push_back(item.strFilePath);
    }
}

void CFileDropListCtrl::SetListChangedCallback(ListChangedCallback callback)
{
    m_pfnListChanged = callback;
}

void CFileDropListCtrl::SetFileFilter(const CString& strFilter)
{
    m_strFileFilter = strFilter;
}

void CFileDropListCtrl::OnDropFiles(HDROP hDropInfo)
{
    UINT nFileCount = DragQueryFile(hDropInfo, 0xFFFFFFFF, NULL, 0);

    for (UINT i = 0; i < nFileCount; ++i)
    {
        TCHAR szFilePath[MAX_PATH] = { 0 };
        DragQueryFile(hDropInfo, i, szFilePath, MAX_PATH);

        CString strFilePath(szFilePath);

        // 检查是否为文件（排除文件夹）
        if (PathIsDirectory(strFilePath))
            continue;

        // 检查文件扩展名（可选：如果需要限制只能添加exe/dll）
        // if (!IsValidFileExtension(strFilePath))
        //     continue;

        AddFile(strFilePath);
    }

    DragFinish(hDropInfo);
}

void CFileDropListCtrl::OnContextMenu(CWnd* pWnd, CPoint point)
{
    CMenu menu;
    menu.CreatePopupMenu();

    menu.AppendMenu(MF_STRING, ID_MENU_ADD, _T("添加文件(&A)..."));
    menu.AppendMenu(MF_SEPARATOR);

    // 只有选中项时才启用删除菜单
    UINT nDeleteFlags = MF_STRING;
    if (GetSelectedCount() == 0)
    {
        nDeleteFlags |= MF_GRAYED;
    }
    menu.AppendMenu(nDeleteFlags, ID_MENU_DELETE, _T("删除选中(&D)"));

    // 只有有项目时才启用清空菜单
    UINT nDeleteAllFlags = MF_STRING;
    if (GetItemCount() == 0)
    {
        nDeleteAllFlags |= MF_GRAYED;
    }
    menu.AppendMenu(nDeleteAllFlags, ID_MENU_DELETE_ALL, _T("清空所有(&C)"));

    // 如果点击位置无效，使用控件中心
    if (point.x == -1 && point.y == -1)
    {
        CRect rc;
        GetClientRect(&rc);
        ClientToScreen(&rc);
        point = rc.CenterPoint();
    }

    int nCmd = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
        point.x, point.y, this);

    switch (nCmd)
    {
    case ID_MENU_ADD:
        BrowseAndAddFiles();
        break;

    case ID_MENU_DELETE:
        RemoveSelectedFiles();
        break;

    case ID_MENU_DELETE_ALL:
        RemoveAllFiles();
        break;
    }
}

void CFileDropListCtrl::OnSize(UINT nType, int cx, int cy)
{
    CListCtrl::OnSize(nType, cx, cy);
    AdjustColumnWidths();
}

void CFileDropListCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    // Delete键删除选中项
    if (nChar == VK_DELETE)
    {
        RemoveSelectedFiles();
        return;
    }

    CListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CFileDropListCtrl::UpdateRowIndices()
{
    // 重建ItemData映射并更新序号
    for (int i = 0; i < GetItemCount() && i < (int)m_vecItems.size(); ++i)
    {
        // 更新序号
        CString strIndex;
        strIndex.Format(_T("%d"), i + 1);
        SetItemText(i, 0, strIndex);

        // 更新ItemData指向正确的vector索引
        SetItemData(i, (DWORD_PTR)i);
    }
}

void CFileDropListCtrl::AdjustColumnWidths()
{
    if (!GetSafeHwnd())
        return;

    CRect rc;
    GetClientRect(&rc);

    int totalWidth = rc.Width();
    if (totalWidth <= 0)
        return;

    // 减去滚动条宽度
    int scrollWidth = ::GetSystemMetrics(SM_CXVSCROLL);
    totalWidth -= scrollWidth;

    // 序号列固定宽度，文件名和路径列按比例分配
    int indexWidth = 50;
    int remainingWidth = totalWidth - indexWidth;
    int fileNameWidth = remainingWidth * 30 / 100;
    int pathWidth = remainingWidth - fileNameWidth;

    SetColumnWidth(0, indexWidth);
    SetColumnWidth(1, fileNameWidth);
    SetColumnWidth(2, pathWidth);
}

bool CFileDropListCtrl::IsFileExists(const CString& strFilePath) const
{
    for (const auto& item : m_vecItems)
    {
        if (item.strFilePath.CompareNoCase(strFilePath) == 0)
        {
            return true;
        }
    }
    return false;
}

CString CFileDropListCtrl::ExtractFileName(const CString& strFilePath)
{
    int nPos = strFilePath.ReverseFind(_T('\\'));
    if (nPos == -1)
    {
        nPos = strFilePath.ReverseFind(_T('/'));
    }

    if (nPos >= 0)
    {
        return strFilePath.Mid(nPos + 1);
    }

    return strFilePath;
}

bool CFileDropListCtrl::IsValidFileExtension(const CString& strFilePath)
{
    CString strExt = PathFindExtension(strFilePath);
    strExt.MakeLower();

    return (strExt == _T(".exe") || strExt == _T(".dll"));
}
