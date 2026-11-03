"""Build-only byte binding. No deployment/execution/identity operations."""
from pathlib import Path
import hashlib
import sys
image=Path(sys.argv[1]);output=Path(sys.argv[2])
data=image.read_bytes()
if not data.startswith(b'\x7fELF'):
    raise SystemExit('expected actual built ELF')
payload='#pragma once\n#include "goal_base.h"\n#define GOAL_APP_SHA "'+hashlib.sha256(data).hexdigest()+'"\n'
output.write_text(payload,encoding='utf-8')
