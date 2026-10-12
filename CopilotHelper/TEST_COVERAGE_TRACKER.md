# Test Coverage Tracker

## 1. Overview & Purpose
This document tracks formal unit test coverage for RecoilEngine, catalogs the existing Catch2 test suite across the engine subsystems, and identifies explicit coverage gaps—with a specific focus on areas modified during macOS porting and MGL Core profile modernizations (`GlobalRendering`, `RenderBuffers`, MGL integration, and audio fallback).

---

## 2. Existing Catch2 Test Inventory

The test suite uses Catch2 (amalgamated single-header in `test/lib/catch2/`) with a custom entrypoint in `test/lib/catch2/catch_main.cpp`. Tests are registered via the `add_spring_test` CMake macro in `test/CMakeLists.txt`.

### Category Breakdown

| Category / Directory | Target Binary | Test Name in CTest | Source File | Description / Scope |
| :--- | :--- | :--- | :--- | :--- |
| **System / Math** | `test_Float3` | `testFloat3` | `test/engine/System/testFloat3.cpp` | `float3` vector operations, SSE/streflop precision, normalization |
| | `test_Matrix44f` | `testMatrix44f` | `test/engine/System/testMatrix44f.cpp` | `CMatrix44f` transformation matrices, inverses, determinant |
| | `test_Matrix44fRotation` | `testMatrix44fRotation` | `test/engine/System/testRotationMatrix44f.cpp` | Euler angles, rotation matrix conversions |
| | `test_Transform` | `testTransform` | `test/engine/System/testTransform.cpp` | Hierarchical affine transforms & quaternions |
| | `test_SQRT` | `testSQRT` | `test/engine/System/testSQRT.cpp` | Fast inverse square root & streflop precision |
| | `test_ClampRad` | `testClampRad` | `test/engine/System/testClampRad.cpp` | Angle wrapping and radial clamping |
| **System / Time & Threading** | `test_SpringTime` | `testSpringTime` | `test/engine/System/Misc/testSpringTime.cpp` | RAII `InitSpringTime`, high-resolution timer ticking |
| | `test_ThreadPool` | `testThreadPool` | `test/engine/System/testThreadPool.cpp` | Multi-threaded task scheduling, worker pool scaling |
| | `test_Mutex` | `testMutex` | `test/other/testMutex.cpp` | Engine mutex implementation performance & correctness |
| **System / Core & Misc** | `test_RectangleOverlapHandler` | `testRectangleOverlapHandler` | `test/engine/System/Misc/RectangleOverlapHandler.cpp` | 2D bounding rectangle overlap detection |
| | `test_BitwiseEnum` | `testBitwiseEnum` | `test/engine/System/Misc/TestBitwiseEnum.cpp` | Type-safe bitwise operators on enum classes |
| | `test_EventClient` | `testEventClient` | `test/engine/System/EventClient.cpp` | Engine event bus dispatch & listener registration |
| **System / Network & I/O** | `test_UDPListener` | `testUDPListener` | `test/engine/System/Net/TestUDPListener.cpp` | UDP socket binding, packet framing, loopback listeners |
| | `test_ILog` | `testILog` | `test/engine/System/Log/TestILog.cpp` | Log severity filtering, stream/file sinks, formatting |
| | `test_FileSystem` | `testFileSystem` | `test/engine/System/FileSystem/TestFileSystem.cpp` | VFS archive scanning, CRC, file resolution |
| **System / Sync & Serialization** | `test_SyncedPrimitive` | `testSyncedPrimitive` | `test/engine/System/Sync/TestSyncedPrimitive.cpp` | Synced integers/floats, checksum hashing |
| | `test_DemoFileExtension` | `testDemoFileExtension` | `test/engine/System/LoadSave/testDemoFileExtension.cpp` | Demo replay file extension parsing & whitespace trimming |
| | `test_Creg` | `testCreg` | *(runs spring-headless --test-creg)* | CREG class reflection & serialization metadata |
| | `test_LoadSave` | `testLoadSave` | `test/engine/System/LoadSave/testCregLoadSave.cpp` | Savegame state serialization and deserialization |
| | `test_SerializeLuaState` | `testSerializeLuaState` | `test/engine/System/testSerializeLuaState.cpp` | Lua VM memory pool state serialization |
| **Simulation** | `test_Ellipsoid` | `testEllipsoid` | `test/engine/Sim/Misc/testEllipsoid.cpp` | 3D ellipsoid collision primitives & ray intersections |
| | `test_QuadField` | `testQuadField` | `test/engine/Sim/Misc/testQuadField.cpp` | Spatial partitioning quadfield cell queries |
| **UnitSync & Scripting** | `test_UnitSync` | `testUnitSync` | `test/unitsync/testUnitSync.cpp` | UnitSync shared library export functions & map parsing |
| | `test_LuaSocketRestrictions` | `testLuaSocketRestrictions` | `test/lib/luasocket/TestRestriction.cpp` | Lua socket sandboxing and network policy enforcement |
| **Memory Management** | `test_MemPoolTypes` | `testMemPoolTypes` | `test/other/testMemPoolTypes.cpp` | SMMalloc memory allocator pool verification |
| | `test_Mimalloc` | `testMimalloc` | `test/other/benchmarkMimalloc.cpp` | Mimalloc allocator integration verification |
| **Rendering** | `test_GlobalRendering` | `testGlobalRendering` | `test/engine/Rendering/TestGlobalRendering.cpp` | CLI windowed precedence (`ResolveFullScreenMode`) & config sync |
| | `test_RenderBuffers` | `testRenderBuffers` | `test/engine/Rendering/TestRenderBuffers.cpp` | Core profile detection, column-major MVP upload, shader templates |
| **System / Platform** | `test_MacTopology` | `testMacTopology` | `test/engine/System/Platform/TestMacTopology.cpp` | macOS GCD thread pin policy defaults & ThreadAffinityGuard no-op |
| **System / Input** | `test_MouseInput` | `testMouseInput` | `test/engine/System/Input/TestMouseInput.cpp` | SDL mouse High-DPI scaling (1.0x, 2.0x, fractional), delta translation, inverse mapping, safe fallbacks |
| **Negative Compile Tests** | `testBitwiseEnum_fail1..3` | `testBitwiseEnum_fail1..3` | `test/engine/System/Misc/TestBitwiseEnum.cpp` | Verifies invalid bitwise enum conversions fail to compile |

