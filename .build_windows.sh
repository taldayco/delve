#!/usr/bin/env bash
# Windows build script - can be run from MSYS2 on Windows

set -e

echo "🏗️  Building Windows DLL..."
make -f Makefile.windows.native clean all

if [ -f bin/delve.windows.debug.dll ]; then
	echo "✅ Build successful: bin/delve.windows.debug.dll"
	echo "📦 File size: $(du -h bin/delve.windows.debug.dll | cut -f1)"
else
	echo "❌ Build failed!"
	exit 1
fi
