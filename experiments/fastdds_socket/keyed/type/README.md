# Experimental bounded keyed type

`DSoftBusKeyedSample.idl` is the source of this generated DDS type. Only the uint32
`key` member contributes to the instance key. `run`, `writer`, `ordinal`, and
the 32-byte payload are ordinary non-key fields. The type is final and bounded.

The seven C++ generated files are unedited Fast DDS Gen 4.3.0 output.
`GENERATION_PROVENANCE.json` records the explicit Java/JAR paths, their hashes,
the exact generation command, and the hashes of the IDL and generated files.
The generator's installation directory contains `3.6.1.0`; this is its bundle
label. The tested Fast DDS runtime is the repository's fixed **3.6.2.0** build.
Generation is not a configure-time, build-time, or runtime requirement because
the generated files are retained.

To regenerate for a source comparison, select a **new output directory**, use
the recorded Java/JAR and IDL, and keep `-ppDisable -flat-output-dir`. Compare all
seven outputs with the receipt before changing retained source. Do not use
`-replace` against a sealed source or artifact directory.

The `experiment_keyed_type` CMake target is shared by the host and native
fixtures. As a standalone host project, this directory uses the repository's
existing `FastDDSBinding.cmake`, an explicit `fastdds_DIR`, the declared host
prefix, and Release DLL ABI. It does not select an arbitrary installed DDS.

`keyed_type_tests` has six independently selected contracts: generated key
identity, non-key independence, a known-answer forced MD5, both XCDR roundtrips
and truncated-buffer rejection, runtime registration of the complete TypeObject,
and actual keyed DDS Writer/Reader creation plus public Writer registration.
The last contract uses a host loopback reference transport only. These type
tests do not constitute DSoftBus device evidence or Reader `get_key_value`
support. The real generated maxima observed by these tests are 60 bytes for
XCDR1, 56 bytes for XCDR2, and a 60-byte maximum serialized payload including
encapsulation; they are independent of RTPS and native message overhead.
