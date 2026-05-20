# Beyond All Reason org overview

This workspace is the RecoilEngine repo, which is part of the beyond-all-reason GitHub organization. RecoilEngine is the engine/runtime and is not the game data itself. Game content (BAR or other mods like NOTA) is distributed as Spring archives (.sdz/.sd7/.sdp) and lives in the Spring data directories (for example ~/.spring/games, ~/.spring/maps, ~/.spring/packages, plus build/base and cont in this workspace).

This matters because engine errors like "missing model" usually mean the content archive is incomplete or missing, not that the engine code is wrong. RecoilEngine can run multiple games, but it only sees whatever content archives are present in those data paths.

## Local BAR setup (macOS, manual)

We used the repo-local pr-downloader to install BAR content into the standard Spring data directory.

- Built pr-downloader from tools/pr-downloader with CMake.
- Downloaded BAR via rapid tag: bar:git:8448c1d0faf89f3b883d65f99be36954c2555042.
- Content now lives under ~/.spring/packages and ~/.spring/rapid.

Notes:

- The rapid package is a .sdp file under ~/.spring/packages.
- The engine reads content from ~/.spring/games, ~/.spring/maps, ~/.spring/packages, build/base, and cont.
- If a game fails with missing model files, it usually means the content archive is incomplete or the wrong game was selected in the start script.

## Related repos in the beyond-all-reason org (high level)

These are the common companion repos to RecoilEngine. The names here are the ones you will likely see referenced by tooling or in docs. The exact set can change over time.

- RecoilEngine (this repo): C++ engine, rendering backends, core simulation, networking, and tools integration.
- Game content repo(s): the actual BAR game data (units, models, textures, scripts). This is what provides objects3d/*.s3o, Lua, and definitions used by the engine.
- Maps repo(s): map archives or tooling to fetch maps. Maps are not part of the engine repo.
- pr-downloader: content downloader used by Spring-based games; pulls archives into ~/.spring.
- BAR launcher / lobby tools: installers and launchers that manage engine and content installs (not part of this repo).

If you want, I can extend this file with specific repo names/links once you tell me which BAR game/content repos you want to target.