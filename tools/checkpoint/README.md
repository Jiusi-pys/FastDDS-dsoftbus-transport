# Explicit checkpoint inputs

These tools are preserved from the reviewed local restoration work package; they do not deploy to devices, grant permission, install tools, or modify Git state. `checkpoint_tool.py` is byte-identical to the retained bounded restorer. `checkpoint_build.py` validates that helper's recorded hash before loading it. Do not edit one independently and silently bypass that check.

Use `python tools/checkpoint/checkpoint_tool.py --help` or `python tools/checkpoint/checkpoint_build.py --help` for the existing commands. Checkpoint bundles, dependency configurations, SDK/platform libraries and historical receipts are external inputs, not included repository data. The historical builder's configuration schema remains version-bound; this promotion is not a claim that arbitrary future SDKs or unreviewed recipes work.

The retained test suite contains 23 self-contained contracts and eight pinned-declaration integration contracts. The latter require an explicit absolute `MDDS_CHECKPOINT_TEST_DECLARATIONS` path to the matching external `BUILD_DEPENDENCIES_V4.json` (SHA-256 is recorded in the candidate preparation receipt), including its declared local dependencies and sealed checkpoint data. Missing configuration fails explicitly; no tests are silently skipped. No historical Python code is imported by this test: the entry and helper come from this directory; external files are verification data.

```text
python -m unittest discover -s tools/checkpoint/tests -p test_checkpoint_build.py
```

Set the fixture environment variable before running that integration suite. It is not needed to build the project or run the root Host CTest suite. A clone does not provide the local external fixture. The original historical suite and all former receipts remain unchanged outside Git.

The additional native source-receipt tests are `tests/restored_native_provenance_tests.ps1`; they exercise the actual builder's provenance block using test boundaries and do not invoke SDK/compiler/device operations.
