#!/usr/bin/env python
"""Generate the isolated, explicitly non-production DDS Security identity set.

Task: CANDIDATE_SECURITY_MITIGATION_FROM_3BE5492
Scope: local Host verification of the CVE-2026-63340 configuration-led
mitigation (DDS Security discovery protection = SIGN) on the
Fast DDS 2.14.7 / Fast CDR 2.2.8 maintenance candidate.

This script only ever writes below the directory given with --out, which the
caller is expected to point inside the repository build isolation. It refuses
to write into the source tree. Private keys produced here are ephemeral test
material: they are never committed, never installed, and are not derived from
any real user, device, vendor or product identity. Subject names carry the
literal marker CVE63340-Isolated-Test / NONPRODUCTION so that a leaked
artifact can never be mistaken for a product credential.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

# --- The single security profile exercised by this task ---------------------
# One profile only; no second product mode is developed here.
DISCOVERY_PROTECTION = "SIGN"
LIVELINESS_PROTECTION = "NONE"
RTPS_PROTECTION = "NONE"
METADATA_PROTECTION = "NONE"
DATA_PROTECTION = "NONE"
ALLOW_UNAUTHENTICATED = "false"
ENABLE_JOIN_ACCESS_CONTROL = "true"
ISOLATED_DOMAIN_MIN = 0
ISOLATED_DOMAIN_MAX = 232
# Domain range deliberately NOT covered by the wrong-permission identity.
DENIED_DOMAIN_MIN = 90
DENIED_DOMAIN_MAX = 99
TOPIC_EXPRESSION = "Cve63340IsolatedTest*"

CA_SUBJECT = {
    "C": "CN",
    "ST": "Isolated",
    "O": "CVE63340-Isolated-Test",
    "OU": "NONPRODUCTION",
    "CN": "CVE63340 Isolated Test CA",
    "emailAddress": "isolated-ca@cve63340.invalid",
}

IDENTITIES = {
    # key: (common name, email, whether its grant covers the governed domain)
    "isolated_a": ("Isolated Secure A", "isolated-a@cve63340.invalid", True),
    "isolated_b": ("Isolated Secure B", "isolated-b@cve63340.invalid", True),
    # Valid certificate chain, but this grant does not cover the test domain.
    "isolated_c": ("Isolated Wrong Permission C", "isolated-c@cve63340.invalid", False),
}


def run(cmd, **kw):
    proc = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if proc.returncode != 0:
        sys.stderr.write("command failed: {}\n{}\n{}\n".format(" ".join(cmd), proc.stdout, proc.stderr))
        raise SystemExit(proc.returncode)
    return proc.stdout


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def identity_subject(common_name, email):
    subject = dict(CA_SUBJECT)
    subject["CN"] = common_name
    subject["emailAddress"] = email
    return subject


def write_conf(path, subject):
    is_ca = path.name == "ca.cnf"
    body = ["prompt=no", "string_mask=utf8only", "distinguished_name=dn"]
    if is_ca:
        # Without an explicit extension section OpenSSL 3 omits basicConstraints
        # on the self-signed CA, and every chain then fails X509_V_ERR_INVALID_CA.
        body.append("x509_extensions=ca_ext")
    body += ["", "[ dn ]"]
    for key, value in subject.items():
        body.append("{}={}".format(key, value))
    if is_ca:
        body += ["", "[ ca_ext ]",
                 "basicConstraints=critical,CA:TRUE,pathlen:0",
                 "keyUsage=critical,keyCertSign,cRLSign",
                 "subjectKeyIdentifier=hash"]
    body.append("")
    path.write_text("\n".join(body), encoding="ascii", newline="\n")


def rfc2253(openssl, cert):
    out = run([openssl, "x509", "-in", str(cert), "-noout", "-subject", "-nameopt", "RFC2253"])
    prefix = "subject="
    for line in out.splitlines():
        line = line.strip()
        if line.startswith(prefix):
            return line[len(prefix):]
    raise SystemExit("could not read RFC2253 subject from " + str(cert))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True, help="isolated output directory")
    parser.add_argument("--openssl", default="openssl")
    parser.add_argument("--source-root", required=True, help="repository root; used only to refuse in-tree writes")
    args = parser.parse_args()

    out = Path(args.out).resolve()
    source_root = Path(args.source_root).resolve()
    if "build" not in out.parts:
        raise SystemExit("refusing to write PKI outside build isolation: " + str(out))
    if source_root in out.parents and len(out.relative_to(source_root).parts) < 2:
        raise SystemExit("refusing to write PKI at the source tree root")
    if out.exists():
        raise SystemExit("output directory already exists; refusing to overwrite: " + str(out))
    out.mkdir(parents=True)

    openssl = shutil.which(args.openssl) or args.openssl
    version = run([openssl, "version"]).strip()

    # --- CA ----------------------------------------------------------------
    write_conf(out / "ca.cnf", CA_SUBJECT)
    run([openssl, "req", "-nodes", "-x509", "-days", "3650",
         "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
         "-keyout", str(out / "ca_key.pem"), "-out", str(out / "ca_cert.pem"),
         "-config", str(out / "ca.cnf")])
    if os.name != "nt":
        os.chmod(out / "ca_key.pem", 0o600)

    ext = out / "identity_ext.cnf"
    ext.write_text("basicConstraints=critical,CA:FALSE\n"
                   "keyUsage=critical,digitalSignature,keyEncipherment\n"
                   "subjectKeyIdentifier=hash\n"
                   "authorityKeyIdentifier=keyid,issuer\n",
                   encoding="ascii", newline="\n")

    subjects = {}
    for name, (common_name, email, _covers) in IDENTITIES.items():
        subject = identity_subject(common_name, email)
        write_conf(out / (name + ".cnf"), subject)
        run([openssl, "req", "-nodes", "-new",
             "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
             "-keyout", str(out / (name + "_key.pem")),
             "-out", str(out / (name + "_req.pem")),
             "-config", str(out / (name + ".cnf"))])
        run([openssl, "x509", "-req", "-days", "3650",
             "-in", str(out / (name + "_req.pem")),
             "-CA", str(out / "ca_cert.pem"), "-CAkey", str(out / "ca_key.pem"),
             "-CAcreateserial", "-extfile", str(ext),
             "-out", str(out / (name + "_cert.pem"))])
        if os.name != "nt":
            os.chmod(out / (name + "_key.pem"), 0o600)
        subjects[name] = rfc2253(openssl, out / (name + "_cert.pem"))

    # --- governance --------------------------------------------------------
    governance = """<?xml version="1.0" encoding="utf-8"?>
