# Full-feature Fast DDS 2.14.5 on the boards, and the content-filter race

Date: 2026-11-05. Boards: RK3588A A `3e01ff55454d202020104033bf453b00` (192.168.77.201),
B `3e01ff55454d202020104433991c3b00` (192.168.77.202). Status vocabulary per `AGENTS.md`.

## 1. Fast DDS 2.14.5 rebuilt for OHOS with every available feature

**Source.** The unmodified 2.14.5 tree the boards already use (`build/r39-ros2-complete-matrix/stock-sdk23-d03/source`),
copied and checked against its `SOURCE_INVENTORY.json`: 3771 files, 0 mismatches. No Fast DDS source change.

**Configuration** (script: `tests/stage1_native_successor/security/build_fastdds_2145_full.sh`). Same toolchain and
dependencies as the board build (SDK 23 `ohos.toolchain.cmake`, arm64-v8a, `c++_shared`, Fast CDR 2.2.5,
foonathan 0.7.4, TinyXML2 6, same `RosTargetBuildOptions.cmake`), with these differences:

| Option | Board build before | Now |
| --- | --- | --- |
| `SECURITY` | OFF | ON (`HAVE_SECURITY 1`) |
| `FASTDDS_STATISTICS` | OFF | ON |
| TLS (TCP) | off (no OpenSSL) | on (`TLS_FOUND 1`) |
| `SQLITE3_SUPPORT` | ON | ON |
| `COMPILE_TOOLS` | OFF | ON (`fast-discovery-server`) |
| `STRICT_REALTIME` | OFF | OFF (a blocking-semantics mode, not a feature; unchanged) |

- OpenSSL 3.5.9, cross-built earlier with the OHOS clang (`--target=aarch64-linux-ohos`, OHOS sysroot, `linux-aarch64`,
  `no-asm`, shared). Its `install` step had failed on a WSL `ranlib` path issue; the built `libssl.so.3` /
  `libcrypto.so.3` and the installed headers are used directly. SHA256: libssl `42e0a6fd…`, libcrypto `26ee9da7…`.
- The OHOS toolchain limits CMake searches to the sysroot, so OpenSSL is passed explicitly
  (`OPENSSL_INCLUDE_DIR`, `OPENSSL_CRYPTO_LIBRARY`, `OPENSSL_SSL_LIBRARY`).
- Result: `libfastrtps.so.2.14.5` SHA256 `823aa802…` (needs libssl/libcrypto), 610 exported security and 1377 statistics
  symbols. The transport and the runner were rebuilt against the new headers (SECURITY changes public structure layouts):
  transport `libfastdds_dsoftbus_stock_214.so` `240b4702…`.

**Deployment** (`$R=/data/local/tmp/mdds_direct_20261103_f47c9e20/lib` on both boards). Backup first: both `lib`
directories saved to `E:/board-lib-backup-20261105/lib_{A,B}.tar`, and the previous library kept on each board as
`libfastrtps.so.2.14.off`. Then `libfastrtps.so.2.14`, `libssl.so.3`, `libcrypto.so.3` replaced/added.

**ABI note for Stage 2.** A SECURITY=ON `libfastrtps` has different public structure layouts. Every C++ consumer of it,
including the ROS 2 RMW and typesupport libraries, must be built against these headers; mixing them with the
SECURITY=OFF build is not safe. Nothing above the transport was changed here.

### Security finding: secure Participants received nothing over DSoftBus (fixed by configuration)

- Symptom: secure Participants initialised (the permissions deny rule was enforced) but never authenticated or
  discovered anything over DSoftBus. The same PKI authenticated over UDP on the Windows host and on one board.
- Root cause (Fast DDS 2.14.5 `RTPSParticipantImpl::createReceiverResources`): a secure Participant caps its receive
  buffers at `std::numeric_limits<uint16_t>::max()` = 65535. `NetworkFactory::BuildReceiverResources` passes
  `min(transport max, 65535)` to `OpenInputChannel`. The DSoftBus deployment used `maxMessageSize` 65536, so the
  transport refused every input channel (capacity 65535 < 65536). Fast DDS does not report a refused input channel.
- UDP never hits this: its maximum is 65500.
- Fix:
  - Transport (`src/DSoftBusTransport.cpp`): the refusal is now printed:
    `DSoftBus input refused: receiver capacity 65535 < transport maxMessageSize 65536 (secure Participants need maxMessageSize <= 65535)`.
  - Deployments with security must use `maxMessageSize` ≤ 65535. The feature runs use 65500, like UDP.
