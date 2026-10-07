"""Isolated test enclaves with exact ROS DDS endpoint grants, never product keys."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
from xml.sax.saxutils import escape
from generate_isolated_pki import run, rfc2253


def generate_ros(out, openssl, run_id, domain):
    if not re.fullmatch('[0-9a-f]{16}', run_id) or not 0 <= domain <= 232:
        raise ValueError('frozen run/domain required')
    seed = out / 'seed'
    root = Path(__file__).resolve().parents[3]
    run([sys.executable, '-B', str(Path(__file__).with_name('generate_isolated_pki.py')),
         '--out', str(seed), '--openssl', openssl, '--source-root', str(root)])
    ns = 'fdds_' + run_id
    topics = ['ros_discovery_info', 'rt/rosout', 'rt/parameter_events']
    for qos in ('reliable', 'best_effort'):
        topics += [f'rt/{ns}/{qos}/from_{side}' for side in 'AB']
        topics += [f'{prefix}/{ns}/{qos}/add{suffix}' for prefix, suffix in [('rq','Request'),('rr','Reply')]]
    for side in 'AB':
        # Every service the unchanged rclcpp node creates for each side. The
        # type-description service is part of the Jazzy node interface and was
        # missing when this list was first written; the access-control plugin
        # refused it at reader creation, which is how it was found.
        for method in ('describe_parameters','get_parameter_types','get_parameters','list_parameters','set_parameters','set_parameters_atomically','get_type_description'):
            topics += [f'{prefix}/{ns}/transport_{side}/{method}{suffix}' for prefix,suffix in [('rq','Request'),('rr','Reply')]]
    governance = (seed / 'governance_sign.xml').read_text(encoding='utf-8')
    governance = governance.replace('<min>0</min>',f'<min>{domain}</min>').replace('<max>232</max>',f'<max>{domain}</max>')
    # This rule requires access checks for every topic; permissions below grant only named endpoints.
    governance = governance.replace('Cve63340IsolatedTest*', '*')
    gov = out / 'governance.xml'; gov.write_text(governance, encoding='utf-8')
    for side, identity in [('A','isolated_a'),('B','isolated_b'),('DENIED','isolated_c')]:
        enclave = out / 'enclaves' / side; enclave.mkdir(parents=True)
        for src,dst in [('ca_cert.pem','identity_ca.cert.pem'),('ca_cert.pem','permissions_ca.cert.pem'),
                        (identity+'_cert.pem','cert.pem'),(identity+'_key.pem','key.pem')]:
            shutil.copyfile(seed/src,enclave/dst)
        granted = topics if side != 'DENIED' else ['rt/DeniedUnrelatedTopic']
        names = ''.join('<topic>'+escape(x)+'</topic>' for x in granted)
        subject = escape(rfc2253(openssl, enclave/'cert.pem'))
        permission = f'''<dds><permissions><grant name="NONPRODUCTION_{side}"><subject_name>{subject}</subject_name>
<validity><not_before>2026-01-01T00:00:00</not_before><not_after>2036-01-01T00:00:00</not_after></validity>
<allow_rule><domains><id>{domain}</id></domains><publish><topics>{names}</topics></publish><subscribe><topics>{names}</topics></subscribe></allow_rule>
<default>DENY</default></grant></permissions></dds>'''
        permissions = enclave/'permissions.xml'; permissions.write_text(permission,encoding='utf-8')
        for src,dst in [(gov,enclave/'governance.p7s'),(permissions,enclave/'permissions.p7s')]:
            run([openssl,'smime','-sign','-in',str(src),'-text','-out',str(dst),'-signer',str(seed/'ca_cert.pem'),'-inkey',str(seed/'ca_key.pem')])
    receipt = dict(run=run_id, domain=domain, granted_dds_topics=topics, default='DENY',
                   governance_wildcard='ACCESS_CHECK_REQUIRED_NOT_TOPIC_GRANT', plugins=['PKI-DH','Access-Permissions','AES-GCM-GMAC'],
                   discovery='SIGN', allow_unauthenticated=False, join_access_control=True,
                   metadata_protection='NONE', data_protection='NONE', production=False)
    (out/'ROS_PKI_INPUTS.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    return receipt


if __name__ == '__main__':
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--openssl',required=True)
    p.add_argument('--run',required=True);p.add_argument('--domain',type=int,default=201)
    a=p.parse_args();generate_ros(a.out,a.openssl,a.run,a.domain)
