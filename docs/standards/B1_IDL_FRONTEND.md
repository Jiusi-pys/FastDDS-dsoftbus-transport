# B1 IDL frontend core

Implementation present, 2026-11-13. The independent review, closure review and
canonical final tester are complete for the applicable host scope. This
milestone is a host build tool; it does not change Fast DDS, Fast CDR, Fast
DDS-Gen, Transport, or device communication.

## Contract

`scripts/standards/compile_idl.py` invokes an explicitly selected Clang
preprocessor, then emits a JSON AST or a structured diagnostic with a nonzero
exit code. It records the executable SHA256, version output, command, compiler
exit code, input and preprocessed hashes, and hashes for existing source paths
seen in line markers. Clang is an external build dependency; it is not bundled
or copied into the repository. No implicit compiler selection or fallback is
used. Preprocessing has a bounded timeout and preserves include diagnostics.

```powershell
$clang = 'C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native/llvm/bin/clang.exe'
python -B scripts/standards/compile_idl.py tests/standards_idl/positive/module.idl `
  --clang $clang --out build/b1-example.json
$env:IDL_CLANG = $clang
python -B tests/standards_idl_tests.py -v
```

The frontend implements the base declaration grammar from
[IDL 4.2](https://www.omg.org/spec/IDL/4.2/PDF), section 7.4.1, with lexical
rules from section 7.2 and name lookup from section 7.5: modules and reopening,
constants, primitive types, typedefs, bounded/unbounded strings and sequences,
wide character/string literals, fixed types, enums, structs, unions, arrays,
native declarations, inline constructed typedefs, and recursive sequence
types through struct/union forward declarations. Anonymous member templates
and arrays are rejected; use a typedef in this core profile.

AST schema 1 provides declarations, a symbol table including inline types,
resolved reference targets, source locations, member types, array dimensions,
bounds, discriminator labels and evaluated constants. Floating/fixed values
are decimal strings in JSON. Integer division truncates toward zero; negative
remainders follow the dividend. Floating evaluation uses decimal intermediates
and range diagnostics, and is not a claim of bit-exact IEEE constant folding.
Diagnostics report the first error with a stable code and location. Line
markers retain included file/line identity; columns refer to preprocessed
text after macro expansion.

## Explicit remaining scope

This is not a complete IDL 4.2 conformance claim. B2 owns extended data types,
interfaces, value/component/template building blocks, annotations and section 9
combination profiles. Non-ASCII identifiers and full numeric semantic/precision
validation remain outside the B1 profile. Unsupported syntax fails
explicitly rather than disappearing from the AST. B3 owns C++ generation and
Fast DDS-Gen differential execution; C and Java mappings remain B4 and B5.

The new corpus is `tests/standards_idl/corpus.json`, with separate positive and
negative IDL files. Tests assert diagnostic codes, selected exact locations,
resolved AST bindings, arithmetic, module reopening, include guards, macros,
conditional preprocessing, dependency identity, and CLI exit/output behavior.

## Validation status

Implementation: PRESENT. Host validation: HOST_TESTED PASS. The canonical
final tester record is [`build/b1-20261113/final-tester/FINDING.md`](../../build/b1-20261113/final-tester/FINDING.md),
with machine-readable details in [`summary.json`](../../build/b1-20261113/final-tester/summary.json)
and [`frontend-summary.json`](../../build/b1-20261113/final-tester/frontend-summary.json).
It records the targeted suite, compiler-backed corpus/AST sweep, R1–R3 closure
probes, legacy and XCDR2 regression runners, documentation consistency, and
whitespace checks as PASS. The earlier retest and review records remain at
[`initial-retest/FINDING.md`](../../build/b1-20261113/initial-retest/FINDING.md)
and [`review/`](../../build/b1-20261113/review/).

Native MSVC/OHOS compilation, RK3588A/DSoftBus execution, and DDS integration
are N/A for this host Python frontend milestone: no runtime or generated C/C++
changes are delivered. Those checks remain required for B3/B4. This is not a
full IDL 4.2 conformance claim: non-ASCII identifiers, bit-exact IEEE constant
evaluation, full numeric semantic/precision validation, the remaining B2
building blocks/annotations/profiles, and C/C++/Java mappings remain
unverified or planned. The pre-commit gate is owned by the orchestrator.
