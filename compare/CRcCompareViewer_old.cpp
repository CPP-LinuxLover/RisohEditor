#include "stdafx.h"
#include "CRcCompareViewer.h"
#include "Resource.h"
#include "LevelLocalize.h"
#include <algorithm>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
    // Map an added-control index (vecAddedCtrlInfo) to the visible index (vecCtrlInfo)
    // using shared_ptr identity first, then fallback to ID for compatibility.
    static int FindCtrlIndexFromAddedIndex(const std::shared_ptr<rceditor::SDialogInfo>& dlg, int addedIndex, UINT ctrlId)
    {
        if (!dlg)
            return -1;

        if (addedIndex >= 0 && addedIndex < (int)dlg->vecAddedCtrlInfo.size())
        {
            auto addedCtrl = dlg->vecAddedCtrlInfo[addedIndex];
            if (addedCtrl)
            {
                for (int i = 0; i < (int)dlg->vecCtrlInfo.size(); ++i)
                {
                    if (dlg->vecCtrlInfo[i] == addedCtrl)
                        return i;
                }
            }
        }

        for (int i = 0; i < (int)dlg->vecCtrlInfo.size(); ++i)
        {
            if (dlg->vecCtrlInfo[i] && dlg->vecCtrlInfo[i]->nIDC == ctrlId)
                return i;
        }
        return -1;
    }

    static int FindAddedIndexByCtrlPtr(const std::shared_ptr<rceditor::SDialogInfo>& dlg, const std::shared_ptr<rceditor::SCtrlInfo>& ctrl)
    {
        if (!dlg || !ctrl)
            return -1;
        for (int i = 0; i < (int)dlg->vecAddedCtrlInfo.size(); ++i)
        {
            if (dlg->vecAddedCtrlInfo[i] == ctrl)
                return i;
        }
        return -1;
    }
}

// 内部编辑对话框类 - 用于编辑次语言文本
class CTextEditDlgInternal : public CDialog
{
public:
    CString m_strPrimary;
    CString m_strSecondary;
    CString m_strResult;
    CEdit m_editSecondary;
    CStatic m_staticPrimary;
    CStatic m_staticPrimaryLabel;
    CStatic m_staticSecondaryLabel;
    CButton m_btnOK;
    CButton m_btnCancel;
    CFont m_font;
    
    CTextEditDlgInternal(const CString& strPrimary, const CString& strSecondary, CWnd* pParent)
        : CDialog()
        , m_strPrimary(strPrimary)
        , m_strSecondary(strSecondary)
        , m_strResult(strSecondary)
    {
        // 动态创建对话框模板
        CRect rect(0, 0, 760, 520);
        DLGTEMPLATE* pTemplate = CreateDialogTemplate(
            WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | WS_VISIBLE,
            rect);
        InitModalIndirect(pTemplate, pParent);
    }
    
    DLGTEMPLATE* CreateDialogTemplate(DWORD dwStyle, const CRect& rect)
    {
        // 分配足够的内存（使用静态缓冲区）
        static BYTE buffer[512];
        memset(buffer, 0, sizeof(buffer));
        
        DLGTEMPLATE* pTemplate = (DLGTEMPLATE*)buffer;
        pTemplate->style = dwStyle;
        pTemplate->dwExtendedStyle = 0;
        pTemplate->cdit = 0;
        pTemplate->x = 0;
        pTemplate->y = 0;
        pTemplate->cx = (short)MulDiv(rect.Width(), 4, LOWORD(GetDialogBaseUnits()));
        pTemplate->cy = (short)MulDiv(rect.Height(), 8, HIWORD(GetDialogBaseUnits()));
        
        WORD* pw = (WORD*)(pTemplate + 1);
        *pw++ = 0; // 无菜单
        *pw++ = 0; // 默认对话框类
        *pw++ = 0; // 无标题（稍后设置）
        
        return pTemplate;
    }
    
    BOOL OnInitDialog() override
    {
        CDialog::OnInitDialog();
        
        // 创建字体
        m_font.CreatePointFont(120, _T("Microsoft YaHei UI"));
        
        CRect rcClient;
        GetClientRect(&rcClient);

        const int margin = 12;
        const int labelH = 24;
        const int primaryH = 90;
        const int gap = 10;
        const int editBottomGap = 20;
        const int btnW = 100;
        const int btnH = 32;

        int y = margin;
        const int right = rcClient.right - margin;
        const int btnY = rcClient.bottom - margin - btnH;
        
        // 主语言标签
        m_staticPrimaryLabel.Create(ML(UI_VIEWER_COL_PRIMARY(gbCurNeedShowLan)), 
            WS_CHILD | WS_VISIBLE, CRect(margin, y, right, y + labelH), this);
        m_staticPrimaryLabel.SetFont(&m_font);
        y += labelH + gap;
        
        // 主语言文本（只读）
        m_staticPrimary.Create(m_strPrimary, 
            WS_CHILD | WS_VISIBLE | WS_BORDER | SS_LEFT | SS_NOPREFIX,
            CRect(margin, y, right, y + primaryH), this);
        m_staticPrimary.SetFont(&m_font);
        y += primaryH + gap;
        
        // 次语言标签
        m_staticSecondaryLabel.Create(ML(UI_VIEWER_COL_SECONDARY(gbCurNeedShowLan)), 
            WS_CHILD | WS_VISIBLE, CRect(margin, y, right, y + labelH), this);
        m_staticSecondaryLabel.SetFont(&m_font);
        y += labelH + gap;

        int editH = btnY - y - editBottomGap;
        if (editH < 120)
            editH = 120;
        
        // 次语言编辑框
        m_editSecondary.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_MULTILINE | ES_WANTRETURN,
            CRect(margin, y, right, y + editH), this, 1001);
        m_editSecondary.SetFont(&m_font);
        m_editSecondary.SetWindowText(m_strSecondary);
        m_editSecondary.SetSel(0, -1);
        m_editSecondary.SetFocus();
        m_editSecondary.SetLimitText(0);
        
        // 按钮
        int btnX = rcClient.right - margin - (btnW * 2 + gap);
        
        m_btnOK.Create(ML(UI_OK(gbCurNeedShowLan)), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            CRect(btnX, btnY, btnX + btnW, btnY + btnH), this, IDOK);
        m_btnOK.SetFont(&m_font);
        
        m_btnCancel.Create(ML(UI_CANCEL(gbCurNeedShowLan)), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(btnX + btnW + gap, btnY, btnX + btnW * 2 + gap, btnY + btnH), this, IDCANCEL);
        m_btnCancel.SetFont(&m_font);
        
        // 设置窗口标题
        SetWindowText(ML(UI_VIEWER_EDIT_TITLE(gbCurNeedShowLan)));
        
        // 居中显示
        CenterWindow();
        
        return FALSE; // 已设置焦点
    }
    
    void OnOK() override
    {
        m_editSecondary.GetWindowText(m_strResult);
        CDialog::OnOK();
    }
};

#ifndef WM_APP_DESIGNSURFACE
#define WM_APP_DESIGNSURFACE (WM_APP + 210)
#endif

IMPLEMENT_DYNAMIC(CRcCompareViewer, CFormRcEditor)

BEGIN_MESSAGE_MAP(CRcCompareViewer, CFormRcEditor)
    ON_WM_SIZE()
    ON_WM_CLOSE()
    ON_WM_TIMER()
    ON_BN_CLICKED(IDC_BTN_NEXT_UNMOD, &CRcCompareViewer::OnBtnNextUnmodified)
    ON_BN_CLICKED(IDC_BTN_PREV_UNMOD, &CRcCompareViewer::OnBtnPrevUnmodified)
    ON_BN_CLICKED(IDC_BTN_EXPORT_RC, &CRcCompareViewer::OnBtnExportRc)
    ON_BN_CLICKED(IDC_BTN_SAVE_RES, &CRcCompareViewer::OnBtnSave)
    ON_BN_CLICKED(IDC_BTN_ACCEPT_ALL_PRIMARY, &CRcCompareViewer::OnBtnAcceptAllPrimary)
    ON_BN_CLICKED(IDC_BTN_MARK_ALL_DONE, &CRcCompareViewer::OnBtnMarkAllDone)
    ON_BN_CLICKED(IDC_CHK_SHOW_PRIMARY, &CRcCompareViewer::OnChkShowPrimary)
    ON_CBN_SELCHANGE(IDC_COMBO_TASK, &CRcCompareViewer::OnComboTaskSelChange)
    ON_CBN_SELCHANGE(IDC_COMBO_FILTER, &CRcCompareViewer::OnComboFilterSelChange)
    ON_NOTIFY(NM_DBLCLK, IDC_LIST_ADDED_ITEMS, &CRcCompareViewer::OnListAddedItemDblClick)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST_ADDED_ITEMS, &CRcCompareViewer::OnListAddedItemSelChange)
    ON_NOTIFY(LVN_ENDLABELEDIT, IDC_LIST_ADDED_ITEMS, &CRcCompareViewer::OnListAddedItemEndLabelEdit)
    ON_MESSAGE(WM_APP_DESIGNSURFACE, &CRcCompareViewer::OnDesignSurfaceNotify)
