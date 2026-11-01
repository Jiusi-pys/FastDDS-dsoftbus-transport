# HDC Goal cleanup evidence

## Maintained scope

`scripts/hdc_goal/goal_windows_backend.py` and `goal_driver.py` are maintained successors of two frozen readiness sources. They retain standalone flat `goal_*` dependency names so a future qualified bundle can replace these files and supply the remaining dependencies. This directory is not a directly runnable actual executor. No new schema, approval, registry or upper-layer DDS behavior is introduced. Existing frozen packages, nonces, once records and actual results remain unpatched and are not reset.

The source-binding report compares ASTs: `observe_all`, `notice_gate`, `outcome`, `start`, `pump` and other unaffected methods remain unchanged. Only the bounded cleanup/finalization changes are claimed here. The two maintained modules and `tests/test_hdc_goal_cleanup.py`, plus this document, form an independent four-file host-tool scope.

## Separate root cleanup from business policy

| Evidence | Meaning |
| --- | --- |
| Root terminal binding, closed original handle, EOF and existing drain-status condition | Determines whether root cleanup can be reported terminal; EOF alone never waives a failed drain/deadline/identity check. |
| Business notice blocker | Remains independently recorded; a client UNKNOWN still rejects A and prevents B from starting. It does not erase separately observed root exit/EOF facts. |
| Marker/clock/encoding/record/close or other cleanup-operation failure | Remains UNKNOWN, never silently converted into clean shutdown. |

Cleanup still uses only existing owned-handle authority. Observer-before-capture and bounded stop/drain rules are retained. Marker EOF/advance, root close and RESULT write are independently attempted and failures retained. Finalization detaches the pin list and attempts each pin close without letting one failure skip the rest; record and close errors are not swallowed. `finish` is idempotent: repeated finalization does not retry actions or reuse closed handles. Business acceptance policy is not relaxed by this separation.

## Execution evidence and limits

Independent initial eight tests, formal two-P2 review, affected three-test retest/closure, then distinct final ten tests passed with native/outer exit0. Tests import the actual two maintained modules with pure contract/FakeRoot seams and exercise the real gate -> halt -> cleanup chain. They do not construct a live backend, load Win32 DLLs, create/signals processes, invoke HDC or verify Native/SDK/DDS behavior. Device/SDK/DDS tests are N/A for this host-only scope, because those actions were prohibited and production Transport code was not changed; host success is not a device substitute.

Final evidence: `E:/Codex-FastDDS-cleanup-evidence-repair-20261102-d01/NEW_FINAL_TEST_RESULT_D01.json`, SHA256 `b4b9a428bcffd9c281101a2285de76be1f23e1b63f109926db76fdef5dec0b08`. Source/AST binding and prior failures remain in that evidence directory. This documentation task did not rerun tests.

Separate passive evidence at 2026-11-01T16:09:27Z attempted only QUERY_LIMITED_INFORMATION|SYNCHRONIZE on the two old PIDs and observed OpenProcess87/unknown. Error87 does not prove exit; original receipts separately establish local125/EOF. The 8710 query had no rows and HRESULT -2146233087; numeric-parent child metadata was empty and grants no ownership. Neither result proves full-tree, USB or remote quiet.

## Remaining diagnostic gap

Current allowed metadata cannot identify whether AUTH_CONTEXT_UNKNOWN came from an auth-word family, an `[e000` prefix, or both. A future explicitly approved live first-client UNKNOWN collector could add only an enum `unknown_trigger_family` with `AUTH_WORD_ONLY`, `ECODE_PREFIX_ONLY` or `BOTH`, without text, fragments, hashes or reversible encoding. That collector/binding and single execution approval are not implemented or requested here. No user viewer or original text is needed for this proposed minimum.

The old readiness seal `2a7b05a287c0243d30ba77bc1314c5f6ee6ccc1c8edacb4b4e6e6a668ea1dcb3` remains historical and unchanged. Prior failures are not relabelled PASS. Overall Stage1 remains NOT_VERIFIED and all37 Stage2 HELD. This independent host-only unit may satisfy its scoped commit gate; documentation Ready YES does not bypass Main's exact four-file staging, graph/manual evidence, pre-commit checks or publication. Graph ALL HIGH59/24 concerns existing dirty files; new-symbol coverage remains UNKNOWN, supplemented by manual source/caller/AST/test evidence, never labelled LOW. No commit or push is claimed by this document.
