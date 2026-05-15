#!/bin/bash
# Copy cucumber-cpp-runner build artifacts to staging directory
# Called by ExternalProject_Add INSTALL_COMMAND
set -e
SRC="$1"
BIN="$2"
STAGE="$3"

# Copy headers (preserve full directory structure)
mkdir -p "$STAGE/include"
find "$SRC/cucumber_cpp" -name "*.hpp" | while read f; do
    rel="${f#$SRC/}"
    dir=$(dirname "$STAGE/include/$rel")
    mkdir -p "$dir"
    cp "$f" "$STAGE/include/$rel"
done 2>/dev/null || true

# Copy dependency headers (fmt, nlohmann_json, pugixml, CLI11, base64, gherkin, messages)
for dep in libfmt-src nlohmann_json-src pugixml-src cli11-src; do
    src_dir="$BIN/_deps/$dep"
    [ -d "$src_dir" ] || continue
    case "$dep" in
        libfmt-src)       cp -r "$src_dir/include/fmt" "$STAGE/include/" 2>/dev/null || true ;;
        nlohmann_json-src) cp -r "$src_dir/include/nlohmann" "$STAGE/include/" 2>/dev/null || true ;;
        pugixml-src)      cp "$src_dir/src/"*.hpp "$STAGE/include/" 2>/dev/null || true ;;
        cli11-src)        cp -r "$src_dir/include/CLI" "$STAGE/include/" 2>/dev/null || true ;;
    esac
done

# Copy gherkin headers
[ -d "$BIN/_deps/cucumber_gherkin-src/cpp/include/gherkin" ] && \
    cp -r "$BIN/_deps/cucumber_gherkin-src/cpp/include/gherkin" "$STAGE/include/"

# Copy messages headers
[ -d "$BIN/_deps/cucumber_messages-src/cpp/include/messages" ] && \
    cp -r "$BIN/_deps/cucumber_messages-src/cpp/include/messages" "$STAGE/include/"

# Copy base64 headers (header-only library)
[ -d "$BIN/external/tobiaslocker/base64/include/base64" ] && \
    cp -r "$BIN/external/tobiaslocker/base64/include/base64" "$STAGE/include/"

# Copy static libraries
mkdir -p "$STAGE/lib"
find "$BIN" -name "*.a" -exec cp {} "$STAGE/lib/" \; 2>/dev/null || true
echo "CCR artifacts copied to $STAGE"
