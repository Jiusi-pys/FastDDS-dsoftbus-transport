# B2 IDL language frontend

Implementation present, 2026-11-13. Independent final host testing and review
closure are complete for the bounded frontend scope documented here.
This is a Windows-host build tool emitting a semantic JSON model. Code generation,
language mappings, middleware execution and native/device artifacts belong to
B3–B5. The Transport and Fast DDS semantics are unchanged.

## Entry points and profiles

```powershell
$clang = 'C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native/llvm/bin/clang.exe'
python -B scripts/standards/compile_idl.py input.idl --clang $clang `
  --profile b2 --out build/input-ast.json
$env:IDL_CLANG = $clang
python -B tests/standards_idl_b2_tests.py -v
python -B tests/standards_idl_tests.py -v
```

The default remains the bounded B1 mode for compatibility. `b2` selects all
building blocks and annotation groups. The seven named modes implement the
compositions listed in [IDL 4.2](https://www.omg.org/spec/IDL/4.2/PDF), section 9:
`minimum-corba`, `plain-corba`, `ccm`, `ccm-generic`, `plain-dds`,
`extensible-dds`, and `rpc-dds`. Unavailable blocks/groups cause `E_PROFILE`.
These are frontend profiles and authored combination fixtures, not a claim
that a corresponding CORBA/CCM/DDS runtime is supplied or formally certified.

## Building-block model

| Section | Block | Model retained in the AST |
| --- | --- | --- |
| 7.4.1 | Core | Modules, constants, type declarations, forward/recursive types and resolved names |
| 7.4.2 | Any | Explicit any type; annotation any values; ordinary any constants rejected |
| 7.4.3 | Interfaces basic | Operations, parameter directions, exceptions, attributes and raises |
| 7.4.4 | Interfaces full | Nested type, constant and exception declarations; inherited lookup |
| 7.4.5 | Value types | State visibility, initializers, value bases and supported interfaces |
| 7.4.6 | CORBA interfaces | Local interfaces, Object, oneway/context, type IDs, prefixes and imports |
| 7.4.7 | CORBA values | Abstract/custom values and interfaces, boxes, truncatable bases, ValueBase |
| 7.4.8 | Components basic | Component bases, provides/uses ports and attributes |
| 7.4.9 | Homes | Managed component, inherited home and factory declarations |
| 7.4.10 | CCM | Event types, supported interfaces, event ports, multiple uses, keys and finders |
| 7.4.11 | Ports/connectors | Port types, normal/mirrored port references and connectors |
| 7.4.12 | Templates | Classified formals, typed constants, deferred expressions, aliases and concrete instantiations |
| 7.4.13 | Extended types | Explicit integer types, maps, bitsets, bitmasks and extended aggregates |
| 7.4.14 | Anonymous types | Member arrays and nested anonymous template types |
| 7.4.15 | Annotations | Typed custom declarations, defaults, named/positional applications and unknown annotation records |

Schema 2 retains declarations, symbols, canonical references and source locations.
It also records the selected blocks/groups, inheritance, supports/manages/port
relationships, parameter modes, annotations, template formals and body tokens.
Generic expressions are represented as deferred expression trees. Instantiation
replays the template body with concrete bindings and rechecks constraints.
Inherited ambiguous lookups, incompatible bases, duplicate declarations and
invalid parameter/raises/oneway combinations fail explicitly.

## Annotation groups and context

The general, data-modeling, units/ranges, data-implementation, code-generation
and interface groups have explicit signatures in `idl_profiles.py`. Required
members, defaults, argument categories and enum choices are checked. Known
annotations are retained with their values; unknown annotations are retained
with `ignored=true` and have no frontend effect. Malformed syntax is an error.

This frontend's actual usage context is restricted as permitted by section 8.2.2:

| Concern | Supported context and frontend checks |
| --- | --- |
| General | IDs/positions on set elements, extensibility/autoid on constructed declarations, optional data, enum values; duplicate explicit sibling IDs/positions and contradictory extensibility rejected |
| Data modeling | Key and must-understand on struct/union members; one default enumerator marker per enum |
| Units/ranges | Data declarations and typedefs; typed and ordered bounds; constant/default values checked against declared constraints |
| Data implementation | Enum/bitmask/integer bit bounds, external data and nested constructed declarations; invalid widths/positions rejected |
| Code generation | Verbatim language, placement and text retained as metadata; no user text is executed or emitted as source in B2 |
| Interfaces | Service interfaces, oneway operations and async interface/operation metadata; one-way signature restrictions checked |

An absent annotation creates no explicit override in the model. Core members
remain required and non-key, and no external/nested/oneway/async request is
inferred. Parameter defaults apply when the annotation is present; e.g. a
present `autoid` selects HASH. Middleware-specific implicit IDs, representation,
generated accessors and runtime enforcement remain backend responsibilities.

## Identifiers and numeric model

Identifiers accept the IDL Latin-1 letter ranges and escaped keywords. Name
identity is case-sensitive with case-collision diagnostics. Other Unicode
identifiers are outside the IDL 4.2 lexical alphabet and are rejected; wide
literal values use the specified Unicode escapes.

Integer expressions retain width/signedness, apply the specified promotions,
check intermediate overflow, and use logical right shift. Negative integer
remainders follow truncation toward zero. Fixed decimal calculations use
double working precision followed by truncation to the specified significant
precision; unary signs and significant trailing-zero handling are exact.

Floating constants use exact rational intermediates and nearest/ties-even
rounding for binary32, binary64 and an explicit minimum extended format with
a 64-bit significand and 15-bit exponent. This is independent of the Windows
native long-double ABI. Normal/subnormal values, signed zero and overflow are
modeled. IDL range validation precedes rounding: an exact real value beyond the
finite format maximum is diagnosed, even when a host language would round it
back to its largest finite value. The JSON coefficient/exponent pair is exact; its decimal string is a
presentation value. A backend must use the declared format and signed-zero
metadata when generating target code.

## Offline interface-repository imports

A CORBA string import identifies a repository scope, not a filename. Previously
available scopes can be imported by their explicit or default ID. External
repository scopes use explicit local source bindings, for example:

```powershell
python -B scripts/standards/compile_idl.py consumer.idl --clang $clang `
  --profile plain-corba --idl-import 'IDL:Types:1.0=types.idl' `
  --out build/consumer-ast.json
