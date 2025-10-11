#!/usr/bin/env bash
set -e

# Build godot-cpp for both targets (ONLY FOR FIRST TIME CROSS PLATFORM DEVELOPMENT SETUP,
# COMMENT THESE LINES OUT AFTER RUNNING THE SCRIPT ONCE)
#(cd godot-cpp && scons platform=linux target=template_debug bits=64)
#(cd godot-cpp && scons platform=windows target=template_debug bits=64 use_mingw=yes)

# Capture Linux build
bear --output compile_commands.linux.json -- make -f Makefile clean all

# Capture Windows cross build
bear --output compile_commands.windows.json -- make -f Makefile.windows clean all

# Merge compile databases
jq -s 'add' compile_commands.linux.json compile_commands.windows.json >compile_commands.json

echo "✅ Builds complete: bin/delve.linux.debug.so + bin/delve.windows.debug.dll"
