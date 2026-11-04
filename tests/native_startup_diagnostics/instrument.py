"""Deterministic, hash-bound successor overlay; never edit frozen inputs."""
import hashlib
from pathlib import Path

REPO=Path(__file__).resolve().parents[2]
SOURCE=REPO/'build/r39-ros2-complete-matrix/candidate-d03/tests/stage1_native_successor'
PINS={'consumer_run.cpp':'8b02cd81164b0272122c0ca3ac503ae84bbb5829d3a34b4ddae6c3fbd06e0f04',
 'direct_goal/direct_main.cpp':'ae16008c082810c1dc4cb8629b7e1fed5387af65e3d84801693603985d9a522a',
 'native_contract.h':'4acd0eeb77d91f60d09cf13f394c7b2dd1ce70d24793512c7e30c2cc7de7ffdc',
 'run_contract.h':'787871fcc835762fdccb2b80de8f3f39f410206718946f5371c03d06ab446c5d'}

def once(text,old,new):
    if text.count(old)!=1:raise ValueError('PINNED_SOURCE_POINT_CHANGED')
    return text.replace(old,new)

def consumer(text):
    text=once(text,'#include "run_protocol.hpp"','#include "run_protocol.hpp"\n#include "startup_diagnostics.hpp"\nnamespace diag=stage1_startup;')
    text=once(text,'NS_RunRequest request;NS_RunControl control;','NS_RunRequest request;NS_RunControl control;diag::Recorder diagnostics;')
    text=once(text,'};\nstruct Signals final:',
      '};\ndiag::Snapshot diag::inspect(const NS_RunHandle* h) noexcept {return diag::snapshot_or_unknown(h?&h->diagnostics:nullptr);}\nstruct Signals final:')
    text=once(text,'auto config=configuration(q);s.runtime=',
      'h.diagnostics.enter(diag::Stage::Configuration);auto config=configuration(q);h.diagnostics.enter(diag::Stage::RuntimeConstruction);s.runtime=')
    text=once(text,'need(s.runtime->SetPreConnectGate([&s,&h]{try{',
      'h.diagnostics.enter(diag::Stage::GateRegistration);need(diag::check(h.diagnostics,s.runtime->SetPreConnectGate([&s,&h]{diag::GateStage gate_stage=diag::GateStage::ObserveListener;h.diagnostics.gate_enter(gate_stage);try{')
    predicate=next(line.strip() for line in text.splitlines() if line.strip().startswith('need(!o.fake_sdk'))
    text=once(text,predicate,'gate_stage=diag::GateStage::CheckListener;h.diagnostics.gate_enter(gate_stage);need(diag::gate_check(h.diagnostics,'+predicate[5:-2]+',diag::GateReason::ListenerRejected,gate_stage));')
    text=once(text,'h.emit(e);h.wait(NS_CONNECT_RELEASE);return true;',
      'gate_stage=diag::GateStage::EmitEvent;h.diagnostics.gate_enter(gate_stage);try{h.emit(e);}catch(...){h.diagnostics.gate_fail(diag::GateReason::EmitFailed,gate_stage);throw;}gate_stage=diag::GateStage::WaitRelease;h.diagnostics.gate_enter(gate_stage);try{h.wait(NS_CONNECT_RELEASE);}catch(...){h.diagnostics.gate_fail(diag::GateReason::WaitFailed,gate_stage);throw;}h.diagnostics.gate_enter(diag::GateStage::Completed);return true;')
    text=once(text,'}catch(...){h.abort();return false;}}));',
      '}catch(...){h.diagnostics.gate_fail(diag::GateReason::UnexpectedException,gate_stage);h.abort();return false;}}),diag::Reason::GateRegistrationRejected,diag::Stage::GateRegistration));')
    text=once(text,'s.descriptor=s.runtime->MakeStockDescriptor(config);need(bool(s.descriptor));',
      'h.diagnostics.enter(diag::Stage::DescriptorCreation);s.descriptor=s.runtime->MakeStockDescriptor(config);need(diag::check(h.diagnostics,bool(s.descriptor),diag::Reason::DescriptorNull,diag::Stage::DescriptorCreation));')
    text=once(text,'s.factory=fd::DomainParticipantFactory::get_instance();',
      'h.diagnostics.enter(diag::Stage::FactoryLookup);s.factory=fd::DomainParticipantFactory::get_instance();')
    for expression,reason,stage in [
      ('s.factory->load_XML_profiles_string(xml,sizeof(xml)-1)==RC::RETCODE_OK','XmlLoadRejected','XmlLoad'),
      ('eprosima::fastrtps::xmlparser::XMLProfileManager::library_settings().intraprocess_delivery==eprosima::fastrtps::INTRAPROCESS_OFF','XmlModeMismatch','XmlMode')]:
        text=once(text,'need('+expression+');','h.diagnostics.enter(diag::Stage::'+stage+');need(diag::check(h.diagnostics,'+expression+',diag::Reason::'+reason+',diag::Stage::'+stage+'));')
    text=once(text,'s.start();need(h.live(),NS_CANCELLED);s.participant=s.factory->create_participant(q.domain,qos);need(s.participant);out.phase=1;',
      'h.diagnostics.enter(diag::Stage::WorkerStart);s.start();need(h.live(),NS_CANCELLED);h.diagnostics.enter(diag::Stage::ParticipantCreation);s.participant=s.factory->create_participant(q.domain,qos);need(diag::check(h.diagnostics,bool(s.participant),diag::Reason::ParticipantNull,diag::Stage::ParticipantCreation));out.phase=1;h.diagnostics.enter(diag::Stage::AfterParticipant);')
    text=once(text,'std::unique_lock<std::mutex> exclusive(single_run,std::try_to_lock);if(!exclusive.owns_lock())return NS_STATE;',
      'h->diagnostics.enter(diag::Stage::ExecuteGuard);std::unique_lock<std::mutex> exclusive(single_run,std::try_to_lock);if(!exclusive.owns_lock()){h->diagnostics.fail(diag::Reason::LockBusy,diag::Stage::ExecuteGuard);return NS_STATE;}')
    text=once(text,'if(h->used)return NS_STATE;h->used=true;h->running=true;',
      'if(h->used){h->diagnostics.fail(diag::Reason::HandleReused,diag::Stage::ExecuteGuard);return NS_STATE;}h->used=true;h->running=true;')
    text=once(text,'need(h->request.local.pid==static_cast<uint32_t>(getpid())',
      'h->diagnostics.enter(diag::Stage::ExecuteChecks);need(h->request.local.pid==static_cast<uint32_t>(getpid())')
    text=once(text,'s=new Session(*h);run_session(*s,r);r.status=NS_OK;',
      'h->diagnostics.enter(diag::Stage::SessionConstruction);s=new Session(*h);run_session(*s,r);r.status=NS_OK;')
    text=once(text,'}catch(int code){r.status=code;h->abort();}catch(...){r.status=NS_STATE;h->abort();}',
      '}catch(int code){h->diagnostics.fail(diag::Reason::StatusThrown,h->diagnostics.entered());r.status=code;h->abort();}catch(...){h->diagnostics.fail(diag::Reason::UnexpectedException,h->diagnostics.entered());r.status=NS_STATE;h->abort();}')
    return text