```

The referenced source is preprocessed with the same recorded options, linked
into the semantic compilation unit and checked for the requested repository
identity. Commands, tool/input hashes, dependency hashes and import stages
are retained. Identical shared physical source lines are coalesced across
import units; macro-dependent disagreements fail explicitly. Missing scopes,
ambiguous IDs and cyclic import dependencies are errors. No live CORBA
Interface Repository, network resolver or middleware runtime is provided.
Opaque non-IDL repository ID formats receive structural validation; their
format-specific external registry semantics are not a B2 runtime claim.

The driver protects input, compiler and bound-source file identities, including
hardlinks, and replaces output atomically. Resource limits reject excessive
template/import/numeric nesting rather than retrying indefinitely.

## Evidence and status

The B2 catalog is `tests/standards_idl/b2/corpus.json`. It owns its nested
positive/negative fixtures and seven composition controls. B1's catalog still
owns the original fixtures; an inventory test checks that the union accounts
for every IDL fixture. B1 behavior is exercised unchanged through its mode.

Implementation: PRESENT. Independent final host validation: PASS. The canonical
tester record is [`build/b2-20261113/final-tester/FINDING.md`](../../build/b2-20261113/final-tester/FINDING.md),
with machine-readable results in [`validsummary.json`](../../build/b2-20261113/final-tester/validsummary.json).
It records 33 B2 tests, 16 B1 compatibility tests, 306 compiler-backed cases,
20 semantic probes, 93 standards-regression tests, 22 XCDR2-regression tests,
strict documentation consistency and staged-whitespace checks as PASS. Review
R1–R5 and C1 are CLOSED; see [`REPORT.md`](../../build/b2-20261113/review/REPORT.md),
[`CLOSURE.md`](../../build/b2-20261113/review/CLOSURE.md) and
[`C1_CLOSURE.md`](../../build/b2-20261113/review/C1_CLOSURE.md). The initial
census filename collision remains retained as a historical FAIL with its
corrective rerun documented in [`EVIDENCE_NAMING_FIX.md`](../../build/b2-20261113/EVIDENCE_NAMING_FIX.md);
the historical census numbers are unchanged.

This PASS is HOST_TESTED and bounded to the Python frontend and schema-2 JSON
model. Native build, RK3588A, DDS, DSoftBus and cross-device integration are
N/A because B2 produces no native, generated or deployable artifact. B1's
default CLI mode retains its bounded-core contract; B2 additionally validates
the numeric and precision behavior listed above. Formal exhaustive IDL
conformance, native language mappings and middleware/runtime behavior are not
inferred from finite corpus coverage. Documentation synchronization is complete
for this record; the Main/Orchestrator PRE_COMMIT decision remains external.
