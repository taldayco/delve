#!/usr/bin/env bash
# Quick sync script for testing - syncs source only, not build artifacts

SOURCE_DIRS="src include delve.gdextension project.godot Makefile.windows.native"
DEST="/mnt/c/Users/thoma/Documents/delve"

echo "🔄 Syncing source files to Windows..."

# Only sync source code and configs, not build artifacts
rsync -av --exclude='*.o' --exclude='*.a' --exclude='*.so' --exclude='*.dll' \
	--exclude='bin/' --exclude='godot-cpp/bin/' --exclude='godot-cpp/gen/' \
	$SOURCE_DIRS "$DEST/"

echo "✅ Sync complete! Build on Windows with:"
echo "   cd $DEST && make -f Makefile.windows.native clean all"
