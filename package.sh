#!/bin/bash
set -e
SO="${1:-libSoundPhysicsLite.so}"
OUT="${2:-SPL.levipack}"
test -f "$SO" || { echo "missing $SO"; exit 1; }
rm -rf _pack && mkdir _pack
cp manifest.json _pack/
cp "$SO" _pack/libSoundPhysicsLite.so
cp icon.png _pack/
(cd _pack && zip -r "../$OUT" manifest.json libSoundPhysicsLite.so icon.png)
rm -rf _pack
echo "Created $OUT"
unzip -l "$OUT"