<!-- Isolated NONPRODUCTION governance for CVE-2026-63340 Host verification.
     Generated by tests/ros2_host/security_candidate/generate_isolated_pki.py.
     Discovery protection is SIGN, the official configuration-led mitigation
     path for the reported unauthenticated DATA_FRAG scenario. -->
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
    xsi:noNamespaceSchemaLocation="omg_shared_ca_domain_governance.xsd">
    <domain_access_rules>
        <domain_rule>
            <domains>
                <id_range>
                    <min>{domain_min}</min>
                    <max>{domain_max}</max>
                </id_range>
            </domains>
            <allow_unauthenticated_participants>{allow_unauth}</allow_unauthenticated_participants>
            <enable_join_access_control>{join_acl}</enable_join_access_control>
            <discovery_protection_kind>{discovery}</discovery_protection_kind>
            <liveliness_protection_kind>{liveliness}</liveliness_protection_kind>
            <rtps_protection_kind>{rtps}</rtps_protection_kind>
            <topic_access_rules>
                <topic_rule>
                    <topic_expression>{topic}</topic_expression>
                    <enable_discovery_protection>true</enable_discovery_protection>
                    <enable_liveliness_protection>false</enable_liveliness_protection>
                    <enable_read_access_control>true</enable_read_access_control>
                    <enable_write_access_control>true</enable_write_access_control>
                    <metadata_protection_kind>{metadata}</metadata_protection_kind>
                    <data_protection_kind>{data}</data_protection_kind>
                </topic_rule>
            </topic_access_rules>
        </domain_rule>
    </domain_access_rules>