END_MESSAGE_MAP()

CRcCompareViewer::CRcCompareViewer(CRcCompareControler* pControler, CWnd* pParent)
    : CFormRcEditor(pParent)
    , m_pControler(pControler)
    , m_nCurrentAddedIndex(-1)
    , m_nCurrentTaskIndex(-1)
    , m_eFilterMode(FILTER_ALL)
    , m_bUpdatingUI(false)
    , m_nBottomPanelHeight(200)
    , m_bShowingPrimaryLayout(false)
    , m_nLastDialogID(0)
{
}

CRcCompareViewer::~CRcCompareViewer()
{
}

void CRcCompareViewer::SetCompareControler(CRcCompareControler* pControler)
{
    m_pControler = pControler;
    if (GetSafeHwnd())
    {
        LoadSecondaryResources();
        RefreshAddedItemsList();
    }
}

BOOL CRcCompareViewer::OnInitDialog()
{
    // 在调用父类OnInitDialog之前，先计算底部面板高度
    // 这样父类的LayoutControls能正确预留空间
    const int nComboRowHeight = 24;
    const int nBtnHeight = 28;
    const int nListHeight = 150;
    const int nGap = 5;
    const int nMargin = 6;
    m_nBottomPanelHeight = nComboRowHeight + nGap + nBtnHeight + nGap + nListHeight + nMargin;

    CFormRcEditor::OnInitDialog();

    // 设置窗口标题
    SetWindowText(ML(UI_VIEWER_TITLE_FULL(gbCurNeedShowLan)));

    // 创建查看器特有的控件
    CreateViewerControls();

    // 刷新任务下拉框
    RefreshTaskCombo();

    // 加载次语言资源
    LoadSecondaryResources();

    // 刷新新增项列表
    RefreshAddedItemsList();

    // 重新布局所有控件（包括父类控件和新增控件）
    LayoutControls();

    // 更新进度显示
    UpdateProgressDisplay();

    // 启动定时器更新进度
    SetTimer(TIMER_UPDATE_PROGRESS, 1000, nullptr);

    // 如果有待翻译项，自动跳转到第一个
    if (m_pControler && m_pControler->GetUnmodifiedCount() > 0)
    {
        NavigateToNextUnmodified();
    }
    // Ensure we call the Win32 ShowWindow with the dialog HWND and the show command
    ::ShowWindow(GetSafeHwnd(), SW_SHOWMAXIMIZED);
    return TRUE;
}

void CRcCompareViewer::OnAfterControlsCreated()
{
    CFormRcEditor::OnAfterControlsCreated();
}

void CRcCompareViewer::OnBeforeDialogClose()
{
    KillTimer(TIMER_UPDATE_PROGRESS);
    CFormRcEditor::OnBeforeDialogClose();
}

void CRcCompareViewer::CreateViewerControls()
{
    // 创建任务标签
    m_staticTaskLabel.Create(_T("任务："), WS_CHILD | WS_VISIBLE | SS_LEFT,
        CRect(0, 0, 40, 20), this, IDC_STATIC_TASK_LABEL);
    m_staticTaskLabel.SetFont(&m_font);

    // 创建任务切换下拉框
    m_comboTask.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        CRect(0, 0, 200, 200), this, IDC_COMBO_TASK);
    m_comboTask.SetFont(&m_font);

    // 创建筛选标签
    m_staticFilterLabel.Create(_T("筛选："), WS_CHILD | WS_VISIBLE | SS_LEFT,
        CRect(0, 0, 40, 20), this, IDC_STATIC_FILTER_LABEL);
    m_staticFilterLabel.SetFont(&m_font);

    // 创建筛选下拉框
    m_comboFilter.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        CRect(0, 0, 140, 200), this, IDC_COMBO_FILTER);
    m_comboFilter.SetFont(&m_font);
    m_comboFilter.AddString(_T("全部"));
    m_comboFilter.AddString(_T("待处理"));
    m_comboFilter.AddString(_T("已翻译"));
    m_comboFilter.AddString(_T("冲突"));
    m_comboFilter.AddString(_T("需调整"));
    m_comboFilter.SetCurSel(FILTER_ALL);

    // 创建显示主语言布局复选框
    m_chkShowPrimary.Create(_T("显示主语言布局"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        CRect(0, 0, 120, 20), this, IDC_CHK_SHOW_PRIMARY);
    m_chkShowPrimary.SetFont(&m_font);

    // 创建新增项列表 - 支持标签编辑
    m_listAddedItems.Create(
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_EDITLABELS,
        CRect(0, 0, 300, 150),
        this,
        IDC_LIST_ADDED_ITEMS);
    m_listAddedItems.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    m_listAddedItems.InsertColumn(COL_TYPE, ML(UI_VIEWER_COL_TYPE(gbCurNeedShowLan)), LVCFMT_LEFT, 60);
    m_listAddedItems.InsertColumn(COL_ID, ML(UI_VIEWER_COL_ID(gbCurNeedShowLan)), LVCFMT_LEFT, 80);
    m_listAddedItems.InsertColumn(COL_PRIMARY_TEXT, ML(UI_VIEWER_COL_PRIMARY(gbCurNeedShowLan)), LVCFMT_LEFT, 160);
    m_listAddedItems.InsertColumn(COL_SECONDARY_TEXT, ML(UI_VIEWER_COL_SECONDARY(gbCurNeedShowLan)), LVCFMT_LEFT, 160);
    m_listAddedItems.InsertColumn(COL_STATUS, ML(UI_VIEWER_COL_STATUS(gbCurNeedShowLan)), LVCFMT_LEFT, 65);
    m_listAddedItems.InsertColumn(COL_SOURCE_DLL, _T("来源DLL"), LVCFMT_LEFT, 120);
    m_listAddedItems.InsertColumn(COL_CONFLICT, _T("冲突"), LVCFMT_LEFT, 90);

    // 创建按钮
    m_btnPrevUnmod.Create(ML(UI_VIEWER_BTN_PREV(gbCurNeedShowLan)), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 90, 25), this, IDC_BTN_PREV_UNMOD);
    m_btnNextUnmod.Create(ML(UI_VIEWER_BTN_NEXT(gbCurNeedShowLan)), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 90, 25), this, IDC_BTN_NEXT_UNMOD);
    m_btnExportRc.Create(ML(UI_VIEWER_BTN_EXPORT(gbCurNeedShowLan)), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 80, 25), this, IDC_BTN_EXPORT_RC);
    m_btnSaveRes.Create(ML(UI_VIEWER_BTN_SAVE(gbCurNeedShowLan)), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 80, 25), this, IDC_BTN_SAVE_RES);
    m_btnAcceptAllPrimary.Create(_T("全部采用主语言"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 130, 25), this, IDC_BTN_ACCEPT_ALL_PRIMARY);
    m_btnMarkAllDone.Create(_T("全部标记完成"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 100, 25), this, IDC_BTN_MARK_ALL_DONE);

    // 创建进度显示
    CString strInit;
    strInit.Format(ML(UI_VIEWER_PROGRESS_FMT(gbCurNeedShowLan)), 0, 0);
    m_staticProgress.Create(strInit, WS_CHILD | WS_VISIBLE | SS_LEFT,
        CRect(0, 0, 180, 20), this, IDC_STATIC_PROGRESS);

    // 设置字体
    m_listAddedItems.SetFont(&m_font);
    m_btnPrevUnmod.SetFont(&m_font);
    m_btnNextUnmod.SetFont(&m_font);
    m_btnExportRc.SetFont(&m_font);
    m_btnSaveRes.SetFont(&m_font);
    m_btnAcceptAllPrimary.SetFont(&m_font);
    m_btnMarkAllDone.SetFont(&m_font);
    m_staticProgress.SetFont(&m_font);
}

