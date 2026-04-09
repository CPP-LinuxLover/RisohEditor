#include "stdafx.h"
#include "CCompareWorkerThread.h"
#include <algorithm>
#include <mutex>

namespace rceditor {

CCompareWorkerThread::CCompareWorkerThread()
    : m_hNotifyWnd(nullptr)
    , m_bCancelled(false)
    , m_bRunning(false)
    , m_bConcurrentLoading(false)
    , m_nMaxConcurrentThreads(0)
{
}

CCompareWorkerThread::~CCompareWorkerThread()
{
    Cancel();
    Wait();
}

bool CCompareWorkerThread::Start()
{
    if (m_bRunning.load())
        return false;

    // 上一次线程即使已执行完，也可能仍处于 joinable 状态。
    // 在重新赋值 m_thread 前必须先回收，否则 std::thread::operator= 会 terminate。
    if (m_thread.joinable())
        m_thread.join();

    if (!m_workerFunc)
        return false;

    if (m_tasks.empty())
        return false;

    m_bCancelled.store(false);
    m_bRunning.store(true);
    m_results.clear();

    if (m_bConcurrentLoading && m_tasks.size() > 1)
        m_thread = std::thread(&CCompareWorkerThread::ThreadProcConcurrent, this);
    else
        m_thread = std::thread(&CCompareWorkerThread::ThreadProc, this);
    return true;
}

bool CCompareWorkerThread::Start(HWND hWnd, const std::vector<SCompareTaskConfig>& tasks, rceditor::TaskWorkerFunc workerFunc)
{
    SetNotifyWnd(hWnd);
    SetTasks(tasks);
    // Wrap the void-returning TaskWorkerFunc into the class's STaskResult-returning TaskWorkerFunc
    SetWorkerFunc([workerFunc](int taskIndex, const SCompareTaskConfig& task,
                               const std::atomic<bool>& cancelFlag,
                               std::function<void(int percent, const CStringW& status)> progressCb) -> STaskResult
    {
        STaskResult result;
        result.taskIndex = taskIndex;
        result.success = true;
        try
        {
            // Adapt 2-arg progress callback to simple 1-arg ProgressCallback
            rceditor::ProgressCallback simpleCb;
            if (progressCb)
                simpleCb = [&progressCb](int percent) { progressCb(percent, CStringW()); };
            workerFunc(taskIndex, task, cancelFlag, simpleCb);
        }
        catch (...)
        {
            result.success = false;
            result.errorMessage = L"Exception in task worker";
        }
        return result;
    });
    return Start();
}

void CCompareWorkerThread::Wait()
{
    if (m_thread.joinable())
        m_thread.join();
}

void CCompareWorkerThread::PostStatus(int taskIndex, const CStringW& status)
{
    if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd))
    {
        // 分配一个堆字符串，接收端负责 delete
        CStringW* pStr = new CStringW(status);
        ::PostMessage(m_hNotifyWnd, WM_COMPARE_STATUS, (WPARAM)taskIndex, (LPARAM)pStr);
    }
}

void CCompareWorkerThread::PostProgress(int taskIndex, int percent)
{
    if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd))
    {
        ::PostMessage(m_hNotifyWnd, WM_COMPARE_PROGRESS, (WPARAM)taskIndex, (LPARAM)percent);
    }
}

void CCompareWorkerThread::ThreadProc()
{
    int totalAdded = 0;
    int errorCount = 0;

    for (int i = 0; i < (int)m_tasks.size(); ++i)
    {
        if (m_bCancelled.load())
            break;

        // 发送状态：正在处理第 N 个任务
        CStringW statusText;
        statusText.Format(L"Processing task %d/%d: %s",
                          i + 1, (int)m_tasks.size(),
                          (LPCWSTR)m_tasks[i].taskName);
        PostStatus(i, statusText);

        // 进度回调：线程安全，通过 PostMessage 转发
        auto progressCb = [this, i](int percent, const CStringW& status)
        {
            PostProgress(i, percent);
            if (!status.IsEmpty())
                PostStatus(i, status);
        };

        // 执行任务
        STaskResult result = m_workerFunc(i, m_tasks[i], m_bCancelled, progressCb);
        result.taskIndex = i;

        if (result.success)
        {
            totalAdded += result.addedItemCount;
        }
        else
        {
            ++errorCount;
            // 发送错误消息
            if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd))
            {
                CStringW* pErr = new CStringW(result.errorMessage);
                ::PostMessage(m_hNotifyWnd, WM_COMPARE_ERROR, (WPARAM)i, (LPARAM)pErr);
            }
        }

        m_results.push_back(std::move(result));

        // 发送单个任务完成消息
        if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd))
        {
            ::PostMessage(m_hNotifyWnd, WM_COMPARE_TASK_DONE,
                          (WPARAM)i, (LPARAM)result.addedItemCount);
        }
    }

    // 发送全部完成消息
    if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd))
    {
        ::PostMessage(m_hNotifyWnd, WM_COMPARE_ALL_DONE,
                      (WPARAM)totalAdded, (LPARAM)errorCount);
    }

    m_bRunning.store(false);
}

