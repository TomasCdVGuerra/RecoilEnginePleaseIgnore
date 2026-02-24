# Recoil is an open source real time strategy game engine

Visit the [Official Website](https://recoilengine.org)

## Get the engine sources

    git clone https://github.com/beyond-all-reason/RecoilEngine --recursive

Recoil is a fork and continuation of an RTS [engine](https://github.com/spring/spring) version 105.0

Visit our [Discord](https://discord.gg/GUpRg6Wz3e) for help, suggestions, bugs, community forum and everything Recoil related.

## Installation

You can use a pre-compiled binary, usually, you want to use an installer or a package prepared for your OS:

* <https://github.com/beyond-all-reason/RecoilEngine/releases>


## Compiling

### macOS (Apple Clang / Xcode)

#### Prerequisites

* **Xcode Command Line Tools** (provides Apple Clang and standard system headers)
  ```bash
  xcode-select --install
  ```
* **CMake ≥ 3.27**
* **Homebrew** package manager (<https://brew.sh>)

#### Install build dependencies

```bash
brew install cmake sdl2 devil freetype zlib
```

#### Clone (including submodules)

```bash
git clone https://github.com/beyond-all-reason/RecoilEngine --recursive
cd RecoilEngine
```

If you already cloned without `--recursive`, initialise the submodules:

```bash
git submodule update --init --recursive
```

#### Build (headless engine)

The *headless* target is the recommended starting point on macOS.  It
omits the full graphics stack and avoids dependencies on X11/XQuartz that
are not typically installed.

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_spring-legacy=OFF \
  -DAI_TYPES=NONE \
  -DENABLE_STREFLOP=OFF

cmake --build build --target engine-headless --parallel $(sysctl -n hw.logicalcpu)
```

The resulting binary is at `build/spring-headless`.

#### Run the smoke test

```bash
cmake --build build --target engine_smoketest
ctest --test-dir build --output-on-failure -R smoketest
```

#### Full legacy (graphical) engine build

The full engine additionally requires X11/XCursor (available via
[XQuartz](https://www.xquartz.org)) and Fontconfig:

```bash
brew install fontconfig expat
# Install XQuartz from https://www.xquartz.org, then:
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAI_TYPES=NONE \
  -DENABLE_STREFLOP=OFF

cmake --build build --target engine-legacy --parallel $(sysctl -n hw.logicalcpu)
```

---

### Linux / Docker (recommended for production builds)

Start with `master` as the primary branch.

Verify you're seeing tags:

```bash
>>> git tag
spring_bar_{BAR105}105.0-430-g2727993
spring_bar_{BAR105}105.1.1-1005-ga7ea1cc
spring_bar_{BAR105}105.1.1-1011-g325620e
spring_bar_{BAR105}105.1.1-1032-gf4d6126
spring_bar_{BAR105}105.1.1-1039-g895d540
spring_bar_{BAR105}105.1.1-1050-g5075cc0
...
```

If you aren't seeing these (often, when you've cloned your fork of the repository and not the upstream version), try the following:

```bash
git remote add upstream https://github.com/beyond-all-reason/RecoilEngine
git fetch --all --tags
```

Make sure `master` is pointing to upstream `master`:

```bash
git checkout master
git branch -u upstream/master
```

### Triggering a build

If you are just starting out and want to get an engine binary, we recommend using our Docker scripts documented in [docker-build-v2/](docker-build-v2/README.md).

If you want to compile the engine without Docker to use a different compiler, to have a better setup with code completion in an IDE, etc., you might want to follow the [building without Docker article](https://recoilengine.org/development/building-without-docker/).

## License

Our Terms are documented in the [LICENSE](LICENSE).

## AI Policy usage

Please adhere to the [AI usage policy](https://github.com/beyond-all-reason/RecoilEngine/blob/master/AI_POLICY.md), if you use such tools.
