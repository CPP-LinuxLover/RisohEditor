#include "stdafx.h"
#include "CFormRcCompare.h"
#include "CRcCompareViewer.h"
#include "CTaskEditDialog.h"
#include <afxcmn.h>

IMPLEMENT_DYNAMIC(CFormRcCompare, CFormRcBase)

CFormRcCompare::CFormRcCompare(CWnd* pParent /*= nullptr*/)
    : CFormRcBase(pParent)
    , m_bInitialized(false)
    , m_bProcessing(false)
{
    // 创建对话框模板
    CRect rcDlg(0, 0, 800, 500);
    DLGTEMPLATE* pTemplate = CreateTemplate(
        WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        rcDlg,
        GetInitialCaption(),
        WS_EX_APPWINDOW);

    InitModalIndirect(pTemplate, pParent);
}

CFormRcCompare::~CFormRcCompare()
{
}

BEGIN_MESSAGE_MAP(CFormRcCompare, CFormRcBase)
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_ERASEBKGND()
    // 项目管理
    ON_BN_CLICKED(IDC_BTN_LOAD_PROJECT, &CFormRcCompare::OnBtnLoadProject)
    ON_BN_CLICKED(IDC_BTN_SAVE_PROJECT, &CFormRcCompare::OnBtnSaveProject)
    ON_BN_CLICKED(IDC_BTN_ADD_TASK, &CFormRcCompare::OnBtnAddTask)
    ON_BN_CLICKED(IDC_BTN_EDIT_TASK, &CFormRcCompare::OnBtnEditTask)
    ON_BN_CLICKED(IDC_BTN_REMOVE_TASK, &CFormRcCompare::OnBtnRemoveTask)
    ON_BN_CLICKED(IDC_BTN_BATCH_COMPARE, &CFormRcCompare::OnBtnBatchCompare)
    ON_BN_CLICKED(IDC_BTN_BATCH_EXPORT, &CFormRcCompare::OnBtnBatchExport)
    // 后台线程消息
    ON_MESSAGE(rceditor::WM_COMPARE_PROGRESS, &CFormRcCompare::OnCompareProgress)
    ON_MESSAGE(rceditor::WM_COMPARE_TASK_DONE, &CFormRcCompare::OnCompareTaskDone)
    ON_MESSAGE(rceditor::WM_COMPARE_ALL_DONE, &CFormRcCompare::OnCompareAllDone)
    ON_MESSAGE(rceditor::WM_COMPARE_ERROR, &CFormRcCompare::OnCompareError)
    ON_MESSAGE(rceditor::WM_COMPARE_STATUS, &CFormRcCompare::OnCompareStatus)
END_MESSAGE_MAP()

BOOL CFormRcCompare::OnInitDialog()
{
    CFormRcBase::OnInitDialog();

    // 创建字体
    m_font.CreatePointFont(90, _T("Microsoft YaHei UI"));

    // 创建项目管理控件
    CreateProjectControls();

    // 布局控件
    LayoutProjectControls();

    // 居中显示
    CenterWindow();

    m_bInitialized = true;

    return TRUE;
}

void CFormRcCompare::OnBeforeDialogClose()
{
    m_controler.ClearFiles();
}


void CFormRcCompare::OnSize(UINT nType, int cx, int cy)
{
    CFormRcBase::OnSize(nType, cx, cy);
    if (m_bInitialized)
    {
        LayoutProjectControls();
        Invalidate();
    }
}

void CFormRcCompare::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
    CFormRcBase::OnGetMinMaxInfo(lpMMI);
    lpMMI->ptMinTrackSize.x = 700;
    lpMMI->ptMinTrackSize.y = 400;
}

BOOL CFormRcCompare::OnEraseBkgnd(CDC* pDC)
{
    CRect rc;
    GetClientRect(&rc);
    pDC->FillSolidRect(&rc, GetSysColor(COLOR_BTNFACE));
    return TRUE;
}

// ============================================================================
// 项目管理控件
// ============================================================================