void CCompareWorkerThread::ThreadProcConcurrent()
{
    // 并发加载模式：多个任务的加载/合并阶段并行执行
    int maxThreads = m_nMaxConcurrentThreads;
    if (maxThreads <= 0)
    {
        maxThreads = (int)std::thread::hardware_concurrency();
        if (maxThreads <= 0)
            maxThreads = 4;
        // 限制合理范围，避免资源耗尽
        if (maxThreads > 8)
            maxThreads = 8;
    }

    const int taskCount = (int)m_tasks.size();
    m_results.resize(taskCount);

    std::atomic<int> totalAdded(0);
    std::atomic<int> errorCount(0);
    std::mutex resultMutex;

    // 简单的任务分派：使用线程池模式
    std::atomic<int> nextTaskIndex(0);
    auto workerLambda = [&]()
    {
        while (!m_bCancelled.load())
        {
            int i = nextTaskIndex.fetch_add(1);
            if (i >= taskCount)
                break;

            // 发送状态
            PostStatus(i, CStringW(L"Processing task ") +
                       std::to_wstring(i + 1).c_str() + L"/" +
                       std::to_wstring(taskCount).c_str() + L": " +
                       m_tasks[i].taskName);

            // 进度回调
            auto progressCb = [this, i](int percent, const CStringW& status)
            {
                PostProgress(i, percent);
                if (!status.IsEmpty())
                    PostStatus(i, status);
            };

            // 执行任务
            STaskResult result = m_workerFunc(i, m_tasks[i], m_bCancelled, progressCb);
            result.taskIndex = i;

            if (result.success)
            {
                totalAdded.fetch_add(result.addedItemCount);
            }
            else
            {
                errorCount.fetch_add(1);
                if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd))
                {
                    CStringW* pErr = new CStringW(result.errorMessage);
                    ::PostMessage(m_hNotifyWnd, WM_COMPARE_ERROR, (WPARAM)i, (LPARAM)pErr);
                }
            }

            {
                std::lock_guard<std::mutex> lock(resultMutex);
                m_results[i] = std::move(result);
            }

            // 发送单个任务完成消息
            if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd))
            {
                ::PostMessage(m_hNotifyWnd, WM_COMPARE_TASK_DONE,
                              (WPARAM)i, (LPARAM)result.addedItemCount);
            }
        }
    };

    // 启动工作线程
    int actualThreads = (std::min)(maxThreads, taskCount);
    std::vector<std::thread> workers;
    workers.reserve(actualThreads);
    for (int t = 0; t < actualThreads; ++t)
    {
        workers.emplace_back(workerLambda);
    }

    // 等待所有线程完成
    for (auto& w : workers)
    {
        if (w.joinable())
            w.join();
    }

    // 发送全部完成消息
    if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd))
    {
        ::PostMessage(m_hNotifyWnd, WM_COMPARE_ALL_DONE,
                      (WPARAM)totalAdded.load(), (LPARAM)errorCount.load());
    }

    m_bRunning.store(false);
}

bool CCompareWorkerThread::CheckTaskFilesAccessible(const SCompareTaskConfig& task, CStringW& outLockedFile)
{
    auto checkFile = [&outLockedFile](const CStringW& path) -> bool
    {
        if (path.IsEmpty())
            return true;

        if (GetFileAttributesW(path.GetString()) == INVALID_FILE_ATTRIBUTES)
            return true; // 文件不存在不算锁定

        HANDLE hFile = CreateFileW(
            path.GetString(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (hFile == INVALID_HANDLE_VALUE)
        {
            outLockedFile = path;
            return false;
        }

        CloseHandle(hFile);
        return true;
    };

    if (!checkFile(task.primaryDllPath)) return false;
    if (!checkFile(task.secondaryDllPath)) return false;
    if (!checkFile(task.primaryRcPath)) return false;
    if (!checkFile(task.secondaryRcPath)) return false;
    if (!checkFile(task.primaryResourceHPath)) return false;
    if (!checkFile(task.secondaryResourceHPath)) return false;

    return true;
}

} // namespace rceditor
