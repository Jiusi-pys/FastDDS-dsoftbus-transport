# Fixed df receipt contract

This local repair addresses a public-source contract defect. A subsequently
approved local405-byte classification confirms the old two-name display-mount
whitelist as the captured receipt incompatibility, without publishing field
values. The original failed attempt remains failed; no additional raw access
or DDS retry is authorized.

The fixed query is `df -k /data/local/tmp`. The gate means that filesystem's
available KiB must reach65536; it does not require its displayed mount-table
name to be `/data`. OpenHarmony Toybox selects by device ID and can display a
different bind-mount directory. The old two-name parser restriction therefore
rejected a valid output class. The repair accepts a bounded lexical absolute
display path while retaining the fixed query, readlink result, three parent
metadata records, root-absence evidence, complete record count, remote status,
ASCII numeric/percentage syntax and every memory/disk threshold. The new
`_display_mount_path` check is lexical; it supplies neither an independent
`st_dev` proof nor realpath attestation, quotas or a hard disk reservation.
The three directory records still concern `/data`, `/data/local` and
`/data/local/tmp`; the exact readlink result and ten logical resource records,
including ordered return-code markers, remain required. Available space is
still measured in KiB and must reach65536.

Display paths must be at most4095 UTF-8 bytes, with no whitespace, backslash,
controls, empty/dot/dot-dot components or trailing slash except root `/`.
Leading/trailing record padding is not stripped. Escaped/space-containing paths
and wrapped device records remain unsupported UNKNOWN and cannot pass. Long unwrapped
device names and horizontal column separators are supported.

The command remains `-k`. Adding `-P` would change the header to a different
units/Capacity grammar, not simply remove wrapping. BusyBox is a public comparison
implementation; it is not evidence that this board runs BusyBox.
No exact mapping from either public source revision to the installed vendor
binary is established, and these sources do not identify the actual failure.

Primary-source basis:

