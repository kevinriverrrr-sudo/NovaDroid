#!/usr/bin/env python3
# NovaDroid 0.4 full package builder: walks pkgstage/, writes package.json
# (SHA-256 per file) and zips to build/NovaDroid-0.4-UniversalPackage.zip.
# The launcher exe is NOT part of the package (shipped separately; the
# bootstrap skips it during extraction - v3boot.cpp).
import os, json, hashlib, zipfile, sys

ROOT = '/home/z/my-project/novadroid/pkgstage'
BUILD = '/home/z/my-project/novadroid/build'
NAME = 'NovaDroid-0.4-UniversalPackage'

def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        while True:
            b = f.read(1 << 20)
            if not b: break
            h.update(b)
    return h.hexdigest()

files = []
for dirpath, dirnames, filenames in os.walk(ROOT):
    for fn in filenames:
        full = os.path.join(dirpath, fn)
        rel = os.path.relpath(full, ROOT).replace('\\', '/')
        if rel == 'package.json':
            continue
        if rel.endswith('.apk'):          # listed but flagged (info only)
            pass
        files.append({
            'path': rel,
            'size': os.path.getsize(full),
            'sha256': sha256(full),
            'critical': rel.startswith(('qemu/', 'adb/', 'images/')),
            'kind': 'app' if rel.startswith('apps/') else ('gl' if 'opengl' in fn or 'gallium' in fn or fn in ('dxil.dll',) else 'runtime'),
        })

pkg = {
    'formatVersion': 1,
    'name': NAME,
    'version': '0.4.0',
    'channel': 'stable',
    'description': 'QEMU 10 + WHPX/virgl, ADB platform-tools, Android-x86 9.0-r2 x86_64 image, Mesa OpenGL runtime (opengl32/libgallium_wgl/d3d12), base apps (F-Droid, APKPure), docs',
    'files': files,
}
with open(os.path.join(ROOT, 'package.json'), 'w', encoding='utf-8') as f:
    json.dump(pkg, f, indent=2)

total = sum(f['size'] for f in files)
print(f'package.json: {len(files)} files, payload {total/1e6:.1f} MB')

zpath = os.path.join(BUILD, NAME + '.zip')
if os.path.exists(zpath):
    os.remove(zpath)
with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_STORED) as z:   # STORED: payload mostly incompressible
    for dirpath, dirnames, filenames in os.walk(ROOT):
        for fn in sorted(filenames):
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, ROOT)
            z.write(full, rel)
zsize = os.path.getsize(zpath)
print('zip:', zsize, 'bytes')
print('zip sha256:', sha256(zpath))
