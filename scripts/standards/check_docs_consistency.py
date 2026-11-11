"""Check the numbers in docs/standards against the sources that establish them.

Six consecutive tester passes found a wrong number in `docs/standards/`, and
several were introduced by the fix for the previous one. The guards therefore
live here, are runnable on their own, and are exercised by
`tests/standards_runner_tests.py` both against the real documents and against
deliberately mutated copies.

Two earlier versions of this file were themselves defective, in the same way
each time: the guard could not reach the fault, and its own validation could not
reach the guard.

  - A digest guard used `\\b` inside a non-raw string, stored two literal
    backspace bytes, matched nothing, and passed while enforcing nothing.
  - A later digest guard compared a token only when its first eight characters
    already matched, so a digest corrupted inside those eight characters was
    never compared; its self-tests mutated only the final character and so could
    not see it.

Hence the present design: every hex token in the documents must be a prefix of a
digest declared below, each with the source that establishes it. A corruption
anywhere in a digest, at any position, produces a token that matches nothing and
is reported. Adding a new hash to the documents requires declaring it here with
its provenance, which is the point.

Run standalone:

    python scripts/standards/check_docs_consistency.py
    python scripts/standards/check_docs_consistency.py --docs DIR --unit-count N

Every guard always runs. The tool never prints a bare success line while a guard
has been skipped.

The five limitations the 2026-11-10 gate-4 review declared are addressed
(2026-11-10, later the same day):

  - **Attribution** is checked: each digest carries `require`/`forbid` context
    patterns, and the text that introduces a cited token must attribute it to
    the artifact whose digest it is (`check_digests`). Swapping the
    `libfastrtps` and `libfastcdr` digests now reports.
  - **More measured quantities** are checked: the patched-matrix run and
    peer-process counts and the gate-on received counts against
    `build/device-patched-20261110/` and `build/final-tester-20261110/f9/`
    (`check_patched_matrix`), and the `schema_version` census and the named
    gate-evidence directories' gate fields against `build/`
    (`check_schema_census`).
  - **The reserved-port table's other columns** are checked for internal
    consistency: `free` rows must say PASS, range rows must say BLOCKED with
    the metatraffic port inside the range (`check_reserved_port_table`).
  - **232** is no longer blanket-exempt: it goes through the same
    domain-table membership check as any other listed number. Only 250 (the
    port multiplier), 139 (the crash exit) and 151 (the DSoftBus domain
    constant) remain exempt.
  - **Per-document presence** is checked: `REQUIRED_CLAIMS` names the claim
    kinds each document must carry, so neutering every CTest claim in one
    document reports even while another document retains one
    (`check_required_claims`).

What this still does NOT check, declared so the limits are known rather than
assumed; none is a statement that is wrong in the documents today:

  - The host's reserved UDP ranges are not re-measured with `netsh`; the
    reserved-port table is checked for internal column consistency only.
  - Quantities written as English words ("one 129 and three 130", "Eight
    domains") are not parsed; only their digit forms are checked.
  - Counts whose raw logs were never retained (the 40/20/15/20 crash-rootcause
    controls, the ssh-device-tester 63/62-style sample counts) cannot be
    measured here and are not checked.
  - Attribution checks the text that introduces a digest, not the artifact
    itself; a document that re-labels an artifact and moves its digest in the
    same edit still passes.
  - The patched-matrix and census guards measure retained `build/` evidence,
    which is gitignored and exists only on the machine that ran the devices.
    On any other checkout, run with `--evidence-optional`: those guards are
    then printed as SKIPPED (never silently dropped) while every
    document-only guard still enforces. Without the flag, a missing evidence
    root fails, so the machine that owns the evidence cannot lose it quietly.
  - The census is a live measurement; retaining any new `results.json`
    without gate fields under `build/` requires updating the documented
    count, which is intended, not incidental.

A caller wanting more should add a guard here with a mutation self-test in
`DocumentationGuardSelfTests`, which is the mechanism that caught the two
earlier guards that could not fail.
"""
import argparse
import hashlib
import json
import re
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DOCS = REPO / "docs/standards"
TESTS = REPO / "tests"
PATCH = REPO / "patches/fastdds/2.14.5/0001-edp-honor-reader-type-consistency-qos.patch"