</dds>
""".format(domain_min=ISOLATED_DOMAIN_MIN, domain_max=ISOLATED_DOMAIN_MAX,
           allow_unauth=ALLOW_UNAUTHENTICATED, join_acl=ENABLE_JOIN_ACCESS_CONTROL,
           discovery=DISCOVERY_PROTECTION, liveliness=LIVELINESS_PROTECTION,
           rtps=RTPS_PROTECTION, topic=TOPIC_EXPRESSION,
           metadata=METADATA_PROTECTION, data=DATA_PROTECTION)

    # Control variant: the same document with the protection knobs moved to the
    # unmitigated setting the advisory describes (no discovery protection and
    # unauthenticated participants accepted). It exists only to show that the
    # harness can observe the difference; it is not a second product mode.
    # The control document must not inherit the profile comment: it says the
    # opposite of what the control body does.
    profile_comment = (
        "<!-- Isolated NONPRODUCTION governance for CVE-2026-63340 Host verification.\n"
        "     Generated by tests/ros2_host/security_candidate/generate_isolated_pki.py.\n"
        "     Discovery protection is SIGN, the official configuration-led mitigation\n"
        "     path for the reported unauthenticated DATA_FRAG scenario. -->")
    control_comment = (
        "<!-- CONTROL DOCUMENT, NOT A PRODUCT PROFILE.\n"
        "     Discovery protection is NONE and unauthenticated participants are\n"
        "     accepted, i.e. the unmitigated setting the advisory describes. It is\n"
        "     generated only so the tests can show they observe the difference. -->")
    if profile_comment not in governance:
        raise SystemExit("governance profile comment drifted; control variant would inherit it")
    governance_control = governance.replace(profile_comment, control_comment, 1).replace(
        "<discovery_protection_kind>{}</discovery_protection_kind>".format(DISCOVERY_PROTECTION),
        "<discovery_protection_kind>NONE</discovery_protection_kind>").replace(
        "<enable_discovery_protection>true</enable_discovery_protection>",
        "<enable_discovery_protection>false</enable_discovery_protection>").replace(
        "<allow_unauthenticated_participants>false</allow_unauthenticated_participants>",
        "<allow_unauthenticated_participants>true</allow_unauthenticated_participants>").replace(
        "<enable_join_access_control>true</enable_join_access_control>",
        "<enable_join_access_control>false</enable_join_access_control>")

    (out / "governance_sign.xml").write_text(governance, encoding="utf-8", newline="\n")
    (out / "governance_none_control.xml").write_text(governance_control, encoding="utf-8", newline="\n")

    # Single-knob probe: ONLY allow_unauthenticated_participants moves. Discovery
    # protection stays SIGN and join access control stays on, so if the
    # discovered-participant count changes here the cause is this field rather
    # than the protection settings.
    governance_allow_unauth_only = governance.replace(
        "<allow_unauthenticated_participants>false</allow_unauthenticated_participants>",
        "<allow_unauthenticated_participants>true</allow_unauthenticated_participants>").replace(
        profile_comment,
        "<!-- SINGLE-KNOB PROBE, NOT A PRODUCT PROFILE.\n"
        "     Identical to the candidate profile except that\n"
        "     allow_unauthenticated_participants is true. Discovery protection stays\n"
        "     SIGN and join access control stays enabled. -->", 1)
    (out / "governance_allow_unauth_only.xml").write_text(
        governance_allow_unauth_only, encoding="utf-8", newline="\n")

    # --- permissions -------------------------------------------------------
    grants = []
    for name, (_cn, _email, covers_test_domain) in IDENTITIES.items():
        if covers_test_domain:
            domains = "<min>{}</min><max>{}</max>".format(ISOLATED_DOMAIN_MIN, ISOLATED_DOMAIN_MAX)
            rule = """            <allow_rule>
                <domains>
                    <id_range>
                        {domains}
                    </id_range>
                </domains>
                <publish>
                    <topics>
                        <topic>{topic}</topic>
                    </topics>
                </publish>
                <subscribe>
                    <topics>
                        <topic>{topic}</topic>
                    </topics>
                </subscribe>
            </allow_rule>""".format(domains=domains, topic=TOPIC_EXPRESSION)
        else:
            # Valid chain, no rule that covers the governed domain at all.
            domains = "<min>{}</min><max>{}</max>".format(DENIED_DOMAIN_MIN, DENIED_DOMAIN_MAX)
            rule = """            <allow_rule>
                <domains>
                    <id_range>
                        {domains}
                    </id_range>
                </domains>
                <publish>
                    <topics>
                        <topic>{topic}</topic>
                    </topics>
                </publish>
            </allow_rule>""".format(domains=domains, topic=TOPIC_EXPRESSION)
        grants.append("""        <grant name="{name}">
            <subject_name>{subject}</subject_name>
            <validity>
                <not_before>2026-01-01T00:00:00</not_before>
                <not_after>2036-01-01T00:00:00</not_after>
            </validity>
{rule}
            <default>DENY</default>
        </grant>""".format(name="IsolatedGrant_" + name, subject=subjects[name], rule=rule))

    permissions = """<?xml version="1.0" encoding="utf-8"?>
