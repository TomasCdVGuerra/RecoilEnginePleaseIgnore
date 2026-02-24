/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// macOS implementation of the cpu_topology interface.
//
// On Intel Macs, all logical CPUs are treated as performance cores; there is
// no public pthread_setaffinity_np so thread pinning is skipped.
//
// On Apple Silicon (M-series), the kernel exposes separate performance and
// efficiency core counts via hw.perflevel0.logicalcpu and
// hw.perflevel1.logicalcpu sysctl keys.  We use these to populate the
// performance-/efficiency-core masks so the thread pool avoids scheduling
// work on low-power E-cores by default.

#include "System/Platform/CpuTopology.h"
#include "System/Log/ILog.h"

#include <unistd.h>
#include <sys/sysctl.h>
#include <bitset>

namespace cpu_topology {

// ProcessorMasks uses uint32_t fields so the mask is capped at 32 logical
// CPUs.  This matches the Linux implementation's MAX_CPUS 32 limit and the
// shared ProcessorMasks type definition.  Current Apple Silicon chips
// (M3 Max: 16 cores, M2 Ultra: 24 cores) fit comfortably within this bound.
static constexpr int MAX_CPUS_MACOS = 32;

// Read a scalar int32 sysctl by name; returns -1 on failure.
static int32_t ReadSysctlInt(const char* name) noexcept
{
	int32_t value = -1;
	size_t  len   = sizeof(value);
	sysctlbyname(name, &value, &len, nullptr, 0);
	return value;
}

ProcessorMasks GetProcessorMasks()
{
	ProcessorMasks masks;

	// Try to detect Apple Silicon P/E-core split.
	// hw.perflevel0.logicalcpu counts P-cores (highest performance level).
	// hw.perflevel1.logicalcpu counts E-cores (efficiency level).
	// These keys are absent on Intel Macs, so ReadSysctlInt returns -1.
	const int32_t pCores = ReadSysctlInt("hw.perflevel0.logicalcpu");
	const int32_t eCores = ReadSysctlInt("hw.perflevel1.logicalcpu");

	const bool hasAppleSiliconTopology = (pCores > 0);

	int nCPUs = static_cast<int>(sysconf(_SC_NPROCESSORS_CONF));
	if (nCPUs <= 0) {
		LOG_L(L_WARNING, "[%s] could not determine CPU count, defaulting to 1", __func__);
		nCPUs = 1;
	}
	if (nCPUs > MAX_CPUS_MACOS)
		nCPUs = MAX_CPUS_MACOS;

	if (hasAppleSiliconTopology) {
		// Apple Silicon: P-cores occupy the first pCores slots; E-cores follow.
		// The kernel schedules threads on P-cores first for GCD/pthreads work,
		// so we mirror that convention in the affinity masks.
		const int nPerf = std::min(static_cast<int>(pCores), nCPUs);
		const int nEff  = std::min(static_cast<int>(std::max(eCores, 0)), nCPUs - nPerf);

		std::bitset<MAX_CPUS_MACOS> perfMask;
		std::bitset<MAX_CPUS_MACOS> effMask;

		for (int i = 0; i < nPerf; ++i)
			perfMask.set(i);
		for (int i = nPerf; i < nPerf + nEff; ++i)
			effMask.set(i);

		masks.performanceCoreMask = static_cast<uint32_t>(perfMask.to_ulong());
		masks.efficiencyCoreMask  = static_cast<uint32_t>(effMask.to_ulong());
		LOG("macOS: Apple Silicon topology detected: %d P-cores, %d E-cores", nPerf, nEff);
	} else {
		// Intel Mac or unknown topology: all CPUs are reported as performance
		// cores since there is no public P/E differentiation API.
		std::bitset<MAX_CPUS_MACOS> perfMask;
		for (int i = 0; i < nCPUs; ++i)
			perfMask.set(i);
		masks.performanceCoreMask = static_cast<uint32_t>(perfMask.to_ulong());
		LOG("macOS: Intel/generic topology detected: %d logical CPUs", nCPUs);
	}

	// macOS does not expose pthread_setaffinity_np, so we cannot observe
	// hardware hyper-threading directly.  Leave both HT masks at zero.
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

	// Query L3 cache size via sysctl; falls back to 0 if unavailable.
	// Apple Silicon uses a shared "System Level Cache" rather than a
	// traditional L3; this is exposed by hw.perflevel0.l2cachesize on some
	// hardware but may not be present on all variants.  We try the standard
	// hw.l3cachesize first (Intel) and fall back to hw.perflevel0.l2cachesize
	// (Apple Silicon) so the thread-pool gets a meaningful grouping hint.
	uint32_t cacheSize = 0;
	{
		size_t len = sizeof(cacheSize);
		if (sysctlbyname("hw.l3cachesize", &cacheSize, &len, nullptr, 0) != 0) {
			// Fallback: try Apple Silicon last-level cache
			sysctlbyname("hw.perflevel0.l2cachesize", &cacheSize, &len, nullptr, 0);
		}
	}

	ProcessorGroupCaches group;
	group.cacheSizes[2] = cacheSize;
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