def main(text):
    text=once(text,'#include "goal_record.hpp"','#include "goal_record.hpp"\n#include "startup_diagnostics.hpp"')
    text=once(text,'const bool users_done=drained&&result_abi&&!result.owners_retained&&rc!=NS_UNLOAD;',
      'const bool users_done=drained&&result_abi&&!result.owners_retained&&rc!=NS_UNLOAD;\n    const auto startup_cause=stage1_startup::inspect(handle);')
    point='files.publish("data-result."+side,'
    text=once(text,point,
      'stage1_startup::publish(startup_cause,[&](const std::string& record){files.publish("cause."+side,record);},[]{(void)std::fputs("GOAL_STARTUP_CAUSE_UNAVAILABLE\\n",stderr);});\n    '+point)
    return text

def inputs():
    result={}
    for rel,digest in PINS.items():
        p=SOURCE/rel;s=p.lstat()
        if s.st_file_attributes&1024 or not p.is_file() or s.st_nlink!=1:raise ValueError('INPUT_METADATA')
        data=p.read_bytes()
        if hashlib.sha256(data).hexdigest()!=digest:raise ValueError('PINNED_NATIVE_SOURCE_CHANGED')
        result[rel]=data
    return result

def overlay():
    original=inputs()
    return {'consumer_run.cpp':consumer(original['consumer_run.cpp'].decode('utf-8-sig')).encode(),
      'direct_goal/direct_main.cpp':main(original['direct_goal/direct_main.cpp'].decode('utf-8-sig')).encode()}