- A/B on the boards: 65536 → 6 refusals per side and no authentication (m113); 65500 → all security cases pass (m112).

### Device results (both boards, DSoftBus transport, new build)

- m134, m135, m136: QoS matrix 8/8 and feature matrix 23/23 per side, controller exit 0, status 0.
- m137, m138: route outage recovered; m139: plain run (fragmented sample, hot rebuild).
- New cases:
  - `security_pki_dh_access_aes_gcm`:
    - PKI-DH authentication (ECDSA P-256 test PKI), Access-Permissions with signed governance and permissions, AES-GCM-GMAC.
    - RTPS, discovery, liveliness, metadata and payload protection all ENCRYPT.
    - 3/3 samples and a 150000-byte fragmented sample each way, acknowledged; the permissions deny rule blocks a writer.
  - `security_rejects_untrusted_identity`: board B presents an identity from an untrusted CA. Both sides report
    UNAUTHORIZED and nothing ever matches.
  - `statistics_and_monitor_service`:
    - Each side enables statistics DataWriters (DATA_COUNT, RTPS_SENT, DISCOVERY, PHYSICAL_DATA, PUBLICATION_THROUGHPUT) and the monitor service.
    - Statistics samples published by the peer arrive over DSoftBus.
    - The peer's RTPS_SENT samples name the DSoftBus locator kind as destination.
- Host: stock 67/67, root 131/133 (the two pre-existing failures).

### Feature classification for Fast DDS 2.14.5 in this environment

| Class | Features |
| --- | --- |
| Supported, enabled and device-tested over DSoftBus | Everything in the feature matrix (see `STAGE1_DIRECT_GOAL_DEVICE_20261104.md`) plus DDS Security (PKI-DH, Access-Permissions, AES-GCM-GMAC, governance and permissions) and Statistics with the monitor service |
| Not implemented by Fast DDS 2.14.5 | TimeBasedFilter, DestinationOrder, Presentation, LatencyBudget, TransportPriority, DurabilityService |
| Supported, built, not tested here | Security logging plugin (`builtin.DDS_LogTopic`); TCP with TLS (a TCP transport feature, not used over DSoftBus); `fast-discovery-server` CLI (Discovery Server itself is tested through the API) |
| Supported, cannot be exercised on these boards | PKCS#11 keys (compiled in, need an OpenSSL `pkcs11` engine and a token, absent); shared-memory transport, data sharing and zero-copy (same-host only; on these boards Fast DDS also reports "Failed to create Shared Memory Manager"); intraprocess delivery (same process, disabled by the harness) |
| Build modes deliberately unchanged | `STRICT_REALTIME` |

## 2. The content-filter race

### Root cause (Fast DDS 2.14.5 source)

There are two defects. Both are inside Fast DDS and independent of the transport.

