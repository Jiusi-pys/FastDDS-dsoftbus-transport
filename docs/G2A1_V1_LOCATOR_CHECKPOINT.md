# G2/A1 V1 locator checkpoint

This document records the acceptance state of the 2026-10-04 G2/A1 package. It
is an evidence snapshot, not a production qualification or device acceptance.

## State

| Layer | State | Evidence boundary |
| --- | --- | --- |
| V1 production path | `IMPLEMENTED` | `LocatorCodec` delegates to V1; descriptor, directory, selection, Host bridge, TargetBridge and public socket DDS paths use the V1 configuration path |
| Build | `PASS` | Release full CONFIG `552/552`, `-j2`, 672.56 s; Host Fast DDS 2.14.5 and API23 TargetBridge clean-first build/link/static checks pass |
| Host integration | `PASS` | Host214 and CTest `14/14`; ROS paired close `20 writes / 20 takes` |
| ASan | `PASS` | Affected `208/208`, `-j2`; prebuilt Fast DDS/Fast CDR/foonathan DLLs were not instrumented |
| Installed consumer | `PASS` | Fresh relocated package consumer build and run |
| Independent review | `PASS` | No remaining blocking finding; receipt `build/g2a1/REVIEW.md` |
| Device / cross-device | `NOT RUN` | This package explicitly excludes HDC and device operations |
| Production qualification | `NOT QUALIFIED` | PF/P4 and existing qualification boundaries remain unchanged |

## Frozen V1 contract

The private non-negative locator kind is `0x40000000`. The 16-byte address key
is the first 16 bytes of SHA-256 over this exact sequence:

```text
ASCII "MDDS-V1-DEVICE" including its NUL
u16BE(namespace byte length)
namespace bytes
u32BE(domain)
u16BE(UDID byte length)
UDID bytes
```

Namespace and UDID are opaque, nonempty byte strings without embedded NULs;
the bounds are 1..116 and 1..64 bytes respectively. No case or Unicode
normalization is applied. Profile occupies the upper 8 port bits and
`slot + 1` occupies the lower 24 bits. Profiles 1/2/3 are structurally
recognized; the current native pipeline supports `BytesNative` profile 1.
Slots are 0..`0x00fffffe`. Zero low bits, invalid profiles and overflow are
rejected. Structural decoding does not recover or authenticate the original
identity.

The independent synthetic vector used by the fixed fixture is:

```text
input:       4d4444532d56312d444556494345000005616c706861010203040006756469642d31
address_key: c3797784976ed68cef55b2f0f22e8cc4
```

The approved plan’s applicable requirements are retained here verbatim:

> locator 使用规范服务名、稳定的 namespace/domain/UDID 摘要和 profile/slot 编码；`networkId` 仅是在线路由信息，不是持久身份。暂时未连接不等于地址不可达；设备离线时当前 route 必须失效，已知不可达目标可以拒绝发送，重新上线后重新解析。initial peers 只用于原始 SPDP bootstrap，不公告虚拟地址，不创建新发现协议。UDP/TCP/SHM 与 DSoftBus 必须共存。
>
> 地址编码固定为：私有非负 locator kind；`address[16]` 为规范 `{namespace, domain, UDID}` 的 SHA-256 截断键，碰撞必须拒绝歧义路由；`port` 高 8 位编码 profile，低 24 位编码 `slot + 1`，零值和非法组合拒绝；同一 Participant 的 metatraffic/user traffic 公告同一 DSoftBus locator。profile 不兼容的 locator 在选择阶段排除。bootstrap 发送面为“已认证在线设备 × 显式 slot 集合”，不公告 bootstrap 虚拟地址。
>
> Fast DDS 的自定义 locator 选择必须按完整地址比较，避免通用 IPv6 multicast 判断误判私有 kind；必要时在 adapter 选择逻辑中遍历并匹配完整 locator，不先改 Fast DDS 核心。DSoftBus 初始化失败时，不能因为 UDP/SHM 仍可用而把 Participant 误报为 DSoftBus ready；需要 checked-create helper，独立记录每次 descriptor 尝试的 `NotAttempted/Starting/Ready/Failed` 和 listener/receiver 是否已就绪。

