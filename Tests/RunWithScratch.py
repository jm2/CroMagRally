"""Run an isolated filesystem test and always clean up its temporary directory."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

parent = os.environ.get("TMPDIR") or (tempfile.gettempdir() if os.name == "nt" else "/var/tmp")
with tempfile.TemporaryDirectory(prefix="cmr-test-", dir=parent) as folder:
    subprocess.run([str(Path(sys.argv[1]).resolve()), folder], check=True)
