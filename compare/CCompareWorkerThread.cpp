// CCompareWorkerThread.cpp --- Background worker thread implementation
//////////////////////////////////////////////////////////////////////////////

#include "CCompareWorkerThread.h"
#include <algorithm>
#include <mutex>

namespace rceditor {

CCompareWorkerThread::CCompareWorkerThread()
	: m_hNotifyWnd(NULL)
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

	// Join previous thread if still joinable
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

bool CCompareWorkerThread::Start(HWND hWnd, const std::vector<SCompareTaskConfig>& tasks,
                                  rceditor::TaskWorkerFunc workerFunc)
{
	SetNotifyWnd(hWnd);
	SetTasks(tasks);
	// Wrap the void-returning TaskWorkerFunc into the class's STaskResult-returning one
	SetWorkerFunc([workerFunc](int taskIndex, const SCompareTaskConfig& task,
	                            const std::atomic<bool>& cancelFlag,
	                            std::function<void(int percent, const std::wstring& status)> progressCb) -> STaskResult
	{
		STaskResult result;
		result.taskIndex = taskIndex;
		result.success = true;
		try
		{
			rceditor::ProgressCallback simpleCb;
			if (progressCb)
				simpleCb = [&progressCb](int percent) { progressCb(percent, std::wstring()); };
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

void CCompareWorkerThread::PostStatus(int taskIndex, const std::wstring& status)
{
	if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd))
	{
		// Heap-allocated string, receiver must delete
		std::wstring* pStr = new std::wstring(status);
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

		// Status: processing task N/M
		std::wstring statusText = L"Processing task ";
		statusText += std::to_wstring(i + 1);
		statusText += L"/";
		statusText += std::to_wstring((int)m_tasks.size());
		statusText += L": ";
		statusText += m_tasks[i].taskName;
		PostStatus(i, statusText);

		// Progress callback via PostMessage
		auto progressCb = [this, i](int percent, const std::wstring& status)
		{
			PostProgress(i, percent);
			if (!status.empty())
				PostStatus(i, status);
		};

		// Execute task
		STaskResult result = m_workerFunc(i, m_tasks[i], m_bCancelled, progressCb);
		result.taskIndex = i;

		if (result.success)
		{
			totalAdded += result.addedItemCount;
		}
		else
		{
			++errorCount;
			if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd))
			{
				std::wstring* pErr = new std::wstring(result.errorMessage);
				::PostMessage(m_hNotifyWnd, WM_COMPARE_ERROR, (WPARAM)i, (LPARAM)pErr);
			}
		}

		m_results.push_back(std::move(result));

		// Notify task done
		if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd))
		{
			::PostMessage(m_hNotifyWnd, WM_COMPARE_TASK_DONE,
			              (WPARAM)i, (LPARAM)result.addedItemCount);
		}
	}

	// Notify all done
	if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd))
	{
		::PostMessage(m_hNotifyWnd, WM_COMPARE_ALL_DONE,
		              (WPARAM)totalAdded, (LPARAM)errorCount);
	}

	m_bRunning.store(false);
}

void CCompareWorkerThread::ThreadProcConcurrent()
{
	int maxThreads = m_nMaxConcurrentThreads;
	if (maxThreads <= 0)
	{
		maxThreads = (int)std::thread::hardware_concurrency();
		if (maxThreads <= 0)
			maxThreads = 4;
		if (maxThreads > 8)
			maxThreads = 8;
	}

	const int taskCount = (int)m_tasks.size();
	m_results.resize(taskCount);

	std::atomic<int> totalAdded(0);
	std::atomic<int> errorCount(0);
	std::mutex resultMutex;

	std::atomic<int> nextTaskIndex(0);
	auto workerLambda = [&]()
	{
		while (!m_bCancelled.load())
		{
			int i = nextTaskIndex.fetch_add(1);
			if (i >= taskCount)
				break;

			std::wstring statusText = L"Processing task ";
			statusText += std::to_wstring(i + 1);
			statusText += L"/";
			statusText += std::to_wstring(taskCount);
			statusText += L": ";
			statusText += m_tasks[i].taskName;
			PostStatus(i, statusText);

			auto progressCb = [this, i](int percent, const std::wstring& status)
			{
				PostProgress(i, percent);
				if (!status.empty())
					PostStatus(i, status);
			};

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
					std::wstring* pErr = new std::wstring(result.errorMessage);
					::PostMessage(m_hNotifyWnd, WM_COMPARE_ERROR, (WPARAM)i, (LPARAM)pErr);
				}
			}

			{
				std::lock_guard<std::mutex> lock(resultMutex);
				m_results[i] = std::move(result);
			}

			if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd))
			{
				::PostMessage(m_hNotifyWnd, WM_COMPARE_TASK_DONE,
				              (WPARAM)i, (LPARAM)result.addedItemCount);
			}
		}
	};

	int actualThreads = (std::min)(maxThreads, taskCount);
	std::vector<std::thread> workers;
	workers.reserve(actualThreads);
	for (int t = 0; t < actualThreads; ++t)
	{
		workers.emplace_back(workerLambda);
	}

	for (auto& w : workers)
	{
		if (w.joinable())
			w.join();
	}

	if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd))
	{
		::PostMessage(m_hNotifyWnd, WM_COMPARE_ALL_DONE,
		              (WPARAM)totalAdded.load(), (LPARAM)errorCount.load());
	}

	m_bRunning.store(false);
}

bool CCompareWorkerThread::CheckTaskFilesAccessible(const SCompareTaskConfig& task,
                                                     std::wstring& outLockedFile)
{
	auto checkFile = [&outLockedFile](const std::wstring& path) -> bool
	{
		if (path.empty())
			return true;

		if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
			return true; // File doesn't exist, not locked

		HANDLE hFile = CreateFileW(
			path.c_str(),
			GENERIC_READ,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL);

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