void CFormRcCompare::CreateProjectControls()
{
    // 项目区域标题
    m_staticProjectGroup.Create(L"比较任务列表", WS_CHILD | WS_VISIBLE | SS_LEFT,
        CRect(0, 0, 200, 20), this, IDC_STATIC_PROJECT_GROUP);
    m_staticProjectGroup.SetFont(&m_font);

    // 任务列表
    m_listTasks.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        CRect(0, 0, 100, 100), this, IDC_LIST_TASKS);
    m_listTasks.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    m_listTasks.InsertColumn(0, L"#", LVCFMT_LEFT, 30);
    m_listTasks.InsertColumn(1, L"主语言文件", LVCFMT_LEFT, 150);
    m_listTasks.InsertColumn(2, L"次语言文件", LVCFMT_LEFT, 150);
    m_listTasks.InsertColumn(3, L"状态", LVCFMT_LEFT, 80);
    m_listTasks.SetFont(&m_font);

    // 按钮
    m_btnLoadProject.Create(L"加载项目", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 100, 28), this, IDC_BTN_LOAD_PROJECT);
    m_btnLoadProject.SetFont(&m_font);

    m_btnSaveProject.Create(L"保存项目", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 100, 28), this, IDC_BTN_SAVE_PROJECT);
    m_btnSaveProject.SetFont(&m_font);

    m_btnAddTask.Create(L"添加任务", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 80, 28), this, IDC_BTN_ADD_TASK);
    m_btnAddTask.SetFont(&m_font);

    m_btnEditTask.Create(L"编辑", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 60, 28), this, IDC_BTN_EDIT_TASK);
    m_btnEditTask.SetFont(&m_font);

    m_btnRemoveTask.Create(L"删除", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 70, 28), this, IDC_BTN_REMOVE_TASK);
    m_btnRemoveTask.SetFont(&m_font);

    m_btnBatchCompare.Create(L"批量比较", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 120, 36), this, IDC_BTN_BATCH_COMPARE);
    m_btnBatchCompare.SetFont(&m_font);

    m_btnBatchExport.Create(L"批量导出", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 100, 36), this, IDC_BTN_BATCH_EXPORT);
    m_btnBatchExport.SetFont(&m_font);

    // 状态栏
    m_staticStatus.Create(L"就绪", WS_CHILD | WS_VISIBLE | SS_LEFT,
        CRect(0, 0, 200, 20), this, IDC_STATIC_STATUS);
    m_staticStatus.SetFont(&m_font);

    // 进度条
    m_progressBar.Create(WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        CRect(0, 0, 200, 18), this, IDC_PROGRESS_BAR);
    m_progressBar.SetRange(0, 100);
    m_progressBar.SetPos(0);

    UpdateBatchButtonState();
}

void CFormRcCompare::LayoutProjectControls()
{
    if (!m_listTasks.GetSafeHwnd())
        return;

    CRect rcClient;
    GetClientRect(&rcClient);

    const int MARGIN = 10;
    const int TITLE_H = 22;
    const int BTN_H = 28;
    const int BTN_BATCH_H = 36;
    const int GAP = 6;
    const int STATUS_H = 20;

    const int width = rcClient.Width() - MARGIN * 2;
    int y = MARGIN;

    // 标题
    m_staticProjectGroup.MoveWindow(MARGIN, y, width, TITLE_H);
    y += TITLE_H + GAP;

    // 工具按钮行
    int bx = MARGIN;
    m_btnLoadProject.MoveWindow(bx, y, 110, BTN_H); bx += 115;
    m_btnSaveProject.MoveWindow(bx, y, 110, BTN_H); bx += 115;
    m_btnAddTask.MoveWindow(bx, y, 90, BTN_H); bx += 95;
    m_btnEditTask.MoveWindow(bx, y, 70, BTN_H); bx += 75;
    m_btnRemoveTask.MoveWindow(bx, y, 80, BTN_H);
    y += BTN_H + GAP;

    // 底部区域（批量按钮 + 状态栏 + 进度）
    const int bottomY = rcClient.Height() - MARGIN;
    const int statusY = bottomY - STATUS_H;
    const int batchY = statusY - GAP - BTN_BATCH_H;

    m_btnBatchCompare.MoveWindow(MARGIN, batchY, 140, BTN_BATCH_H);
    m_btnBatchExport.MoveWindow(MARGIN + 145, batchY, 120, BTN_BATCH_H);

    int statusWidth = width / 2;
    m_staticStatus.MoveWindow(MARGIN, statusY, statusWidth, STATUS_H);
    m_progressBar.MoveWindow(MARGIN + statusWidth + GAP, statusY,
        width - statusWidth - GAP, STATUS_H);

    // 中间任务列表占据剩余空间
    int listTop = y;
    int listBottom = batchY - GAP;
    int listH = listBottom - listTop;
    if (listH < 80)
        listH = 80;
    m_listTasks.MoveWindow(MARGIN, listTop, width, listH);

    // 自适应列宽
    m_listTasks.SetColumnWidth(0, 40);
    m_listTasks.SetColumnWidth(1, (width - 40 - 100) / 2);
    m_listTasks.SetColumnWidth(2, (width - 40 - 100) / 2);
    m_listTasks.SetColumnWidth(3, 100);
}

