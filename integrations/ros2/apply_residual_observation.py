"""Observe real container values in an exact isolated 2.14.5 copy; never delete."""
import argparse,hashlib,json,shutil
from pathlib import Path

CAPTURE = r'''
void DomainParticipantImpl::capture_residual_snapshot(const char* reason, bool returned,
        int value, std::uint64_t call) noexcept
{
    using namespace fastdds_dsoftbus::ros2_teardown;
    ResidualSnapshot out;
    out.participant=reinterpret_cast<std::uintptr_t>(get_participant());
    out.delete_call=call;out.return_known=returned;out.selected_return=value;
    bool reason_truncated=false;CopyResidualText(out.reason,sizeof(out.reason),reason,std::strlen(reason),reason_truncated);
    out.capture_failed=reason_truncated;
    try {
        {std::lock_guard<std::mutex> lock(mtx_pubs_);
            out.counts[0]=publishers_.size();out.known_mask|=1;
            for(auto it=publishers_.begin();it!=publishers_.end()&&out.stored<residual_object_capacity;++it){
                auto& row=out.objects[out.stored++];row.kind=0;row.sample_order=1;
                row.object=reinterpret_cast<std::uintptr_t>(it->first);row.implementation=reinterpret_cast<std::uintptr_t>(it->second);
            }}
        {std::lock_guard<std::mutex> lock(mtx_subs_);
            out.counts[1]=subscribers_.size();out.known_mask|=2;
            for(auto it=subscribers_.begin();it!=subscribers_.end()&&out.stored<residual_object_capacity;++it){
                auto& row=out.objects[out.stored++];row.kind=1;row.sample_order=2;
                row.object=reinterpret_cast<std::uintptr_t>(it->first);row.implementation=reinterpret_cast<std::uintptr_t>(it->second);
            }}
        {std::lock_guard<std::mutex> lock(mtx_topics_);
            out.counts[2]=topics_by_handle_.size();out.topic_factories=topics_.size();out.filtered_topics=filtered_topics_.size();out.known_mask|=4;
            for(auto it=topics_by_handle_.begin();it!=topics_by_handle_.end()&&out.stored<residual_object_capacity;++it){
                const auto* topic=it->second;auto& row=out.objects[out.stored++];row.kind=2;row.sample_order=3;
                row.object=reinterpret_cast<std::uintptr_t>(topic);
                auto* implementation=topic->get_impl();row.implementation=reinterpret_cast<std::uintptr_t>(implementation);
                row.name_known=true;const auto& name=topic->get_name();CopyResidualText(row.name,sizeof(row.name),name.data(),name.size(),row.text_truncated);
                row.type_known=true;const auto& type=topic->get_type_name();CopyResidualText(row.type_name,sizeof(row.type_name),type.data(),type.size(),row.text_truncated);
                if(implementation){row.referenced_known=true;row.referenced=implementation->is_referenced();}
            }}
    }catch(...){out.capture_failed=true;}
    // No container lock is held when entering the independent metadata cache.
    (void)RecordResidual(out);
}
'''

def apply(source):
    here=Path(__file__).resolve().parent
    baseline=json.loads((here/'RESIDUAL_UPSTREAM.json').read_text())
    for row in baseline['files']:
        if hashlib.sha256((source/row['path']).read_bytes()).hexdigest()!=row['base_sha256']:
            raise ValueError('Residual observation prerequisite changed: '+row['path'])
    domain=source/'src/cpp/fastdds/domain'
    header=domain/'DomainParticipantImpl.hpp';text=header.read_text()
    needle='    bool has_active_entities();'
    if text.count(needle)!=1:raise ValueError('Internal declaration anchor missing')
    text=text.replace(needle,needle+'\n\n    // Value-only sequential container samples; original deletion semantics unchanged.\n    void capture_residual_snapshot(const char*, bool, int, std::uint64_t) noexcept;')
    header.write_text(text,encoding='utf-8',newline='\n')
    implementation=domain/'DomainParticipantImpl.cpp';text=implementation.read_text()
    needle='bool DomainParticipantImpl::has_active_entities()'
    if text.count(needle)!=1:raise ValueError('Internal definition anchor missing')
    text='#include "TeardownDiagnostics.hpp"\n'+text.replace(needle,CAPTURE+'\n'+needle)
    implementation.write_text(text,encoding='utf-8',newline='\n')
    factory=domain/'DomainParticipantFactory.cpp';text=factory.read_text()
    needle='            return teardown_call.Return(ReturnCode_t(ReturnCode_t::RETCODE_PRECONDITION_NOT_MET),"ACTIVE_ENTITIES","PUBLIC_RETURN_CODE");'
    if text.count(needle)!=1:raise ValueError('Actual rejecting branch missing')
    text=text.replace(needle,'            part->impl_->capture_residual_snapshot("ACTIVE_ENTITIES",true,4,teardown_call.seq);\n'+needle)
    needle='        VectorIt vit = participants_.find(part->get_domain_id());'
    if text.count(needle)!=1:raise ValueError('Actual pre-delete branch missing')
    text=text.replace(needle,'        part->impl_->capture_residual_snapshot("EMPTY_PRE_DELETE",false,0,teardown_call.seq);\n\n'+needle)
    factory.write_text(text,encoding='utf-8',newline='\n')
    for name in ('TeardownDiagnostics.hpp','ResidualDiagnostics.hpp','FastddsHook.hpp','NativePrefix.hpp'):
        shutil.copyfile(here/name,domain/name)
    return [{'path':str(p.relative_to(source)),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
            for p in (header,implementation,factory,domain/'TeardownDiagnostics.hpp',domain/'ResidualDiagnostics.hpp',domain/'FastddsHook.hpp')]

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);a=p.parse_args()
    print(json.dumps({'scope':'BOUNDED_RESIDUAL_VALUE_OBSERVATION_ONLY','files':apply(a.source.resolve())},indent=2))
