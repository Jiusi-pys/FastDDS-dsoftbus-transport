# Windows process identity: exact source and retained handle

`scripts/windows_process_identity.py` is a read-only host metadata library. Import performs no process query or Win32 DLL load. `Win32ProcessMetadata` queries only an already legally retained caller-supplied process handle; it does not call OpenProcess, look up a process by PID, terminate a process, or close that process handle. It temporarily opens and closes a query token for owner/session metadata. The caller must prevent concurrent close/reuse of its process handle throughout capture/verification.

`capture_native_identity` reads PID/native creation time twice around image/owner/session queries through the same retained handle and rejects changed identity. `verify_bound_handle` first validates record structure/source, then requires exact full-identity equality. `expected_for_stop_core` is a strict data adapter, not permission to stop anything. No function confers approval, ownership, signal rights or once-budget authority.

The record format `WIN32_PROCESS_IDENTITY_V1` declares source `win32.GetProcessTimes.lpCreationTime`, unit `100ns`, epoch `1601-01-01T00:00:00Z`, and serializes creation time as lossless decimal text. Combining FILETIME high/low words and comparing uses integers, never float/datetime. `resolution_100ns=1` means the raw representation loses no 100ns units; it is not a claim about physical clock resolution or accuracy. A source field is a format/provenance assertion by its producer, not independent proof that Windows supplied the value. Untrusted records still require a genuine retained-handle capture and separate authority checks.

CIM's six-fractional-digit CreationDate is auxiliary only. It must not be promoted to exact native execution identity, padded into apparent precision, accepted with epsilon, or used to replace an approved timestamp. A one-tick mismatch even in the same microsecond bucket rejects. Unknown/non-native source, unknown fields, owner/image/session differences and unstable handle identity reject rather than guessing.

## Verified boundary

The independent final reports six canonical tests plus two offline bridge tests, both native/outer exit0. The canonical round creates one owned CPython child and uses the fixed System32 PowerShell only for that child's ProcessId/CreationDate query; cleanup uses its original Popen ownership. Initial native-minus-CIM sample was seven ticks; final single sample was three ticks, with native birth stable before/after and terminal. These observations demonstrate precision loss for those samples only, not the cause of the older HDC four-tick mismatch or proof that the old PID was the same process. No real HDC target is rebound here. Do not change the old580 to584 or relax exact comparison.

Actual test commands (already executed by the tester, not automatically run by this document):

```text
C:\Users\17715\anaconda3\python.exe -I -B C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\tests\test_windows_process_identity.py
C:\Users\17715\anaconda3\python.exe -I -B E:\Codex-FastDDS-process-birth-binding-20261031-d01\test_successor_binding.py
```

Environment: installed 64-bit Anaconda Python3.13.9, `-I -B`; fixture evidence and TEMP/TMP were under `E:/Codex-FastDDS-process-birth-binding-20261031-d01`, using final_fixture for the final round. Old import FAIL, E-only explicit sys.path repair and two Fake bridge retests remain retained. The E bridge is Fake wiring, not a new actual executor or approval. No C#/Native/Transport build or installation occurred.

This independent three-file host-only task has no HDC/DSoftBus/DDS or OpenHarmony Native/SDK business calls and no runtime transport changes. It does use the Win32 native metadata APIs described above. Device, Native and DDS integration tests are N/A for this scope, not replaced by host PASS. Formal review reports no findings; distinct final is bound to current source. Overall Stage1 remains NOT_VERIFIED and all37 Stage2 HELD. The historical global Transport failure does not itself prohibit an otherwise gated independent host-only commit. Scoped documentation Ready YES is conditional on Main's remaining graph/pre-commit checks and exact three-file staging; GitNexus UNKNOWN/partial results still require resolution. No commit/push had been claimed at the documentation evidence snapshot; later Main publication receipts are separate. Future real-target native rebinding is a separate approved plan, never an instruction to query or stop the old HDC instance.

Evidence: `E:/Codex-FastDDS-process-birth-binding-20261031-d01/FINAL_TEST_RESULT_D01.json`, SHA256 ef0f7331f6d0c814f36d910bffb325bdf69195c29c0a61d18e44fe84c979fbf0. Source SHA25622c3c3223d912c11b53ec2fcb013cfd78ac365f7b8c93a548b24c71b0d41f68d; test SHA25635d2929609c03a2e435ea626312ce78996a8a6b4d8870f0964b11dc28c11189d.
