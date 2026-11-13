"""Generate PUBLIC TEST-ONLY C2 TLS fixtures; these keys are not credentials."""
import argparse
from datetime import datetime,timezone
import hashlib
import ipaddress
import json
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID,ExtendedKeyUsageOID

def generate(output):
    out=Path(output)
    if out.exists():raise ValueError('fixture directory must be new')
    out.mkdir(parents=True)
    start=datetime(2025,1,1,tzinfo=timezone.utc);end=datetime(2035,1,1,tzinfo=timezone.utc)
    def name(text):return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,text)])
    def root(text):
        key=ec.generate_private_key(ec.SECP256R1());n=name(text)
        cert=x509.CertificateBuilder().subject_name(n).issuer_name(n).public_key(key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(start).not_valid_after(end)
        cert=cert.add_extension(x509.BasicConstraints(ca=True,path_length=0),True).add_extension(x509.KeyUsage(False,False,False,False,False,True,True,False,False),True).add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()),False).add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(key.public_key()),False).sign(key,hashes.SHA256())
        return key,cert
    ca_key,ca=root('C2 PUBLIC TEST ROOT');_,other=root('C2 UNTRUSTED TEST ROOT')
    key=ec.generate_private_key(ec.SECP256R1())
    def server(filename,hostname,expiry=end):
        builder=x509.CertificateBuilder().subject_name(name(hostname)).issuer_name(ca.subject).public_key(key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(start).not_valid_after(expiry)
        sans=[x509.DNSName(hostname)]
        if hostname=='localhost':sans.append(x509.IPAddress(ipaddress.ip_address('127.0.0.1')))
        cert=builder.add_extension(x509.BasicConstraints(ca=False,path_length=None),True).add_extension(x509.SubjectAlternativeName(sans),False).add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]),False).add_extension(x509.KeyUsage(True,False,False,False,False,False,False,False,False),True).add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()),False).add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()),False).sign(ca_key,hashes.SHA256())
        (out/filename).write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    (out/'ca.pem').write_bytes(ca.public_bytes(serialization.Encoding.PEM));(out/'untrusted-ca.pem').write_bytes(other.public_bytes(serialization.Encoding.PEM))
    server('server.pem','localhost');server('wrong-host.pem','wrong-host.example');server('expired.pem','localhost',datetime(2025,12,1,tzinfo=timezone.utc))
    for filename,value in [('server.key',key),('mismatched.key',ec.generate_private_key(ec.SECP256R1()))]:
        (out/filename).write_bytes(value.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
    manifest={'purpose':'PUBLIC TEST ONLY, never deploy these keys','generator':'scripts/standards/generate_web_test_certs.py','validity':'2025-01-01 through 2035-01-01 UTC; expired control ends 2025-12-01','hashes':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir()}}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8',newline='\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output');generate(p.parse_args().output)