*Total registered CTest test cases: 33.*

---

## 3. Subsystem Audit

This audit evaluates the major engine subsystems across `rts/Sim`, `rts/System`, `rts/Rendering`, `rts/Lua`, `rts/Net`, and `rts/Game`, cross-referencing implementation files against automated Catch2 test coverage. Special emphasis is given to platform-specific code, thread topology, input pipelines, and deterministic simulation boundaries.

### Simulation Subsystem (`rts/Sim`)
- **[Sim/Misc - Spatial Partitioning & QuadField]**: 🟢 Tested (`testQuadField` verifies 2D/3D quadfield cell indexing, rectangle intersection queries, and spatial queries).
- **[Sim/Misc - Geometric Primitives & Ellipsoid]**: 🟢 Tested (`testEllipsoid` tests 3D ellipsoid collision primitives, geometric scaling, and ray intersections).
- **[Sim/Units - Unit Lifecycle & State Machines]**: 🔴 Untested (Unit creation via `UnitLoader`, death/decay events, experience scaling, state transitions, and command queues lack unit tests).
- **[Sim/Weapons - Ballistics & Targeting]**: 🔴 Untested (Ballistic trajectories, weapon reload timers, turret traversal limits, leading targeting math, and damage calculation formulas are untested).
- **[Sim/Projectiles - Physics & Explosion Damage]**: 🔴 Untested (`ProjectileHandler`, area-of-effect splash damage radius calculations, and explosion generator logic have no unit tests).
- **[Sim/MoveTypes - Locomotion & Steering]**: 🔴 Untested (`GroundMoveType`, `AirMoveType`, `HoverAirMoveType`, obstacle avoidance steering, slope speed modifiers, and braking curves lack unit tests).
- **[Sim/Path - QTPFS & Pathfinding]**: 🔴 Untested (Quadtree Path Finding System (`QTPFS`), hierarchical path cache invalidation, flow fields, and terrain heatmaps are untested).
- **[Sim/Features - World Objects & Reclaimables]**: 🔴 Untested (`FeatureHandler`, wreckage conversion, tree crushing, and energy/metal resource reclamation lack test coverage).
- **[Sim/Ecs - Entity Component System]**: 🔴 Untested (`entt` registry management, component view iteration, and entity lifecycle hooks are unverified by tests).
- **[Sim/Misc - LOS & Sensor Modeling]**: 🔴 Untested (`LosHandler`, radar/sonar coverage grids, stealth/jamming cloaking math, and seismic detection are untested).
- **[Sim/Misc - Collision Detection]**: 🔴 Untested (`CollisionHandler`, collision volume hierarchies, and terrain ground-blocking object maps have no unit tests).

