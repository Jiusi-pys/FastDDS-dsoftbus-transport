# Route lifecycle native acceptance — 2026-10-06

## Status

This is the concise evidence index for the bounded native route-lifecycle
acceptance based on baseline `eea2759cac4064f065a5995b2ac262acb9b5fde6`.
Implementation is present. The final tester reports `overall=PASS` and
`commit_gate=PASS`; the final source review reports `PASS` with no blocking
findings. This is scoped acceptance evidence, not production qualification.

## Device feature evidence

The independent raw feature receipt is
`.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002/iterations/iteration-40/windows/data-23/INDEPENDENT_DEVICE_FEATURE_ACCEPTANCE.json`,
run `5dacd43f25fe2cbd`, SHA-256
`2e9017e0d590795017eff731af89b147f13cc50af9278eb64ddf028bcf105a66`.
It records genuine SDK registration and `OnNodeOffline`/`OnNodeOnline`
callbacks, fresh ordered `GetAllNodeDeviceInfo`/`GetNodeKeyInfo` resolution,
revision 2 unavailable and revision 3 available, four attempted new writes
refused by the product route admission gate before any SDK call during
OFFLINE, and post-online delivery. PRE was 8
writes/8 takes; the OFFLINE window separately recorded 2 late-history takes;
POST was 12 writes/12 takes. Original Runtime, Participant, ROS context entities,
GUIDs, V1 locator and listeners were retained; public delete, Runtime close
and application return were zero.

The three scoped device statuses are:

- `OFFLINE_ROUTE_INVALIDATION_DEVICE_VERIFIED`
- `ONLINE_ROUTE_RERESOLUTION_DEVICE_VERIFIED`
- `ROS2_ROUTE_RETURN_CONTINUITY_VERIFIED`

The device network identity remained the same. Fresh resolver/query evidence
is valid, but this run does not establish changed-network device behavior.

## Build, host, and review evidence

The final tester receipt is
`build/route-lifecycle-native-20261006/final-tester/FINAL_TESTER_RESULT_R2.json`.
Current local batches passed: PowerShell/Python argv 8, route capture 16,
route contract 4, route analysis 7, preparation 1, strict load 11, V1 window
configuration 6, and materialization 4. Immutable data-23 raw reanalysis and
the reused C++/ASan/API23 evidence were hash-checked; no SDK command was rerun
in the final phase. The API23 package receipt contains 109 members: 107
dependency hashes were reused, while the bridge DSO and ordinary ROS consumer
were rebuilt. Package compile/link evidence and the separate runtime evidence
above are kept distinct.

The postrun source review is
`build/route-lifecycle-native-20261006/FINAL_SOURCE_POSTRUN_REVIEW.json` and
is `PASS`, with graph scope recorded as 137 changed symbols, 40 files and 11
affected flows at retained HIGH risk. Dynamic PowerShell/helper paths remain
partially unresolved by the graph and were checked with direct source and raw
receipts.

## Boundaries retained

The original outer data-23 executor result remains a preserved failure caused
by PowerShell scalar route-argument character splatting; it is not rewritten as
a whole-window PASS. The repair validated the typed-string-array path and raw
reanalysis only. SDK record incarnation/epoch/drain, changed-network device
behavior, zero-loss or all-races proof, unbounded reconnect, same-ID reuse,
PF/P4, QualificationProfile and production qualification remain
`NOT_PROVEN`/unchanged. The default feature remains OFF and the default real
backend remains `NO_REACCEPT`. Private identities, permissions, claims,
routes, tokens, SDK/DSO contents and raw logs are intentionally excluded.

## Status transition

`NATIVE_PREPARATION_HOST_TARGET_VERIFIED / DEVICE_NATIVE_CAPTURE_READY=NO /
COMMIT_GATE=FAIL` → bounded native feature acceptance PASS with the three
scoped device statuses above; `NOT_PRODUCTION_QUALIFIED` remains.
