/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// macOS implementation of ThreadStart and related thread-control utilities.
// SIGUSR1-based thread suspend/resume (used on Linux) is not available via
// public macOS APIs, so Suspend/Resume return THREADERR_MISC and the
// watchdog falls back to the no-op path.
//
// Linux/ThreadSupport.h is included here because Threading.h includes it on
// all non-Windows platforms and it declares the shared ThreadStart /
// SetupCurrentThreadControls signatures needed by the threading layer.

#include <functional>
#include <memory>
#include <pthread.h>

#include "System/Log/ILog.h"
#include "System/Platform/Threading.h"
#include "Linux/ThreadSupport.h"

namespace Threading
{

	static void SetupCurrentThreadControlsImpl(std::shared_ptr<ThreadControls> &threadCtls)
	{
		if (threadCtls.get() != nullptr)
		{
			LOG_L(L_WARNING, "[%s] thread already has ThreadControls installed", __func__);
			return;
		}

		// Prefer make_shared to avoid a separate allocation for the control block.
		threadCtls = std::make_shared<Threading::ThreadControls>();
		threadCtls->handle = GetCurrentThread();
		threadCtls->running.store(true);
	}

	void SetupCurrentThreadControls(std::shared_ptr<ThreadControls> &threadCtls)
	{
		SetupCurrentThreadControlsImpl(threadCtls);
	}

	void ThreadStart(
		std::function<void()> taskFunc,
		std::shared_ptr<ThreadControls> *ppCtlsReturn,
		ThreadControls *tempCtls)
	{
		SetupCurrentThreadControlsImpl(localThreadControls);

		if (ppCtlsReturn != nullptr)
			*ppCtlsReturn = localThreadControls;

		{
			// Notify the creating thread that this thread is initialised and
			// ready.  tempCtls->mutSuspend is already held by the creator.
			tempCtls->mutSuspend.lock();
			// Log the handle as a pointer; avoid integer-format warnings since
			// pthread_t is an opaque pointer on macOS.
			LOG_L(L_DEBUG, "[%s] new thread handle %p", __func__,
				  static_cast<void *>(localThreadControls->handle));
			tempCtls->condInitialized.notify_all();
			tempCtls->mutSuspend.unlock();
		}

		taskFunc();

		localThreadControls->mutSuspend.lock();
		localThreadControls->running = false;
		localThreadControls->mutSuspend.unlock();
	}

	SuspendResult ThreadControls::Suspend()
	{
		// Thread suspend via SIGUSR1 is not available on macOS.
		return Threading::THREADERR_MISC;
	}

	SuspendResult ThreadControls::Resume()
	{
		// Thread suspend via SIGUSR1 is not available on macOS.
		return Threading::THREADERR_MISC;
	}

} // namespace Threading