void CRcCompareViewer::LayoutViewerControls()
{
    if (!GetSafeHwnd())
        return;

    CRect rcClient;
    GetClientRect(&rcClient);

    const int nMargin = 6;
    const int nBtnHeight = 28;
    const int nBtnWidth = 90;
    const int nComboRowHeight = 24;
    const int nListHeight = 150;
    const int nGap = 5;

    // 底部面板高度：combo行 + 按钮行 + 列表（不包含外边距）
    m_nBottomPanelHeight = nComboRowHeight + nGap + nBtnHeight + nGap + nListHeight + nMargin;

    // 计算底部面板的起始位置
    int nPanelTop = rcClient.bottom - nMargin - m_nBottomPanelHeight;

    // === 第一行：任务切换下拉框 + 筛选下拉框 ===
    int nRowY = nPanelTop;
    int nX = nMargin;

    if (m_staticTaskLabel.GetSafeHwnd())
    {
        m_staticTaskLabel.MoveWindow(nX, nRowY + 3, 40, 18);
        nX += 42;
    }
    if (m_comboTask.GetSafeHwnd())
    {
        m_comboTask.MoveWindow(nX, nRowY, 220, 200);
        nX += 225;
    }
    if (m_staticFilterLabel.GetSafeHwnd())
    {
        m_staticFilterLabel.MoveWindow(nX, nRowY + 3, 40, 18);
        nX += 42;
    }
    if (m_comboFilter.GetSafeHwnd())
    {
        m_comboFilter.MoveWindow(nX, nRowY, 130, 200);
        nX += 135;
    }
    if (m_chkShowPrimary.GetSafeHwnd())
    {
        m_chkShowPrimary.MoveWindow(nX, nRowY + 2, 130, 20);
    }

    // === 第二行：按钮行 ===
    int nBtnTop = nRowY + nComboRowHeight + nGap;
    int nBtnLeft = nMargin;

    if (m_btnPrevUnmod.GetSafeHwnd())
    {
        m_btnPrevUnmod.MoveWindow(nBtnLeft, nBtnTop, nBtnWidth, nBtnHeight);
        nBtnLeft += nBtnWidth + nGap;
    }
    if (m_btnNextUnmod.GetSafeHwnd())
    {
        m_btnNextUnmod.MoveWindow(nBtnLeft, nBtnTop, nBtnWidth, nBtnHeight);
        nBtnLeft += nBtnWidth + nGap * 2;
    }
    if (m_btnAcceptAllPrimary.GetSafeHwnd())
    {
        m_btnAcceptAllPrimary.MoveWindow(nBtnLeft, nBtnTop, 130, nBtnHeight);
        nBtnLeft += 135;
    }
    if (m_btnMarkAllDone.GetSafeHwnd())
    {
        m_btnMarkAllDone.MoveWindow(nBtnLeft, nBtnTop, 100, nBtnHeight);
        nBtnLeft += 105;
    }
    if (m_staticProgress.GetSafeHwnd())
    {
        m_staticProgress.MoveWindow(nBtnLeft, nBtnTop + 4, 200, 20);
    }

    // 右侧按钮
    int nRightBtnLeft = rcClient.right - nMargin - 90;
    if (m_btnSaveRes.GetSafeHwnd())
    {
        m_btnSaveRes.MoveWindow(nRightBtnLeft, nBtnTop, 90, nBtnHeight);
        nRightBtnLeft -= 90 + nGap;
    }
    if (m_btnExportRc.GetSafeHwnd())
    {
        m_btnExportRc.MoveWindow(nRightBtnLeft, nBtnTop, 80, nBtnHeight);
    }

    // === 第三行：新增项列表 ===
    int nListTop = nBtnTop + nBtnHeight + nGap;
    int nListWidth = rcClient.Width() - nMargin * 2;
    int nPanelBottom = rcClient.bottom - nMargin;

    if (m_listAddedItems.GetSafeHwnd())
    {
        m_listAddedItems.MoveWindow(nMargin, nListTop, nListWidth, nPanelBottom - nListTop);
    }
}

void CRcCompareViewer::OnSize(UINT nType, int cx, int cy)
{
    // 直接调用CDialog的OnSize，跳过父类的布局
    CDialog::OnSize(nType, cx, cy);
    
    if (!m_initialized)
        return;
    
    // 使用自己的布局函数
    LayoutControls();
}

void CRcCompareViewer::LayoutControls()
{
    CRect client;
    GetClientRect(&client);

    const int margin = 6;
    const int treeW = 260;
    const int propW = 360;
    const int split = 6;
    const int toolbarH = 28;
    const int searchH = 24;
    const int bottomBarH = 28;
    
    // 底部新增项面板的参数（必须与LayoutViewerControls保持一致）
    const int nComboRowHeight = 24;
    const int nBtnHeight = 28;
    const int nListHeight = 150;
    const int nGap = 5;
    m_nBottomPanelHeight = nComboRowHeight + nGap + nBtnHeight + nGap + nListHeight + margin;

    CRect rcToolbar(margin, margin, client.right - margin, margin + toolbarH);
    
    // 主体区域需要为底部新增项面板留出空间
    // 底部面板占用的总高度 = m_nBottomPanelHeight + split（面板与主体区域的间隔）
    int bodyBottom = client.bottom - margin - m_nBottomPanelHeight - split;
    CRect rcBody(margin, rcToolbar.bottom + split, client.right - margin, bodyBottom);

    if (m_toolbar.GetSafeHwnd())
        m_toolbar.MoveWindow(rcToolbar);

    CRect rcLeft(rcBody.left, rcBody.top, rcBody.left + treeW, rcBody.bottom);
    CRect rcSearchEdit(rcLeft.left, rcLeft.top, rcLeft.right - 70 - split, rcLeft.top + searchH);
    CRect rcSearchBtn(rcSearchEdit.right + split, rcLeft.top, rcLeft.right, rcLeft.top + searchH);
    CRect rcTree(rcLeft.left, rcSearchEdit.bottom + split, rcLeft.right, rcLeft.bottom);
    CRect rcProp(rcBody.right - propW, rcBody.top, rcBody.right, rcBody.bottom);
    CRect rcCenter(rcTree.right + split, rcBody.top, rcProp.left - split, rcBody.bottom);
    CRect rcSurface(rcCenter.left, rcCenter.top, rcCenter.right, std::max(rcCenter.top, rcCenter.bottom - bottomBarH - split));
    CRect rcBottom(rcCenter.left, rcSurface.bottom + split, rcCenter.right, rcCenter.bottom);
    CRect rcCheck(rcBottom.left, rcBottom.top, rcBottom.left + 80, rcBottom.bottom);
    int comboW = 80;
    if (m_comboBadge.GetSafeHwnd())
        comboW = std::min(140, std::max(80, CalcComboBestWidth(m_comboBadge)));
    CRect rcCombo(rcCheck.right + split, rcBottom.top, rcCheck.right + split + comboW, rcBottom.bottom);

    if (m_editSearch.GetSafeHwnd())
        m_editSearch.MoveWindow(rcSearchEdit);
    if (m_btnSearch.GetSafeHwnd())
        m_btnSearch.MoveWindow(rcSearchBtn);

    if (m_tree.GetSafeHwnd())
        m_tree.MoveWindow(rcTree);
    if (m_surface && m_surface->GetSafeHwnd())
        m_surface->MoveWindow(rcSurface);
    // 让派生类布局等级 UI 控件
    LayoutLevelUiControls(rcCheck, rcCombo);
    
    // 属性控件布局
#ifdef USE_BOTH_PROPERTY_CONTROLS
    CRect rcPropTop(rcProp.left, rcProp.top, rcProp.right, rcProp.top + rcProp.Height() / 2 - split / 2);
    CRect rcPropBottom(rcProp.left, rcProp.top + rcProp.Height() / 2 + split / 2, rcProp.right, rcProp.bottom);
    if (m_propGrid.GetSafeHwnd())
        m_propGrid.MoveWindow(rcPropTop);
    if (m_propList.GetSafeHwnd())
        m_propList.MoveWindow(rcPropBottom);
#else
    #ifdef USE_PROPERTY_GRID
    if (m_propGrid.GetSafeHwnd())
        m_propGrid.MoveWindow(rcProp);
    #endif
    #ifdef USE_PROPERTY_LIST
    if (m_propList.GetSafeHwnd())
        m_propList.MoveWindow(rcProp);
    #endif
#endif

    // 布局底部新增项控件
    LayoutViewerControls();
}

