#!/bin/bash
# Bootstrap the pinned vcpkg tool for CI.
#
# GitHub's release-asset edge occasionally returns 504 for vcpkg-glibc.  The
# bootstrap script's own `curl --retry 3` is not enough when the edge is
# unhealthy, so fall back to a checksum-verified download through a GitHub
# release mirror.  Both paths use the SHA from scripts/vcpkg-tool-metadata.txt.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "$0")/.." && pwd -P)"
cd "$ROOT"

# shellcheck source=../vcpkg/scripts/vcpkg-tool-metadata.txt
. "$ROOT/vcpkg/scripts/vcpkg-tool-metadata.txt"

if [[ ! -x vcpkg/vcpkg ]] || ! vcpkg/vcpkg version >/dev/null 2>&1; then
    rm -f vcpkg/vcpkg vcpkg/vcpkg.part
    if ! ./vcpkg/bootstrap-vcpkg.sh -disableMetrics; then
        echo "vcpkg bootstrap failed; trying release-asset mirrors..." >&2
        url="https://github.com/microsoft/vcpkg-tool/releases/download/${VCPKG_TOOL_RELEASE_TAG}/vcpkg-glibc"
        tmp="$(mktemp)"
        downloaded=0
        for prefix in \
            "https://gh-proxy.com/" \
            "https://ghproxy.net/" \
            "https://mirror.ghproxy.com/"; do
            echo "  trying ${prefix}${url}" >&2
            if curl -L --fail --retry 3 --retry-delay 5 --tlsv1.2 \
                    --connect-timeout 20 --max-time 300 \
                    "${prefix}${url}" -o "$tmp"; then
                if echo "${VCPKG_GLIBC_SHA}  ${tmp}" | sha512sum -c - >/dev/null; then
                    chmod +x "$tmp"
                    mv "$tmp" vcpkg/vcpkg
                    downloaded=1
                    break
                fi
                echo "  checksum mismatch from ${prefix}" >&2
            fi
        done
        rm -f "$tmp"
        if [[ "$downloaded" -ne 1 ]]; then
            # Release assets can be unavailable through both GitHub and every
            # configured mirror (504 from the release edge).  Git transport and
            # codeload are separate endpoints; build the same pinned tool from
            # source rather than failing the whole matrix.
            echo "release assets unavailable; building vcpkg tool from pinned source..." >&2
            src_dir="$(mktemp -d)"
            trap 'rm -rf "$src_dir"' EXIT
            if ! git clone --depth 1 --branch "$VCPKG_TOOL_RELEASE_TAG" \
                    https://github.com/microsoft/vcpkg-tool.git "$src_dir/src" >&2; then
                echo "unable to obtain vcpkg-glibc (official bootstrap, mirrors, and git source failed)" >&2
                exit 1
            fi
            if ! cmake -S "$src_dir/src" -B "$src_dir/build" -G Ninja \
                    -DCMAKE_BUILD_TYPE=Release \
                    -DVCPKG_DEVELOPMENT_WARNINGS=OFF \
                    -DVCPKG_BUILD_FUZZING=OFF >&2; then
                echo "vcpkg tool source configure failed" >&2
                exit 1
            fi
            if ! cmake --build "$src_dir/build" --target vcpkg -j "${BUILD_JOBS:-2}" >&2; then
                echo "vcpkg tool source build failed" >&2
                exit 1
            fi
            cp "$src_dir/build/vcpkg" vcpkg/vcpkg
            chmod +x vcpkg/vcpkg
            downloaded=1
        fi
    fi
fi

vcpkg/vcpkg version
