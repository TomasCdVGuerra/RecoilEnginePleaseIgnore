# Smoke test: validates that core engine platform utilities initialise and
# exit cleanly.  No SDL2/OpenGL/DevIL dependency so it runs in any
# standard CI environment.

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

set(smoketest_libs "")

add_executable(engine_smoketest ${smoketest_src})
target_link_libraries(engine_smoketest ${smoketest_libs} ${test_common_libraries})
target_include_directories(engine_smoketest PRIVATE
    "${ENGINE_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/src-generated/engine"
)

add_test(NAME smoketest COMMAND engine_smoketest)
add_dependencies(tests engine_smoketest)