void CRcCompareViewer::LoadSecondaryResources()
{
    if (!m_pControler)
        return;

    m_openedEditors.clear();
    m_openedResources.clear();

    // 兼容旧单文件模式：从控制器已加载的次语言编辑器读取
    std::vector<CString> vecPaths;
    m_pControler->GetSecondaryFilePaths(vecPaths);

    for (const auto& strPath : vecPaths)
    {
        auto pRes = m_pControler->GetSecondaryResource(strPath);
        auto pEditor = m_pControler->GetSecondaryEditor(strPath);
        if (pRes && pEditor)
        {
            m_openedEditors[strPath] = pEditor;
            m_openedResources[strPath] = pRes;
        }
    }

    // 项目模式：从任务数据同步次语言资源，确保 viewer 可绑定到 design surface
    const auto& vecTaskData = m_pControler->GetTaskDataVec();
    for (const auto& td : vecTaskData)
    {
        if (!td.bLoaded)
            continue;
        if (td.config.secondaryDllPath.IsEmpty())
            continue;
        if (!td.secondaryEditor)
            continue;

        CString secondaryPath(td.config.secondaryDllPath);
        m_openedEditors[secondaryPath] = td.secondaryEditor;
        m_openedResources[secondaryPath] = std::make_shared<rceditor::SSingleResourceInfo>(
            rceditor::DeepCloneResourceInfo(td.secondaryBinaryRes));
    }

    // 如果有文件，设置第一个为活动
    if (!vecPaths.empty())
    {
        SetActiveFile(vecPaths[0]);
        return;
    }

    if (!m_openedResources.empty())
    {
        SetActiveFile(m_openedResources.begin()->first);
    }
}

void CRcCompareViewer::RefreshAddedItemsList()
{
    if (!m_pControler || !m_listAddedItems.GetSafeHwnd())
        return;

    m_bUpdatingUI = true;

    m_listAddedItems.DeleteAllItems();

    const auto& vecItems = m_pControler->GetAddedItems();

    // 预计算所有 Control 类型项的验证结果并缓存，避免 MatchesFilter 和列表插入代码各调一次
    m_mapValidationCache.clear();
    if (m_pControler)
    {
        const auto& taskVec = m_pControler->GetTaskDataVec();
        for (int i = 0; i < (int)vecItems.size(); ++i)
        {
            const auto& item = vecItems[i];
            if (item.eType != EAddedItemType::Control)
                continue;

            std::shared_ptr<rceditor::SDialogInfo> pDlg;
            for (const auto& td : taskVec)
            {
                if (!td.bMerged)
                    continue;
                auto it = td.secondaryBinaryRes.mapLangAndDialogInfo.find(item.nDialogID);
                if (it != td.secondaryBinaryRes.mapLangAndDialogInfo.end())
                {
                    pDlg = it->second;
                    break;
                }
            }

            if (pDlg && item.nCtrlIndex >= 0 && item.nCtrlIndex < (int)pDlg->vecAddedCtrlInfo.size())
            {
                auto pCtrl = pDlg->vecAddedCtrlInfo[item.nCtrlIndex];
                if (pCtrl)
                {
                    int ctrlIdx = -1;
                    for (int ci = 0; ci < (int)pDlg->vecCtrlInfo.size(); ++ci)
                    {
                        if (pDlg->vecCtrlInfo[ci] == pCtrl)
                        {
                            ctrlIdx = ci;
                            break;
                        }
                    }

                    m_mapValidationCache[i] = rceditor::CControlAutoLayout::ValidateControlPosition(
                        pCtrl->ctrlRect, pDlg->rectDlg, pDlg->vecCtrlInfo, ctrlIdx);
                }
            }
        }
    }

    int nRow = 0;
    for (int i = 0; i < (int)vecItems.size(); ++i)
    {
        const auto& item = vecItems[i];

        // 应用筛选
        if (!MatchesFilter(item, i))
            continue;

        // 类型
        CString strType;
        switch (item.eType)
        {
        case EAddedItemType::Control:
            strType = ML(UI_VIEWER_TYPE_CONTROL(gbCurNeedShowLan));
            break;
        case EAddedItemType::String:
            strType = ML(UI_VIEWER_TYPE_STRING(gbCurNeedShowLan));
            break;
        case EAddedItemType::Menu:
            strType = ML(UI_VIEWER_TYPE_MENU(gbCurNeedShowLan));
            break;
        case EAddedItemType::DialogCaption:
            strType = ML(UI_VIEWER_TYPE_CAPTION(gbCurNeedShowLan));
            break;
        }

        // ID
        CString strID;
        switch (item.eType)
        {
        case EAddedItemType::Control:
            strID.Format(_T("%u-%u"), item.nDialogID, item.nCtrlID);
            break;
        case EAddedItemType::String:
            strID.Format(_T("%u"), item.nStringID);
            break;
        case EAddedItemType::Menu:
            strID.Format(_T("%u"), item.nMenuID);
            break;
        default:
            strID.Format(_T("%u"), item.nDialogID);
            break;
        }

        // Source DLL — extract filename from path
        CString strSourceDLL;
        if (!item.csFilePath.IsEmpty())
        {
            int nLastSlash = item.csFilePath.ReverseFind(_T('\\'));
            if (nLastSlash >= 0)
                strSourceDLL = item.csFilePath.Mid(nLastSlash + 1);
            else
                strSourceDLL = item.csFilePath;
        }

        // Conflict detection
        CString strConflict = _T("-");
        if (m_pControler)
        {
            const auto& conflicts = m_pControler->GetConflicts();
            for (const auto& c : conflicts)
            {
                bool match = false;
                if (item.eType == EAddedItemType::Control && c.dialogId == item.nDialogID)
                    match = true;
                else if (c.filePath == item.csFilePath)
                    match = true;
                if (match)
                {
                    switch (c.type)
                    {
                    case rceditor::EConflictType::MacroNameConflict: strConflict = _T("宏名冲突"); break;
                    case rceditor::EConflictType::IdValueConflict: strConflict = _T("ID值冲突"); break;
                    case rceditor::EConflictType::ControlIdConflict: strConflict = _T("控件ID冲突"); break;
                    default: break;
                    }
                    break;
                }
            }

            // 边界校验：使用预计算的缓存结果
            if (item.eType == EAddedItemType::Control)
            {
                auto cacheIt = m_mapValidationCache.find(i);
                if (cacheIt != m_mapValidationCache.end() && cacheIt->second.bOutOfBounds)
                {
                    strConflict = _T("越界");
                }
            }
        }

        int nInserted = m_listAddedItems.InsertItem(nRow, strType);
        m_listAddedItems.SetItemText(nInserted, COL_ID, strID);
        m_listAddedItems.SetItemText(nInserted, COL_PRIMARY_TEXT, rceditor::CStringConverter::ConvertToT(item.csPrimaryText));
        m_listAddedItems.SetItemText(nInserted, COL_SECONDARY_TEXT, rceditor::CStringConverter::ConvertToT(item.csSecondaryText));
        m_listAddedItems.SetItemText(nInserted, COL_STATUS, 
            item.bModified ? ML(UI_VIEWER_STATUS_TRANSLATED(gbCurNeedShowLan)) : ML(UI_VIEWER_STATUS_PENDING(gbCurNeedShowLan)));
        m_listAddedItems.SetItemText(nInserted, COL_SOURCE_DLL, strSourceDLL);
        m_listAddedItems.SetItemText(nInserted, COL_CONFLICT, strConflict);
        m_listAddedItems.SetItemData(nInserted, (DWORD_PTR)i);
        ++nRow;
    }

    m_bUpdatingUI = false;
}

void CRcCompareViewer::UpdateListItemDisplay(int nAddedIndex)
{
    if (!m_pControler || !m_listAddedItems.GetSafeHwnd())
        return;

    const auto* pItem = m_pControler->GetAddedItem(nAddedIndex);
    if (!pItem)
        return;

    // 查找对应的列表行
    for (int i = 0; i < m_listAddedItems.GetItemCount(); ++i)
    {
        if ((int)m_listAddedItems.GetItemData(i) == nAddedIndex)
        {
            m_listAddedItems.SetItemText(i, COL_SECONDARY_TEXT, rceditor::CStringConverter::ConvertToT(pItem->csSecondaryText));
            m_listAddedItems.SetItemText(i, COL_STATUS, 
                pItem->bModified ? ML(UI_VIEWER_STATUS_TRANSLATED(gbCurNeedShowLan)) : ML(UI_VIEWER_STATUS_PENDING(gbCurNeedShowLan)));
            break;
        }
    }
}

void CRcCompareViewer::UpdateProgressDisplay()
{
    if (!m_pControler || !m_staticProgress.GetSafeHwnd())
        return;

    CString strProgress = GetProgressText();
    m_staticProgress.SetWindowText(strProgress);
}