1. **R1: writer-side reader filters vs `write()`.**
   - `StatefulWriter` / `StatelessWriter` call `on_reader_discovery` with the writer mutex released ("listener is called without locks taken").
   - `DataWriterImpl::remove_reader_filter` and `process_reader_filter_info` then change `ReaderFilterCollection` without that mutex: they destroy filters, return expressions to the pool and insert entries.
   - Meanwhile `DataWriterImpl::write()` → `perform_create_new_change` evaluates the same filters under the writer mutex.
   - The removal path was fixed upstream by eProsima commit `0bd912836` (PR #5014, "Fix data race on reader filter removal"), first released in v3.0.0. It was never backported: v2.14.7, the last 2.14, still lacks it. The add/update path (`process_reader_filter_info`) is unlocked even on current master.
2. **R2: the DDS-SQL filter factory pool.**
   - `DDSFilterFactory` keeps one `ObjectPool<DDSFilterExpression*>` (a plain `std::vector`) per DomainParticipant, with no lock.
   - User threads use it through ContentFilteredTopic create/delete (under the participant's `mtx_topics_`).
   - Discovery threads use it through writer-side filters for remote readers (no common mutex).
   - Unfixed upstream. This is the `delete_content_filter` crash seen in m43.

Both are triggered by discovery processing, which runs on whichever thread delivers RTPS messages: UDP and TCP
receive threads, the DSoftBus pump, or the event thread on lease expiry.

### Evidence with the standard transports, board to board

Reproducer `tests/stage1_native_successor/cft_race/cft_race.cpp` (Fast DDS only, no DSoftBus):
- Board A: a DataWriter writing every 1 ms, optionally with local ContentFilteredTopic churn.
- Board B: churn of 6 remote filtered DataReaders.

| Run (60 s, board to board) | A: local CFT churn | B readers | Writer-side filtering | Result on A |
| --- | --- | --- | --- | --- |
| UDPv4 ×3 | yes | filtered | on | SIGSEGV 3/3 |
| UDPv4 ×3 | no | filtered | on | SIGSEGV 3/3 |
| TCPv4 | no | filtered | on | SIGSEGV (324 matches before the crash) |
| UDPv4 / TCPv4 | no | unfiltered | on | no crash (5448 / 4866 matches) |
| UDPv4 | yes | unfiltered | on | no crash (19138 matches) |
| UDPv4 / TCPv4 | yes | filtered | **off** | no crash (18805 / 18400 matches) |
| UDPv4 | no | filtered | **off** | no crash (5274 matches) |

- Every crash has the same stack: `DataWriterImpl::write` → `perform_create_new_change` → `DDSFilterExpression::evaluate`
  → `DynamicData::clear_all_values` on a recycled expression (faultlogger `cppcrash-*` on board A).
- The first TCP runs did not match at all (wrong initial-peer logical port) and are not counted.

### Same stress over DSoftBus (runner opt-in `TMPDIR/cft_stress`, case `cft_race_stress_dsoftbus`, 30–45 s)

- Writer-side filtering on: board A crashed in m114, m117, m120, m125, m129, m130 with the same stack.
- Writer-side filtering off: m126, m127, m128 clean (exit 0, about 9100 matches each); m115, m116, m118, m119, m123,
  m124 survived the stress too. Those earlier runs then ran past the harness's 120 s run window; this is why the
  stress now runs alone at 30 s.

### Answers

1. **Same race with UDP/TCP?** Yes. It crashes faster with UDP and TCP between the boards than in the earlier
   DSoftBus feature runs, because the reproducer churns remote filtered readers much harder.
2. **Differences that explain DSoftBus behaviour?** None in the trigger. With UDP/TCP, Fast DDS's own receive threads
   run discovery; with DSoftBus, the runtime pump does. Either way discovery runs concurrently with user writes. The
   earlier rare DSoftBus crash (m43) came from a lighter workload, not from a transport difference.
3. **Cause:** an independent concurrency defect in Fast DDS (R1 and R2), not a DSoftBus/TCP/UDP difference.
4. **Upstream:**
   - R1 removal path: `0bd912836` (v3.0.0+), not in 2.14.x.
   - R1 add path and R2: unfixed.
5. **Fix inside the transport?** Not possible without violating the architecture. The race is between a Fast DDS
   discovery thread and the application's `write()` under a mutex the transport cannot see. The DSoftBus transport
   cannot order these, and UDP/TCP show the same crash with no DSoftBus involved.
6. **Compatible workaround (no Fast DDS change), verified on the boards with UDP, TCP and DSoftBus:** disable
   writer-side content filtering. Filtering then happens in the readers, with the same results but more bandwidth.
   - Public API: `DataWriterQos::writer_resource_limits().reader_filters_allocation.maximum = 0`.
   - XML profile, applicable without code changes:
     `<writer_resource_limits><reader_filters_allocation><maximum>0</maximum></reader_filters_allocation></writer_resource_limits>`.
   - What remains: R2 between user threads only, i.e. ContentFilteredTopic create/delete/`set_expression_parameters`
     issued concurrently from several application threads. Serialise those calls in the application.
7. **Proposed Fast DDS fix, NOT applied (needs approval):** `docs/proposals/fastdds-2.14.5-content-filter-race.patch`
   (3 files, 12 added lines):
   - Backport `0bd912836` (writer mutex in `remove_reader_filter`).
   - The same lock in `process_reader_filter_info`.
   - A mutex around `DDSFilterFactory`'s expression pool.
   - Lock order checked: `unregister_content_filter_factory` takes `mtx_topics_` then the writer mutex;
     the patch takes the writer mutex and then only the leaf pool mutex.

Evidence: `E:/Codex-FastDDS-stage1-directdds-manual-m107…m139-20261104/`, reproducer outputs on the boards
(`/data/local/tmp/sectest/cft_*.out`), faultlogger `cppcrash-*` on board A, host A/B `E:/sec_host_b`, `E:/tl_host_b`.
