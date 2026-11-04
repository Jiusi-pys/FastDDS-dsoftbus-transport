"""Required-mode endpoint default/validation guard; existing QoS is never silently rewritten."""
import argparse
import hashlib
import json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);a=p.parse_args()
for role,folder,member,attrs,marker in [
    ('writer','publisher','datawriter','pub_attr','    EPROSIMA_LOG_INFO(PUBLISHER, "CREATING WRITER IN TOPIC: " << topic->get_name());'),
    ('reader','subscriber','datareader','sub_attr','    EPROSIMA_LOG_INFO(SUBSCRIBER, "CREATING SUBSCRIBER IN TOPIC: " << topic->get_name());')]:
    name='PublisherImpl.cpp' if role=='writer' else 'SubscriberImpl.cpp'
    f=a.source/'src/cpp/fastdds'/folder/name
    identity=json.loads(Path(__file__).with_name('UPSTREAM.json').read_text())
    expected=next(x for x in identity['files'] if x['path'].endswith('/'+name))
    assert hashlib.sha256(f.read_bytes()).hexdigest()==expected['base_sha256'], 'upstream endpoint content mismatch'
    s=f.read_text();assert 'FDDST_ENDPOINT' not in s
    s='#include <cstdlib>\n#include <cstdio>\n'+s
    needle=f'    utils::set_qos_from_attributes(default_{member}_qos_, {attrs});'
    assert s.count(needle)==1
    # Preserve explicit XML QoS. The required-mode no-XML default is set before
    # XML loading in XMLProfileManager, never by rewriting an effective profile.
    guard=f'''    if(std::getenv("FASTDDS_DSOFTBUS_REQUIRED_CONFIG")) {{
        const auto& effective = (&qos == &DATA{role.upper()}_QOS_DEFAULT) ? default_{member}_qos_ : qos;
        if(effective.data_sharing().kind()!=OFF) {{ std::fputs("FDDST_ENDPOINT_DATASHARING_REJECTED\\n",stderr); return nullptr; }}
        std::fprintf(stderr,"FDDST_ENDPOINT role={role} data_sharing=OFF topic=%s\\n",topic->get_name().c_str());std::fflush(stderr);
    }}
'''
    assert s.count(marker)==1;s=s.replace(marker,guard+marker);f.write_text(s)

f=a.source/'src/cpp/rtps/xmlparser/XMLProfileManager.cpp'
identity=json.loads(Path(__file__).with_name('UPSTREAM.json').read_text())
expected=next(x for x in identity['files'] if x['path'].endswith('/XMLProfileManager.cpp'))
assert hashlib.sha256(f.read_bytes()).hexdigest()==expected['base_sha256'], 'upstream XML defaults content mismatch'
s=f.read_text()
for role,typename in [('publisher','PublisherAttributes'),('subscriber','SubscriberAttributes')]:
    needle=f'{typename} default_{role}_attributes;'
    assert s.count(needle)==1
    s=s.replace(needle, f'''{typename} default_{role}_attributes = [] {{
    {typename} value;
    // Pure configuration only: no Runtime, native call, thread or ownership.
    // Explicit XML defaults loaded later overwrite this value and are validated.
    if(std::getenv("FASTDDS_DSOFTBUS_REQUIRED_CONFIG")) value.qos.data_sharing.off();
    return value;
}}();''')
f.write_text(s)