void CFormRcCompare::RefreshTaskList()
{
    m_listTasks.DeleteAllItems();
    for (int i = 0; i < (int)m_project.tasks.size(); ++i)
    {
        const auto& t = m_project.tasks[i];
        CStringW numStr;
        numStr.Format(L"%d", i + 1);
        int idx = m_listTasks.InsertItem(i, numStr);

        // 只显示文件名
        CStringW priName = t.primaryDllPath;
        int pos = priName.ReverseFind(L'\\');
        if (pos >= 0) priName = priName.Mid(pos + 1);
        m_listTasks.SetItemText(idx, 1, priName);

        CStringW secName = t.secondaryDllPath;
        pos = secName.ReverseFind(L'\\');
        if (pos >= 0) secName = secName.Mid(pos + 1);
        m_listTasks.SetItemText(idx, 2, secName);

        // 状态
        CStringW status = L"就绪";
        if (i < (int)m_controler.GetTaskDataVec().size())
        {
            auto* td = m_controler.GetTaskData(i);
            if (td)
            {
                if (td->bExported) status = L"已导出";
                else if (td->bMerged) status = L"已合并";
                else if (td->bLoaded) status = L"已加载";
                if (!td->lastError.IsEmpty()) status = L"错误";
            }
        }
        m_listTasks.SetItemText(idx, 3, status);
    }
    UpdateBatchButtonState();
}

void CFormRcCompare::UpdateBatchButtonState()
{
    bool hasTasks = !m_project.tasks.empty();
    m_btnBatchCompare.EnableWindow(hasTasks && !m_bProcessing);
    m_btnBatchExport.EnableWindow(hasTasks && !m_bProcessing);
    m_btnAddTask.EnableWindow(!m_bProcessing);
    m_btnEditTask.EnableWindow(!m_bProcessing);
    m_btnRemoveTask.EnableWindow(!m_bProcessing);
    m_btnLoadProject.EnableWindow(!m_bProcessing);
    m_btnSaveProject.EnableWindow(!m_bProcessing);
}

void CFormRcCompare::SetProcessingUI(bool bProcessing)
{
    m_bProcessing = bProcessing;
    UpdateBatchButtonState();
}

// ============================================================================
// 项目管理事件
// ============================================================================

void CFormRcCompare::OnBtnLoadProject()
{
    CFileDialog dlg(TRUE, L"json", NULL,
                    OFN_HIDEREADONLY | OFN_FILEMUSTEXIST,
                    L"项目文件 (*.json)|*.json|所有文件 (*.*)|*.*||", this);
    dlg.m_ofn.lpstrTitle = L"加载比较项目";
    if (dlg.DoModal() != IDOK)
        return;

    rceditor::CompareProjectConfigManager mgr;
    rceditor::SCompareProject proj;
    if (!mgr.LoadProjectConfig(CStringW(dlg.GetPathName()), proj))
    {
        AfxMessageBox(L"加载项目文件失败。", MB_ICONERROR);
        return;
    }

    m_project = proj;
    m_controler.SetProject(m_project);
    RefreshTaskList();
    m_staticStatus.SetWindowText(L"项目已加载。");
}

