# Distribution and attribution inventory

This foundation package does not distribute platform SDKs/DSOs, board identities,
tokens, permissions or private raw logs. Legal platform inputs must be obtained
externally from their authorized supplier. This inventory is not a grant of rights
to third-party binaries or a release-license decision for first-party code.

| Material | Source / license | Distribution action |
| --- | --- | --- |
| Fast DDS 2.14.5 baseline and source-bound 2.14.7 candidate, Fast CDR 2.2.5 baseline and source-bound 2.2.8 candidate, and generated example type sources | eProsima official repositories; Apache-2.0; original generated copyright notices retained | When distributed, preserve license/NOTICE and changed-file notices; generated example license installed with package; candidate release/git commits are documented in the maintenance candidate receipt |
| foonathan_memory 0.7.4 | foonathan/memory; zlib license | External dependency; retain its copyright/license when separately distributed |
| TinyXML2 Host 10.0.0 and API23 target 6.0.0 | leethomason/tinyxml2; zlib license in source header | Separate external dependency inputs; source/version and retained notice needed for release; no bundling or upgrade is implied |
| Asio | chriskohlhoff/asio; Boost Software License 1.0 | Header dependency; retain license and bind actual build input version |
| ROS 2 Jazzy packages | Official per-package repositories and package.xml/license files | Existing original RMW is unchanged; per-package SBOM/license obligations are not replaced by a distro-wide label |
| LLVM/OpenHarmony toolchain, SDK and native platform libraries | OpenHarmony/vendor-authorized distribution and terms | Not bundled; source checkout license does not authorize redistribution of vendor binary images |
| First-party Transport / new tooling | No root first-party release LICENSE was located | FIRST_PARTY_LICENSE_DECISION_REQUIRED before public release; owner must approve license and rights. This task does not invent ownership or apply Apache to all first-party files |

Upstream Fast DDS 2.14.5 base is `ebcb5c05e9da9e262041e8616bbaff61ab40a296`.
The source-bound maintenance candidate uses Fast DDS `010b908dc3bb2b6f2818d61ac8db462d01b79a1f`
and Fast CDR `1bc9c919311ffce555b445560bf7ae062a4e1c72`; these inputs do not settle
first-party licensing or vendor SDK/DSO distribution rights.
Existing factory/endpoint/XML and teardown patches are documented by
`integrations/ros2/UPSTREAM.json`, `TEARDOWN_UPSTREAM.json`,
`RESIDUAL_UPSTREAM.json`, `TOPIC_RTTI_UPSTREAM.json` and their scripts/patch files.
They are integration and diagnostic changes, not security repairs, and their
base hashes must be refreshed when a maintenance migration is approved. Their
existence is not an unmodified upstream binary claim. No RMW patch is added.

Neither private `.artifacts` nor original developer-machine paths are a public
dependency source. Public sources: [Fast DDS](https://github.com/eProsima/Fast-DDS),
[Fast CDR](https://github.com/eProsima/Fast-CDR),
[foonathan/memory](https://github.com/foonathan/memory),
[TinyXML2](https://github.com/leethomason/tinyxml2),
[Asio](https://github.com/chriskohlhoff/asio),
[ROS 2 repositories](https://github.com/ros2/ros2),
[OpenHarmony DSoftBus](https://gitcode.com/openharmony/communication_dsoftbus).
