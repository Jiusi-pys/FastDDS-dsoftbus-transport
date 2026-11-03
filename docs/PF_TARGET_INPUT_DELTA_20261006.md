# PF target-input delta — production foundation

This is an increment to existing [PF-01/PF-02 issues](P0_IMPLEMENTATION_20260920.md),
not another duplicate waiting-for-platform package. No supplier message, upload,
fault test, SDK change or PF release was performed. The 2026-09-20 binary audit and
2026-09-21 timeout/incarnation investigation remain the original local evidence;
their raw DSOs, disassembly, identities and permission records are not distributed.
Target source equivalence is unproven, so supplier answers must bind actual images.

## Reused target identifiers

Original scope: two RK3588A nodes, OpenHarmony 6.1.0.31, SDK API23/6.1.0.32.
These are non-secret binary identifiers, not bundled binaries or fresh device evidence.

| ELF | SHA-256 | Build ID |
| --- | --- | --- |
| libsoftbus_client.z.so | 9b431d2976616becbbe5b3b18f3c9507ca42f7a15c965c9aa86457f4ff0e6087 | c096d8dba5158f904f4fa43a2b46df5f |
| libFillpSo.open.z.so | a2b08d2df91e13b90fbe217c7cf867afacd7e2bfe4b99d228761755efd73d358 | 7636f163fdff4fecc464429200b40d42 |
| libsoftbus_utils.z.so | 9bd88a406d979ddc58e304a103027ec08056f859054af46be709969179df16b4 | d5c0acf9d40e9719d3bba91aa1d7b76f |
| libsoftbus_adapter.z.so | 8c957f4d4eb57742fbdff189abbe4b636b84fe9ab029c86fc816974791ba686b | 0db8f30a1a5aecd25a5e46b46ef50bd8 |

## Still missing evidence (only target-bound inputs)

| Existing issue / evidence item | Required supplier evidence | Smallest discriminating future test |
| --- | --- | --- |
| PF-01 payload last use | For selected profile/channel/invocation, identify the last SDK read/reference of caller payload and any copied/queued payload, including error and close | Trace one invocation with buffer ownership transitions; return/completion alone cannot be assumed last-use |
| PF-01 native call end | Public finite termination mechanism, allowed calling stage, requested/result/effective value, target kernel configuration and fault coverage | Same-process stalled-send/close invocation timestamps and return/error, correlated native wait/channel; wrapper timeout is not call termination |
| PF-01 owner/channel last use | Native reference-drop and actual resource release boundary for the same channel incarnation, including worker references | Trace return, every owned reference final use and final release, not only public Shutdown return |
| PF-01 / PF-02 work and callbacks | All relevant callbacks/queued work either consumed to completion or invalidated with a target-version contract; classify pending ACK/completion work separately | Pause at one defined work stage, close, then prove each work class completed/invalidated and cannot reference retired owner/buffer |
| PF-02 pre-effect original-event identity | SDK-created event incarnation preserved and checked/excluded before first native resource lookup/mutation with effects, including UDP old event and replacement numeric ID | Old-event/new-resource overlap trace proving rejection before current resource mutation, not merely at adapter OnError |

## Public mechanism candidates and limits

Presence below refers to original target binary/header observations. It does not
mean the mechanism was actually invoked on the relevant channel or has an official guarantee.

| Candidate | Present on original target? | What it can address | What it cannot establish | Supplier input and minimum discriminator |
| --- | --- | --- | --- | --- |
| SendBytes / Shutdown | Exported; TDC send/ref-drop and deferred close paths audited | Selected normal return and some last-reference release paths | Stalled send finite exit, callback/work drain, original event epoch; Shutdown is void | Bind exact route/fault/kernel; prove finite native exit plus all five evidence items above |
| Existing TCP_USER_TIMEOUT-style timer request | Client/utils/adapter target branch exists; setter invocation/rc/effective and kernel behavior not observed for actual channel | Possibly connection no-progress termination | Total invocation/close budget, unrelated routes, payload/owner/callback settlement | Official kernel/SDK support, actual channel option set/read/effective/override trace; one no-progress test under approved budget |
| Bind timeout/QOS_TYPE_MAX_WAIT_TIMEOUT / MAX_IDLE_TIMEOUT | Public options declared; stage-specific behavior needs target evidence | Connection establishment/idle policy | Send deadline or global cancellation/drain | Official stage/supported value/rc/effective table and one distinct connection-state test; no inference from name |
| SendBytesAsync + OnBytesSent | Header and target exports present; target requires negotiated TLV + NEED_ACK; audited TDC path still calls synchronous native send | Candidate ACK/completion observation after supported negotiation | Native call finite return, cancellation, drain or payload last-use; callback has socket/dataSeq/error, no original SDK epoch | Official payload lifetime/completion/error/close ordering contract; one invocation trace spanning synchronous send and completion/late-work retirement |
| OnShutdown / OnError and public Socket ID | Public callback/API present | Public terminal notification and owned adapter admission transition | Notification consumption/drain, native record incarnation, protection before earlier native resource effects | Exact dispatch/record lifetime contract; old event queued before replacement ID demonstrates pre-effect exclusion or failure |
| Continuously live listener/service record + fixed entrances | Ordinary source deletion path and bounded live-table evidence; full abnormal target equivalence unproven | Candidate continuity anchor, adapter captured-lease and tombstone isolation | SDK-buffered original attribution, all destruction paths, same-name/new-ID continuity | Supplier target record lifetime/invalidation proof plus one last-anchor/abnormal-loss branch; ambiguous continuity refuses new ownership |
| Public cancellation/drain/epoch receipt covering original events | No qualifying public contract/receipt found in current audited boundary | Would close appropriate last-use/termination or pre-effect identity item if provided | Internal generation, private fd, late check or process kill cannot substitute | Supplier must identify supported mechanism or official fix and bound semantics to target version; no Codex SDK patch |

Disposition distinguishes three cases: **existing guarantee but missing evidence**
requires an official target-bound guarantee and trace; it is not yet confirmed that
such a guarantee exists. **Current public SDK boundary lacks the required mechanism**
requires supplier confirmation and official supported fix/version if no existing
mechanism qualifies. **Product/architecture choice** includes acceptable fault/close
budgets, restart behavior and whether a previously retained hard requirement changes;
only the product owner can decide. No current arbitrary numeric budget or local
generation is used to force closure. Broker/IPC/SDK workarounds remain excluded.

PF-03 RAW pressure/integrity and PF-04 native self-route remain their existing
issues, not silently removed by the BYTES-first packaging work. Local preflight,
maintenance analysis and public documentation continue while supplier answers are
absent. Qualification remains blocked; this foundation package is not itself a
WAITING_FOR_PLATFORM task.
