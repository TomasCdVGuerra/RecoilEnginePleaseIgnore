# Place executables and shared libs under "build-dir/",
# instead of under "build-dir/rts/"
# This way, we have the build-dir structure more like the install-dir one,
# which makes testing spring in the builddir easier, eg. like this:
# cd build-dir
# SPRING_DATADIR=$(pwd) ./spring
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

add_definitions(-DHEADLESS)
add_definitions(-DNO_SOUND)
remove_definitions(-DAVI_CAPTURING)

set(OpenGL_GL_PREFERENCE LEGACY)

find_package(OpenGL 3.0 REQUIRED)


# headlessstubs are our stubs that replace libGL, libGLU, libGLEW, libSDL (yes really!)
list(APPEND engineHeadlessLibraries headlessStubs)
list(APPEND engineHeadlessLibraries ${SPRING_SIM_LIBRARIES})
list(APPEND engineHeadlessLibraries engineSystemNet)
list(APPEND engineHeadlessLibraries ${engineCommonLibraries})
#list(APPEND engineHeadlessLibraries engineaGui)
list(APPEND engineHeadlessLibraries no-sound)
list(APPEND engineHeadlessLibraries engineSim)
list(APPEND engineHeadlessLibraries squish)
list(APPEND engineHeadlessLibraries pr-downloader)
list(APPEND engineHeadlessLibraries RmlUi::Core)
list(APPEND engineHeadlessLibraries RmlUi::Debugger)
list(APPEND engineHeadlessLibraries lunasvg)

include_directories(${ENGINE_SRC_ROOT_DIR}/lib/assimp/include)
include_directories(${ENGINE_SRC_ROOT_DIR}/lib/asio/include)
include_directories(${ENGINE_SRC_ROOT_DIR}/lib/slimsig/include)
include_directories(${ENGINE_SRC_ROOT_DIR}/lib/cereal/include)

# Build a headless game library from the shared game sources directly.
# This keeps headless independent from the legacy build toggle, so
# BUILD_spring-legacy=OFF remains a valid configure option.
add_library(GameHeadless STATIC)
target_sources(GameHeadless PRIVATE ${sources_engine_Game})
add_dependencies(GameHeadless generateVersionFiles)
target_include_directories(GameHeadless PRIVATE "${CMAKE_BINARY_DIR}/src-generated/engine")

target_link_libraries(GameHeadless PRIVATE
	Tracy::TracyClient
	headlessStubs
	RmlUi::Core
	prd::jsoncpp
	nowide::nowide
	fmt::fmt
)

if (TARGET streflop)
	target_link_libraries(GameHeadless PRIVATE streflop)
endif()
### Build the executable
add_executable(engine-headless ${engineSources} ${ENGINE_ICON})
target_link_libraries(engine-headless no-sound ${engineHeadlessLibraries} GameHeadless no-sound)

# Export symbols for plugin access (replaces CMP0065 OLD behavior)
set_target_properties(engine-headless PROPERTIES ENABLE_EXPORTS TRUE)

if(MSVC)
	target_link_libraries(engine-headless engine-natvis)
endif()

# Apply strict warning flags to the headless engine when requested.
# This does not affect vendored library targets; it is scoped to this target
# only so third-party code compiled into libraries remains unaffected.
if (RECOIL_WERROR AND NOT MSVC)
	target_compile_options(engine-headless PRIVATE
		-Wall -Wextra -Wpedantic -Werror
	)
endif()

if    (MINGW)
	# To enable console output/force a console window to open
	set_target_properties(engine-headless PROPERTIES LINK_FLAGS "-Wl,-subsystem,console")
endif (MINGW)

if (APPLE)
	# Phase 11: macOS portability -- rpath and ad-hoc code signing.
	#
	# @executable_path/../Frameworks lets the binary find .dylib/.framework
	# files placed alongside the bundle in a Frameworks/ subdirectory, which
	# is the standard macOS app layout and what macdeployqt produces.
	# @loader_path is the fallback for flat build-directory layouts where
	# libraries live in the same directory as the binary.
	#
	# Without these entries the binary can only load the libraries it was
	# linked against via their absolute install-time paths, which breaks on
	# any machine that did not build the engine from source.
	set_target_properties(engine-headless PROPERTIES
		MACOSX_RPATH ON
		BUILD_RPATH   "@loader_path"
		INSTALL_RPATH "@executable_path/../Frameworks;@loader_path"
	)

	# Ad-hoc code signing is required for binaries to run on macOS 12+ when
	# not distributed through the App Store or a notarised disk image.
	# Using "-" as the identity creates an ad-hoc signature (no Apple
	# Developer account required) which satisfies Gatekeeper for local use
	# and is sufficient for CI runners.
	find_program(CODESIGN_TOOL codesign)
	if (CODESIGN_TOOL)
		add_custom_command(TARGET engine-headless POST_BUILD
			COMMAND ${CODESIGN_TOOL} --force --sign -
			"$<TARGET_FILE:engine-headless>"
			COMMENT "Ad-hoc signing engine-headless for macOS portability"
			VERBATIM
		)
	endif()
endif (APPLE)


### Install the executable
install(TARGETS engine-headless DESTINATION ${BINDIR})

# Only build & install spring-headless executable & dependencies
# use cases:
# * make spring-headless
# * make install-spring-headless
create_engine_build_and_install_target(headless)
