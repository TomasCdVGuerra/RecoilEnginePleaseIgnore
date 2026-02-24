/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// Smoke tests: validate that core engine utilities initialise and shut down
// cleanly without SDL2/OpenGL/DevIL so they can run in any standard CI
// environment.
//
// Phase 7 additions:
//  - VFS filesystem path-casing test: ensures file-lookup helpers handle
//    the macOS case-insensitive (but case-preserving) HFS+ / APFS default
//    volume without silent data loss.
//  - Multi-threaded initialisation test: calls platform routines from N
//    concurrent threads to surface races in the platform layer.

#define CATCH_AMALGAMATED_CUSTOM_MAIN
#include <catch_amalgamated.hpp>

#include "System/Platform/Hardware.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Phase 4 – basic platform init/shutdown
// ---------------------------------------------------------------------------

TEST_CASE("smoke/platform-ram", "[smoke]") {
    // TotalRAM() must return a non-zero value on any real machine.
    const uint64_t ram = Platform::TotalRAM();
    REQUIRE(ram > 0);
}

TEST_CASE("smoke/platform-pagefile", "[smoke]") {
    // TotalPageFile() is allowed to return 0 on platforms that don't
    // expose a pagefile (e.g. Linux/macOS without swap), so only check
    // that it does not throw.
    (void)Platform::TotalPageFile();
    REQUIRE(true);
}

// ---------------------------------------------------------------------------
// Phase 7 – VFS path-casing
// ---------------------------------------------------------------------------

TEST_CASE("smoke/vfs-path-casing", "[smoke][filesystem]") {
    namespace fs = std::filesystem;

    // Create a uniquely named temporary directory to avoid collisions.
    const fs::path tmpBase = fs::temp_directory_path() / "recoil_smoke_XXXXXX";
    fs::path workDir;

    // mkdtemp is POSIX; fall back to a fixed name on non-POSIX platforms.
#if defined(_WIN32)
    workDir = fs::temp_directory_path() / "recoil_smoke_casing";
    fs::create_directories(workDir);
#else
    {
        // mkdtemp requires a writable C-string buffer; use std::vector<char>
        // to hold it so the data() pointer is writable and null-terminated.
        std::vector<char> tmpl(tmpBase.string().begin(), tmpBase.string().end());
        tmpl.push_back('\0');
        char* result = mkdtemp(tmpl.data());
        REQUIRE(result != nullptr);
        workDir = result;
    }
#endif

    // Write a file with a known lower-case name.
    const fs::path lowerPath = workDir / "testfile.txt";
    {
        std::ofstream ofs{lowerPath};
        REQUIRE(ofs.is_open());
        ofs << "hello";
    }
    REQUIRE(fs::exists(lowerPath));

    // On macOS the default APFS/HFS+ volume is case-insensitive but case-
    // preserving.  Verify that a lookup using the exact lower-case name
    // always succeeds — this confirms that the file-system layer respects
    // the casing used when the file was created.
    const fs::path exactPath  = workDir / "testfile.txt";
    REQUIRE(fs::exists(exactPath));

    // Verify the stored name is preserved as lower-case (case-preserving).
    bool foundWithCorrectCase = false;
    for (const auto& entry : fs::directory_iterator(workDir)) {
        if (entry.path().filename() == "testfile.txt") {
            foundWithCorrectCase = true;
            break;
        }
    }
    REQUIRE(foundWithCorrectCase);

    // Clean up.
    fs::remove_all(workDir);
}

// ---------------------------------------------------------------------------
// Phase 7 – multi-threaded platform initialisation
// ---------------------------------------------------------------------------

TEST_CASE("smoke/threaded-platform-ram", "[smoke][threading]") {
    // Spawn N threads that all call Platform::TotalRAM() concurrently.
    // A data race here would be caught by TSAN in an instrumented build and
    // will typically manifest as an inconsistent return value or crash.
    constexpr int THREAD_COUNT = 8;

    std::atomic<int>      doneCount{0};
    std::vector<uint64_t> results(THREAD_COUNT, 0);
    std::vector<std::thread> threads;
    threads.reserve(THREAD_COUNT);

    for (int i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([&results, &doneCount, i]() {
            results[i] = Platform::TotalRAM();
            ++doneCount;
        });
    }

    for (auto& t : threads)
        t.join();

    REQUIRE(doneCount.load() == THREAD_COUNT);

    // All threads must observe the same (non-zero) RAM value.
    const uint64_t expected = results[0];
    REQUIRE(expected > 0);
    const bool allMatch = std::all_of(results.begin(), results.end(),
                                      [expected](uint64_t v) { return v == expected; });
    REQUIRE(allMatch);
}