void CFormRcCompare::OnBtnSaveProject()
{
    CFileDialog dlg(FALSE, L"json", NULL,
                    OFN_OVERWRITEPROMPT,
                    L"项目文件 (*.json)|*.json|所有文件 (*.*)|*.*||", this);
    dlg.m_ofn.lpstrTitle = L"保存比较项目";
    if (dlg.DoModal() != IDOK)
        return;

    rceditor::CompareProjectConfigManager mgr;
    if (!mgr.SaveProjectConfig(CStringW(dlg.GetPathName()), m_project))
    {
        AfxMessageBox(L"保存项目文件失败。", MB_ICONERROR);
        return;
    }
    m_staticStatus.SetWindowText(L"项目已保存。");
}

void CFormRcCompare::OnBtnAddTask()
{
    rceditor::SCompareTaskConfig newTask;
    rceditor::CTaskEditDialog dlg(newTask, this);
    if (dlg.DoModal() == IDOK)
    {
        m_project.tasks.push_back(newTask);
        m_controler.SetProject(m_project);
        RefreshTaskList();
    }
}

void CFormRcCompare::OnBtnEditTask()
{
    int sel = -1;
    POSITION pos = m_listTasks.GetFirstSelectedItemPosition();
    if (pos) sel = m_listTasks.GetNextSelectedItem(pos);
    if (sel < 0 || sel >= (int)m_project.tasks.size())
    {
        AfxMessageBox(L"请先选择要编辑的任务。", MB_ICONINFORMATION);
        return;
    }

    rceditor::CTaskEditDialog dlg(m_project.tasks[sel], this);
    if (dlg.DoModal() == IDOK)
    {
        m_controler.SetProject(m_project);
        RefreshTaskList();
    }
}

void CFormRcCompare::OnBtnRemoveTask()
{
    int sel = -1;
    POSITION pos = m_listTasks.GetFirstSelectedItemPosition();
    if (pos) sel = m_listTasks.GetNextSelectedItem(pos);
    if (sel < 0 || sel >= (int)m_project.tasks.size())
    {
        AfxMessageBox(L"请先选择要删除的任务。", MB_ICONINFORMATION);
        return;
    }

    m_project.tasks.erase(m_project.tasks.begin() + sel);
    m_controler.SetProject(m_project);
    RefreshTaskList();
}

void CFormRcCompare::OnBtnBatchCompare()
{
    if (m_project.tasks.empty())
        return;

    // 预检查：验证所有文件是否可访问（未被其他进程锁定）
    for (int i = 0; i < (int)m_project.tasks.size(); ++i)
    {
        CStringW lockedFile;
        if (!rceditor::CCompareWorkerThread::CheckTaskFilesAccessible(m_project.tasks[i], lockedFile))
        {
            CStringW msg;
            msg.Format(L"任务 %d（%s）：文件被其他进程占用：\n%s\n\n请关闭占用该文件的程序后重试。",
                       i + 1, (LPCWSTR)m_project.tasks[i].taskName, (LPCWSTR)lockedFile);
            AfxMessageBox(msg, MB_ICONERROR);
            return;
        }
    }

    // 重置控制器
    m_controler.ClearFiles();
    m_controler.SetProject(m_project);
    m_controler.GetTaskDataVec().resize(m_project.tasks.size());

    SetProcessingUI(true);
    m_progressBar.SetPos(0);
    m_staticStatus.SetWindowText(L"批量比较中...");

    // 构建任务处理函数
    rceditor::TaskWorkerFunc workerFunc = [this](int taskIndex,
                                                  const rceditor::SCompareTaskConfig& task,
                                                  const std::atomic<bool>& cancelFlag,
                                                  rceditor::ProgressCallback progressCb)
    {
        m_controler.ProcessSingleTask(taskIndex, task, cancelFlag, progressCb);
    };

    // 多任务时启用并发加载
    if (m_project.tasks.size() > 1)
    {
        m_worker.SetConcurrentLoading(true);
    }
    else
    {
        m_worker.SetConcurrentLoading(false);
    }

    m_worker.Start(GetSafeHwnd(), m_project.tasks, workerFunc);
}

