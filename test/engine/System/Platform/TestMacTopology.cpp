/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <catch_amalgamated.hpp>

#include "System/Platform/CpuTopology.h"
#include "System/Platform/ThreadAffinityGuard.h"

#ifdef __APPLE__

TEST_CASE("MacTopology: Apple Silicon/Darwin thread policy defaults", "[Platform][MacTopology]")
{
	SECTION("GetThreadPinPolicy defaults to THREAD_PIN_POLICY_NONE") {
		// On macOS, Grand Central Dispatch handles thread distribution;
		// threads must not be pinned to specific physical cores.
		CHECK(cpu_topology::GetThreadPinPolicy() == cpu_topology::THREAD_PIN_POLICY_NONE);
	}

	SECTION("GetProcessorMasks marks all available cores as performance cores for GCD") {
		const auto masks = cpu_topology::GetProcessorMasks();
		CHECK(masks.performanceCoreMask == ~0u);
		CHECK(masks.efficiencyCoreMask == 0u);
		CHECK(masks.hyperThreadLowMask == 0u);
		CHECK(masks.hyperThreadHighMask == 0u);
	}

	SECTION("GetProcessorCache returns empty cache structure letting OS manage cache awareness") {
		const auto caches = cpu_topology::GetProcessorCache();
		CHECK(caches.groupCaches.empty());
	}
}

TEST_CASE("ThreadAffinityGuard: macOS safe no-op verification", "[Platform][ThreadAffinityGuard]")
{
	SECTION("Default constructor and destructor safely no-op without error") {
		REQUIRE_NOTHROW([]() {
			ThreadAffinityGuard guard;
		}());
	}

	SECTION("Core-indexed constructor and destructor safely no-op without error") {
		REQUIRE_NOTHROW([]() {
			ThreadAffinityGuard guard(0);
		}());

		REQUIRE_NOTHROW([]() {
			ThreadAffinityGuard guard(7);
		}());
	}

	SECTION("Multiple nested guards execute cleanly in local scopes") {
		REQUIRE_NOTHROW([]() {
			ThreadAffinityGuard outerGuard;
			{
				ThreadAffinityGuard innerGuard(1);
			}
		}());
	}
}

#else

TEST_CASE("MacTopology: Non-Apple platform stub", "[Platform][MacTopology]")
{
	// Placeholder for non-Apple test runners
	SUCCEED("MacTopology tests are only applicable on Apple/Darwin platforms.");
}

#endif