Applicable definitions include `service_slot` from deployment configuration,
explicit namespace/domain/UDID identity, profile and initial peer slots;
`networkId` remains mutable online routing data and is never the identity.

## Runtime and bridge evidence

The final Host probe observed SPDP `SendBytes` on both sides with PID `0x31`
and `0x32`; each observation retained the full private kind, all 16 address
bytes and the encoded port. The existing 24-byte locator CDR layout was
unchanged. The focused evidence covers the independent golden vectors,
collision rejection, two-slot production wiring, `FF` address selection, and
the old production port-1 mutation: build 0 / test 1 failed for the intended
production-wiring reason while the helper remained unchanged, then the source
was restored. The retained mutation receipt is
`build/g2a1/mutation-receipt.json`.

The ROS 2.14 source was prepared from clean Fast DDS `2.14.5` baseline
`ebcb5c05e9da9e262041e8616bbaff61ab40a296` using the single
`integrations/ros2/prepare_fastdds_source.py` entry point. This run executed all
five steps, verified all six headers, and produced the complete 18-file
affected-file end-state hash receipt at
`build/g2a1/prepared-end-state-complete.json`. That receipt belongs to this
run and is not an old golden manifest. The actual Host and API23 TargetBridge
were built/linked against the prepared source; the current NativeBackend
interface and A2 `max_msg_size_no_frag` correction were consumed, and no Fake
implementation entered the target package.

## Production wiring

`ConfigureV1` derives the key, encodes the locator and canonical service name,
builds the bounded identity directory, and publishes the same complete local
locator for metatraffic and user traffic only after validation. Directory
registration rejects conflicting canonical identities and preserves the prior
route on a forced truncated-key collision. `ValidateV1` checks profile, full
locator validity, identity coverage, canonical names and advertisement
equality. Selection compares kind, all 16 address bytes and the full port and
rejects incompatible profiles. No synthetic production fallback was added;
legacy port-1 configuration is refused, and arbitrary historical overlap
cannot be inferred from raw bytes alone. V1 selection is explicit through the
V1 configuration/bridge `locator_version` boundary.

The canonical hash contract is reused from the existing implementation. The
golden fixture is `tests/v1_golden_vectors.json`; the production wiring and
collision checks are covered by `tests/v1_production_tests.cpp` and the
affected directory/selection tests.

## Validation details and limits

The final named tester reported:

- the failure-close harness itself exited 0, while its monitored child
  processes exited 4 as expected;
- the forward-0 pair returned 1 with the expected negative ledger: both sides
  exited 4, writes/takes were 0, sends were 15, ingress was 0, and active
  pumps were 0;
- two original death tests failed when run concurrently in the common working
  directory because they shared `srtc_close_child_output.txt`; the final
  concurrent run used a unique working directory per test, with unchanged
  assertions and no source fix, and the original race evidence remains
  preserved;
- the older high-parallel `experiment.matrix_control_receipts` failure remains
  separate and unexplained.

The current lifetime test does not prove the old/new unlocked-read distinction;
the deterministic selector, slot/profile and deduplication tests do not prove
absence of all races. TSan was not run. Debug was statically audited only.
Existing A5/C1 table/route lifetime and `NO_REACCEPT` behavior is unchanged.
The package therefore stops at host and target build/integration evidence:
device execution, SDK internal drain/cancellation/quiescence, reconnect or
re-accept, and production qualification remain unverified.

## Graph evidence boundary

The refreshed pre-stage index reported `173` changed symbols across `39` files
and `0` resolved affected flows with low risk. The final staged
`detect_changes --scope all` reported `188` changed symbols across the same
`39` files, `0` resolved affected flows and low risk; neither result had a
`partial` or `truncated` flag. Index caps and unresolved dynamic dispatch
remain limitations. A zero affected-flow result is not evidence that callers
are unused or that there is no regression; source call-site review, build
results and runtime evidence govern this package’s acceptance.
