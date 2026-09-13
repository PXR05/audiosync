"""Run native sync regressions in a disposable workspace and configuration."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="audiosync-sync-") as temp:
    env = dict(os.environ, APPDATA=temp, HOME=temp, XDG_CONFIG_HOME=temp)
    result = subprocess.run([binary], cwd=temp, env=env)
    sys.exit(result.returncode)
