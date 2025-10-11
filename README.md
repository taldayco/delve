# Delve - Cross-Platform GDExtension Project (Godot 4.5)

## Development Setup

### Prerequisites
- **Linux/WSL2**: g++, make, scons, bear, jq
- **Windows**: MSYS2 MinGW 64-bit with g++, make

### First-Time Setup

#### 1. Clone with submodules
```bash
git clone --recursive <your-repo-url>
cd delve
```

#### 2. Build godot-cpp (one time per platform)

**On Linux/WSL2:**
```bash
cd godot-cpp
scons platform=linux target=template_debug
scons platform=windows target=template_debug use_mingw=yes  # for cross-compilation
cd ..
```

**On Windows (MSYS2 MinGW 64-bit):**
```bash
cd godot-cpp
scons platform=windows target=template_debug
cd ..
```

### Building

#### Linux Build (WSL2)
```bash
make clean all
```

#### Windows Cross-Build (from Linux/WSL2)
```bash
make -f Makefile.windows clean all
```

#### Windows Native Build (MSYS2 on Windows)
```bash
make -f Makefile.windows.native clean all
```

#### Build Both Platforms with IDE Support (Linux/WSL2)
```bash
./build_all.sh
```

## Cross-Platform Development Workflow

### Option A: Git-based (Recommended for commits)
1. Develop and test on WSL2
2. Commit and push changes
3. Pull on Windows machine
4. Build on Windows: `./build_windows.sh`
5. Test in Godot

### Option B: Quick Sync (for rapid iteration)
1. Develop on WSL2
2. Run `./sync_to_windows.sh`
3. Build on Windows: `./build_windows.sh`
4. Test in Godot

## Project Structure
```
delve/
├── src/                    # C++ source files
│   ├── map/               # Map generation system
│   └── register_types.cpp
├── include/               # Header files
│   ├── map/
│   └── rng_manager.hpp
├── bin/                   # Compiled binaries (gitignored)
├── godot-cpp/            # Godot C++ bindings (submodule)
├── Makefile              # Linux build
├── Makefile.windows      # Windows cross-compile (from Linux)
├── Makefile.windows.native # Windows native build
└── delve.gdextension     # GDExtension config
```

## Class Overview

### MapGenerator
Node-based procedural map generator that creates a grid-based dungeon map.

**Key Features:**
- Configurable grid dimensions (height/width)
- Randomized node placement with offset
- Weighted random node type assignment
- Boss node on final floor

**Configuration:**
- `MAP_HEIGHT`: Number of floors (default: 15)
- `MAP_WIDTH`: Number of columns (default: 7)
- `X_DIST` / `Y_DIST`: Node spacing
- `PLACEMENT_RANDOMNESS`: Position variance
- Node type weights (ENEMY, WENNY, SHELTER)

### MapNode
Resource-based node representing a single map location.

**Properties:**
- `type`: Node type enum (NOT_ASSIGNED, ENEMY, LOOT, SHELTER, WENNY, BOSS)
- `row` / `column`: Grid position
- `position`: Vector2 world position
- `next_nodes`: Array of connected nodes
- `selected`: Current selection state

### RNGManager
Singleton random number generator wrapper.

**API:**
- `get()`: Access singleton instance
- `randomize()`: Randomize seed
- `set_seed(uint64_t)`: Set specific seed
- `randf()`: Random float [0, 1)
- `randf_range(a, b)`: Random float in range
- `randi_range(a, b)`: Random int in range

## Troubleshooting

### Windows build fails with "g++: command not found"
- Make sure you're in **MSYS2 MinGW 64-bit** terminal (not MSYS2 MSYS)
- Terminal prompt should show `MINGW64`
- Install toolchain: `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make`

### Missing godot-cpp library
- Build godot-cpp first (see First-Time Setup)
- Check that `godot-cpp/bin/` contains the platform-specific `.a` file
- Linux: `libgodot-cpp.linux.template_debug.x86_64.a`
- Windows: `libgodot-cpp.windows.template_debug.x86_64.a`

### Godot can't find extension
- Verify DLL/SO exists in `bin/` directory
- Check `delve.gdextension` paths match your file names
- Restart Godot after building new binaries
- Ensure entry_symbol matches: `map_library_init`

### Cross-compilation "Argument list too long" error
- This is a Windows command line length limitation
- Build godot-cpp natively on each platform instead
- Use `Makefile.windows.native` on Windows rather than cross-compiling

### Changes not reflected in Godot
- Rebuild the extension
- Close and reopen Godot (hot reload doesn't always work)
- Check Godot console for loading errors
- Verify the correct build type is being loaded (debug vs release)

## Tips & Best Practices

### Development
- Use WSL2 as primary development environment
- Keep compile_commands.json updated for IDE autocomplete
- Run `./build_all.sh` before committing to test both platforms
- Use `UtilityFunctions::print()` for debugging output in Godot

### Building
- Only build godot-cpp once per platform (takes ~5 minutes)
- Use `make clean` if you change Makefiles or includes
- Object files (`.o`) and libraries (`.a`, `.dll`, `.so`) are gitignored
- Each developer needs to build godot-cpp on their own machine

### Git Workflow
- Never commit build artifacts (`bin/`, `godot-cpp/bin/`, `*.o`, etc.)
- Submodule `godot-cpp` is tracked, but its build outputs are not
- Use branches for features, test on both platforms before merging
- Keep Windows and Linux binaries in sync with code changes

## Performance Notes

### Build Times (approximate)
- godot-cpp initial build: 3-5 minutes
- Full extension rebuild: 10-30 seconds
- Incremental rebuild: 2-5 seconds

### Optimization
- Debug builds are larger and slower but have symbols for debugging
- Release builds: Change `BUILD_TYPE := release` in Makefile
- Use `-O2` or `-O3` flags in CXXFLAGS for release builds

## Future Enhancements

- [ ] Release build configurations
- [ ] Automated testing
- [ ] CI/CD pipeline (GitHub Actions)
- [ ] Pre-built binaries for releases
- [ ] Complete map generation algorithm implementation
- [ ] Path generation between nodes
- [ ] Node type assignment logic

## Credits

Built with [Godot Engine](https://godotengine.org/) and [godot-cpp](https://github.com/godotengine/godot-cpp).
