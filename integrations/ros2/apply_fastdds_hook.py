"""Apply the bounded factory patch to a separately archived 2.14.5 source tree."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);a=p.parse_args()
factory=a.source/'src/cpp/fastdds/domain/DomainParticipantFactory.cpp'
identity=json.loads(Path(__file__).with_name('UPSTREAM.json').read_text())
expected=next(x for x in identity['files'] if x['path'].endswith('DomainParticipantFactory.cpp'))
assert hashlib.sha256(factory.read_bytes()).hexdigest()==expected['base_sha256'], 'upstream factory content mismatch'
s=factory.read_text();assert 'fastdds_dsoftbus_hook' not in s,'already patched'
assert 'project(fastrtps VERSION "2.14.5"' in (a.source/'CMakeLists.txt').read_text()
s='#include "FastddsHook.hpp"\n'+s
needle='    const DomainParticipantQos& pqos = (&qos == &PARTICIPANT_QOS_DEFAULT) ? default_participant_qos_ : qos;'
assert s.count(needle)==1
s=s.replace(needle,'    DomainParticipantQos pqos = (&qos == &PARTICIPANT_QOS_DEFAULT) ? default_participant_qos_ : qos;\n    bool hook_ok=false;\n    auto hook_lease=fastdds_dsoftbus_hook::prepare(did,&pqos,hook_ok);\n    if(!hook_ok)return nullptr;\n    fastdds_dsoftbus_hook::PendingLeaseGuard pending_owner(hook_lease);')
needle='        if (factory_qos_.entity_factory().autoenable_created_entities)'
assert s.count(needle)==1
s=s.replace(needle,'        fastdds_dsoftbus_hook::attach(dom_part,hook_lease);\n        pending_owner.disarm();\n'+needle)
needle='        delete dom_part_impl;\n        return nullptr;'
assert s.count(needle)==1
s=s.replace(needle,'        delete dom_part_impl;\n        fastdds_dsoftbus_hook::fail_pending(hook_lease);\n        pending_owner.disarm();\n        return nullptr;')
# Convert only the participant lock to a unlockable RAII lock; cleanup remains
# after the actual deletion and outside this mutex. Failed deletion keeps lease.
start=s.index('ReturnCode_t DomainParticipantFactory::delete_participant(');end=s.index('DomainParticipant* DomainParticipantFactory::create_participant(',start)
block=s[start:end].replace('std::lock_guard<std::mutex> guard(mtx_participants_);','std::unique_lock<std::mutex> guard(mtx_participants_);')
needle='                    delete (*pit);';assert block.count(needle)==1
block=block.replace(needle,needle+'\n                    auto* deleted_part=part;')
needle='                    break;';assert block.count(needle)==1
block=block.replace(needle,'                    if (vit->second.empty()) participants_.erase(vit);\n                    guard.unlock();\n                    return fastdds_dsoftbus_hook::after_delete(deleted_part) ? ReturnCode_t::RETCODE_OK : ReturnCode_t::RETCODE_ERROR;')
s=s[:start]+block+s[end:]
# Preserve direct factory teardown but flag retained plugin owners, not PASS.
start=s.index('DomainParticipantFactory::~DomainParticipantFactory()');end=s.index('DomainParticipantFactory::get_instance()',start)
block=s[start:end];needle='                delete pit;'
assert block.count(needle)==1
block=block.replace(needle,'                fastdds_dsoftbus_hook::destructor_notice(pit->get_participant());\n'+needle)
s=s[:start]+block+s[end:]
factory.write_text(s)
shutil.copyfile(Path(__file__).with_name('FastddsHook.hpp'),factory.with_name('FastddsHook.hpp'))
