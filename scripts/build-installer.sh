#!/bin/bash
# NovaDroid installer stub (Stage 5 TZ 3.1) + payload assembly
set -e
export PATH=/home/z/mingw-root/usr/bin:$PATH
cd /home/z/my-project/novadroid

x86_64-w64-mingw32-windres resources/setup.rc -O coff -o resources/setup_res.o

x86_64-w64-mingw32-g++ -std=c++17 -O2 -municode -mwindows \
    src/v4setup.cpp resources/setup_res.o \
    -o build/NovaDroid-Setup-stub.exe \
    -lshell32 -lshlwapi -lole32 -ladvapi32 -luuid \
    -static -static-libgcc -static-libstdc++ -s

# append payload: launcher exe + size + magic
python3 - << 'EOF'
import struct, os
stub = open('build/NovaDroid-Setup-stub.exe','rb').read()
payload = open('build/NovaDroidLauncher.exe','rb').read()
out = stub + payload + struct.pack('<q', len(payload)) + b'NDSETUP10'
open('build/NovaDroid-0.4-Setup.exe','wb').write(out)
print('installer:', len(out), 'bytes')
EOF

file build/NovaDroid-0.4-Setup.exe
