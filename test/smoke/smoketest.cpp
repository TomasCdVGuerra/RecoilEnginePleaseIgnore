/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// Minimal smoke test: validates that core engine utilities initialise and
// shut down cleanly.  Intentionally kept free of SDL2/OpenGL/DevIL so that
// it can run in any CI environment that has the standard build dependencies.

#define CATCH_AMALGAMATED_CUSTOM_MAIN
#include <catch_amalgamated.hpp>

#include "System/Platform/Hardware.h"

TEST_CASE("smoke/platform-ram", "[smoke]") {
    // TotalRAM() must return a non-zero value on any real machine.
    uint64_t ram = Platform::TotalRAM();
    REQUIRE(ram > 0);
}

TEST_CASE("smoke/platform-pagefile", "[smoke]") {
    // TotalPageFile() is allowed to return 0 on platforms that don't
    // expose a pagefile (e.g. Linux/macOS without swap), so only check
    // that it does not throw.
    (void)Platform::TotalPageFile();
    REQUIRE(true);
}
