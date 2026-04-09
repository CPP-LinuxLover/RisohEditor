#include "stdafx.h"
#include "CTaskEditDialog.h"
#include "resource.h"
#include <afxdlgs.h>

namespace rceditor {

IMPLEMENT_DYNAMIC(CTaskEditDialog, CDialog)

BEGIN_MESSAGE_MAP(CTaskEditDialog, CDialog)
    ON_BN_CLICKED(ECtrlID::IDC_BTN_PRI_DLL, &CTaskEditDialog::OnBrowsePrimaryDll)
    ON_BN_CLICKED(ECtrlID::IDC_BTN_PRI_RC, &CTaskEditDialog::OnBrowsePrimaryRc)
    ON_BN_CLICKED(ECtrlID::IDC_BTN_PRI_H, &CTaskEditDialog::OnBrowsePrimaryH)
    ON_BN_CLICKED(ECtrlID::IDC_BTN_SEC_DLL, &CTaskEditDialog::OnBrowseSecondaryDll)
    ON_BN_CLICKED(ECtrlID::IDC_BTN_SEC_RC, &CTaskEditDialog::OnBrowseSecondaryRc)
    ON_BN_CLICKED(ECtrlID::IDC_BTN_SEC_H, &CTaskEditDialog::OnBrowseSecondaryH)
    ON_BN_CLICKED(ECtrlID::IDC_BTN_OUT_DIR, &CTaskEditDialog::OnBrowseOutputDir)
    ON_WM_SIZE()
END_MESSAGE_MAP()

CTaskEditDialog::CTaskEditDialog(SCompareTaskConfig& task, CWnd* pParent)
    : CDialog()
    , m_task(task)
{
    DLGTEMPLATE* pTemplate = CreateDialogTemplate(760, 410);
    InitModalIndirect(pTemplate, pParent);
}

CTaskEditDialog::~CTaskEditDialog()
{
}

BOOL CTaskEditDialog::OnInitDialog()
{
    CDialog::OnInitDialog();

    SetWindowText(L"编辑比较任务");

    // 字体
    m_font.CreatePointFont(90, L"Segoe UI");

    CreateControls();
    PopulateData();

    // 调整窗口大小
    SetWindowPos(NULL, 0, 0, 760, 410, SWP_NOMOVE | SWP_NOZORDER);
    CenterWindow(GetParent());

    LayoutControls();

    return TRUE;
}

void CTaskEditDialog::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
}

void CTaskEditDialog::OnOK()
{
    CollectData();
    CDialog::OnOK();
}

void CTaskEditDialog::OnSize(UINT nType, int cx, int cy)
{
    CDialog::OnSize(nType, cx, cy);
    if (m_edtPriDll.GetSafeHwnd())
        LayoutControls();
}

// ============================================================================
// 控件创建
// ============================================================================

static const int ROW_HEIGHT = 26;
static const int ROW_SPACING = 4;
static const int LABEL_WIDTH = 130;
static const int BTN_WIDTH = 34;
static const int MARGIN = 10;
static const int GROUP_HEADER_HEIGHT = 22;

DLGTEMPLATE* CTaskEditDialog::CreateDialogTemplate(int widthPx, int heightPx)
{
    m_dlgTemplateBuffer.assign(512, 0);

    DLGTEMPLATE* pTemplate = reinterpret_cast<DLGTEMPLATE*>(m_dlgTemplateBuffer.data());
    pTemplate->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | WS_VISIBLE | WS_THICKFRAME;
    pTemplate->dwExtendedStyle = WS_EX_DLGMODALFRAME;
    pTemplate->cdit = 0;
    pTemplate->x = 0;
    pTemplate->y = 0;
    pTemplate->cx = (short)MulDiv(widthPx, 4, LOWORD(GetDialogBaseUnits()));
    pTemplate->cy = (short)MulDiv(heightPx, 8, HIWORD(GetDialogBaseUnits()));

    WORD* pw = reinterpret_cast<WORD*>(pTemplate + 1);
    *pw++ = 0;
    *pw++ = 0;
    *pw++ = 0;

    return pTemplate;
}