### System & Platform Architecture (`rts/System`)
- **[System/Math - Vector & Matrix Foundations]**: 🟢 Tested (`testFloat3`, `testMatrix44f`, `testMatrix44fRotation`, `testTransform`, `testSQRT`, `testClampRad` cover affine math, fast inverse square root, and streflop floating-point precision).
- **[System/Time - High-Resolution Clocking]**: 🟢 Tested (`testSpringTime` tests high-resolution monotonic timer progression and RAII timer initialization).
- **[System/Threading - Task Scheduling Pool]**: 🟢 Tested (`testThreadPool` covers thread worker scaling, task queue submission, and concurrent job completion).
- **[System/Platform - Thread Topology & Hardware Core Detection]**: 🟢 Tested for macOS (`testMacTopology` verifies that Apple platforms default to `THREAD_PIN_POLICY_NONE`, expose generic performance masks for Grand Central Dispatch, and that `ThreadAffinityGuard` safely no-ops); 🔴 Untested for Linux/Windows (`Linux/CpuTopology.cpp` `/proc/cpuinfo` parsing, Windows `GetLogicalProcessorInformationEx`, and NUMA core pinning remain unverified).
- **[System/Platform - Crash Handling & Watchdog]**: 🔴 Untested (`Watchdog.cpp` heartbeat monitors, thread freeze detection, signal trapping across OSes, and stack unwinding/symbolication have no automated tests).
- **[System/Platform - OS Windowing & Shared Libraries]**: 🔴 Untested (`WindowManagerHelper`, `ScopedFileLock`, `SharedLib` dynamic symbol resolution, and clipboard integration lack test coverage).
- **[System/Input - Mouse Event Pipeline & High-DPI Scaling]**: 🟢 Tested for SDL Mouse Input (`testMouseInput` verifies SDL mouse coordinate translation, macOS Retina 2.0x scaling, fractional scaling factors, delta translation, inverse mapping, and safe zero/negative DPI fallbacks); 🔴 Untested for keyboard mapping (`KeyInput.cpp` key modifier bitmasks, hotkey bindings, and text input events remain unverified).
- **[System/Sync - Deterministic Primitives & SyncCheck]**: 🟢 Tested (`testSyncedPrimitive` covers `CSyncChecker` hashing and checksum generation for synced numeric types).
- **[System/Sync - Multiplayer Simulation Boundaries & Desync Diagnostics]**: 🔴 Untested (`SyncDebugger`, `DumpState.cpp`, `DumpHistory.cpp`, `FPUCheck.cpp`, and cross-client state snapshot serialization on desync lack automated tests).
- **[System/FileSystem - Virtual File System & Archives]**: 🟢 Tested (`testFileSystem` covers VFS directory mapping, 7zip archive reading, CRC calculations, and file resolution).
- **[System/LoadSave - Serialization & Savegames]**: 🟢 Tested (`testDemoFileExtension` and `testCregLoadSave` cover demo extension parsing, CREG class reflection, and state serialization).
- **[System/Log - Logging & Severity Filtering]**: 🟢 Tested (`testILog` verifies severity filtering, console sinks, file sinks, and message formatting).
- **[System/Memory - Specialized Allocators]**: 🟢 Tested (`testMemPoolTypes` and `testMimalloc` cover SMMalloc and mimalloc thread cache allocators).