- [OpenHarmony Toybox df argument/mount selection](https://github.com/openharmony/third_party_toybox/blob/c4fa066c749ee7f394ddb188cc1027449c8211ca/toys/posix/df.c#L142)
- [Printed mount directory and formatting](https://github.com/openharmony/third_party_toybox/blob/c4fa066c749ee7f394ddb188cc1027449c8211ca/toys/posix/df.c#L106)
- [Mount-table decoding](https://github.com/openharmony/third_party_toybox/blob/c4fa066c749ee7f394ddb188cc1027449c8211ca/lib/portability.c#L140)
- [BusyBox comparison, including POSIX header and wrapping](https://github.com/mirror/busybox/blob/371fe9f71d445d18be28c82a2a6d82115c8af19d/coreutils/df.c#L185)

The [cleanup causal record](../build/df-receipt-analysis-20261102-d01/CLEANUP_CAUSAL_ANALYSIS_D01.json)
shows that the previous actual entry and four HDC roots have terminal, EOF and original
identity-binding receipts. The entry exited1. Strict cleanup qualification is
false because `dds_observation.qualify` compares the retained prior business
failure `OBSERVATION_OR_RECEIPT_UNKNOWN` against the sole permitted notification
reason `CLEANUP_NOTICE_BLOCKED_OR_UNKNOWN`. That mismatch is not evidence of an
OS-close failure. Unknown descendants, remote termination and SDK restoration
remain UNKNOWN; no additional signals or HDC calls were made to resolve them.

## Independent validation and actual status

The [initial tester](../build/df-receipt-analysis-20261102-d01/INITIAL_TEST_RESULT_D01.json)
passed46 host tests, zero skips. The retained
[D01 review](../build/df-receipt-analysis-20261102-d01/REVIEW_CLOSURE_D01.json)
identified a P2 guard issue in the ignored local diagnostic: an existing RESULT
must stop a new attempt even if INTENT is absent. The reader now checks both
control paths before operator/target work, retaining exclusive INTENT creation;
unknown or inaccessible control state also stops. The subsequent independent
[D02 review closure](../build/df-receipt-analysis-20261102-d01/REVIEW_CLOSURE_D02.json)
closes that finding with zero open blockers. The fresh post-review
[final tester](../build/df-receipt-analysis-20261102-d01/FINAL_TEST_RESULT_D01.json)
passed47 tests, zero skips, failures or errors:

| Suite | Tests | Exit | Skips |
| --- | ---: | ---: | ---: |
| Resource receipt parser | 22 | 0 | 0 |
| DDS composition | 14 | 0 | 0 |
| Prepared local diagnostic | 11 | 0 | 0 |
| Total | 47 | all0 | 0 |

The increase from46 to47 is the new prior-RESULT guard test. This final result
is canonical; the initial46 is retained as pre-fix evidence. Final-result SHA256:
`9c3955b46d17ef06481effef827eed0f540c257dfa9770b4adc1eee2554d8312`.
D02 review SHA256:
`3909e7ea1ccd328e61296ddaa2beb7b348dbea013dc54f4227059aad01a3bf61`.

Python sources compiled without pyc; no Native build was needed or performed.
The local diagnostic suite used one real Win32 read on a newly created own
synthetic405-byte fixture, then closed its original handle and removed that
fixture. It did not access the actual target. Other checks used synthetic
receipts and driver/backend fixtures. Code/test/reader pins, the frozen card,
manifest/bindings, consumed once, 18 frozen input rows and protected68 metadata
remained unchanged during final testing. The consumed frozen runtime retains
its original parser; this repair does not patch it.

The [latest actual result](../build/directdds-builtin-package-20261103-d01/prepare/ACTUAL_TEST_RESULT_D01.json)
and [outcome](../build/directdds-builtin-package-20261103-d01/prepare/MAIN_ACTUAL_OUTCOME_D01.json)
for commit `61bec0b` retain entry exit1 and FAIL_OR_HOLD: server, targets and provider `readonly.1`
passed, then `readonly.2` failed with `DF_RECORD_UNKNOWN`. Deployment, identity
selection/restore, probes and real/cross-device DDS were NOT_RUN; once was
consumed and no automatic retry occurred. That actual result's DEVICE_TESTED=true
describes only the HDC/read-only prefix, not DDS integration. This new repair is
HOST_TESTED only, DEVICE_TESTED=false. The completed local classification below
confirms the host receipt incompatibility; it proves no Native, SDK, network or
DDS success. Overall DDS/Stage1 remains NOT_VERIFIED.

Documentation readiness and the final tester's `Commit gate: PASS` cover local
repair/preparation only. Main retains graph/pre-commit disposition and Git
transitions. No test, review or documentation result grants actual read or DDS
execution authority.

## Completed local diagnostic and current boundary

The ignored [D02 proposal](../build/df-receipt-analysis-20261102-d01/LOCAL_DF405_DIAGNOSTIC_PROPOSAL_D02.json)
bound the reviewed reader source and private target metadata. At the earlier
47-test snapshot, its operator binding, actual INTENT and actual RESULT were
absent and the actual body was unread. A later specific direct approval was
executed exactly once: the [actual-read result](../build/df-receipt-analysis-20261102-d01/LOCAL405_ACTUAL_TEST_RESULT_D01.json)
and [outcome](../build/df-receipt-analysis-20261102-d01/LOCAL405_MAIN_OUTCOME_D01.json)
report one ReadFile call for `[0,405)`, exactly405 bytes, same original handle,
identity/path checks before and after, and successful close. Operator, INTENT
and RESULT now exist; the local read permit is CONSUMED_NO_RETRY. The old DDS
once also remains spent. No receipt was reopened for this documentation.

The enums report complete standard `df -k` framing, six fields on one line,
no edge whitespace, three unsigned ASCII numeric columns, an attached percent,
OTHER_LEXICAL_ABSOLUTE display mount, strict candidate ACCEPTED and no
unclassified remainder. The classified mount lies outside the historical
two-name whitelist. This confirms that the published d9c3036 repair addresses
this captured resource-receipt mismatch; no additional parser change is needed
for it. It does not attest current free resources or change the prior FAIL.

Only fixed framing, header, row-shape, numeric/percentage syntax, path-category,
candidate-acceptance and unknown-remainder enums may be returned. No raw text,
variable values or content hashes are emitted. The exact file identity stays
in ignored private records. Unsupported whitespace/escaped/wrapped paths or
`-P` header variants remain UNKNOWN and cannot pass. The reported candidate
acceptance does not confirm the vendor binary, authorize HDC or grant a DDS
retry. The local read performed no HDC/SDK/device/identity action and emitted no
raw fields or content hashes. The [fresh full DDS binding](DIRECT_DDS_DF_EXECUTION.md)
has separate initial14/review/final14 offline evidence, reusing this phase's47
component tests by exact hashes; those47 were not rerun. New-candidate DDS is
NOT_RUN, DEVICE_TESTED=false, and needs a new specific single-run grant.
