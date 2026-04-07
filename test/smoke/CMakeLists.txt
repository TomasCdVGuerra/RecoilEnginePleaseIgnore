# Smoke test: validates that core engine platform utilities initialise and
# exit cleanly.  No SDL2/OpenGL/DevIL dependency so it runs in any
# standard CI environment.
#
# Phase 7 additions require <filesystem> and <thread> (C++17 standard).

set(ENGINE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/rts")

set(smoketest_src
    "${CMAKE_CURRENT_SOURCE_DIR}/smoketest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../lib/catch2/catch_main.cpp"
)

if (WIN32)
    list(APPEND smoketest_src "${ENGINE_SOURCE_DIR}/System/Platform/Win/Hardware.cpp")
else ()
    # Both Linux and macOS use the POSIX sysconf-based Hardware.cpp; there
    # is no separate Mac/Hardware.cpp in the engine (by design).
    list(APPEND smoketest_src "${ENGINE_SOURCE_DIR}/System/Platform/Linux/Hardware.cpp")
endif ()

# Threads required for the multi-threaded init test (Phase 7).
find_package(Threads REQUIRED)
set(smoketest_libs Threads::Threads)

add_executable(engine_smoketest ${smoketest_src})
target_link_libraries(engine_smoketest ${smoketest_libs} ${test_common_libraries})
target_include_directories(engine_smoketest PRIVATE
    "${ENGINE_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/src-generated/engine"
)

# Apply strict warning flags to the smoke test when requested.
# Catch2's catch_main.cpp is explicitly excluded because it instantiates
# the amalgamated test framework header which is not under our control.
if (RECOIL_WERROR AND NOT MSVC)
    target_compile_options(engine_smoketest PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    # Remove -Werror from the vendored Catch2 main entry point so that
    # any warnings in the test framework itself do not fail the build.
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../lib/catch2/catch_main.cpp"
        PROPERTIES COMPILE_OPTIONS "-Wno-error"
    )
endif()

add_test(NAME smoketest COMMAND engine_smoketest)
add_dependencies(tests engine_smoketest)
