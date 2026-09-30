# Local executor fixtures

These sources preserve the tested local-only INSTALLED_RUNTIME command boundary,
text producers, recovery checks and negative scenarios. They do not implement DDS
or SDK communication and must never be selected by a device contract.

The full-flow orchestration is a version-bound acceptance harness: run.py and
several replay/matrix helpers require the retained task artifacts and the declared
2026-09-29 external build/fixture paths. A clone alone cannot run that historical
flow. Its fixed root names and driver digest deliberately refuse alternate inputs;
they are not a generic permission or device admission interface.

fake-hdc-shim.c is an unchanged source copy of the reviewed r6 local PE shim.
The tested PE digest in the helpers refers to the retained original binary, not
to an arbitrary rebuild of this source. No PE binary, private fixture input,
platform state, claim or device evidence is installed or committed here.

The ordinary contract tests use their own temporary fixtures; driver protocol
and recovery tests require an explicitly supplied local fixture. Missing external
fixtures are NOT_RUN, never device or functional PASS. See the saved acceptance
index under .artifacts/installed-runtime-device/run-20260929-local/.
