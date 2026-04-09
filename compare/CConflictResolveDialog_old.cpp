#include "stdafx.h"
#include "CConflictResolveDialog.h"
#include "resource.h"

namespace rceditor {

IMPLEMENT_DYNAMIC(CConflictResolveDialog, CDialog)

BEGIN_MESSAGE_MAP(CConflictResolveDialog, CDialog)
    ON_WM_SIZE()
    ON_BN_CLICKED(ECtrlID::IDC_BTN_KEEP_ALL, &CConflictResolveDialog::OnBtnKeepAll)
    ON_BN_CLICKED(ECtrlID::IDC_BTN_PRIMARY_ALL, &CConflictResolveDialog::OnBtnUsePrimaryAll)
    ON_BN_CLICKED(ECtrlID::IDC_BTN_AUTO_ALL, &CConflictResolveDialog::OnBtnAutoAll)
    ON_NOTIFY(LVN_ITEMCHANGED, ECtrlID::IDC_LIST_CONFLICTS, &CConflictResolveDialog::OnListItemChanged)
    ON_CBN_SELCHANGE(ECtrlID::IDC_COMBO_RESOLUTION, &CConflictResolveDialog::OnComboResolutionChanged)
END_MESSAGE_MAP()

CConflictResolveDialog::CConflictResolveDialog(
    const std::vector<SConflictInfo>& conflicts, CWnd* pParent)
    : CDialog(IDD_ABOUTBOX, pParent)
    , m_selectedIndex(-1)
{
    m_entries.reserve(conflicts.size());
    for (const auto& c : conflicts)
    {
        SConflictResolutionEntry entry;
        entry.conflict = c;
        entry.resolution = EConflictResolution::KeepSecondary;
        entry.manualValue = 0;
        m_entries.push_back(entry);
    }
}

CConflictResolveDialog::~CConflictResolveDialog()
{
}

BOOL CConflictResolveDialog::OnInitDialog()
{
    CDialog::OnInitDialog();
    SetWindowText(L"冲突解决");

    m_font.CreatePointFont(90, L"Segoe UI");

    CreateControls();
    PopulateList();

    SetWindowPos(NULL, 0, 0, 700, 500, SWP_NOMOVE | SWP_NOZORDER);
    CenterWindow(GetParent());

    // 统计
    CStringW stats;
    stats.Format(L"冲突总数：%d", (int)m_entries.size());
    m_staticStats.SetWindowText(stats);

    LayoutControls();
    return TRUE;
}

void CConflictResolveDialog::OnOK()
{
    // 收集手动值
    if (m_selectedIndex >= 0 && m_selectedIndex < (int)m_entries.size())
    {
        if (m_entries[m_selectedIndex].resolution == EConflictResolution::Manual)
        {
            CStringW valText;
            m_edtManualValue.GetWindowText(valText);
            m_entries[m_selectedIndex].manualValue = (UINT)_wtoi(valText);
        }
    }
    CDialog::OnOK();
}

void CConflictResolveDialog::OnSize(UINT nType, int cx, int cy)
{
    CDialog::OnSize(nType, cx, cy);
    if (m_listConflicts.GetSafeHwnd())
        LayoutControls();
}

// ============================================================================
// 控件创建
// ============================================================================

void CConflictResolveDialog::CreateControls()
{
    int y = 10;

    // 统计标签
    m_staticStats.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                         CRect(10, y, 400, y + 20), this, IDC_STATIC_STATS);
    m_staticStats.SetFont(&m_font);
    y += 24;

    // 批量按钮
    m_btnKeepAll.Create(L"全部保留次语言", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        CRect(10, y, 160, y + 26), this, IDC_BTN_KEEP_ALL);
    m_btnKeepAll.SetFont(&m_font);

    m_btnPrimaryAll.Create(L"全部采用主语言", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                           CRect(170, y, 310, y + 26), this, IDC_BTN_PRIMARY_ALL);
    m_btnPrimaryAll.SetFont(&m_font);

    m_btnAutoAll.Create(L"全部自动重分配", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        CRect(320, y, 470, y + 26), this, IDC_BTN_AUTO_ALL);
    m_btnAutoAll.SetFont(&m_font);
    y += 32;

    // 冲突列表
    m_listConflicts.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                           CRect(10, y, 680, y + 250), this, IDC_LIST_CONFLICTS);
    m_listConflicts.SetFont(&m_font);
    m_listConflicts.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    m_listConflicts.InsertColumn(0, L"#", LVCFMT_LEFT, 30);
    m_listConflicts.InsertColumn(1, L"类型", LVCFMT_LEFT, 100);
    m_listConflicts.InsertColumn(2, L"主语言", LVCFMT_LEFT, 140);
    m_listConflicts.InsertColumn(3, L"次语言", LVCFMT_LEFT, 140);
    m_listConflicts.InsertColumn(4, L"解决策略", LVCFMT_LEFT, 120);
    m_listConflicts.InsertColumn(5, L"描述", LVCFMT_LEFT, 200);
    y += 256;

    // 详情面板
    m_staticDetail.Create(L"请选择冲突以查看详情", WS_CHILD | WS_VISIBLE | SS_LEFT,
                          CRect(10, y, 400, y + 20), this, IDC_STATIC_DETAIL);
    m_staticDetail.SetFont(&m_font);
    y += 24;

    // 解决策略下拉
    m_comboResolution.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
                             CRect(10, y, 200, y + 200), this, IDC_COMBO_RESOLUTION);
    m_comboResolution.SetFont(&m_font);
    m_comboResolution.AddString(L"保留次语言");
    m_comboResolution.AddString(L"采用主语言");
    m_comboResolution.AddString(L"自动重分配");
    m_comboResolution.AddString(L"手动指定值");

    // 手动值编辑框
    m_edtManualValue.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                            CRect(210, y, 310, y + 24), this, IDC_EDIT_MANUAL_VALUE);
    m_edtManualValue.SetFont(&m_font);
    m_edtManualValue.EnableWindow(FALSE);
}