# Numbers that sit beside domain talk and are not domain ids: the port
# multiplier, the crash exit code, and the DSoftBus domain constant. 232 is NOT
# here: it is a real domain row, so it must pass the same table-membership
# check as any other listed number. Its other role, the runner's domain cap, is
# written as "capped at 232" / "0..232", which the domain-reference patterns do
# not capture, so no exemption is needed for it.
NON_DOMAIN_CONSTANTS = frozenset({250, 139, 151})

# Digests the documents are permitted to cite. A value is either measured from a
# file present in this tree, or declared with the retained record that
# establishes it. Tokens in the documents must be a prefix of one of these.
MEASURED_DIGESTS = {
    "patch, current working tree": PATCH,
}
DECLARED_DIGESTS = {
    # Historical patch revisions. The hunks never changed; only the explanatory
    # header did, so all three apply to pristine v2.14.5 and yield the same
    # source. See build/final-fix-20261110/FINDING.md.
    "patch, as built and deployed":
        "7fd2fb0429ddaa30c7b7b8ff5b6594681201f7d0c1ce3a920343390d09c3bba9",
    "patch, after the gate-scope disclosure":
        "0c581ee6304cedae94808c284cf3614744fe0c1ca5194ce8d1eb726de76a90e9",
    # Device deployment, recorded in build/device-patched-20261110/FINDING.md.
    # The two libraries reproduce from /e/fdds-patched-20261110/2.14.5/prefix/lib;
    # the probe does NOT live there, it reproduces from
    # build/ohos-patched-214/dds_type_probe.
    "device libfastrtps.so.2.14 (patched)":
        "416179dd29e033d97964a1654fa601863ebbcb68154abb6dd092c77088a8ea4d",
    "device libfastcdr.so.2 (Fast CDR 2.2.5)":
        "113f8ef51c667c29c729dafc5fa20d0038688c217cd5bea99d4f13e2661d6b00",
    "device dds_type_probe (OHOS arm64)":
        "9602b973ea7f8f22ec61db016b21350e2f89a4ba423f39cf258543e530129455",
    # The 3.6.2-line Fast CDR whose soname collision caused the exit 139.
    # Retained in build/ssh-device-tester-20261110/mixed-libsha-A.log.
    "Fast CDR 2.3.7 (3.6.2 line)":
        "d735d942b10df5e63c98df3682cd95b4aba2c461d51554bfb52305900820e7ee",
}

# Attribution: the text that introduces a cited digest must attribute it to the
# artifact whose digest it is. For a token in a markdown table row the scope is
# that row; in prose it is the text between the previous hex token (bounded to
# the paragraph) and this one. `require` must match the scope, `forbid` must
# not. The forbid patterns are what distinguish the two `libfastcdr.so.2`
# builds, whose rows both contain the library name.
DIGEST_CONTEXT = {
    "patch, current working tree": {"require": r"(?i)working[- ]tree"},
    "patch, as built and deployed": {"require": r"(?i)as built|fastdds_patch"},
    "patch, after the gate-scope disclosure": {"require": r"(?i)gate-scope disclosure"},
    "device libfastrtps.so.2.14 (patched)": {"require": r"libfastrtps"},
    "device libfastcdr.so.2 (Fast CDR 2.2.5)":
        {"require": r"libfastcdr", "forbid": r"2\.3\.7"},
    "device dds_type_probe (OHOS arm64)": {"require": r"dds_type_probe"},
    "Fast CDR 2.3.7 (3.6.2 line)":
        {"require": r"(?i)libfastcdr|fast ?cdr", "forbid": r"2\.2\.5"},
}

# Prefix for problems that mean "the retained evidence is not on this
# machine" rather than "a document is wrong". --evidence-optional downgrades
# exactly these to printed SKIPPED lines; everything else always fails.
MISSING_EVIDENCE = "[missing evidence] "

