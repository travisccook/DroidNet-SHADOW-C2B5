"""Print the build flag that defines GIT_REV for the firmware.

Run by PlatformIO through `build_flags = !python3 scripts/git_rev.py` (a dynamic
build flag). The value is the short commit SHA, plus "-dirty" when tracked files
have uncommitted changes, or "unknown" if git is unavailable. SHADOW sends it as
the "fw" field of its WDP Device Announce, so it is limited to 14 characters
(the width of WCB's ?WDP,DA column) and to [0-9a-z-] (WCB does not escape JSON).
"""

import os
import re
import subprocess

MAX_LEN = 14
PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def git(*args):
    return subprocess.check_output(
        ["git", *args], cwd=PROJECT_DIR, stderr=subprocess.DEVNULL, text=True
    ).strip()


def revision():
    try:
        rev = git("rev-parse", "--short=7", "HEAD")
        if git("status", "--porcelain", "--untracked-files=no"):
            rev += "-dirty"
    except (OSError, subprocess.CalledProcessError):
        rev = "unknown"
    rev = re.sub(r"[^0-9a-z-]", "", rev.lower())[:MAX_LEN]
    return rev or "unknown"


if __name__ == "__main__":
    print("'-DGIT_REV=\"%s\"'" % revision())
