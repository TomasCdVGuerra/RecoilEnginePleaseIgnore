/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// macOS implementation of the cpu_topology interface.
// pthread_setaffinity_np is not available on macOS, so thread affinity is
// not supported. All online CPUs are reported as performance cores with no
// hyper-threading differentiation.

#include "System/Platform/CpuTopology.h"
#include "System/Log/ILog.h"

#include <unistd.h>
#include <sys/sysctl.h>
#include <bitset>

namespace cpu_topology {

// ProcessorMasks uses uint32_t fields, so the mask is limited to 32 logical
// CPUs regardless of this constant.  This matches the Linux implementation's
// MAX_CPUS 32 limit and the shared ProcessorMasks type definition.
static constexpr int MAX_CPUS_MACOS = 32;

ProcessorMasks GetProcessorMasks()
{
	ProcessorMasks masks;

	int nCPUs = static_cast<int>(sysconf(_SC_NPROCESSORS_CONF));
	if (nCPUs <= 0) {
		LOG_L(L_WARNING, "[%s] could not determine CPU count, defaulting to 1", __func__);
		nCPUs = 1;
	}
	if (nCPUs > MAX_CPUS_MACOS)
		nCPUs = MAX_CPUS_MACOS;

	// macOS does not expose pthread_setaffinity_np or P/E-core topology
	// through public APIs, so treat all online CPUs as performance cores.
	std::bitset<MAX_CPUS_MACOS> perfMask;
	for (int i = 0; i < nCPUs; ++i)
		perfMask.set(i);

	masks.performanceCoreMask = static_cast<uint32_t>(perfMask.to_ulong());
	masks.efficiencyCoreMask  = 0;
	masks.hyperThreadLowMask  = 0;
	masks.hyperThreadHighMask = 0;

	return masks;
}

ProcessorCaches GetProcessorCache()
{
	ProcessorCaches caches;

	int nCPUs = static_cast<int>(sysconf(_SC_NPROCESSORS_CONF));
	if (nCPUs <= 0) nCPUs = 1;
	if (nCPUs > MAX_CPUS_MACOS) nCPUs = MAX_CPUS_MACOS;

	// Query L3 cache size via sysctl; falls back to 0 if unavailable
	// (e.g. Apple Silicon exposes shared last-level cache differently).
	uint32_t l3Size = 0;
	{
		size_t len = sizeof(l3Size);
		sysctlbyname("hw.l3cachesize", &l3Size, &len, nullptr, 0);
	}

	ProcessorGroupCaches group;
	group.cacheSizes[2] = l3Size;
	for (int i = 0; i < nCPUs; ++i)
		group.groupMask |= (0x1u << static_cast<unsigned>(i));

	caches.groupCaches.push_back(group);
	return caches;
}

ThreadPinPolicy GetThreadPinPolicy()
{
	// macOS does not provide public APIs for thread CPU pinning, so
	// report NONE to let the engine skip affinity configuration.
	return THREAD_PIN_POLICY_NONE;
}

} // namespace cpu_topology
