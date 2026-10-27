#!/usr/bin/env bash
# Builds a .deb for Ubuntu 24.04 / Linux Mint 22 inside Docker, linked against the distro's Qt5 and DCMTK.
# Usage: scripts/package_deb.sh [output-dir]   (default: dist/)
set -euo pipefail

SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$(realpath -m "${1:-$SRC_DIR/dist}")"
CONTACT="$(git -C "$SRC_DIR" config user.name) <$(git -C "$SRC_DIR" config user.email)>"
mkdir -p "$OUT_DIR"

docker run --rm -i \
    -v "$SRC_DIR:/src:ro" -v "$OUT_DIR:/out" \
    -e CONTACT="$CONTACT" -e OWNER="$(id -u):$(id -g)" \
    ubuntu:24.04 bash -euo pipefail <<'IN_CONTAINER'
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    build-essential cmake git python3 ca-certificates dpkg-dev file \
    qtbase5-dev libqt5svg5 libdcmtk-dev > /dev/null
git config --global --add safe.directory '*'

mkdir /work
tar -C /src --exclude=./build --exclude=./dist -cf - . | tar -C /work -xf -
cd /work
python3 scripts/generate_app_info.py
VERSION="$(python3 scripts/get_version.py | tr '-' '+')"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DDCMEDIT_BUILD_TESTS=OFF \
    -DCPACK_PACKAGE_VERSION="$VERSION" -DCPACK_PACKAGE_CONTACT="$CONTACT"
cmake --build build -j"$(nproc)"
(cd build && cpack -G DEB)

cp build/*.deb /out/
chown "$OWNER" /out/*.deb
IN_CONTAINER

ls -l "$OUT_DIR"/*.deb
