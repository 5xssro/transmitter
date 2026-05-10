# PlatformIO: git SHA + GitHub repo for OTA (no committed generated header)
Import("env")

import re
import subprocess
import sys
from pathlib import Path


def git_txt(cwd: Path, *args: str) -> str:
    try:
        r = subprocess.run(
            ["git", "-C", str(cwd), *args],
            capture_output=True,
            text=True,
            timeout=12,
            check=False,
        )
        return (r.stdout or "").strip() if r.returncode == 0 else ""
    except OSError:
        return ""


def parse_github_remote(url: str) -> tuple[str, str]:
    if not url:
        return ("5xssro", "transmitter")
    m = re.search(r"github\.com[:/]([^/]+)/([^/.]+)", url)
    if m:
        return (m.group(1), m.group(2))
    return ("5xssro", "transmitter")


project_dir = Path(env["PROJECT_DIR"])
sha = git_txt(project_dir, "rev-parse", "HEAD")
if len(sha) != 40:
    sha = "0" * 40

remote = git_txt(project_dir, "config", "--get", "remote.origin.url")
owner, repo = parse_github_remote(remote)

# GCC -D "MACRO=\"value\""
env.Append(
    CPPDEFINES=[
        ("FIRMWARE_GIT_SHA_FULL", '\\"' + sha + '\\"'),
        ("GITHUB_RELEASE_OWNER", '\\"' + owner + '\\"'),
        ("GITHUB_RELEASE_REPO", '\\"' + repo + '\\"'),
    ]
)