CString CRcCompareViewer::GetProgressText() const
{
    if (!m_pControler)
    {
        CString strProgress;
        strProgress.Format(ML(UI_VIEWER_PROGRESS_FMT(gbCurNeedShowLan)), 0, 0);
        return strProgress;
    }

    int nTotal = m_pControler->GetAddedItemCount();
    int nModified = nTotal - m_pControler->GetUnmodifiedCount();

    CString strProgress;
    strProgress.Format(ML(UI_VIEWER_PROGRESS_FMT(gbCurNeedShowLan)), nModified, nTotal);
    return strProgress;
}

bool CRcCompareViewer::CanClose() const
{
    if (!m_pControler)
        return true;

    return m_pControler->IsAllModified();
}

bool CRcCompareViewer::NavigateToAddedItem(int nIndex)
{
    if (!m_pControler)
        return false;

    const auto* pItem = m_pControler->GetAddedItem(nIndex);
    if (!pItem)
        return false;

    m_nCurrentAddedIndex = nIndex;

    // 记录当前对话框ID和文件路径（用于主语言布局切换）
    m_nLastDialogID = pItem->nDialogID;
    m_csLastFilePath = pItem->csFilePath;

    // 如果当前正在显示主语言布局，切换回secondary
    if (m_bShowingPrimaryLayout)
    {
        m_bShowingPrimaryLayout = false;
        m_chkShowPrimary.SetCheck(BST_UNCHECKED);
        if (m_surface)
            m_surface->SetReadOnly(false);
    }

    // 仅对 Dialog 类型项启用"显示主语言布局"复选框
    bool bIsDialogItem = (pItem->eType == EAddedItemType::Control ||
                          pItem->eType == EAddedItemType::DialogCaption);
    if (m_chkShowPrimary.GetSafeHwnd())
        m_chkShowPrimary.EnableWindow(bIsDialogItem ? TRUE : FALSE);

    // 切换到对应文件
    if (!pItem->csFilePath.IsEmpty())
    {
        // 检查文件是否已加载
        auto it = m_openedResources.find(pItem->csFilePath);
        if (it != m_openedResources.end())
        {
            SetActiveFile(pItem->csFilePath);
        }
    }

    // 根据类型处理跳转
    bool bNavigated = false;
    
    switch (pItem->eType)
    {
    case EAddedItemType::Control:
        {
            // 查找对话框
            if (m_pRes)
            {
                auto itDlg = m_pRes->mapLangAndDialogInfo.find(pItem->nDialogID);
                if (itDlg != m_pRes->mapLangAndDialogInfo.end() && itDlg->second)
                {
                    // 设置对话框到surface
                    if (m_surface)
                    {
                        m_surface->SetDialogInfo(itDlg->second);
                        m_surface->SetCurrentLevel(m_curLevel);
                        
                        // 查找并选中控件。对于 STATIC(ID=-1) 需优先使用新增索引。
                        int nCtrlIndex = FindCtrlIndexFromAddedIndex(itDlg->second, pItem->nCtrlIndex, pItem->nCtrlID);
                        
                        if (nCtrlIndex >= 0)
                        {
                            // 使用SelectControlByIndex来选中控件
                            m_surface->ClearSelection();
                            m_surface->SelectControlByIndex(nCtrlIndex, false, false);
                            m_surface->EnsureControlVisibleByIndex(nCtrlIndex);
                            m_surface->Invalidate(FALSE);
                            bNavigated = true;
                        }
                    }
                    
                    // 在树中选中对话框
                    HTREEITEM hItem = FindTreeItemForDialog(pItem->nDialogID, pItem->csFilePath);
                    if (hItem)
                    {
                        m_tree.SelectItem(hItem);
                        m_tree.EnsureVisible(hItem);
                    }
                }
            }
        }
        break;

    case EAddedItemType::DialogCaption:
        {
            // 查找并选中对话框
            if (m_pRes)
            {
                auto itDlg = m_pRes->mapLangAndDialogInfo.find(pItem->nDialogID);
                if (itDlg != m_pRes->mapLangAndDialogInfo.end() && itDlg->second)
                {
                    if (m_surface)
                    {
                        m_surface->SetDialogInfo(itDlg->second);
                        m_surface->SetCurrentLevel(m_curLevel);
                        m_surface->ClearSelection();
                        m_surface->Invalidate(FALSE);
                        bNavigated = true;
                    }
                    
                    HTREEITEM hItem = FindTreeItemForDialog(pItem->nDialogID, pItem->csFilePath);
                    if (hItem)
                    {
                        m_tree.SelectItem(hItem);
                        m_tree.EnsureVisible(hItem);
                    }
                }
            }
        }
        break;

    case EAddedItemType::String:
    case EAddedItemType::Menu:
        // 字符串和菜单类型暂不支持在surface中显示
        // 可以在树中尝试定位
        bNavigated = SelectAddedItemInTree(*pItem);
        break;
    }

    // 更新列表选择
    if (m_listAddedItems.GetSafeHwnd())
    {
        m_bUpdatingUI = true;
        // 取消所有选择
        for (int i = 0; i < m_listAddedItems.GetItemCount(); ++i)
        {
            m_listAddedItems.SetItemState(i, 0, LVIS_SELECTED | LVIS_FOCUSED);
        }
        // 选中目标项
        for (int i = 0; i < m_listAddedItems.GetItemCount(); ++i)
        {
            if ((int)m_listAddedItems.GetItemData(i) == nIndex)
            {
                m_listAddedItems.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                m_listAddedItems.EnsureVisible(i, FALSE);
                break;
            }
        }
        m_bUpdatingUI = false;
    }

    // 更新属性列表
    UpdatePropertyGrid();

    return bNavigated;
}

bool CRcCompareViewer::NavigateToNextUnmodified()
{
    if (!m_pControler)
        return false;

    int nNext = m_pControler->GetNextUnmodifiedIndex(m_nCurrentAddedIndex);
    if (nNext < 0)
    {
        // 从头开始
        nNext = m_pControler->GetNextUnmodifiedIndex(-1);
    }

    if (nNext >= 0)
    {
        return NavigateToAddedItem(nNext);
    }

    // 所有项都已翻译
    AfxMessageBox(ML(UI_VIEWER_ALL_TRANSLATED(gbCurNeedShowLan)), MB_OK | MB_ICONINFORMATION);
    return false;
}

bool CRcCompareViewer::SelectAddedItemInTree(const SAddedItemInfo& item)
{
    if (!m_tree.GetSafeHwnd())
        return false;

    HTREEITEM hItem = nullptr;

    switch (item.eType)
    {
    case EAddedItemType::Control:
        hItem = FindTreeItemForControl(item.nDialogID, item.nCtrlID, item.csFilePath);
        break;

    case EAddedItemType::String:
    case EAddedItemType::Menu:
    case EAddedItemType::DialogCaption:
        hItem = FindTreeItemForDialog(item.nDialogID, item.csFilePath);
        break;
    }

    if (hItem)
    {
        m_tree.SelectItem(hItem);
        m_tree.EnsureVisible(hItem);
        return true;
    }

    return false;
}

void CRcCompareViewer::HighlightAddedItemInSurface(const SAddedItemInfo& item)
{
    if (!m_surface)
        return;

    if (item.eType == EAddedItemType::Control)
    {
        // 尝试选中控件
        // 这里需要根据实际的CDesignSurfaceWnd接口来实现
        // m_surface->SelectControlById(item.nCtrlID);
    }
}

int CRcCompareViewer::CheckCurrentSelectionIsAddedItem()
{
    // 检查当前在设计面板中选中的项是否是新增项
    // 需要根据实际实现
    return -1;
}

void CRcCompareViewer::OnTextPropertyChanged(const CStringW& csNewText)
{
    if (m_nCurrentAddedIndex < 0 || !m_pControler)
        return;

    // 更新控制器中的文本
    m_pControler->UpdateItemText(m_nCurrentAddedIndex, csNewText);

    // 同步更新到资源模型
    const auto* pItem = m_pControler->GetAddedItem(m_nCurrentAddedIndex);
    if (pItem && m_pRes)
    {
        switch (pItem->eType)
        {
        case EAddedItemType::Control:
            {
                auto itDlg = m_pRes->mapLangAndDialogInfo.find(pItem->nDialogID);
                if (itDlg != m_pRes->mapLangAndDialogInfo.end() && itDlg->second)
                {
                    bool updated = false;
                    // 优先使用新增控件索引，避免 STATIC(ID=-1) 多个时误更新。
                    if (pItem->nCtrlIndex >= 0 && pItem->nCtrlIndex < (int)itDlg->second->vecAddedCtrlInfo.size())
                    {
                        auto& addedCtrl = itDlg->second->vecAddedCtrlInfo[pItem->nCtrlIndex];
                        if (addedCtrl)
                        {
                            addedCtrl->csText = csNewText;
                            updated = true;
                        }
                    }

                    if (!updated)
                    {
                        for (auto& ctrl : itDlg->second->vecCtrlInfo)
                        {
                            if (ctrl && ctrl->nIDC == pItem->nCtrlID)
                            {
                                ctrl->csText = csNewText;
                                break;
                            }
                        }
                    }
                }
            }
            break;

        case EAddedItemType::DialogCaption:
            {
                auto itDlg = m_pRes->mapLangAndDialogInfo.find(pItem->nDialogID);
                if (itDlg != m_pRes->mapLangAndDialogInfo.end() && itDlg->second)
                {
                    itDlg->second->csCaption = csNewText;
                }
            }
            break;

        case EAddedItemType::String:
        case EAddedItemType::Menu:
            // TODO: 实现字符串和菜单的更新
            break;
        }
    }

    // 刷新列表显示
    UpdateListItemDisplay(m_nCurrentAddedIndex);

    // 刷新surface显示
    if (m_surface)
    {
        m_surface->Invalidate(FALSE);
    }

    // 更新进度
    UpdateProgressDisplay();
}

