#!/usr/bin/env bash
#
# Runs pluginval against the INSTALLED VST3 and Audio Unit.
#
#   packaging/run-pluginval.sh
#
# pluginval is the industry's own validator (Tracktion, free, open source). It
# tests things the in-house suites deliberately do not: parameter fuzzing, bus
# layout negotiation, state restore from random data, editor open/close under
# load. Passing our checks and failing this would still mean a plug-in that
# misbehaves in somebody's host.
#
# Strictness 10 is the maximum and is what this ships against.

set -euo pipefail

PV="${PLUGINVAL:-}"
if [ -z "$PV" ]; then
    for c in "/Applications/pluginval.app/Contents/MacOS/pluginval" \
             "$HOME/Applications/pluginval.app/Contents/MacOS/pluginval" \
             "$(command -v pluginval || true)"; do
        [ -n "$c" ] && [ -x "$c" ] && PV="$c" && break
    done
fi

if [ -z "$PV" ]; then
    echo "pluginval not found."
    echo
    echo "  Download the free build from"
    echo "    https://github.com/Tracktion/pluginval/releases/latest"
    echo "  put pluginval.app in /Applications, and run this again."
    echo
    echo "  Or point at it directly:  PLUGINVAL=/path/to/pluginval $0"
    exit 1
fi

echo "using $PV"
"$PV" --version

fail=0
for p in "$HOME/Library/Audio/Plug-Ins/VST3/AFTERHIT.vst3" \
         "$HOME/Library/Audio/Plug-Ins/Components/AFTERHIT.component"; do
    echo
    echo "== $(basename "$p") =="
    if [ ! -e "$p" ]; then
        echo "  NOT INSTALLED - skipping"
        fail=1
        continue
    fi
    #  pluginval prints SUCCESS or FAILED as its last meaningful line. Trust
    #  that rather than the exit code, which is 0 in some failure modes.
    out=$("$PV" --strictness-level 10 --validate "$p" 2>&1) || true
    if printf '%s' "$out" | grep -q "^SUCCESS"; then
        echo "  SUCCESS at strictness 10"
    else
        echo "  FAILED"
        printf '%s\n' "$out" | grep -iE "fail|error|!!!" | head -20
        fail=1
    fi
done

echo
[ "$fail" = "0" ] && echo "both formats pass" || { echo "something did not pass"; exit 1; }
