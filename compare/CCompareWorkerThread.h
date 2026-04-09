#pragma once

// CCompareWorkerThread.h --- Background worker thread for batch compare tasks
//
// Uses std::thread + PostMessage to drive multi-task comparison asynchronously.

#ifndef _INC_WINDOWS
	#include <windows.h>
#endif
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>
#include <string>
#include "CompareProjectConfig.h"

namespace rceditor {

// Custom message IDs
constexpr UINT WM_COMPARE_PROGRESS  = WM_USER + 500;  // wParam=taskIndex, lParam=percent(0-100)
constexpr UINT WM_COMPARE_TASK_DONE = WM_USER + 501;  // wParam=taskIndex, lParam=addedCount
constexpr UINT WM_COMPARE_ALL_DONE  = WM_USER + 502;  // wParam=totalAdded, lParam=errorCount
constexpr UINT WM_COMPARE_ERROR     = WM_USER + 503;  // wParam=taskIndex, lParam=ptr to wstring (caller must delete)
constexpr UINT WM_COMPARE_STATUS    = WM_USER + 504;  // wParam=taskIndex, lParam=ptr to wstring (caller must delete)

using ProgressCallback = std::function<void(int)>;
using TaskWorkerFunc = std::function<void(
	int taskIndex,
	const SCompareTaskConfig& task,
	const std::atomic<bool>& cancelFlag,
	ProgressCallback progressCb
)>;

struct STaskResult
{
	int taskIndex;
	int addedItemCount;
	bool success;
	std::wstring errorMessage;
	std::vector<std::wstring> warnings;

	STaskResult() : taskIndex(-1), addedItemCount(0), success(false) {}
};

class CCompareWorkerThread
{
public:
	// Worker function signature (called per task from background thread)
	using TaskWorkerFunc = std::function<STaskResult(
		int taskIndex,
		const SCompareTaskConfig& task,
		const std::atomic<bool>& cancelFlag,
		std::function<void(int percent, const std::wstring& status)> progressCallback
	)>;

	CCompareWorkerThread();
	~CCompareWorkerThread();

	// Non-copyable
	CCompareWorkerThread(const CCompareWorkerThread&) = delete;
	CCompareWorkerThread& operator=(const CCompareWorkerThread&) = delete;

	void SetNotifyWnd(HWND hWnd) { m_hNotifyWnd = hWnd; }
	void SetTasks(const std::vector<SCompareTaskConfig>& tasks) { m_tasks = tasks; }
	void SetWorkerFunc(TaskWorkerFunc func) { m_workerFunc = std::move(func); }

	bool Start();
	bool Start(HWND hWnd, const std::vector<SCompareTaskConfig>& tasks, rceditor::TaskWorkerFunc workerFunc);

	void Cancel() { m_bCancelled.store(true); }
	bool IsRunning() const { return m_bRunning.load(); }
	bool IsCancelled() const { return m_bCancelled.load(); }
	void Wait();

	const std::vector<STaskResult>& GetResults() const { return m_results; }

	void SetConcurrentLoading(bool bConcurrent) { m_bConcurrentLoading = bConcurrent; }
	void SetMaxConcurrentThreads(int nMaxThreads) { m_nMaxConcurrentThreads = nMaxThreads; }

	static bool CheckTaskFilesAccessible(const SCompareTaskConfig& task, std::wstring& outLockedFile);

private:
	void ThreadProc();
	void ThreadProcConcurrent();
	void PostStatus(int taskIndex, const std::wstring& status);
	void PostProgress(int taskIndex, int percent);

	HWND m_hNotifyWnd;
	std::vector<SCompareTaskConfig> m_tasks;
	TaskWorkerFunc m_workerFunc;

	std::thread m_thread;
	std::atomic<bool> m_bCancelled;
	std::atomic<bool> m_bRunning;

	std::vector<STaskResult> m_results;

	bool m_bConcurrentLoading;
	int m_nMaxConcurrentThreads;
};

} // namespace rceditor
