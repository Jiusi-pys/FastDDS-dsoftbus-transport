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

What this does NOT check, declared so the limits are known rather than assumed.
A gate-4 review found each of these by injection; none is a statement that is
wrong in the documents today, and chasing them indefinitely would cost more than
it protects:

  - **Attribution.** The digest check tests set membership, not which artifact a
    digest belongs to. Swapping the `libfastrtps` and `libfastcdr` digests
    between two table rows passes, because both remain declared values.
  - **Most measured quantities.** Only the unit count, the CTest count and the
    domain/port arithmetic are checked. Sample counts, run counts, the
    `schema_version` census, the number of reserved ranges and the approval date
    are not, so an edit to one of those is unprotected.
  - **The reserved-port table's other columns.** Only `port == 7400 + 250 *
    domain` is verified; the "Reserved range" and "Result" cells are not checked
    against the host.
  - **The exempt constants.** `NON_DOMAIN_CONSTANTS` skips 250, 232, 139 and
    151. 232 is also a real domain row, so a wrong 232 in a domain list is
    skipped rather than checked.
  - **Per-document presence.** The "found nothing at all" guards are
    corpus-level. Neutering every CTest claim in one document still passes while
    another document retains one; only neutering all of them reports.

A caller wanting more should add a guard here with a mutation self-test in
`DocumentationGuardSelfTests`, which is the mechanism that caught the two
earlier guards that could not fail.
"""
import argparse
import hashlib
import re
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DOCS = REPO / "docs/standards"
TESTS = REPO / "tests"
PATCH = REPO / "patches/fastdds/2.14.5/0001-edp-honor-reader-type-consistency-qos.patch"

# Numbers that sit beside domain talk and are not domain ids.
NON_DOMAIN_CONSTANTS = frozenset({250, 232, 139, 151})

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

HEX_TOKEN = re.compile(r"(?<![0-9a-fA-F])[0-9a-fA-F]{8,64}(?![0-9a-fA-F])")
NN_CLAIM = re.compile(r"(\d+)\s*/\s*(\d+)\s+(?:PASS|unit)", re.IGNORECASE)
N_PASSED = re.compile(r"(?<![\d/])(\d+)\s+passed(?![\d/])")
CTEST_NN = re.compile(r"CTest\s+(\d+)\s*/\s*(\d+)")
TABLE_ROW = re.compile(r"^\|\s*(\d{1,3})\s*\|\s*(\d{4,5})\s*\|", re.MULTILINE)
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
        for raw in HEX_TOKEN.findall(text):
            if raw.isdigit():
                continue                            # a date or a faultlogger id
            token = raw.lower()
            found += 1
            matches = [name for name, value in digests.items() if value.startswith(token)]
            if not matches:
                problems.append("%s cites %s, which is not a prefix of any declared "
                                "digest; measure it and declare it in "
                                "check_docs_consistency.py" % (document.name, raw))
    if current is not None and not any(
            current in document.read_text(encoding="utf-8").replace("`", "")
            for document in documents(docs)):
        problems.append("no document states the patch's full sha256 %s" % current)
    if not found:
        problems.append("no digests found in the documents at all")
    return problems


SELF_CONTAINED_GUARDS = {
    "ctest": check_ctest_count,
    "ports": check_port_arithmetic,
    "domains": check_domain_references,
    "digests": check_digests,
}


def main():
    parser = argparse.ArgumentParser(description="Check docs/standards against its sources.")
    parser.add_argument("--docs", default=None, help="documentation directory to check")
    parser.add_argument("--patch", default=None, help="patch file whose digest is documented")
    parser.add_argument("--tests", default=None, help="directory holding the test suite")
    parser.add_argument("--unit-count", type=int, default=None,
                        help="number of tests the suite contains; measured automatically "
                             "when omitted, never skipped")
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

    for problem in problems:
        print("INCONSISTENT:", problem)
    if problems:
        print("%d documentation inconsistency/ies" % len(problems))
        return 1
    print("documentation consistent (%d tests, %d declared digests)"
          % (expected, len(known_digests(arguments.patch))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