void CConflictResolveDialog::LayoutControls()
{
    CRect rc;
    GetClientRect(&rc);

    if (m_listConflicts.GetSafeHwnd())
    {
        CRect rcList;
        m_listConflicts.GetWindowRect(&rcList);
        ScreenToClient(&rcList);
        m_listConflicts.SetWindowPos(NULL, rcList.left, rcList.top,
                                     rc.Width() - 20, rc.Height() - 180, SWP_NOZORDER);
    }
}

// ============================================================================
// 数据展示
// ============================================================================

void CConflictResolveDialog::PopulateList()
{
    m_listConflicts.DeleteAllItems();

    for (int i = 0; i < (int)m_entries.size(); ++i)
    {
        const auto& e = m_entries[i];
        CStringW numStr;
        numStr.Format(L"%d", i + 1);
        int idx = m_listConflicts.InsertItem(i, numStr);

        CStringW typeStr;
        switch (e.conflict.type)
        {
        case EConflictType::MacroNameConflict: typeStr = L"Macro Name"; break;
        case EConflictType::IdValueConflict:   typeStr = L"ID Value"; break;
        case EConflictType::ControlIdConflict: typeStr = L"Control ID"; break;
        default: typeStr = L"Unknown"; break;
        }
        m_listConflicts.SetItemText(idx, 1, typeStr);

        CStringW priStr;
        priStr.Format(L"%s = %u", (LPCWSTR)e.conflict.primaryName, e.conflict.primaryValue);
        m_listConflicts.SetItemText(idx, 2, priStr);

        CStringW secStr;
        secStr.Format(L"%s = %u", (LPCWSTR)e.conflict.secondaryName, e.conflict.secondaryValue);
        m_listConflicts.SetItemText(idx, 3, secStr);

        m_listConflicts.SetItemText(idx, 4, L"Keep Secondary");
        m_listConflicts.SetItemText(idx, 5, e.conflict.description);
    }
}

void CConflictResolveDialog::UpdateDetailPanel(int selectedIndex)
{
    m_selectedIndex = selectedIndex;
    if (selectedIndex < 0 || selectedIndex >= (int)m_entries.size())
    {
        m_staticDetail.SetWindowText(L"请选择冲突以查看详情");
        m_comboResolution.EnableWindow(FALSE);
        m_edtManualValue.EnableWindow(FALSE);
        return;
    }

    const auto& e = m_entries[selectedIndex];
    m_staticDetail.SetWindowText(e.conflict.description);
    m_comboResolution.EnableWindow(TRUE);
    m_comboResolution.SetCurSel((int)e.resolution);

    m_edtManualValue.EnableWindow(e.resolution == EConflictResolution::Manual);
    if (e.resolution == EConflictResolution::Manual)
    {
        CStringW valStr;
        valStr.Format(L"%u", e.manualValue);
        m_edtManualValue.SetWindowText(valStr);
    }
}

// ============================================================================
// 事件处理
// ============================================================================

void CConflictResolveDialog::OnListItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
    *pResult = 0;

    if (pNMLV->uNewState & LVIS_SELECTED)
    {
        UpdateDetailPanel(pNMLV->iItem);
    }
}

void CConflictResolveDialog::OnComboResolutionChanged()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= (int)m_entries.size())
        return;

    int sel = m_comboResolution.GetCurSel();
    if (sel < 0) return;

    m_entries[m_selectedIndex].resolution = (EConflictResolution)sel;

    // 更新列表显示
    static const LPCWSTR resNames[] = { L"保留次语言", L"采用主语言", L"自动重分配", L"手动指定值" };
    m_listConflicts.SetItemText(m_selectedIndex, 4, resNames[sel]);

    m_edtManualValue.EnableWindow(sel == (int)EConflictResolution::Manual);
    if (sel == (int)EConflictResolution::Manual)
    {
        CStringW valStr;
        valStr.Format(L"%u", m_entries[m_selectedIndex].manualValue);
        m_edtManualValue.SetWindowText(valStr);
    }
}

void CConflictResolveDialog::OnBtnKeepAll()
{
    ApplyResolutionToAll(EConflictResolution::KeepSecondary);
}

void CConflictResolveDialog::OnBtnUsePrimaryAll()
{
    ApplyResolutionToAll(EConflictResolution::UsePrimary);
}

void CConflictResolveDialog::OnBtnAutoAll()
{
    ApplyResolutionToAll(EConflictResolution::AutoReassign);
}

void CConflictResolveDialog::ApplyResolutionToAll(EConflictResolution res)
{
    static const LPCWSTR resNames[] = { L"保留次语言", L"采用主语言", L"自动重分配", L"手动指定值" };
    for (int i = 0; i < (int)m_entries.size(); ++i)
    {
        m_entries[i].resolution = res;
        m_listConflicts.SetItemText(i, 4, resNames[(int)res]);
    }

    if (m_selectedIndex >= 0)
    {
        m_comboResolution.SetCurSel((int)res);
        m_edtManualValue.EnableWindow(res == EConflictResolution::Manual);
    }
}

} // namespace rceditor
