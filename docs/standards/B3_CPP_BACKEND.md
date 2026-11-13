# B3 independent DDS C++ backend

Final documentation sync, 2026-11-13. This milestone consumes B2's schema-2 semantic
model and emits C++ through public Fast DDS and Fast CDR APIs. It changes no
Fast DDS, Fast CDR, DDS/RTPS or DSoftBus implementation.

## Generation and accepted data subset

```powershell
python -B scripts/standards/generate_cpp.py integrations/standards/idl/BoundedRecord.idl `
  --clang C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native/llvm/bin/clang.exe `
  --fastdds 3.6.2 --out build/my-b3-generated
```

The destination must be new. The driver records the frontend/preprocessor
evidence, source and dependency identities, backend sources, target ABI and
all generated file hashes in `generation.json`. Output is published from an
owned staging directory. This emitter does not invoke Fast DDS-Gen or copy a
pre-generated fixture. The legacy `generate_types.py` entry stays available.

The shared DDS data subset includes modules, integer/boolean constants,
scoped enums, typedefs, primitive integer/boolean/char/octet/float/double
members, bounded narrow strings and sequences, multidimensional arrays,
nested nonrecursive structs, and FINAL/APPENDABLE extensibility. Members use
sequential IDs. Keys can be primitive, enum or bounded narrow-string members.
Generated accessors, versioned PubSubType classes and CdrAux files retain the
shape accepted by the existing `generated_roundtrip.cpp` harness. The 3.6.2
backend also emits public TypeObject registration for this subset.

B3's native mapping is narrower than B2's language model. Unsupported
declarations or annotations cause `E_BACKEND`; they are not silently omitted.
In particular, mutable/optional data, unions, recursive data, inheritance,
wide strings, long double/fixed/maps/bitsets, interfaces/RPC and custom mapped
annotation effects require later backend work. Unknown annotations already
marked ignored by the frontend retain that IDL behavior. Backend names must
be portable ASCII C++ identifiers; C++ keywords and nested type declarations
are rejected. Strings/sequences and each array extent have finite bounds
from 1 through 65535. A conservative sample-capacity ceiling is 1 MiB.

## Serialization and differential acceptance

Fast CDR performs XCDR1/XCDR2 encoding and size calculation. Generated
validation checks declared bounds and enums; failed deserialization commits
no partial destination. Key computation uses a local buffer per invocation,
big-endian key encoding and the target release's CDR version/MD5 rule. The
MD5 threshold follows the pinned generator's declared maximum-key metadata,
independent of the selected payload representation. The
maximum sample capacity is a conservative bound, not a claim that the C++
memory layout is plain or loanable. No shared mutable key buffer is used.

`scripts/standards/run_cpp_differential.py` compares this backend against
hash/version-bound Fast DDS-Gen 3.3.1 (Fast DDS 2.14.5) or 4.3.0 (3.6.2).
It supplies both parsers with the same recorded preprocessor output, builds
independent executables, and compares actual serialized bytes, instance keys
and, on 3.6.2, Minimal/Complete TypeIdentifiers. `--dds` runs both generated
writer/reader directions with XCDR1 and XCDR2 over UDP. `--build-only` records
BUILDS with execution/comparison NOT_RUN; it cannot be combined with `--dds`.
Each invocation requires a new evidence directory.

The fixtures are the original keyed `BoundedRecord.idl` and `B3Cases.idl`,
which adds enum/alias/array/nested-sequence, alignment, extreme integer,
bounded empty/maximum data, appendable and multi-member/string-key controls.
The own-backend harness also tests invalid bounds, small buffers, truncated
decode, unchanged failure destinations/keys and concurrent serialization.
The CMake gates verify generated files and generation input hashes before
building; the original roundtrip entry verifies additional runtime helpers.

`B3Combined.idl` includes both fixtures and wraps their types. The combination
target includes and links all three independently generated artifacts together,
testing shared declarations across directories and translation units. Runtime,
data, serialization and registration guards reject incompatible backend
revisions, ABIs or duplicate type definitions. TypeObject registration uses
inline definitions with deterministic names to preserve this composition.
Nested collection controls include arrays of sequences and sequences of
sequences. Each recursion level owns its helper names and registration labels.
For the 3.6.2 ABI, anonymous nested collections containing named elements need
an explicit typedef for the inner collection. That target SDK's public
TypeObject validation also rejects the pinned generator's anonymous form;
the backend diagnoses it with `E_BACKEND` before publication. Primitive/string
anonymous nesting and typedef-wrapped complex nesting remain supported.
The 2.14.5 data backend retains the anonymous complex form. This is an explicit
version-bound backend limit; B2's semantic model is unchanged.
User declarations that collide with generated support or registration symbols
are rejected before publishing C++.
Additional controls exercise nested `std` name shadowing and distinct
qualified names that would collide if anonymous registry labels were flattened.

The generated data API supplies const-reference getters and mutable-reference
getters. References obtained from a temporary data object must not outlive
that object. Wire and TypeIdentifier equality do not imply source or binary
identity with Fast DDS-Gen output; full language-mapping certification is not
claimed by this finite consumer contract.

## Final validation boundary

| Dimension | State |
| --- | --- |
| Implementation | PRESENT for the bounded shared subset |
| Build | PASS; final host and native artifacts |
| Host test | PASS; 11 B3 tests, B2/B1 regressions, standards/XCDR2 regressions, ABI differential, host DDS, combined and legacy CTest lanes |
| Device test | PASS; 32 exact cross-board UDP scenarios across both RK3588A physical roles |
| Integration test | PASS for the exercised public DDS/UDP scope |
| Review | PASS; R1–R3 CLOSED with no actionable findings |
| Overall product | NOT VERIFIED; B4 C and B5 Java/JNI remain PLANNED |

Authoritative host evidence is [`FINAL_HOST_FINDING.md`](../../build/b3-20261113/final-host/FINAL_HOST_FINDING.md); native evidence is [`FINAL_NATIVE_FINDING.md`](../../build/b3-20261113/final-native/FINAL_NATIVE_FINDING.md). Review evidence is [`REPORT.md`](../../build/b3-20261113/review/REPORT.md) and [`CLOSURE.md`](../../build/b3-20261113/review/CLOSURE.md). The final host record includes the strict documentation-consistency and diff checks.

DSoftBus is **N/A** for B3: no DSoftBus Transport source changed and the
validated path is public DDS over UDP. B4 owns DSoftBus-backed C acceptance.
This finite subset is not exhaustive IDL, XTypes Complete or language-mapping
certification; C and Java/JNI remain B4/B5.