### Graphics & Rendering Subsystem (`rts/Rendering`)
- **[Rendering/GlobalRendering - CLI Mode Precedence]**: 🟢 Tested (`testGlobalRendering` tests `ResolveFullScreenMode` precedence rules ensuring windowed CLI flags override fullscreen configurations).
- **[Rendering/GL/RenderBuffers - Core Profile MVP Upload]**: 🟢 Tested (`testRenderBuffers` covers headless column-major matrix multiplication, uniform dispatch to `u_mvpMatrix`, context predicate checks, and shader template syntax).
- **[Rendering/GlobalRendering - Viewport & Display Geometry]**: 🔴 Untested (Multi-monitor bounding box calculations, desktop resolution auto-detection, aspect ratio letterboxing, and borderless window sizing have no tests).
- **[Rendering/Shaders - Program Management & Pipeline]**: 🔴 Untested (`CShaderHandler`, shader compilation failure fallbacks, shader cache deduplication, and uniform state caching are untested).
- **[Rendering/Textures - Texture Management & Atlases]**: 🔴 Untested (`TextureAtlas`, row/quadtree atlas allocators, runtime texture compression, and mipmap generation lack unit tests).
- **[Rendering/Models - 3D Mesh Parsing & Hierarchies]**: 🔴 Untested (Parsing and mesh hierarchy compilation for 3DO, S3O, OBJ, and GLTF/Assimp assets are unverified).
- **[Rendering/Fonts - Typography & Text Layout]**: 🔴 Untested (Freetype glyph rasterization, texture atlas creation, UTF-8 parsing, and `TextWrap.cpp` line breaking have no unit tests).
- **[Rendering/Env - Environmental Rendering]**: 🔴 Untested (Terrain heightmesh tessellation, water reflection/refraction shaders, dynamic skyboxes, and atmospheric sun lighting lack automated tests).

### Lua Scripting Subsystem (`rts/Lua`)
- **[Lua/Serialization - VM State Save/Load]**: 🟢 Tested (`testSerializeLuaState` covers Lua memory pool state serialization into CREG archives).
- **[Lua/Security - Socket Restrictions]**: 🟢 Tested (`testLuaSocketRestrictions` verifies that luasocket is restricted to local addresses and sandboxed policies).
- **[Lua/Synced vs Unsynced - State Boundary Separation]**: 🔴 Untested (`LuaSyncedCtrl`, `LuaSyncedRead`, `LuaUnsyncedCtrl`, `LuaUnsyncedRead` strict separation between deterministic synced states and unsynced UI scripts is not verified by unit tests).
- **[Lua/OpenGL - Graphics Bindings]**: 🔴 Untested (`LuaOpenGL`, `LuaShaders`, `LuaFBOs`, `LuaVBO`, `LuaVAO` bindings that expose GPU pipelines to Lua widgets are untested).
- **[Lua/Parser - Definition File Ingestion]**: 🔴 Untested (`LuaParser` evaluation of unit definitions, weapon profiles, and map metadata tables lacks unit tests).
- **[Lua/CallIns - Engine Event Dispatch]**: 🔴 Untested (Engine-to-Lua event dispatches like `GameFrame`, `UnitCreated`, `UnitDestroyed`, and `DrawWorld` have no automated tests).

### Networking & Multiplayer Subsystem (`rts/Net` & `rts/System/Net`)
- **[Net/UDPListener - Loopback Socket Binding]**: 🟢 Tested (`testUDPListener` covers local UDP port binding, listening, and raw packet reception).
- **[Net/UDPConnection - Packet Reliability Layer]**: 🔴 Untested (`UDPConnection.cpp` windowing, ACK handling, sequence number tracking, resend timeouts, packet fragmentation, and MTU discovery are untested).
- **[Net/GameServer - Multiplayer Host Orchestration]**: 🔴 Untested (`GameServer.cpp` client handshake protocol, player slot assignment, readiness tracking, and game tick synchronization lack unit tests).
- **[Net/AutohostInterface - Remote Lobby Protocol]**: 🔴 Untested (Communication with external match lobbies and autohost bots over TCP sockets has no automated coverage).

