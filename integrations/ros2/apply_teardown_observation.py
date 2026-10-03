"""Read-only return observations applied to a declared isolated Fast DDS 2.14.5 copy.

Does not change an upstream return, destroy an entity, or call an SDK. No downloads.
"""
import argparse, hashlib, json, re, shutil
from pathlib import Path

def need(value, reason):
    if not value: raise ValueError(reason)

def apply(source):
    here=Path(__file__).resolve().parent
    identities=json.loads((here/'TEARDOWN_UPSTREAM.json').read_text(encoding='utf-8'))
    files={row['path']:source/row['path'] for row in identities['files']}
    for row in identities['files']:
        need(hashlib.sha256(files[row['path']].read_bytes()).hexdigest()==row['base_sha256'],'exact public API base mismatch: '+row['path'])
    factory=source/'src/cpp/fastdds/domain/DomainParticipantFactory.cpp'
    need(hashlib.sha256(factory.read_bytes()).hexdigest()==identities['factory_after_existing_hook_sha256'],'exact prerequisite factory hook mismatch')
    # Public forwarding calls have already released implementation locks at return.
    classes={'domain/DomainParticipant.cpp':('DomainParticipant',{'delete_publisher':'pub','delete_subscriber':'sub','delete_topic':'topic','delete_contentfilteredtopic':'a_topic','delete_contained_entities':'this'}),
             'publisher/Publisher.cpp':('Publisher',{'delete_datawriter':'writer','delete_contained_entities':'this'}),
             'subscriber/Subscriber.cpp':('Subscriber',{'delete_datareader':'reader','delete_contained_entities':'this'})}
    for suffix,(cls,methods) in classes.items():
        f=source/'src/cpp/fastdds'/suffix;s=f.read_text(encoding='utf-8');s='#include "../domain/TeardownDiagnostics.hpp"\n'+s if cls!='DomainParticipant' else '#include "TeardownDiagnostics.hpp"\n'+s
        for method,obj in methods.items():
            pattern=r'(ReturnCode_t '+cls+r'::'+method+r'\([^\{]+\{\n)(.*?)(\n\})'
            match=re.search(pattern,s,re.S);need(match is not None,'missing public boundary '+cls+'::'+method)
            body=match.group(2)
            # Select parameter token from the actual declaration; CFT varies in name.
            if method!='delete_contained_entities':
                param=re.search(r'\*\s*(\w+)\)',match.group(1));need(param,'CFT argument missing');obj=param.group(1)
            body=re.sub(r'return ([^;]+);',r'return teardown_call.Return(ReturnCode_t(\1));',body)
            need('teardown_call.Return' in body,'public boundary has no return '+method)
            body='    fastdds_dsoftbus::ros2_teardown::Call teardown_call("'+cls+'_'+method+'",this,'+obj+');\n'+body
            s=s[:match.start()]+match.group(1)+body+match.group(3)+s[match.end():]
        f.write_text(s,encoding='utf-8',newline='\n')
    f=source/'src/cpp/fastdds/domain/DomainParticipantFactory.cpp';s=f.read_text(encoding='utf-8')
    begin=s.index('ReturnCode_t DomainParticipantFactory::delete_participant(');end=s.index('DomainParticipant* DomainParticipantFactory::create_participant(',begin)
    block=s[begin:end];needle='    using PartVectorIt ='
    need(block.count(needle)==1,'factory start anchor')
    block=block.replace(needle,'    fastdds_dsoftbus::ros2_teardown::Call teardown_call("Factory_delete_participant",this,part,true);\n'+needle)
    prefix='fastdds_dsoftbus::ros2_teardown::Record("OriginalDeleteParticipant",this,part,"DDS_RETURN_CODE",true,'
    needle='            return ReturnCode_t::RETCODE_PRECONDITION_NOT_MET;'
    need(block.count(needle)==1,'active entity branch anchor')
    block=block.replace(needle,'            '+prefix+'ReturnCode_t(ReturnCode_t::RETCODE_PRECONDITION_NOT_MET)(),teardown_call.seq,"ACTIVE_ENTITIES");\n            return teardown_call.Return(ReturnCode_t(ReturnCode_t::RETCODE_PRECONDITION_NOT_MET),"ACTIVE_ENTITIES","PUBLIC_RETURN_CODE");')
    needle='                    return fastdds_dsoftbus_hook::after_delete(deleted_part) ? ReturnCode_t::RETCODE_OK : ReturnCode_t::RETCODE_ERROR;'
    need(block.count(needle)==1,'post-delete mapping anchor')
    block=block.replace(needle,'                    '+prefix+'0,teardown_call.seq,"REMOVED");\n                    fastdds_dsoftbus::ros2_teardown::FlushControl("BEFORE_RUNTIME_CLOSE");\n                    const bool hook_clean=fastdds_dsoftbus_hook::after_delete(deleted_part);\n                    return teardown_call.Return(ReturnCode_t(hook_clean ? ReturnCode_t::RETCODE_OK : ReturnCode_t::RETCODE_ERROR),"POST_DELETE_MAPPING","PUBLIC_RETURN_CODE");')
    block=block.replace('            return ReturnCode_t::RETCODE_OK;','            '+prefix+'0,teardown_call.seq,"DOMAIN_FOUND_NO_REMOVAL");\n            return teardown_call.Return(ReturnCode_t(ReturnCode_t::RETCODE_OK),"DOMAIN_FOUND_NO_REMOVAL","PUBLIC_RETURN_CODE");')
    block=block.replace('    return ReturnCode_t::RETCODE_ERROR;','    '+prefix+'ReturnCode_t(ReturnCode_t::RETCODE_ERROR)(),teardown_call.seq,"NOT_FOUND_OR_NULL");\n    return teardown_call.Return(ReturnCode_t(ReturnCode_t::RETCODE_ERROR),"NOT_FOUND_OR_NULL","PUBLIC_RETURN_CODE");')
    f.write_text(s[:begin]+block+s[end:],encoding='utf-8',newline='\n')
    shutil.copyfile(here/'TeardownDiagnostics.hpp',f.with_name('TeardownDiagnostics.hpp'))
    return [{'path':str(p.relative_to(source)),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in list(files.values())+[f]]

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True);args=parser.parse_args()
    print(json.dumps({'version':'2.14.5','scope':'PUBLIC_RETURN_OBSERVATION_NOT_DELETE_SEMANTICS_CHANGE','files':apply(args.source.resolve())},indent=2))
