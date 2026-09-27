#!/usr/bin/env bash
#
# Downloads the Windows delivery zip from the latest SUCCESSFUL CI run into
# dist/, next to the macOS zip.   packaging/fetch-windows-ci.sh [run-id]
#
# Refuses a run built from a different commit than this tree's HEAD, so dist/
# never pairs a Mac build with a Windows build of other code.

set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="$(sed -n 's/^project(AfterHit VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
NAME="AFTERHIT-$VERSION-Windows-Setup"

RUN="${1:-$(gh run list --workflow windows.yml --status success --limit 1 --json databaseId -q '.[0].databaseId')}"
[ -n "$RUN" ] || { echo "no successful Windows run yet"; exit 1; }
SHA="$(gh run view "$RUN" --json headSha -q .headSha)"
if [ "$SHA" != "$(git rev-parse HEAD)" ] && [ -n "$(git diff --name-only "$SHA" HEAD -- Source CMakeLists.txt packaging .github)" ]; then
    echo "run $RUN was built from $SHA, and the code has changed since - rerun CI first"; exit 1
fi

mkdir -p dist
rm -f "dist/$NAME.zip"
gh run download "$RUN" -n "$NAME" -D dist
ls -lh dist