int CTaskEditDialog::CreatePathRow(int y, UINT idLabel, UINT idEdit, UINT idBtn,
                                    const CStringW& labelText, CStatic& label, CEdit& edit, CButton& btn)
{
    CRect rcClient;
    GetClientRect(&rcClient);
    int editWidth = rcClient.Width() - MARGIN * 2 - LABEL_WIDTH - BTN_WIDTH - 8;
    if (editWidth < 100) editWidth = 100;

    int x = MARGIN;
    label.Create(labelText, WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(x, y + 3, x + LABEL_WIDTH, y + ROW_HEIGHT), this, idLabel);
    label.SetFont(&m_font);
    x += LABEL_WIDTH + 4;

    edit.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                CRect(x, y, x + editWidth, y + ROW_HEIGHT), this, idEdit);
    edit.SetFont(&m_font);
    x += editWidth + 4;

    btn.Create(L"...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
               CRect(x, y, x + BTN_WIDTH, y + ROW_HEIGHT), this, idBtn);
    btn.SetFont(&m_font);

    return y + ROW_HEIGHT + ROW_SPACING;
}

void CTaskEditDialog::CreateControls()
{
    int y = MARGIN;

    // ---- 主语言组 ----
    m_lblPriGroup.Create(L"主语言（源语言）", WS_CHILD | WS_VISIBLE | SS_LEFT,
                         CRect(MARGIN, y, 400, y + GROUP_HEADER_HEIGHT), this, IDC_STATIC_PRIMARY_GROUP);
    m_lblPriGroup.SetFont(&m_font);
    y += GROUP_HEADER_HEIGHT + 4;

    y = CreatePathRow(y, IDC_STATIC_PRI_DLL, IDC_EDIT_PRI_DLL, IDC_BTN_PRI_DLL,
                       L"DLL路径：", m_lblPriDll, m_edtPriDll, m_btnPriDll);
    y = CreatePathRow(y, IDC_STATIC_PRI_RC, IDC_EDIT_PRI_RC, IDC_BTN_PRI_RC,
                       L"RC路径：", m_lblPriRc, m_edtPriRc, m_btnPriRc);
    y = CreatePathRow(y, IDC_STATIC_PRI_H, IDC_EDIT_PRI_H, IDC_BTN_PRI_H,
                       L"resource.h路径：", m_lblPriH, m_edtPriH, m_btnPriH);

    y += 8;

    // ---- 次语言组 ----
    m_lblSecGroup.Create(L"次语言（目标语言）", WS_CHILD | WS_VISIBLE | SS_LEFT,
                         CRect(MARGIN, y, 400, y + GROUP_HEADER_HEIGHT), this, IDC_STATIC_SEC_GROUP);
    m_lblSecGroup.SetFont(&m_font);
    y += GROUP_HEADER_HEIGHT + 4;

    y = CreatePathRow(y, IDC_STATIC_SEC_DLL, IDC_EDIT_SEC_DLL, IDC_BTN_SEC_DLL,
                       L"DLL路径：", m_lblSecDll, m_edtSecDll, m_btnSecDll);
    y = CreatePathRow(y, IDC_STATIC_SEC_RC, IDC_EDIT_SEC_RC, IDC_BTN_SEC_RC,
                       L"RC路径：", m_lblSecRc, m_edtSecRc, m_btnSecRc);
    y = CreatePathRow(y, IDC_STATIC_SEC_H, IDC_EDIT_SEC_H, IDC_BTN_SEC_H,
                       L"resource.h路径：", m_lblSecH, m_edtSecH, m_btnSecH);

    y += 8;

    // ---- 输出组 ----
    m_lblOutGroup.Create(L"输出（导出路径）", WS_CHILD | WS_VISIBLE | SS_LEFT,
                         CRect(MARGIN, y, 400, y + GROUP_HEADER_HEIGHT), this, IDC_STATIC_OUT_GROUP);
    m_lblOutGroup.SetFont(&m_font);
    y += GROUP_HEADER_HEIGHT + 4;

    y = CreatePathRow(y, IDC_STATIC_OUT_DIR, IDC_EDIT_OUT_DIR, IDC_BTN_OUT_DIR,
                       L"输出目录：", m_lblOutDir, m_edtOutDir, m_btnOutDir);

    m_btnOk.Create(L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                   CRect(0, 0, 80, ROW_HEIGHT + 2), this, IDOK);
    m_btnOk.SetFont(&m_font);

    m_btnCancel.Create(L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                       CRect(0, 0, 80, ROW_HEIGHT + 2), this, IDCANCEL);
    m_btnCancel.SetFont(&m_font);
}

