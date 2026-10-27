#!/usr/bin/env bash
# Builds dcmedit and its unit tests on Linux using a conda-forge environment
# (no root, no vcpkg). Usage: scripts/build_with_conda.sh [env-name]
set -euo pipefail

ENV_NAME="${1:-dcmedit-dev}"
SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$SRC_DIR/build"

source "$(conda info --base)/etc/profile.d/conda.sh"

if ! conda env list | grep -qE "^$ENV_NAME\s"; then
    conda create -y -n "$ENV_NAME" -c conda-forge --override-channels \
        "qt-main=5.15" dcmtk "catch2=2" trompeloeil libgl-devel \
        cmake make cxx-compiler pkg-config
fi
conda activate "$ENV_NAME"

# conda-forge trompeloeil exports trompeloeil::trompeloeil; the project links "trompeloeil" (vcpkg name).
mkdir -p "$BUILD_DIR"
cat > "$BUILD_DIR/conda_compat.cmake" <<'EOF'
find_package(trompeloeil CONFIG REQUIRED)
if(NOT TARGET trompeloeil)
  add_library(trompeloeil ALIAS trompeloeil::trompeloeil)
endif()
EOF

cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" \
    -DCMAKE_PROJECT_INCLUDE="$BUILD_DIR/conda_compat.cmake" \
    -DCMAKE_CXX_FLAGS="-Wall -Wextra -pedantic"
cmake --build "$BUILD_DIR" -j"$(nproc)"

(cd "$BUILD_DIR/test" && QT_QPA_PLATFORM=offscreen ./unit-test)

echo "Built: $BUILD_DIR/dcmedit"