# Evidence the documents describe by name; measured by the guards below.
DEVICE_PATCHED = REPO / "build/device-patched-20261110"
F9_RERUN = REPO / "build/final-tester-20261110/f9"
MIXED_DEFAULT = REPO / "build/mixed-default-20261111"
BUILD_ROOT = REPO / "build"
GATE_EVIDENCE = {"review-fix-gate-off": False, "review-fix-gate-on": True}
SCHEMA_V1_EVIDENCE = ("prepatch-214", "patched-gate-off", "patched-gate-on")

# The claim kinds each document must carry. A document losing every instance of
# a required kind reports here even while another document retains one.
REQUIRED_CLAIMS = {
    "ARCHITECTURE.md": {"unit_ratio", "ctest", "digest"},
    "BASELINE.md": {"unit_ratio", "unit_prose", "ctest", "domain_table",
                    "reserved_table", "digest"},
    "DEVICE_EVIDENCE_20261110.md": {"unit_ratio", "ctest", "digest"},
}

HEX_TOKEN = re.compile(r"(?<![0-9a-fA-F])[0-9a-fA-F]{8,64}(?![0-9a-fA-F])")
NN_CLAIM = re.compile(r"(\d+)\s*/\s*(\d+)\s+(?:PASS|unit)", re.IGNORECASE)
N_PASSED = re.compile(r"(?<![\d/])(\d+)\s+passed(?![\d/])")
CTEST_NN = re.compile(r"CTest\s+(\d+)\s*/\s*(\d+)")
TABLE_ROW = re.compile(r"^\|\s*(\d{1,3})\s*\|\s*(\d{4,5})\s*\|", re.MULTILINE)
RESERVED_ROW = re.compile(
    r"^\|\s*(\d{1,3})\s*\|\s*(\d{4,5})\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|",
    re.MULTILINE)
RANGE_CELL = re.compile(r"^(\d{4,5})\s*-\s*(\d{4,5})$")
RUNS_CLAIM = re.compile(r"(\d+) cross-board case runs")
PEERS_CLAIM = re.compile(r"(\d+) (?:peer processes|per-peer `?\.out`? files)")
RECEIVED_CLAIM = re.compile(r"(\d+) received")
RECEIVED_FIELD = re.compile(r"received: (\d+)")
CENSUS_CLAIM = re.compile(r"(\d+)\s+of\s+them\s+at\s+the\s+time\s+of\s+writing")
RESULT_LINE = re.compile(r"DDS_STANDARDS_RESULT (\{.*\})")
GATEON_READER = re.compile(r"^(?:AtoB|BtoA)-on-(?:appendable|mutable)_added_member-r\.out$")
NUMBER_LIST = re.compile(r"(?<![\d.])\d{2,3}(?:\s*,\s*\d{2,3})*\s+and\s+\d{2,3}(?![\d.])")
DOMAIN_ADJACENT = re.compile(r"domains?\s+(\d{2,3})(?![\d.])", re.IGNORECASE)
SMALL_INT = re.compile(r"(?<![\d.\-/])(\d{2,3})(?![\d.\-/])")


