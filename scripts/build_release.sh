#!/usr/bin/env bash
# Builds the release firmware from a clean checkout of a commit, where include/controller_macs.h
# doesn't exist, and checks that none of this machine's controller addresses got into it.
#
#   scripts/build_release.sh [<commit>]        (default: HEAD)
#
# Writes build/release/SHADOWC2B5_<short commit>.hex and prints its path. Attach that file to the
# GitHub Release for the commit (see README, "Put the firmware on SHADOW").
set -euo pipefail

root="$(git -C "$(dirname "$0")/.." rev-parse --show-toplevel)"
commit="$(git -C "$root" rev-parse --short "${1:-HEAD}")"
pio="${PIO:-$HOME/.platformio/penv/bin/pio}"
work="$(mktemp -d)"
cleanup() {
    git -C "$root" worktree remove --force "$work/tree" >/dev/null 2>&1 || true
    rm -rf "$work"
}
trap cleanup EXIT

git -C "$root" worktree add --detach "$work/tree" "$commit" >/dev/null
if [ -e "$work/tree/include/controller_macs.h" ]; then
    echo "build_release: include/controller_macs.h is committed at $commit; refusing" >&2
    exit 1
fi
(cd "$work/tree" && "$pio" run -e SHADOWC2B5 >"$work/build.log" 2>&1) || {
    tail -30 "$work/build.log" >&2
    exit 1
}

mkdir -p "$root/build/release"
out="$root/build/release/SHADOWC2B5_${commit}.hex"
cp "$work/tree/.pio/build/SHADOWC2B5/firmware.hex" "$out"

check=(python3 "$root/scripts/check_release_hex.py" "$out")
if [ -f "$root/include/controller_macs.h" ]; then
    check+=(--macs-from "$root/include/controller_macs.h")
fi
"${check[@]}"
echo "$out"