void CTaskEditDialog::LayoutControls()
{
    CRect rcClient;
    GetClientRect(&rcClient);

    int editWidth = rcClient.Width() - MARGIN * 2 - LABEL_WIDTH - BTN_WIDTH - 8;
    if (editWidth < 100) editWidth = 100;

    // 遍历所有 edit + button 对，调整宽度
    struct EditBtnPair { CEdit* edit; CButton* btn; };
    EditBtnPair pairs[] = {
        {&m_edtPriDll, &m_btnPriDll}, {&m_edtPriRc, &m_btnPriRc}, {&m_edtPriH, &m_btnPriH},
        {&m_edtSecDll, &m_btnSecDll}, {&m_edtSecRc, &m_btnSecRc}, {&m_edtSecH, &m_btnSecH},
        {&m_edtOutDir, &m_btnOutDir},
    };

    for (auto& pair : pairs)
    {
        if (!pair.edit->GetSafeHwnd()) continue;
        CRect rcEdit;
        pair.edit->GetWindowRect(&rcEdit);
        ScreenToClient(&rcEdit);
        int x = MARGIN + LABEL_WIDTH + 4;
        pair.edit->SetWindowPos(NULL, x, rcEdit.top, editWidth, ROW_HEIGHT, SWP_NOZORDER);
        pair.btn->SetWindowPos(NULL, x + editWidth + 4, rcEdit.top, BTN_WIDTH, ROW_HEIGHT, SWP_NOZORDER);
    }

    const int btnW = 90;
    const int btnH = 28;
    const int btnGap = 10;
    const int btnY = rcClient.bottom - MARGIN - btnH;

    if (m_btnCancel.GetSafeHwnd())
        m_btnCancel.SetWindowPos(NULL, rcClient.right - MARGIN - btnW, btnY, btnW, btnH, SWP_NOZORDER);
    if (m_btnOk.GetSafeHwnd())
        m_btnOk.SetWindowPos(NULL, rcClient.right - MARGIN - btnW * 2 - btnGap, btnY, btnW, btnH, SWP_NOZORDER);
}

// ============================================================================
// 数据填充 / 收集
// ============================================================================

void CTaskEditDialog::PopulateData()
{
    m_edtPriDll.SetWindowText(m_task.primaryDllPath);
    m_edtPriRc.SetWindowText(m_task.primaryRcPath);
    m_edtPriH.SetWindowText(m_task.primaryResourceHPath);

    m_edtSecDll.SetWindowText(m_task.secondaryDllPath);
    m_edtSecRc.SetWindowText(m_task.secondaryRcPath);
    m_edtSecH.SetWindowText(m_task.secondaryResourceHPath);

    CStringW outDir = m_task.outputDir;
    if (outDir.IsEmpty())
        outDir = BuildDefaultOutputDir();
    m_edtOutDir.SetWindowText(outDir);
}

void CTaskEditDialog::CollectData()
{
    CStringW text;

    m_edtPriDll.GetWindowText(text); m_task.primaryDllPath = text;
    m_edtPriRc.GetWindowText(text);  m_task.primaryRcPath = text;
    m_edtPriH.GetWindowText(text);   m_task.primaryResourceHPath = text;

    m_edtSecDll.GetWindowText(text); m_task.secondaryDllPath = text;
    m_edtSecRc.GetWindowText(text);  m_task.secondaryRcPath = text;
    m_edtSecH.GetWindowText(text);   m_task.secondaryResourceHPath = text;

    m_edtOutDir.GetWindowText(text);
    if (text.IsEmpty())
        text = BuildDefaultOutputDir();
    m_task.outputDir = text;
    m_task.outputDllPath.Empty();
    m_task.outputRcPath.Empty();
    m_task.outputResourceHPath.Empty();
}

