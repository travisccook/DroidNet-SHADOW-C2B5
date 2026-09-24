#!/usr/bin/env python3
"""Prints the build flag that sets SHADOW_FW_VERSION to the git commit being built.

platformio.ini runs it with `build_flags = !python3 scripts/git_rev.py`. The version is the
short commit hash, with "+" appended when tracked files differ from that commit, so a build
from an edited tree can't pass for the commit. It shows in @SM READY and in the WCB device
announce, and main.cpp's static_assert holds it to 11 characters.
"""

import os
import subprocess

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def git(*args):
    try:
        return subprocess.run(["git", *args], cwd=REPO, capture_output=True, text=True, check=False)
    except OSError:  # git isn't installed
        return None


def version():
    rev = git("rev-parse", "--short=7", "HEAD")
    if rev is None or rev.returncode != 0:
        return "nogit"
    ver = rev.stdout.strip()[:10]
    if git("diff", "--quiet", "HEAD", "--").returncode != 0:
        ver += "+"
    return ver


print("-D SHADOW_FW_VERSION='\"%s\"'" % version())