void CRcCompareViewer::BeginEditSecondaryText(int nListIndex)
{
    if (!m_listAddedItems.GetSafeHwnd() || nListIndex < 0)
        return;

    int nAddedIndex = (int)m_listAddedItems.GetItemData(nListIndex);
    if (!m_pControler)
        return;

    const auto* pItem = m_pControler->GetAddedItem(nAddedIndex);
    if (!pItem)
        return;

    // 获取当前的次语言文本
    CString strCurrentText = rceditor::CStringConverter::ConvertToT(pItem->csSecondaryText);
    CString strPrimaryText = rceditor::CStringConverter::ConvertToT(pItem->csPrimaryText);

    // 创建编辑对话框
    CString strTitle;
    strTitle.Format(_T("%s - %s"), 
        ML(UI_VIEWER_COL_SECONDARY(gbCurNeedShowLan)),
        ML(UI_VIEWER_EDIT_TITLE(gbCurNeedShowLan)));

    // 使用简单的输入对话框（基于MessageBox风格的模态对话框）
    // 创建一个临时的模态对话框来编辑文本
    CString strNewText = ShowTextEditDialog(strPrimaryText, strCurrentText, strTitle);
    
    // 如果用户确认了编辑（返回非空或与原文不同）
    if (strNewText != strCurrentText)
    {
        CStringW csNewText = rceditor::CStringConverter::ToWide(CStringA(CT2A(strNewText)));
        m_nCurrentAddedIndex = nAddedIndex;
        OnTextPropertyChanged(csNewText);
    }
}

CString CRcCompareViewer::ShowTextEditDialog(const CString& strPrimaryText, 
                                              const CString& strCurrentText,
                                              const CString& strTitle)
{
    CTextEditDlgInternal dlg(strPrimaryText, strCurrentText, this);
    if (dlg.DoModal() == IDOK)
    {
        return dlg.m_strResult;
    }
    
    return strCurrentText; // 取消时返回原文本
}

#ifdef USE_PROPERTY_LIST
void CRcCompareViewer::UpdatePropertyList()
{
    CFormRcEditor::UpdatePropertyList();

    // 可以在这里添加新增项特有的属性显示
}

void CRcCompareViewer::OnPropertyListChanged(int nIndex, const CPropertyListCtrl::SPropertyItem& item)
{
    CFormRcEditor::OnPropertyListChanged(nIndex, item);

    // 检查是否是文本属性变更
    if (item.strName == _T("文本") || item.strName == _T("Text"))
    {
        OnTextPropertyChanged(rceditor::CStringConverter::ToWide(CStringA(CT2A(item.strValue))));
    }
}
#endif

#ifdef USE_PROPERTY_GRID
void CRcCompareViewer::UpdatePropertyGridCtrl()
{
    CFormRcEditor::UpdatePropertyGridCtrl();
}

void CRcCompareViewer::OnPropertyGridChanged(int nIndex, const CPropertyGridWrapper::SPropertyItem& item)
{
    CFormRcEditor::OnPropertyGridChanged(nIndex, item);

    // 检查是否是文本属性变更
    if (item.strName == _T("文本") || item.strName == _T("Text"))
    {
        OnTextPropertyChanged(rceditor::CStringConverter::ToWide(CStringA(CT2A(item.strValue))));
    }
}
#endif

void CRcCompareViewer::RebuildSearchTree()
{
    // 可以重写以只显示新增项
    CFormRcEditor::RebuildSearchTree();
}

void CRcCompareViewer::OnClose()
{
    if (!CanClose())
    {
        int nRet = AfxMessageBox(
            ML(UI_VIEWER_CLOSE_CONFIRM(gbCurNeedShowLan)),
            MB_YESNO | MB_ICONWARNING);

        if (nRet != IDYES)
            return;
    }

    CFormRcEditor::OnCancel();
}

void CRcCompareViewer::OnBtnNextUnmodified()
{
    NavigateToNextUnmodified();
}

void CRcCompareViewer::OnBtnPrevUnmodified()
{
    if (!m_pControler)
        return;

    // 向前查找未修改项
    const auto& vecItems = m_pControler->GetAddedItems();
    int nStart = (m_nCurrentAddedIndex <= 0) ? (int)vecItems.size() - 1 : m_nCurrentAddedIndex - 1;

    for (int i = nStart; i >= 0; --i)
    {
        if (!vecItems[i].bModified)
        {
            NavigateToAddedItem(i);
            return;
        }
    }

    // 从尾部继续
    for (int i = (int)vecItems.size() - 1; i > m_nCurrentAddedIndex; --i)
    {
        if (!vecItems[i].bModified)
        {
            NavigateToAddedItem(i);
            return;
        }
    }

    AfxMessageBox(ML(UI_VIEWER_ALL_TRANSLATED(gbCurNeedShowLan)), MB_OK | MB_ICONINFORMATION);
}

void CRcCompareViewer::OnBtnExportRc()
{
    if (!m_pControler)
        return;

    // ------------------------------------------------------------------
    // 优先检查当前任务是否已加载 secondary RC / resource.h（全量写回）
    // ------------------------------------------------------------------
    const int taskIdx = m_pControler->GetCurrentTaskIndex();
    const auto* pTask = m_pControler->GetTaskData(taskIdx);
    const bool bHasRcH = pTask && pTask->bLoaded
        && !pTask->secondaryRcContent.lines.empty()
        && !pTask->secondaryHeader.rawLines.empty();

    if (bHasRcH)
    {
        // ---- 全量导出路径：secondaryRcContent + secondaryHeader ----
        // 1. 选择 .rc 保存路径
        CFileDialog rcDlg(FALSE, _T("rc"), _T("exported.rc"),
            OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
            ML(UI_VIEWER_RC_FILE_FILTER(gbCurNeedShowLan)),
            this);
        if (rcDlg.DoModal() != IDOK)
            return;
        CString strRcPath = rcDlg.GetPathName();

        // 2. 推导 resource.h 默认路径（与 .rc 同目录）
        CString strDefaultH = strRcPath;
        int dotPos = strDefaultH.ReverseFind(_T('.'));
        if (dotPos > 0)
            strDefaultH = strDefaultH.Left(dotPos);
        strDefaultH += _T("_resource.h");

        // 用对话框确认 resource.h 保存位置
        CFileDialog hDlg(FALSE, _T("h"), strDefaultH,
            OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
            ML(UI_VIEWER_H_FILE_FILTER(gbCurNeedShowLan)),
            this);
        if (hDlg.DoModal() != IDOK)
            return;
        CString strHPath = hDlg.GetPathName();

        // 3. 写出
        rceditor::CRcFileWriter writer;
        bool bRcOK = writer.WriteRcFile(pTask->secondaryRcContent, CStringW(strRcPath));
        bool bHOK  = writer.WriteResourceHeader(pTask->secondaryHeader, CStringW(strHPath));

        if (bRcOK && bHOK)
            AfxMessageBox(ML(UI_VIEWER_EXPORT_RC_H_SUCCESS(gbCurNeedShowLan)), MB_OK | MB_ICONINFORMATION);
        else
            AfxMessageBox(ML(UI_VIEWER_EXPORT_FAILED(gbCurNeedShowLan)), MB_OK | MB_ICONERROR);
        return;
    }

    // ------------------------------------------------------------------
    // 未导入 rc/h —— 弹出警告，只能导出资源重建版
    // ------------------------------------------------------------------
    if (AfxMessageBox(ML(UI_VIEWER_EXPORT_RC_WARN(gbCurNeedShowLan)),
                      MB_YESNO | MB_ICONWARNING) != IDYES)
    {
        return;
    }

    CStringW csRcText = m_pControler->ExportFullRcText();

    CFileDialog dlg(FALSE, _T("rc"), _T("added_resources.rc"),
        OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
        ML(UI_VIEWER_RC_FILE_FILTER(gbCurNeedShowLan)),
        this);

    if (dlg.DoModal() == IDOK)
    {
        CString strPath = dlg.GetPathName();

        // 保存为UTF-8带BOM
        CFile file;
        if (file.Open(strPath, CFile::modeCreate | CFile::modeWrite))
        {
            // UTF-8 BOM
            BYTE bom[] = { 0xEF, 0xBB, 0xBF };
            file.Write(bom, 3);

            // 转换为UTF-8并写入
            std::string utf8Text = rceditor::CStringConverter::ToUtf8(csRcText);
            file.Write(utf8Text.c_str(), (UINT)utf8Text.length());
            file.Close();

            AfxMessageBox(ML(UI_VIEWER_EXPORT_SUCCESS(gbCurNeedShowLan)), MB_OK | MB_ICONINFORMATION);
        }
        else
        {
            AfxMessageBox(ML(UI_VIEWER_EXPORT_FAILED(gbCurNeedShowLan)), MB_OK | MB_ICONERROR);
        }
    }
}