### Game Logic & Control Subsystem (`rts/Game`)
- **[Game/MainLoop - Simulation Tick Loop]**: 🔴 Untested (`Game.cpp` deterministic frame advance, pause toggling, speed changes, and frame interpolation are entirely unverified by unit tests).
- **[Game/Commands - Command Processing Pipeline]**: 🔴 Untested (`SyncedGameCommands`, `UnsyncedGameCommands`, order dispatching, and action executors have no tests).
- **[Game/Selection - Unit Grouping & Control]**: 🔴 Untested (`SelectedUnitsHandler`, group formation assignments, and multi-unit command distribution lack tests).
- **[Game/PreGame - Match Initialization & Scripting]**: 🔴 Untested (`GameSetup`, `PreGame`, start script key-value parsing, allyteam assignments, and player spawn placement have no tests).
- **[Game/UI - Camera & Interaction Controllers]**: 🔴 Untested (`CameraHandler`, camera tracking modes, mouse ray casting to terrain, and minimap coordinate conversion are untested).

---

## 4. Explicit Coverage Gaps in Recently Touched Subsystems

Before our work, **zero rendering tests existed in `test/`** (the `test/engine/Rendering/` directory did not even exist). The following critical areas were identified as having no automated unit test coverage:

### 1. `GlobalRendering` (Windowing & Display Settings)
- **CLI Flag Precedence Logic (`cliWindowed` vs `cliFullScreen` vs config):**
  - Bug context: `cliWindowed` command-line switch was previously superseded by saved fullscreen configuration or `-f` flags.
  - Fix: Explicit precedence order where `cliWindowed` strictly forces windowed mode (`fullScreen = false`) regardless of config or other flags.
  - *Coverage Gap:* No tests verify the boolean truth table for `SetFullScreen()`.
- **Display Resolution & Borderless Fallback:**
  - *Coverage Gap:* Resolution detection, window positioning bounds, and aspect ratio calculation when resizing or switching modes.

### 2. `RenderBuffers` (GL Core Profile & Shaders)
- **`PlatformShaderProfile::RequiresCoreShaderHeader()`:**
  - Fix context: Apple platforms running MGL Core Profile (OpenGL 4.1 Core) require Core `#version` headers and uniform-based MVP matrices, while legacy compatibility contexts do not.
  - *Coverage Gap:* No unit tests verifying context flag detection.
- **`PlatformShaderProfile::UploadCoreMvpMatrix()`:**
  - Fix context: Core profile shaders cannot access built-in `gl_ModelViewProjectionMatrix`. The C++ layer queries `GL_PROJECTION_MATRIX` and `GL_MODELVIEW_MATRIX`, computes `mvp = projection * modelView` in column-major order, and uploads `u_mvpMatrix`.
  - *Coverage Gap:* No unit tests verifying column-major matrix multiplication correctness, null program safety, or correct uniform name dispatch.
- **Shader Template Variants (`RenderBuffers.inl`):**
  - *Coverage Gap:* No tests verifying that `vsRenderBufferSrcCore` contains `uniform mat4 u_mvpMatrix;` and does not reference `gl_ModelViewProjectionMatrix`, while `vsRenderBufferSrcCompat` retains legacy symbols.

### 3. MGL Integration & Topology
- **macOS Hardware Topology Detection:**
  - Fix context: `MacTopology.cpp` query routines replaced Linux `/proc/cpuinfo` parsing on Apple Silicon/Darwin.
  - *Coverage Gap:* Core count, cache line size, and performance vs efficiency core queries on Darwin (now verified via `test_MacTopology`).
- **Audio Fallback:**
  - Fix context: OpenAL device initialization gracefully falls back to muted/null stubs if no default output device is present.
  - *Coverage Gap:* No unit tests for device failure detection.

---

## 5. Test Implementation Plan & Progress

