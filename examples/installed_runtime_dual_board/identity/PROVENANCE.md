# Identity-entry component provenance

This directory is derived from the reviewed, device-proven identity
entry used by the authorized public-Socket experiment. It exists so the
installed-runtime consumer can carry an auditable identity launcher that is
clearly separated from the public MDDS runtime use in `../main.cpp`.

| out-of-repo path | repository source | source sha256 |
| --- | --- | --- |
| identity/token_first_entry.hpp | probes/token_first_entry.hpp | 227ef2e135abe697029581be1e8f326dfcfb1148473259c1ee7db608f47967e4 |
| identity/token_first_entry.cpp | probes/token_first_entry.cpp | 6be90a5afea8f672e0e4d123e153278b954aac777fc6101684590809a2b2c2f0 |
| token_bridge.h | experiments/fastdds_socket/TokenBridge.h | cfb2615da8b8e47dff69e220b77410a51637edcb2c4d1db80991643bc8ee858e |

Repository HEAD at copy time: `507e31a91dce9d4f4165971fc922ba71f4884f80`

The table records the initial copy provenance. The reviewed installed-runtime
consumer subsequently adapted the identity `Begin` lifetime and the token
allowlist for the installed execution contract. Those adaptations are part of the
current files and are validated by the current source/package acceptance records;
the table's source hashes must not be read as claiming that the current files are
byte-identical to the initial copies.

`main.cpp` includes no MDDS probe, test, artifact or private header. The identity
component is compiled into the experimental launcher/consumer executable only to
set and restore the process access token; it does not touch the MDDS public
Runtime, descriptor or Fast DDS path. The installed consumer removes the original
two-token compile-time allowlist; expected identity is a frozen external input.
CLI/preflight/range, single-main-task and SDK owned/normal/granted checks remain.
The fixed process identity is `fastdds_dsoftbus_probe`
(`ohos.permission.DISTRIBUTED_DATASYNC`). The permission itself is written and
removed only by the separately reviewed `native_permission_probe`, never here.
