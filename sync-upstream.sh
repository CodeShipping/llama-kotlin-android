#!/bin/bash
# sync-upstream.sh — Update llama.cpp submodule and check for breakage
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SUBMODULE_DIR="$SCRIPT_DIR/app/src/main/cpp/llama.cpp"

echo "🦙 llama-kotlin-android: Upstream Sync"
echo "======================================="

# Get current state
cd "$SUBMODULE_DIR"
CURRENT_SHA=$(git rev-parse HEAD)
CURRENT_SHORT=$(git rev-parse --short HEAD)

# Fetch latest
git fetch origin --tags
LATEST_TAG=$(git tag --sort=-version:refname | grep -E '^b[0-9]+$' | head -1)
LATEST_SHA=$(git rev-parse "refs/tags/$LATEST_TAG")
LATEST_SHORT=$(git rev-parse --short "$LATEST_SHA")

if [ "$CURRENT_SHA" = "$LATEST_SHA" ]; then
    echo "✅ Already up to date at $LATEST_TAG ($CURRENT_SHORT)"
    exit 0
fi

echo "📦 Current: $CURRENT_SHORT"
echo "📦 Latest:  $LATEST_TAG ($LATEST_SHORT)"
echo ""

# Show relevant API changes
echo "🔍 Checking for API changes in llama.h..."
DIFF=$(git diff "$CURRENT_SHA" "$LATEST_SHA" -- include/llama.h include/llama-chat.h 2>/dev/null || true)

if [ -n "$DIFF" ]; then
    # Extract functions used in our JNI
    cd "$SCRIPT_DIR"
    USED_FUNCS=$(grep -ohP 'llama_\w+|ggml_\w+' app/src/main/cpp/llama_jni.cpp app/src/main/cpp/llama_context_wrapper.cpp 2>/dev/null | sort -u)

    echo ""
    echo "⚠️  API changes detected. Checking functions we use:"
    FOUND_BREAKING=false
    for func in $USED_FUNCS; do
        if echo "$DIFF" | grep -q "^-.*$func"; then
            echo "  ❌ $func — may have changed/removed"
            FOUND_BREAKING=true
        fi
    done

    if [ "$FOUND_BREAKING" = false ]; then
        echo "  ✅ No breaking changes to functions we use"
    fi
else
    echo "  ✅ No API header changes"
fi

echo ""

# Ask to proceed
if [ "${1}" != "--yes" ]; then
    read -p "Update submodule to $LATEST_TAG? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Aborted."
        exit 0
    fi
fi

# Update submodule
echo ""
echo "📥 Updating submodule..."
cd "$SUBMODULE_DIR"
git checkout "$LATEST_SHA"

cd "$SCRIPT_DIR"
echo ""
echo "🔨 Attempting build..."
if ./gradlew :app:assembleRelease 2>&1 | tee /tmp/llama-build.log | tail -5; then
    echo ""
    echo "✅ Build succeeded! Ready to commit:"
    echo "   git add app/src/main/cpp/llama.cpp"
    echo "   git commit -m 'chore: update llama.cpp to $LATEST_TAG'"
else
    echo ""
    echo "❌ Build failed. Check /tmp/llama-build.log"
    echo ""
    echo "Common fixes:"
    echo "  1. Check if llama.h function signatures changed"
    echo "  2. Check if struct members were renamed"
    echo "  3. Check the changelog: https://github.com/ggml-org/llama.cpp/releases/tag/$LATEST_TAG"
    echo ""
    echo "To revert: cd app/src/main/cpp/llama.cpp && git checkout $CURRENT_SHA"
    exit 1
fi
