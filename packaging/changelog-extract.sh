#!/usr/bin/env bash
#
# Print the CHANGELOG.md section for a version, for use as a GitHub release
# body. Falls back to the [Unreleased] section when the versioned heading
# does not exist yet (the usual case when a tag is pushed before the
# changelog is rewritten).
#
#   packaging/changelog-extract.sh 0.1.0
#
# Env: CHANGELOG (default: CHANGELOG.md)
#
set -euo pipefail

ver="${1:?usage: changelog-extract.sh <version>}"
cl="${CHANGELOG:-CHANGELOG.md}"
[ -f "$cl" ] || { echo "(no $cl)"; exit 0; }

awk -v ver="$ver" '
    /^## / {
        if (capture) exit
        if (index($0, "[" ver "]") || index($0, "[Unreleased]")) { capture = 1; next }
    }
    capture { print }
' "$cl"
