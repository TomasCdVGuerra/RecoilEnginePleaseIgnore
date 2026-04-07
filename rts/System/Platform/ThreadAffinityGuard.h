#ifndef THREAD_AFFINITY_GUARD_H__
#define THREAD_AFFINITY_GUARD_H__

#ifdef _WIN32
#include <windows.h>
#elif !defined(__APPLE__)
#include <sched.h>
#endif

class ThreadAffinityGuard {
private:
#ifdef _WIN32
	DWORD_PTR savedAffinity;
	HANDLE threadHandle;
#elif defined(__APPLE__)
	// macOS does not expose cpu_set_t or sched_{get,set}affinity via
	// public APIs; the guard is a deliberate no-op on this platform.
#else
	cpu_set_t savedAffinity;
	pid_t tid;
#endif
	bool affinitySaved;

public:
	// Constructor: Saves the current thread's affinity
	ThreadAffinityGuard();

	// Destructor: Restores the saved affinity if it was successfully stored
	~ThreadAffinityGuard();

	// Delete copy constructor to prevent copying
	ThreadAffinityGuard(const ThreadAffinityGuard&) = delete;

	// Delete copy assignment operator to prevent assignment
	ThreadAffinityGuard& operator=(const ThreadAffinityGuard&) = delete;
};

#endif