- [x] **`test_GlobalRendering`**: Catch2 test suite covering `CGlobalRendering::SetFullScreen` / `ResolveFullScreenMode` across all combinations of `cliWindowed`, `cliFullScreen`, and `cfgFullScreen`.
  - **Status**: PASSED (23 assertions across 2 test cases in 0.00s).
  - **Covered**:
    - Absolute precedence of `cliWindowed = true` over `cliFullScreen` and stored configuration.
    - Fullscreen enablement via `cliFullScreen = true` when `cliWindowed = false`.
    - Truth-table fallbacks to configuration when no CLI flags are present.
    - Synchronous `configHandler` state updates via `StubConfigHandler`.
- [x] **`test_RenderBuffers`**: Catch2 test suite covering `PlatformShaderProfile` headlessly:
  - **Status**: PASSED (46 assertions across 5 test cases in 0.00s).
  - **Covered**:
    - Null program pointer safety (zero GL calls, no segmentation faults).
    - Column-major matrix multiplication (`projection * modelView`) against known transformation fixtures (identity matrices, affine translation + non-uniform scale).
    - Uniform upload verification (`u_mvpMatrix`, transpose=false).
    - Context predicate validation (`RequiresCoreShaderHeader`).
    - Core vs Compat shader template definitions in `RenderBuffers.inl`.
- [x] **`test_MacTopology`**: Catch2 test suite covering Apple Silicon/Darwin CPU topology and thread pinning:
  - **Status**: PASSED (10 assertions across 2 test cases in 0.00s).
  - **Covered**:
    - `GetThreadPinPolicy()` defaulting to `THREAD_PIN_POLICY_NONE` (delegating thread management to Grand Central Dispatch).
    - `GetProcessorMasks()` exposing all cores as performance cores for GCD scheduling (`performanceCoreMask = ~0u`, `efficiencyCoreMask = 0`).
    - `GetProcessorCache()` returning empty cache descriptors.
    - `ThreadAffinityGuard` RAII constructors and destructors safely no-opping on macOS without errors or exceptions.

- [x] **`test_MouseInput`**: Catch2 test suite covering SDL mouse coordinate translation under Retina High-DPI:
  - **Status**: PASSED (72 assertions across 6 test cases in 0.01s).
  - **Covered**:
    - Direct scaling for 1.0x (standard), 2.0x (Retina), fractional scales (1.25x, 1.5x) with subpixel rounding, and multi-monitor negative coordinates.
    - Delta movement scaling (`xrel, yrel`) for 1.0x, 2.0x, fractional, and zero deltas.
    - Inverse window coordinate transformation (`PixelToWindowCoords`) for SDL mouse warping and round-trip fidelity.
    - Zero/negative scaling safety fallbacks (gracefully defaulting to 1.0f without divide-by-zero).
    - Null `globalRendering` pointer safety fallback.
    - Dynamic synchronization with `CGlobalRendering::pixelScaleX/Y`.
    - SDL event translation pipeline (`SDL_MOUSEMOTION`, `SDL_MOUSEBUTTONDOWN`, `SDL_MOUSEBUTTONUP`) with mock `CMouseHandler` receiving scaled physical pixel coordinates and deltas.

---

## 6. Verification Commands & Results

```bash
# Build specific test binaries
/opt/homebrew/bin/cmake --build build-arm64 --target test_GlobalRendering test_RenderBuffers test_MacTopology test_MouseInput

# Run via CTest
ctest --test-dir build-arm64 -R "(GlobalRendering|RenderBuffers|MacTopology|MouseInput)" --output-on-failure -V

# Run binaries directly with Catch2 verbose output
./build-arm64/test/test_GlobalRendering -s
./build-arm64/test/test_RenderBuffers -s
./build-arm64/test/test_MacTopology -s
./build-arm64/test/test_MouseInput -s
```

### Verified CTest Execution Output
```
1/4 Test #30: testGlobalRendering ..............   Passed    0.01 sec (23 assertions in 2 test cases)
2/4 Test #31: testRenderBuffers ................   Passed    0.00 sec (46 assertions in 5 test cases)
3/4 Test #32: testMacTopology ..................   Passed    0.00 sec (10 assertions in 2 test cases)
4/4 Test #33: testMouseInput ...................   Passed    0.01 sec (72 assertions in 6 test cases)

100% tests passed, 0 tests failed out of 4
Total Test time (real) = 0.03 sec
```