<!-- Isolated NONPRODUCTION permissions for CVE-2026-63340 Host verification.
     Subject names are the exact RFC2253 subjects of the generated certificates. -->
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
    xsi:noNamespaceSchemaLocation="http://www.omg.org/spec/DDS-Security/20170801/omg_shared_ca_permissions.xsd">
    <permissions>
{grants}
    </permissions>
</dds>
""".format(grants="\n".join(grants))
    (out / "permissions.xml").write_text(permissions, encoding="utf-8", newline="\n")

    # --- S/MIME signing (PKCS#7, as required by the DDS Security plugins) ---
    for stem in ("governance_sign", "governance_none_control", "governance_allow_unauth_only", "permissions"):
        run([openssl, "smime", "-sign", "-in", str(out / (stem + ".xml")), "-text",
             "-out", str(out / (stem + ".smime")),
             "-signer", str(out / "ca_cert.pem"), "-inkey", str(out / "ca_key.pem")])

    # --- receipt -----------------------------------------------------------
    artifacts = {}
    for path in sorted(out.iterdir()):
        if path.is_file() and path.suffix in (".pem", ".xml", ".smime", ".cnf", ".srl"):
            artifacts[path.name] = sha256_file(path)
    receipt = {
        "generator": "tests/ros2_host/security_candidate/generate_isolated_pki.py",
        "openssl_version": version,
        "non_production_marker": "NONPRODUCTION / CVE63340-Isolated-Test",
        "discovery_protection_kind": DISCOVERY_PROTECTION,
        "control_discovery_protection_kind": "NONE",
        "allow_unauthenticated_participants": ALLOW_UNAUTHENTICATED,
        "enable_join_access_control": ENABLE_JOIN_ACCESS_CONTROL,
        "rtps_protection_kind": RTPS_PROTECTION,
        "topic_expression": TOPIC_EXPRESSION,
        "subjects_rfc2253": subjects,
        "artifact_sha256": artifacts,
        "private_keys_committed": False,
    }
    (out / "PKI_RECEIPT.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({"out": str(out), "subjects": subjects, "artifacts": len(artifacts)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
