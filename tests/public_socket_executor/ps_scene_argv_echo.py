"""Real native-process argv oracle; no SDK, HDC or private input."""
import json
import sys

print(json.dumps(sys.argv[1:]))