CStringW CTaskEditDialog::BuildDefaultOutputDir() const
{
    CStringW binPath;
    if (m_edtSecDll.GetSafeHwnd())
        m_edtSecDll.GetWindowText(binPath);
    if (binPath.IsEmpty())
        binPath = m_task.secondaryDllPath;
    if (binPath.IsEmpty())
        return L"";

    int slashPos = binPath.ReverseFind(L'\\');
    if (slashPos < 0)
        slashPos = binPath.ReverseFind(L'/');

    CStringW dir;
    CStringW fileName = binPath;
    if (slashPos >= 0)
    {
        dir = binPath.Left(slashPos);
        fileName = binPath.Mid(slashPos + 1);
    }

    int dotPos = fileName.ReverseFind(L'.');
    if (dotPos > 0)
        fileName = fileName.Left(dotPos);

    if (fileName.IsEmpty())
        fileName = L"merged";

    if (dir.IsEmpty())
        return fileName + L"_merged";
    return dir + L"\\" + fileName + L"_merged";
}

// ============================================================================
// 文件浏览
// ============================================================================

void CTaskEditDialog::BrowseForFile(CEdit& edit, const CStringW& filter, const CStringW& title)
{
    CStringW currentPath;
    edit.GetWindowText(currentPath);

    CFileDialog dlg(TRUE, NULL, currentPath.IsEmpty() ? NULL : (LPCWSTR)currentPath,
                    OFN_HIDEREADONLY | OFN_FILEMUSTEXIST,
                    filter, this);
    dlg.m_ofn.lpstrTitle = title;

    if (dlg.DoModal() == IDOK)
    {
        edit.SetWindowText(dlg.GetPathName());
    }
}

void CTaskEditDialog::BrowseForFolder(CEdit& edit, const CStringW& title)
{
    CStringW currentPath;
    edit.GetWindowText(currentPath);

    CFolderPickerDialog dlg(currentPath, OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST, this, 0);
    dlg.m_ofn.lpstrTitle = title;

    if (dlg.DoModal() == IDOK)
    {
        edit.SetWindowText(dlg.GetPathName());
    }
}

void CTaskEditDialog::OnBrowsePrimaryDll()
{
    BrowseForFile(m_edtPriDll, L"二进制文件 (*.dll;*.exe)|*.dll;*.exe|DLL 文件 (*.dll)|*.dll|EXE 文件 (*.exe)|*.exe|所有文件 (*.*)|*.*||", L"选择主语言二进制文件");
}

void CTaskEditDialog::OnBrowsePrimaryRc()
{
    BrowseForFile(m_edtPriRc, L"RC 文件 (*.rc)|*.rc|所有文件 (*.*)|*.*||", L"选择主语言 RC 文件");
}

void CTaskEditDialog::OnBrowsePrimaryH()
{
    BrowseForFile(m_edtPriH, L"头文件 (*.h)|*.h|所有文件 (*.*)|*.*||", L"选择主语言 resource.h");
}

void CTaskEditDialog::OnBrowseSecondaryDll()
{
    BrowseForFile(m_edtSecDll, L"二进制文件 (*.dll;*.exe)|*.dll;*.exe|DLL 文件 (*.dll)|*.dll|EXE 文件 (*.exe)|*.exe|所有文件 (*.*)|*.*||", L"选择次语言二进制文件");

    CStringW outDir;
    m_edtOutDir.GetWindowText(outDir);
    if (outDir.IsEmpty())
        m_edtOutDir.SetWindowText(BuildDefaultOutputDir());
}

void CTaskEditDialog::OnBrowseSecondaryRc()
{
    BrowseForFile(m_edtSecRc, L"RC 文件 (*.rc)|*.rc|所有文件 (*.*)|*.*||", L"选择次语言 RC 文件");
}

void CTaskEditDialog::OnBrowseSecondaryH()
{
    BrowseForFile(m_edtSecH, L"头文件 (*.h)|*.h|所有文件 (*.*)|*.*||", L"选择次语言 resource.h");
}

void CTaskEditDialog::OnBrowseOutputDir()
{
    BrowseForFolder(m_edtOutDir, L"选择输出目录");
}

} // namespace rceditor