void CFormRcCompare::OnBtnBatchExport()
{
    if (m_project.tasks.empty())
        return;

    // 导出前校验所有任务
    CStringW allWarnings;
    int warningTaskCount = 0;
    for (int i = 0; i < (int)m_project.tasks.size(); ++i)
    {
        std::vector<CStringW> taskWarnings;
        m_controler.ValidateBeforeExport(i, taskWarnings);
        if (!taskWarnings.empty())
        {
            ++warningTaskCount;
            CStringW taskHeader;
            taskHeader.Format(L"\n任务 %d（%s）：\n", i + 1, (LPCWSTR)m_project.tasks[i].taskName);
            allWarnings += taskHeader;
            for (const auto& w : taskWarnings)
            {
                allWarnings += L"  - ";
                allWarnings += w;
                allWarnings += L"\n";
            }
        }
    }

    // 如果有警告，让用户确认是否继续
    if (!allWarnings.IsEmpty())
    {
        CStringW confirmMsg;
        confirmMsg.Format(L"在 %d 个任务中发现校验警告：\n%s\n是否继续导出？",
                          warningTaskCount, (LPCWSTR)allWarnings);
        if (AfxMessageBox(confirmMsg, MB_YESNO | MB_ICONWARNING) != IDYES)
            return;
    }

    int exported = 0;
    int failed = 0;
    CStringW firstError;
    for (int i = 0; i < (int)m_project.tasks.size(); ++i)
    {
        if (m_controler.ExportTask(i))
        {
            ++exported;
        }
        else
        {
            ++failed;
            auto* td = m_controler.GetTaskData(i);
            if (td && !td->lastError.IsEmpty() && firstError.IsEmpty())
                firstError = td->lastError;
        }
    }

    CStringW msg;
    if (failed > 0 && !firstError.IsEmpty())
        msg.Format(L"已导出 %d / %d 个任务，失败 %d 个。\n首个错误：%s", exported, (int)m_project.tasks.size(), failed, (LPCWSTR)firstError);
    else
        msg.Format(L"已导出 %d / %d 个任务。", exported, (int)m_project.tasks.size());
    AfxMessageBox(msg, MB_ICONINFORMATION);
    RefreshTaskList();
}

// ============================================================================
// 后台线程消息处理
// ============================================================================

LRESULT CFormRcCompare::OnCompareProgress(WPARAM wParam, LPARAM lParam)
{
    int taskIndex = (int)wParam;
    int progress = (int)lParam;

    // 整体进度 = (taskIndex * 100 + progress) / totalTasks
    int total = (int)m_project.tasks.size();
    if (total > 0)
    {
        int overall = (taskIndex * 100 + progress) / total;
        m_progressBar.SetPos(overall);
    }
    return 0;
}

LRESULT CFormRcCompare::OnCompareTaskDone(WPARAM wParam, LPARAM lParam)
{
    int taskIndex = (int)wParam;

    // 合并完成后的处理
    m_controler.MergeTask(taskIndex);

    CStringW status;
    status.Format(L"任务 %d/%d 已完成。", taskIndex + 1, (int)m_project.tasks.size());
    m_staticStatus.SetWindowText(status);

    RefreshTaskList();
    return 0;
}

LRESULT CFormRcCompare::OnCompareAllDone(WPARAM wParam, LPARAM lParam)
{
    SetProcessingUI(false);
    m_progressBar.SetPos(100);

    int total = m_controler.GetAddedItemCount();
    CStringW msg;
    msg.Format(L"批量比较完成，发现 %d 个新增项。", total);
    m_staticStatus.SetWindowText(msg);

    if (total > 0)
    {
        if (AfxMessageBox(msg + L"\n是否打开查看器？", MB_YESNO | MB_ICONQUESTION) == IDYES)
        {
            CRcCompareViewer viewer(&m_controler, this);
            viewer.DoModal();
        }
    }
    else
    {
        AfxMessageBox(L"未发现差异。", MB_ICONINFORMATION);
    }

    RefreshTaskList();
    return 0;
}

LRESULT CFormRcCompare::OnCompareError(WPARAM wParam, LPARAM lParam)
{
    CStringW* pMsg = reinterpret_cast<CStringW*>(lParam);
    if (pMsg)
    {
        m_staticStatus.SetWindowText(*pMsg);
        delete pMsg;
    }
    return 0;
}

LRESULT CFormRcCompare::OnCompareStatus(WPARAM wParam, LPARAM lParam)
{
    CStringW* pMsg = reinterpret_cast<CStringW*>(lParam);
    if (pMsg)
    {
        m_staticStatus.SetWindowText(*pMsg);
        delete pMsg;
    }
    return 0;
}
