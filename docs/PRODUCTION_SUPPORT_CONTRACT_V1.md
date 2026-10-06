# Production support contract v1 — foundation, 2026-10-06

The product goal is real production deployment and public use. Bounded experiments
are evidence toward that goal, not the final delivery standard. This first package
provides install-input checking and a traceable support contract; it does not qualify
production. No production profile has been issued and P4 remains unreleased.

## Baseline and intended support

Baseline `facca428ab9a15649f44f2bdf216e446c7d56d5d` is the route-lifecycle
acceptance commit. Its parent `eea2759` supplied Host route invalidation/resolution;
the [native acceptance](ROUTE_LIFECYCLE_NATIVE_ACCEPTANCE_20261006.md) records the
separate bounded native result. The original outer data-23 failure is retained;
feature raw reanalysis is not a whole-window PASS. This package does not rerun V1,
reaccept or ordinary ROS acceptance. Unrelated dirty and private materials remain
outside the commit. Older task-local commit prohibitions are historical; this task
authorizes one validated local foundation commit only.

| Dimension | First intended production target | Current supported evidence / restriction |
| --- | --- | --- |
| Platform | RK3588A, KaihongOS/OpenHarmony 6.1.0.31, AArch64 little-endian | Bounded two-board evidence only; vendor source/binary equivalence and application platform contract unverified |
| SDK and C++ ABI | SDK API 23 / native 6.1.0.32, Clang 15.0.4, `c++_shared` | Headers from authorized communication_dsoftbus `5518313`; not SDK-shipped stable NDK declarations; four native ELF identities retained in PF delta |
| ROS frontend | ROS 2 Jazzy, original RMW, patched Fast DDS 2.14.5, Fast CDR 2.2.5, TinyXML2 6.0.0, foonathan_memory 0.7.4 | Source-bound factory/teardown/topic type/endpoint adaptations already exist; no RMW change; exact Jazzy package set must be externally supplied and versioned, not inferred from distro name |
| Separate standalone ABI | Fast DDS 3.6.2.0 / Fast CDR 2.3.5 / foonathan_memory 0.7.4, shared Release | Existing CONFIG delivery; Windows MSVC 19.44 `/MD` Host evidence. Never mix 2.14 and 3.6 or their CDR ABIs in one process |
| Transport boundary | Public DSoftBus Socket API below Fast DDS TransportInterface | No Session API, RTPS/QoS workaround, broker/helper data plane or IPC redesign |
| Channel/profile | BYTES_NATIVE first production candidate | BYTES native selected TDC/IP evidence, not every SDK route. RAW_PLAIN/RAW_ENCRYPTED remain required candidate profiles with PF-03 gaps; no removal or silent mapping to BYTES |
| Topology | Two RK3588A nodes, one listener per active Participant, owned active/accepted connections | Two-node bounded evidence; multi-owner/Participant/reader, three-node, mixed UDP/TCP/SHM and retained native self-route need their own qualification branches |
| Legal deployment | Vendor-supported bundle/process identity, policy namespace, owned normal-APL token and required permissions; paired devices and operational SA/networking | JSON, token ID and local library files only describe inputs. Actual policy/grant/token ownership and source equivalence remain unverified offline; no shared private identity is distributed |

The product target includes all previously retained hard requirements. Absence from
the first preflight's accepted experimental schema is not removal of a requirement.
Other firmware, SDK, compiler, STL, CPU and ROS package tuples are unverified until
bound to their own contract and execution evidence. No tuple is production-qualified.

## Behavior and evidence mapping

| Requirement | Existing evidence | Missing implementation / device branch / platform mechanism |
| --- | --- | --- |
| Normal startup / authenticated identity before native effects | Installed consumer and token-first entry; V1 package-scoped names; bounded discovery/write/take windows | Public deployment identity contract and loader namespace/source binding; failed startup residual cleanup matrix; production backend registration remains unavailable |
| Offline sends / route return | facca428 bounded native OnNodeOffline/Online; offline new writes refused before SDK, fresh resolver return and retained entities | Changed-network device branch, general offline race/callback overlap and product recovery budget; no zero-loss inference from PRE/POST |
| Reconnect | Existing bounded normal-close, fresh-ID reaccept with peer-ready gate | Process restart, arbitrary faults and same-ID support unverified; production defaults still NO_REACCEPT. Assistance-free/general recovery is not established by that gate |
| Normal close | DDS deletion, pump join/Runtime close and token restoration observed in bounded windows | Failure-close, in-flight send, channel last use and callback/work settlement; PF-01 and PF-02 remain necessary |
| ID reuse / event isolation | Host fixed entrances, captured owner/receiver leases, terminal no-app drop and permanent tombstones | Original native event identity before first native mutation, target record continuity/epoch and drain. Internal generation cannot replace SDK epoch; ambiguous continuity must stop admission safely |
| Resource exhaustion | Host queue/buffer/owner/slot bounded rejection contracts | Device memory/thread/connection limits, compiler/allocation failures, SDK exhaustion and recoverability. Never recycle exposed retired entrance to improve capacity |
| Fault recovery / safe stop | Host cancellation/admission models and preserved failed BYTES 1-second candidate | Supplier mechanism for finite native termination and physical settlement. A watchdog kill is a safety intervention, not an orderly-close pass |
| Security / malformed traffic | Current version/security-build inputs inspected; official advisories inventoried separately | Maintenance patch selection, hostile RTPS/CDR tests, resource bounds and deployment threat model; DSoftBus authentication does not prove RTPS parser unreachability |
| Packaging / public use | Existing relocatable CONFIG and new offline preflight | First-party release licensing decision, legal vendor library distribution/source provenance and production qualification; no private SDK/DSO/log redistribution |

