#!/bin/bash
# NovaDroid v0.2 build script (cross-compile from Linux via mingw-w64)
set -e
export PATH=/home/z/mingw-root/usr/bin:$PATH
export CC=x86_64-w64-mingw32-gcc
export CXX=x86_64-w64-mingw32-g++
cd /home/z/my-project/novadroid

# resources (icon + version info)
x86_64-w64-mingw32-windres resources/app.rc -O coff -o resources/res.o

# compile+link
# PKG0_*: baked PRIMARY direct vikingfile /d/ link of the 0.3 Gaming Core
# package (strict whole-zip SHA-256 verification before extraction).
x86_64-w64-mingw32-g++ -std=c++17 -O2 -municode -mwindows \
    -DPKG0_URL='L"https://vikingfile.com/d/BpeVB2VoJd/NovaDroid-0.3-GamingCore-Package.zip"' \
    -DPKG0_SIZE=1041410801LL \
    -DPKG0_SHA='L"a9eda55f56e30e4c78efcbeb5ad527e78ae3380f6a18446487ab2e4b6151e397"' \
    src/main.cpp \
    src/ui.cpp src/pages.cpp src/dialogs.cpp \
    src/core.cpp src/backend.cpp src/util.cpp src/json.cpp src/strings.cpp \
    src/v2core.cpp src/v2apk.cpp src/v2keymap.cpp src/v2perf.cpp \
    src/v2images.cpp src/v2ui_dlg.cpp src/v2ui_pages.cpp src/v2ui_wizard.cpp \
    src/v3gpu.cpp src/v3input.cpp src/v3crash.cpp src/v3boot.cpp src/v3ui.cpp \
    src/v4compat.cpp src/v4prod.cpp src/v4adv.cpp src/v4game.cpp src/v4ui.cpp src/v4dlg.cpp \
    src/sha256.cpp \
    resources/res.o \
    -o build/NovaDroidLauncher.exe \
    -lgdi32 -lshell32 -lshlwapi -lcomdlg32 -lole32 -loleaut32 -ladvapi32 -ldwmapi \
    -lmsimg32 -lpsapi -lcomctl32 -luuid -luxtheme -lurlmon -lwinhttp \
    -lws2_32 -lgdiplus -lbcrypt -lcrypt32 \
    -static -static-libgcc -static-libstdc++ -s

ls -la build/NovaDroidLauncher.exe
file build/NovaDroidLauncher.exe