def sha256(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def documents(docs=None):
    directory = Path(docs) if docs else DOCS
    return sorted(directory.glob("*.md")) if directory.is_dir() else []


def known_digests(patch=None):
    """Every digest the documents may cite, with the source of each."""
    digests = dict(DECLARED_DIGESTS)
    for name, path in MEASURED_DIGESTS.items():
        target = Path(patch) if (patch and path is PATCH) else path
        if Path(target).is_file():
            digests[name] = sha256(target)
    return digests


def measure_suite_count(tests=None):
    """Count the tests in the suite, so no caller has to assert the number.

    Returns (count, problems). A discovery failure is reported, never silently
    treated as zero.
    """
    directory = Path(tests) if tests else TESTS
    if not directory.is_dir():
        return None, ["test directory not found: %s" % directory]
    try:
        suite = unittest.TestLoader().discover(
            str(directory), pattern="standards_runner_tests.py",
            top_level_dir=str(directory))
    except Exception as error:                      # pragma: no cover - defensive
        return None, ["could not load the test suite: %s" % error]
    broken = [str(case) for case in _flatten(suite) if "_FailedTest" in type(case).__name__]
    if broken:
        return None, ["the test suite failed to import: %s" % "; ".join(broken[:3])]
    count = suite.countTestCases()
    if not count:
        return None, ["the test suite reported zero tests"]
    return count, []


def _flatten(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from _flatten(item)
        else:
            yield item


def domain_table(docs=None):
    """Every domain/port pair documented in a markdown table."""
    pairs = []
    for document in documents(docs):
        for domain, port in TABLE_ROW.findall(document.read_text(encoding="utf-8")):
            pairs.append((document.name, int(domain), int(port)))
    return pairs


def check_unit_count(expected, docs=None):
    """Documented unit-test counts must equal the number of tests that exist.

    The `N/N` form and the prose `N passed` form are counted separately, so
    removing the only instance of either makes this guard report rather than
    fall silent.
    """
    problems, ratio_seen, prose_seen = [], 0, 0
    for document in documents(docs):
        text = document.read_text(encoding="utf-8")
        for first, second in NN_CLAIM.findall(text):
            if (first, second) == ("1", "1"):
                continue                            # CTest, see check_ctest_count
            ratio_seen += 1
            if first != second:
                problems.append("%s claims %s/%s, which is not a clean pass"
                                % (document.name, first, second))
            elif int(first) != expected:
                problems.append("%s documents %s/%s unit tests but the suite has %d"
                                % (document.name, first, second, expected))
        # Only where a test count can live. "230 passed every attempt" is a
        # DOMAIN, and conflating the two is how a regex repair once rewrote that
        # domain into the unit-test count.
        for line in text.splitlines():
            if "pytest" not in line and "unittest" not in line:
                continue
            for value in N_PASSED.findall(line):
                prose_seen += 1
                if int(value) != expected:
                    problems.append("%s says '%s passed' but the suite has %d"
                                    % (document.name, value, expected))
    if not ratio_seen:
        problems.append("no documented N/N unit-test count found at all")
    if not prose_seen:
        problems.append("no documented 'N passed' unit-test count found at all")
    return problems


def check_ctest_count(docs=None):
    """Each release runs exactly one CTest test, so any `CTest N/N` must be 1/1."""
    problems, found = [], 0
    for document in documents(docs):
        for first, second in CTEST_NN.findall(document.read_text(encoding="utf-8")):
            found += 1
            if (first, second) != ("1", "1"):
                problems.append("%s claims CTest %s/%s; each release runs exactly one test"
                                % (document.name, first, second))
    if not found:
        problems.append("no documented CTest count found at all")
    return problems


def check_port_arithmetic(docs=None):
    """Every documented domain/port pair must satisfy 7400 + 250 * domain."""
    pairs = domain_table(docs)
    problems = ["%s: domain %d with port %d contradicts 7400 + 250 * %d"
                % (name, domain, port, domain)
                for name, domain, port in pairs if port != 7400 + 250 * domain]
    if not pairs:
        problems.append("no domain/port table rows found at all")
    return problems


def check_domain_references(docs=None):
    """Numbers written as domains must be domains the table knows about.

    This is the guard for the defect that prompted this file: a regex rewrote the
    domain `230` into a unit-test count inside prose. A table-row check cannot
    see that, and an upper-bound check cannot either, because the substituted
    value was below the domain ceiling.

    Only numbers actually presented as domains are examined: members of a list
    like "219, 222, 225 and 230", and numbers written straight after the word
    "domain". Scanning every small integer in any paragraph mentioning domains
    reported ordinary quantities such as "251 samples" as domains.
    """
    docs_list = documents(docs)
    known = {domain for _, domain, _ in domain_table(docs)}
    if not known:
        return ["no domain/port table rows found at all"]
    problems, examined = [], 0
    for document in docs_list:
        # Paragraphs, not sentences: these documents are hard-wrapped, and a
        # sentence pattern that cannot cross a newline inspects almost nothing.
        for paragraph in re.split(r"\n\s*\n", document.read_text(encoding="utf-8")):
            if "domain" not in paragraph.lower():
                continue
            # Members of a list are a result set drawn from the domain table,
            # so each must be in it: that is what catches a foreign value
            # substituted into "219, 222, 225 and 230".
            listed = []
            for listing in NUMBER_LIST.findall(paragraph):
                listed += SMALL_INT.findall(listing)
            # A lone "domain 190" is a legitimate reference to a domain the
            # table does not inventory, so only the runner's range applies.
            singles = DOMAIN_ADJACENT.findall(paragraph)
            for number in listed + singles:
                value = int(number)
                if value in NON_DOMAIN_CONSTANTS:
                    continue
                examined += 1
                if value > 232:
                    problems.append("%s names %d as a domain, above the 0..232 range "
                                    "the runner accepts" % (document.name, value))
            for number in listed:
                value = int(number)
                if value in NON_DOMAIN_CONSTANTS or value > 232:
                    continue
                if value not in known:
                    problems.append("%s lists %d among domains drawn from the domain "
                                    "table, but it is not in that table"
                                    % (document.name, value))
    if not examined:
        problems.append("no domain references found to check at all")
    return problems


def check_digests(docs=None, patch=None):
    """Every hex token in the documents must be a prefix of a declared digest.

    A corruption at any position, including inside the first characters, yields
    a token that matches nothing. Purely decimal tokens are skipped: the
    documents cite faultlogger ids and dates that are not hashes.
    """
    digests = known_digests(patch)
    current = digests.get("patch, current working tree")
    problems, found = [], 0
    if current is None:
        problems.append("patch file not found, so its digest could not be measured")
    for document in documents(docs):
        text = document.read_text(encoding="utf-8").replace("`", "")
        previous_end = 0
        for match in HEX_TOKEN.finditer(text):
            raw = match.group(0)
            if raw.isdigit():
                continue                            # a date or a faultlogger id
            token = raw.lower()
            found += 1
            matches = [name for name, value in digests.items() if value.startswith(token)]
            if not matches:
                problems.append("%s cites %s, which is not a prefix of any declared "
                                "digest; measure it and declare it in "
                                "check_docs_consistency.py" % (document.name, raw))
            else:
                problems += _attribution_problems(document.name, text, match,
                                                  previous_end, matches)
            previous_end = match.end()
    if current is not None and not any(
            current in document.read_text(encoding="utf-8").replace("`", "")
            for document in documents(docs)):
        problems.append("no document states the patch's full sha256 %s" % current)
    if not found:
        problems.append("no digests found in the documents at all")
    return problems


def _attribution_problems(name, text, match, previous_end, matches):
    """The scope that introduces a token must attribute it to its artifact.

    Scope: the token's table row when it sits in one, otherwise the prose
    between the previous hex token (bounded to this paragraph) and the token.
    The token passes if ANY matched digest's context accepts the scope, so a
    digest declared without context patterns is never reported here.
    """
    start = match.start()
    line_start = text.rfind("\n", 0, start) + 1
    if text[line_start:line_start + 1] == "|":
        line_end = text.find("\n", start)
        scope = text[line_start:line_end if line_end != -1 else len(text)]
    else:
        paragraph_start = text.rfind("\n\n", 0, start)
        paragraph_start = 0 if paragraph_start == -1 else paragraph_start + 2
        scope = text[max(previous_end, paragraph_start):start]
    acceptable = []
    for digest_name in matches:
        context = DIGEST_CONTEXT.get(digest_name)
        if context is None:
            return []                               # declared without context
        ok = bool(re.search(context["require"], scope))
        if ok and "forbid" in context:
            ok = not re.search(context["forbid"], scope)
        if ok:
            return []
        acceptable.append(digest_name)
    return ["%s cites %s where the surrounding text does not attribute it to %s"
            % (name, match.group(0), " or ".join("'%s'" % d for d in acceptable))]


def check_reserved_port_table(docs=None):
    """The reserved-port table's columns must agree with each other.

    `free` rows must say PASS; range rows must say BLOCKED with the
    metatraffic port inside the range. The host's excluded ranges are not
    re-measured here; this is the internal consistency of what is written.
    """
    problems, found = [], 0
    for document in documents(docs):
        for domain, port, cell, result in RESERVED_ROW.findall(
                document.read_text(encoding="utf-8")):
            found += 1
            port = int(port)
            result = result.strip()
            if result not in ("PASS", "BLOCKED"):
                problems.append("%s: domain %s has Result cell %r, which is neither "
                                "PASS nor BLOCKED" % (document.name, domain, result))
                continue
            if cell.strip().lower() == "free":
                if result != "PASS":
                    problems.append("%s: domain %s is 'free' but marked %s, not PASS"
                                    % (document.name, domain, result))
                continue
            bounds = RANGE_CELL.match(cell.strip())
            if not bounds:
                problems.append("%s: domain %s has reserved-range cell %r, which is "
                                "neither 'free' nor 'lo-hi'" % (document.name, domain, cell))
                continue
            low, high = int(bounds.group(1)), int(bounds.group(2))
            if not low <= port <= high:
                problems.append("%s: domain %s port %d lies outside its own reserved "
                                "range %d-%d" % (document.name, domain, port, low, high))
            if result != "BLOCKED":
                problems.append("%s: domain %s is inside a reserved range but marked "
                                "%s, not BLOCKED" % (document.name, domain, result))
    if not found:
        problems.append("no reserved-port table rows found at all")
    return problems


def _parse_record(raw, origin, problems):
    """One probe/evidence JSON record, or None with the fault reported."""
    try:
        return json.loads(raw)
    except ValueError as error:
        problems.append("%s holds malformed JSON: %s" % (origin, error))
        return None


def _gateon_received(evidence, problems):
    """The gate-on appendable/mutable reader received counts, measured."""
    values = []
    for out in sorted(Path(evidence).glob("*.out")):
        if not GATEON_READER.match(out.name):
            continue
        records = RESULT_LINE.findall(out.read_text(encoding="utf-8", errors="replace"))
        if not records:
            problems.append("%s holds no DDS_STANDARDS_RESULT line" % out.name)
            continue
        record = _parse_record(records[-1], out.name, problems)
        if record is not None:
            values.append(record.get("received"))
    return values


def check_patched_matrix(docs=None, evidence=None, f9=None, mixed=None):
    """Patched-matrix counts in the documents must match the retained runs.

    Measured from the evidence: the number of per-peer `.out` files (every
    'N peer processes' / 'N per-peer .out files' claim must equal it, every
    'N cross-board case runs' claim half of it), and the gate-on
    appendable/mutable received counts (every 'N received' claim must be a
    measured value and every measured value must be claimed; a nonzero
    'received: N' claim must come from this matrix or the f9 re-run).
    """
    evidence = Path(evidence) if evidence else DEVICE_PATCHED
    f9 = Path(f9) if f9 else F9_RERUN
    if not evidence.is_dir():
        return [MISSING_EVIDENCE + "patched-matrix directory not found: %s" % evidence]
    problems = []
    outs = list(evidence.glob("*.out"))
    if len(outs) % 2:
        problems.append("the patched-matrix evidence holds an odd number of .out "
                        "files (%d); peers come in pairs" % len(outs))
    received = _gateon_received(evidence, problems)
    f9_received = []
    if f9.is_dir():
        for out in sorted(f9.glob("*.out")):
            for raw in RESULT_LINE.findall(out.read_text(encoding="utf-8", errors="replace")):
                record = _parse_record(raw, out.name, problems)
                if record is not None:
                    f9_received.append(record.get("received"))
    else:
        problems.append("f9 re-run directory not found: %s" % f9)
    # The mixed-version default-validation re-run is a second accepted source
    # for received counts; its verdicts are graded by its own records, so
    # only the reader result lines are measured here.
    mixed = Path(mixed) if mixed else MIXED_DEFAULT
    mixed_received = []
    if mixed.is_dir():
        for out in sorted(mixed.glob("*-r.out")):
            for raw in RESULT_LINE.findall(out.read_text(encoding="utf-8", errors="replace")):
                record = _parse_record(raw, out.name, problems)
                if record is not None:
                    mixed_received.append(record.get("received"))
    else:
        problems.append("mixed-default re-run directory not found: %s" % mixed)
    accepted = set(received) | set(f9_received) | set(mixed_received) | {0}
    claimed_received, claims_seen = set(), 0
    for document in documents(docs):
        text = document.read_text(encoding="utf-8").replace("`", "")
        for value in RUNS_CLAIM.findall(text):
            claims_seen += 1
            if int(value) != len(outs) // 2:
                problems.append("%s claims %s cross-board case runs; the evidence "
                                "holds %d" % (document.name, value, len(outs) // 2))
        for value in PEERS_CLAIM.findall(text):
            claims_seen += 1
            if int(value) != len(outs):
                problems.append("%s claims %s peer processes/.out files; the "
                                "evidence holds %d" % (document.name, value, len(outs)))
        for value in RECEIVED_CLAIM.findall(text):
            claims_seen += 1
            claimed_received.add(int(value))
            if int(value) not in accepted:
                problems.append("%s claims '%s received', which no retained reader "
                                "record (patched matrix, f9 or mixed re-run) holds"
                                % (document.name, value))
        for value in RECEIVED_FIELD.findall(text):
            if int(value) not in accepted:
                problems.append("%s claims 'received: %s', which no retained reader "
                                "record (patched matrix, f9 or mixed re-run) holds"
                                % (document.name, value))
    for value in set(received) - claimed_received:
        problems.append("the evidence records a gate-on received count of %s that "
                        "no document claims" % value)
    if not claims_seen:
        problems.append("no patched-matrix count claims found at all")
    return problems


def check_schema_census(docs=None, build_root=None):
    """The schema_version census and the named gate evidence, measured.

    Every 'N of them at the time of writing' claim must equal the number of
    retained `results.json` files that predate the gate fields, and the
    documents' named evidence directories must hold what is said of them:
    schema 1 without gate keys for the three-way comparison, `gate_on`
    False/True for the self-evidencing pair.
    """
    build_root = Path(build_root) if build_root else BUILD_ROOT
    problems, claims = [], 0
    named = list(SCHEMA_V1_EVIDENCE) + list(GATE_EVIDENCE)
    if not build_root.is_dir() or not any((build_root / n).is_dir() for n in named):
        return [MISSING_EVIDENCE + "no retained evidence directories under %s"
                % build_root]
    v1 = [p for p in build_root.rglob("results.json")
          if '"gate_on"' not in p.read_text(encoding="utf-8", errors="replace")]
    for document in documents(docs):
        for value in CENSUS_CLAIM.findall(document.read_text(encoding="utf-8")):
            claims += 1
            if int(value) != len(v1):
                problems.append("%s says %s retained files predate the gate fields; "
                                "%d do" % (document.name, value, len(v1)))
    if not claims:
        problems.append("no schema_version census claim found at all")
    for name in SCHEMA_V1_EVIDENCE:
        path = build_root / name / "results.json"
        if not path.is_file():
            problems.append("documented evidence %s/results.json is missing" % name)
            continue
        record = _parse_record(path.read_text(encoding="utf-8"), str(path), problems)
        if record is None:
            continue
        if record.get("schema_version") != 1 or "gate_on" in record:
            problems.append("%s is documented as schema 1 without gate keys, but "
                            "holds schema %r" % (name, record.get("schema_version")))
    for name, expected in GATE_EVIDENCE.items():
        path = build_root / name / "results.json"
        if not path.is_file():
            problems.append("documented evidence %s/results.json is missing" % name)
            continue
        record = _parse_record(path.read_text(encoding="utf-8"), str(path), problems)
        if record is None:
            continue
        if record.get("gate_on") is not expected:
            problems.append("%s is documented as gate_on=%s, but records %r"
                            % (name, expected, record.get("gate_on")))
    return problems


def _claims_in(document):
    """Which claim kinds a document actually carries."""
    text = document.read_text(encoding="utf-8")
    kinds = set()
    if any(pair != ("1", "1") for pair in NN_CLAIM.findall(text)):
        kinds.add("unit_ratio")
    for line in text.splitlines():
        if ("pytest" in line or "unittest" in line) and N_PASSED.search(line):
            kinds.add("unit_prose")
    if CTEST_NN.search(text):
        kinds.add("ctest")
    if TABLE_ROW.search(text):
        kinds.add("domain_table")
    if RESERVED_ROW.search(text):
        kinds.add("reserved_table")
    if any(not raw.isdigit()
           for raw in HEX_TOKEN.findall(text.replace("`", ""))):
        kinds.add("digest")
    return kinds


def check_required_claims(docs=None):
    """Each document must still carry the claim kinds required of it.

    This is the per-document replacement for reading the corpus-level
    "found nothing at all" reports as per-document coverage.
    """
    problems = []
    present = {document.name: document for document in documents(docs)}
    for name, required in sorted(REQUIRED_CLAIMS.items()):
        document = present.get(name)
        if document is None:
            problems.append("required document %s is missing" % name)
            continue
        for kind in sorted(required - _claims_in(document)):
            problems.append("%s no longer carries any %s claim" % (name, kind))
    if not present:
        problems.append("no documents found at all")
    return problems


SELF_CONTAINED_GUARDS = {
    "ctest": check_ctest_count,
    "ports": check_port_arithmetic,
    "domains": check_domain_references,
    "digests": check_digests,
    "reserved": check_reserved_port_table,
    "matrix": check_patched_matrix,
    "census": check_schema_census,
    "claims": check_required_claims,
}


def main():
    parser = argparse.ArgumentParser(description="Check docs/standards against its sources.")
    parser.add_argument("--docs", default=None, help="documentation directory to check")
    parser.add_argument("--patch", default=None, help="patch file whose digest is documented")
    parser.add_argument("--tests", default=None, help="directory holding the test suite")
    parser.add_argument("--unit-count", type=int, default=None,
                        help="number of tests the suite contains; measured automatically "
                             "when omitted, never skipped")
    parser.add_argument("--evidence-optional", action="store_true",
                        help="on a checkout without the retained build/ evidence, "
                             "report the evidence-backed guards as SKIPPED instead "
                             "of failing; a skip is always printed, never silent")
    arguments = parser.parse_args()

    problems = []
    expected = arguments.unit_count
    if expected is None:
        expected, measurement_problems = measure_suite_count(arguments.tests)
        problems += measurement_problems
    if expected is not None:
        problems += check_unit_count(expected, arguments.docs)
    problems += check_ctest_count(arguments.docs)
    problems += check_port_arithmetic(arguments.docs)
    problems += check_domain_references(arguments.docs)
    problems += check_digests(arguments.docs, arguments.patch)
    problems += check_reserved_port_table(arguments.docs)
    problems += check_patched_matrix(arguments.docs)
    problems += check_schema_census(arguments.docs)
    problems += check_required_claims(arguments.docs)

    skipped = [p for p in problems if p.startswith(MISSING_EVIDENCE)]
    if arguments.evidence_optional:
        problems = [p for p in problems if not p.startswith(MISSING_EVIDENCE)]
        for skip in skipped:
            print("SKIPPED:", skip[len(MISSING_EVIDENCE):])
    for problem in problems:
        print("INCONSISTENT:", problem)
    if problems:
        print("%d documentation inconsistency/ies" % len(problems))
        return 1
    if arguments.evidence_optional and skipped:
        print("documentation consistent (%d tests, %d declared digests; "
              "%d evidence guard(s) SKIPPED, not run on this machine)"
              % (expected, len(known_digests(arguments.patch)), len(skipped)))
    else:
        print("documentation consistent (%d tests, %d declared digests)"
              % (expected, len(known_digests(arguments.patch))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