Relevant original contracts remain [V1 plan](FASTDDS_DSOFTBUS_TRANSPORT_V1_PLAN.md),
[P0/PF issues](P0_IMPLEMENTATION_20260920.md) and
[reconnect disposition](RECONNECT_REQUIREMENT_CLOSEOUT_20261005.md). The exact original
A5/C1 source-incomplete finding text is not reconstructed from later summaries.

## Product values and qualification binding

No approved production numeric budgets were located in the current source-bound
requirements. These fields require the product owner to supply values, units,
measurement endpoints, fault scope and allowed percentile/worst-case behavior:

| Product value | Status | Must be bound to |
| --- | --- | --- |
| Recovery duration and tolerated loss/duplicates | NEEDS_PRODUCT_VALUE | Fault onset, route re-resolution, first accepted write and first valid take; per-node clocks |
| Send and shutdown budgets / safe-stop escalation | NEEDS_PRODUCT_VALUE | Invocation, error/return, payload/owner/channel last-use, worker/callback settlement and residual identity restoration |
| Offered load, burst, payload size and sustained duration | NEEDS_PRODUCT_VALUE | QoS, fragmentation, readers/writers/Participants, topology and memory policy |
| Resident memory, queue/pool, thread, socket/connection and listener limits | NEEDS_PRODUCT_VALUE | Live versus cumulative resources, rejection behavior and post-failure recovery |
| Allowed restart/ID reuse behavior and operator intervention | NEEDS_PRODUCT_VALUE | Supported fault classes and platform event identity/epoch guarantees |

Historical 1-second failure, 5-second waits, 320-second TCP candidate, 100 cycles,
72-hour proposals, 65536-byte assembly defaults or SDK header socket limits are
not production standards. They retain their experiment/configuration meanings.

An eventual QualificationProfile must bind exact firmware/SDK/native ELF and product
artifact hashes, effective API options/stages, compiler/STL/dependency ABI, legal
identity conditions, profile/route/topology capacity, approved budgets, tests and
run evidence. Matching version strings or `production=true` cannot supply it.

## Next fault validation group (PLANNED, not authorized to execute here)

| Branch | Prerequisite and smallest discriminator | Expected result / disqualifying observation |
| --- | --- | --- |
| Native stalled send + concurrent close | PF-01 supplier finite-exit/last-use mechanism plus approved budgets; one same-process invocation/channel trace | Return/error within approved budget, each last-use and work/callback completion or invalidation observed; timeout wrapper/kill alone fails orderly close |
| Old event + replacement numeric ID | PF-02 original-event identity guard before first native effect and legal supplier mechanism | Old event rejected before lookup/mutation of replacement; late OnError or current generation alone fails |
| Device offline then changed network / reconnect | Approved topology and recovery budget, same retained entities, real ordered SDK events/resolution | Offline attempts denied before SDK; return resolves fresh route and correct peer; capture late-history separately; no stale-route admission |
| Permission/SA loss or peer/process restart | Explicit future authorization, vendor-supported legal lifecycle, restoration contract | Failed startup/recovery safely retains or releases owned resources; report primary and cleanup errors; no fabricated send/close success |
| Native/adapter resource exhaustion | Product resource bounds and legal platform path, one resource class at a time | Rejection before forbidden side effect, existing owners remain valid, safe-stop within approved budget; tombstone reuse/leak fails |
| Malformed RTPS/CDR, RELIABLE range/fragment and filter input | Selected maintenance candidate; isolated Host parser harness before any device branch | Bounded allocation/CPU, valid error outcome, no crash; parser mitigation and transport continuity measured separately |

No SDK, firmware, policy or original RMW changes and no device fault injection are
performed in this foundation package. PF-01/PF-02 input deltas and maintenance
candidates can progress locally without a supplier reply; production eligibility cannot.
