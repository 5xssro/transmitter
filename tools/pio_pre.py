# PlatformIO: generate build_info_generated.h before build
Import("env")

import subprocess
import sys
from pathlib import Path

project_dir = Path(env["PROJECT_DIR"])
script = project_dir / "tools" / "gen_build_info.py"
subprocess.check_call([sys.executable, str(script), str(project_dir)])