void CRcCompareViewer::OnBtnSave()
{
    if (!m_pControler)
        return;

    if (m_pControler->SaveSecondaryResource())
    {
        AfxMessageBox(ML(UI_VIEWER_SAVE_SUCCESS(gbCurNeedShowLan)), MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        AfxMessageBox(ML(UI_VIEWER_SAVE_FAILED(gbCurNeedShowLan)), MB_OK | MB_ICONERROR);
    }
}

void CRcCompareViewer::OnListAddedItemDblClick(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMITEMACTIVATE pNMIA = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
    *pResult = 0;

    if (pNMIA->iItem >= 0)
    {
        int nAddedIndex = (int)m_listAddedItems.GetItemData(pNMIA->iItem);
        
        // 检查是否双击了次语言文本列
        if (pNMIA->iSubItem == COL_SECONDARY_TEXT)
        {
            // 仅在当前选择不是目标项时才导航，避免重复遍历和额外控件查找
            if (m_nCurrentAddedIndex != nAddedIndex)
            {
                NavigateToAddedItem(nAddedIndex);
            }
            
            // 开始编辑次语言文本
            BeginEditSecondaryText(pNMIA->iItem);
        }
        else
        {
            // 双击其他列，只导航不编辑
            NavigateToAddedItem(nAddedIndex);
        }
    }
}

void CRcCompareViewer::OnListAddedItemSelChange(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
    *pResult = 0;

    if (m_bUpdatingUI)
        return;

    if ((pNMLV->uChanged & LVIF_STATE) && (pNMLV->uNewState & LVIS_SELECTED))
    {
        int nIndex = (int)m_listAddedItems.GetItemData(pNMLV->iItem);
        m_nCurrentAddedIndex = nIndex;
        // 选择变化时自动跳转到对应项
        NavigateToAddedItem(nIndex);
    }
}

void CRcCompareViewer::OnListAddedItemEndLabelEdit(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVDISPINFO* pDispInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
    *pResult = FALSE;

    if (pDispInfo->item.pszText != nullptr)
    {
        // 获取新文本
        CString strNewText = pDispInfo->item.pszText;
        int nListIndex = pDispInfo->item.iItem;
        int nAddedIndex = (int)m_listAddedItems.GetItemData(nListIndex);

        // 更新文本
        CStringW csNewText = rceditor::CStringConverter::ToWide(CStringA(CT2A(strNewText)));
        OnTextPropertyChanged(csNewText);

        *pResult = TRUE;  // 接受编辑
    }
}

void CRcCompareViewer::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == TIMER_UPDATE_PROGRESS)
    {
        UpdateProgressDisplay();
    }

    CFormRcEditor::OnTimer(nIDEvent);
}

void CRcCompareViewer::OnControlPositionChanged(UINT nDialogID, UINT nCtrlID)
{
    if (!m_pControler || m_currentIniPath.IsEmpty())
        return;

    // 使用控件ID标记新增项为已修改
    if (m_pControler->MarkControlModifiedByID(nDialogID, nCtrlID, m_currentIniPath))
    {
        // 刷新列表显示（内部已预计算所有验证结果到 m_mapValidationCache）
        RefreshAddedItemsList();
        
        // 更新进度
        UpdateProgressDisplay();
    }

    // 使用缓存的验证结果显示边界警告（避免重复调用 ValidateControlPosition）
    if (m_pControler)
    {
        const auto& vecItems = m_pControler->GetAddedItems();
        for (int i = 0; i < (int)vecItems.size(); ++i)
        {
            const auto& item = vecItems[i];
            if (item.eType == EAddedItemType::Control && 
                item.nDialogID == nDialogID && item.nCtrlID == nCtrlID)
            {
                auto cacheIt = m_mapValidationCache.find(i);
                if (cacheIt != m_mapValidationCache.end() && !cacheIt->second.IsValid())
                {
                    CString warningMsg;
                    warningMsg.Format(_T("警告：%s"), (LPCTSTR)CString(cacheIt->second.description));
                    m_staticProgress.SetWindowText(warningMsg);
                }
                break;
            }
        }
    }
}

LRESULT CRcCompareViewer::OnDesignSurfaceNotify(WPARAM wParam, LPARAM lParam)
{
    // 先调用父类处理
    CFormRcEditor::OnDesignSurfaceNotify(wParam, lParam);

    // 检查是否是控件属性变更通知（包括位置变化）
    // wParam 是通知类型，对应 ESurfaceNotify 枚举
    using ESurfaceNotify = rceditor::CRcDesignSurfaceWnd::ESurfaceNotify;
    ESurfaceNotify notifyType = static_cast<ESurfaceNotify>(wParam);

    if (notifyType == ESurfaceNotify::CtrlPropertiesChanged)
    {
        // 控件属性已变更（包括位置调整）
        // 检查当前选中的控件，标记为已修改
        if (m_surface && m_pControler)
        {
            auto dlg = m_surface->GetDialogInfo();
            auto selCtrl = m_surface->GetSelectedCtrl();

            auto markOneCtrl = [&](const std::shared_ptr<rceditor::SCtrlInfo>& ctrl)
            {
                if (!dlg || !ctrl)
                    return;

                // 优先按 vecAddedCtrlInfo 的索引标记，解决 STATIC(ID=-1) 重复问题。
                int addedIdx = FindAddedIndexByCtrlPtr(dlg, ctrl);
                bool marked = false;
                if (addedIdx >= 0)
                {
                    marked = m_pControler->MarkControlModifiedByIndex(dlg->nIDD, addedIdx, m_currentIniPath);
                    // 将位置/风格同步到编辑器资源，确保导出时能反映设计面板的修改
                    m_pControler->UpdateItemProperties(dlg->nIDD, addedIdx, m_currentIniPath,
                                                       ctrl->ctrlRect, ctrl->dwStyle, ctrl->dwExStyle, ctrl->blMultiLineEditCtrl);
                }
                if (!marked)
                    marked = m_pControler->MarkControlModifiedByID(dlg->nIDD, ctrl->nIDC, m_currentIniPath);
                UNREFERENCED_PARAMETER(marked);
            };
            
            if (dlg && selCtrl)
            {
                markOneCtrl(selCtrl);
            }
            else
            {
                // 多选情况：遍历所有选中的控件
                auto sel = m_surface->GetMultiSelectedIndices();
                if (dlg && !sel.empty())
                {
                    for (int idx : sel)
                    {
                        if (idx >= 0 && idx < (int)dlg->vecCtrlInfo.size())
                        {
                            auto ctrl = dlg->vecCtrlInfo[idx];
                            if (ctrl)
                            {
                                markOneCtrl(ctrl);
                            }
                        }
                    }
                }
            }

            RefreshAddedItemsList();
            UpdateProgressDisplay();

            // 使用缓存的验证结果显示边界警告（避免重复调用 ValidateControlPosition）
            if (dlg && selCtrl && m_pControler)
            {
                const auto& vecItems = m_pControler->GetAddedItems();
                for (int vi = 0; vi < (int)vecItems.size(); ++vi)
                {
                    const auto& item = vecItems[vi];
                    if (item.eType == EAddedItemType::Control && 
                        item.nDialogID == dlg->nIDD && item.nCtrlID == selCtrl->nIDC)
                    {
                        auto cacheIt = m_mapValidationCache.find(vi);
                        if (cacheIt != m_mapValidationCache.end() && !cacheIt->second.IsValid())
                        {
                            CString warningMsg;
                            warningMsg.Format(_T("警告：%s"), (LPCTSTR)CString(cacheIt->second.description));
                            m_staticProgress.SetWindowText(warningMsg);
                        }
                        break;
                    }
                }
            }
        }
    }

    // 对话框尺寸变更通知 — 同步到编辑器资源
    if (notifyType == ESurfaceNotify::DialogSizeChanged)
    {
        if (m_surface && m_pControler)
        {
            auto dlg = m_surface->GetDialogInfo();
            if (dlg)
            {
                m_pControler->UpdateDialogRect(dlg->nIDD, m_currentIniPath, dlg->rectDlg);
            }
        }
    }

    // 控件被删除通知 — 同步刷新新增项列表与进度
    if (notifyType == ESurfaceNotify::ControlsDeleted)
    {
        if (m_pControler)
        {
            // 重新收集新增项（vecAddedCtrlInfo 已在 DeleteSelectedControls 中同步更新）
            m_pControler->RebuildAddedItemsFromModel();
        }
        RefreshAddedItemsList();
        UpdateProgressDisplay();
    }

    return 0;
}

