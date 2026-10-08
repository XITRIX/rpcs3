#!/usr/bin/env python3
"""Run shader-semantic range checks against extracted production functions."""
from pathlib import Path
import subprocess
import sys

here = Path(__file__).resolve().parent
subprocess.run([sys.executable, str(here / 'run-minecraft-vertex-tests.py'),
                '--test-source', str(here / 'WRC4VertexRangeTests.cpp'),
                *sys.argv[1:]], check=True)