void CRcCompareViewer::RefreshTaskCombo()
{
    if (!m_comboTask.GetSafeHwnd() || !m_pControler)
        return;

    m_comboTask.ResetContent();

    // 如果有项目配置，显示各任务
    const auto& project = m_pControler->GetProject();
    if (!project.tasks.empty())
    {
        for (int i = 0; i < (int)project.tasks.size(); ++i)
        {
            CString strText;
            CString strDllName = project.tasks[i].primaryDllPath;
            int nSlash = strDllName.ReverseFind(_T('\\'));
            if (nSlash >= 0) strDllName = strDllName.Mid(nSlash + 1);
            strText.Format(_T("任务 %d：%s"), i + 1, (LPCTSTR)strDllName);
            m_comboTask.AddString(strText);
        }

        // 添加 "All Tasks" 选项
        m_comboTask.AddString(_T("-- 全部任务 --"));
        m_comboTask.SetCurSel((int)project.tasks.size()); // select "All Tasks"
    }
    else
    {
        // 无项目配置，单DLL模式
        m_comboTask.AddString(_T("当前任务"));
        m_comboTask.SetCurSel(0);
    }
}

void CRcCompareViewer::SwitchToTask(int nTaskIndex)
{
    if (!m_pControler)
        return;

    m_nCurrentTaskIndex = nTaskIndex;
    m_nCurrentAddedIndex = -1;

    if (nTaskIndex >= 0)
    {
        m_pControler->SetCurrentTaskIndex(nTaskIndex);

        // 加载该任务的次语言资源
        const auto& vecTaskData = m_pControler->GetTaskDataVec();
        if (nTaskIndex < (int)vecTaskData.size() && vecTaskData[nTaskIndex].bLoaded)
        {
            // 设置活跃文件为该任务的次语言 DLL
            const auto& config = vecTaskData[nTaskIndex].config;
            if (!config.secondaryDllPath.IsEmpty())
            {
                auto it = m_openedEditors.find(config.secondaryDllPath);
                if (it != m_openedEditors.end())
                {
                    SetActiveFile(config.secondaryDllPath);
                }
            }
        }
    }

    RefreshAddedItemsList();
    UpdateProgressDisplay();
}

void CRcCompareViewer::ApplyFilter()
{
    RefreshAddedItemsList();
    UpdateProgressDisplay();
}

bool CRcCompareViewer::MatchesFilter(const SAddedItemInfo& item, int itemIndex) const
{
    switch (m_eFilterMode)
    {
    case FILTER_ALL:
        return true;

    case FILTER_PENDING:
        return !item.bModified;

    case FILTER_TRANSLATED:
        return item.bModified;

    case FILTER_CONFLICT:
        {
            if (!m_pControler)
                return false;
            const auto& conflicts = m_pControler->GetConflicts();
            for (const auto& c : conflicts)
            {
                if (item.eType == EAddedItemType::Control && c.dialogId == item.nDialogID)
                    return true;
                if (c.filePath == item.csFilePath && !item.csFilePath.IsEmpty())
                    return true;
            }
            return false;
        }

    case FILTER_NEEDS_ADJUST:
        {
            // 显示需要布局调整的控件（新增且未修改的控件 + 超出边界的控件）
            if (item.eType != EAddedItemType::Control)
                return false;
            if (!item.bModified)
                return true; // 未修改的新增控件需要调整

            // 使用缓存的验证结果检查是否超出边界
            if (itemIndex >= 0)
            {
                auto cacheIt = m_mapValidationCache.find(itemIndex);
                if (cacheIt != m_mapValidationCache.end())
                    return !cacheIt->second.IsValid();
            }
            return false;
        }
    }

    return true;
}

void CRcCompareViewer::OnComboTaskSelChange()
{
    int nSel = m_comboTask.GetCurSel();
    if (nSel == CB_ERR)
        return;

    const auto& project = m_pControler ? m_pControler->GetProject() : rceditor::SCompareProject();

    if (nSel >= (int)project.tasks.size())
    {
        // "All Tasks" selected
        SwitchToTask(-1);
    }
    else
    {
        SwitchToTask(nSel);
    }
}

void CRcCompareViewer::OnComboFilterSelChange()
{
    int nSel = m_comboFilter.GetCurSel();
    if (nSel == CB_ERR)
        return;

    m_eFilterMode = static_cast<EFilterMode>(nSel);
    ApplyFilter();
}

void CRcCompareViewer::OnChkShowPrimary()
{
    if (!m_surface || !m_pControler || m_nLastDialogID == 0)
        return;

    bool bChecked = (m_chkShowPrimary.GetCheck() == BST_CHECKED);

    if (bChecked)
    {
        // 切换到主语言布局（只读参考）
        auto* pTaskData = m_pControler->GetTaskData(m_nCurrentTaskIndex);
        if (!pTaskData)
            return;

        auto itDlg = pTaskData->primaryBinaryRes.mapLangAndDialogInfo.find(m_nLastDialogID);
        if (itDlg == pTaskData->primaryBinaryRes.mapLangAndDialogInfo.end() || !itDlg->second)
        {
            AfxMessageBox(_T("主语言中未找到对应对话框。"), MB_OK | MB_ICONWARNING);
            m_chkShowPrimary.SetCheck(BST_UNCHECKED);
            return;
        }

        m_surface->SetDialogInfo(itDlg->second);
        m_surface->SetReadOnly(true);
        m_surface->Invalidate(FALSE);
        m_bShowingPrimaryLayout = true;
    }
    else
    {
        // 切换回secondary语言布局（可编辑）
        if (m_pRes)
        {
            auto itDlg = m_pRes->mapLangAndDialogInfo.find(m_nLastDialogID);
            if (itDlg != m_pRes->mapLangAndDialogInfo.end() && itDlg->second)
            {
                m_surface->SetDialogInfo(itDlg->second);
            }
        }
        m_surface->SetReadOnly(false);
        m_surface->Invalidate(FALSE);
        m_bShowingPrimaryLayout = false;
    }
}

void CRcCompareViewer::OnBtnAcceptAllPrimary()
{
    if (!m_pControler)
        return;

    int nRet = AfxMessageBox(
        _T("是否对未修改项全部采用主语言文本？\n此操作会将所有待处理项标记为已翻译。"),
        MB_YESNO | MB_ICONQUESTION);

    if (nRet != IDYES)
        return;

    const auto& vecItems = m_pControler->GetAddedItems();
    for (int i = 0; i < (int)vecItems.size(); ++i)
    {
        if (!vecItems[i].bModified)
        {
            // Accept primary text as secondary text and mark as modified
            m_pControler->UpdateItemText(i, vecItems[i].csPrimaryText);
        }
    }

    RefreshAddedItemsList();
    UpdateProgressDisplay();
}

void CRcCompareViewer::OnBtnMarkAllDone()
{
    if (!m_pControler)
        return;

    int nRet = AfxMessageBox(
        _T("是否将所有项标记为完成？\n此操作会将所有剩余项标记为已翻译。"),
        MB_YESNO | MB_ICONQUESTION);

    if (nRet != IDYES)
        return;

    auto& vecItems = const_cast<std::vector<SAddedItemInfo>&>(m_pControler->GetAddedItems());
    for (auto& item : vecItems)
    {
        item.bModified = true;
    }

    RefreshAddedItemsList();
    UpdateProgressDisplay();
}
