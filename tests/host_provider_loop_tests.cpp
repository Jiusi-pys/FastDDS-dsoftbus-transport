#include "V1NativeNameTestSupport.hpp"
#include "HostProviderTestSupport.hpp"
namespace {
void Defaults(){auto d=Config();Check(d.ConfigureV1(ScopedHostDeployment({{"default-host",0,"synthetic-local"},17,v1::Profile::BytesNative,"default.ns",{}})).code==DirectoryError::None,"V1 default precondition");std::unique_ptr<rtps::TransportInterface> t(d.create_transport());Check(!t->init(),"default factory disabled even host variant");Check(dynamic_cast<DSoftBusTransport*>(t.get())->last_error().error==FacadeError::NativeBackendUnavailable,"default error");auto loop=std::make_shared<HostProviderLoop>(std::make_shared<NullBackend>());auto injected=HostFacadeTestAccess::Create(d,loop);Check(!injected->init(),"null injection unavailable");}
void Loop(){F f;f.Init();Check(f.Send(),"owned TX admission");Check(f.mock->sends==0&&f.loop->Observe().tx.queued==1,"no direct provider send");auto result=f.loop->PumpSend();Check(result.status==NativeStatus::None&&result.actual_return_code==0,"real native invocation");Check(f.mock->sent==std::vector<std::uint8_t>({1,2,3,4})&&f.receiver.calls==0,"TX owned copy and RX deferred");Check(f.loop->PumpReceive()==BackendStatus::None,"original receiver lease");Check(f.receiver.calls==1&&f.receiver.bytes==f.mock->sent&&f.receiver.local==L(2)&&f.receiver.remote==L(3),"owned RX bytes/source");f.Close();Check(f.mock->closes==1,"one native close");}
void Preopen(){F f;f.Init(false);Check(!f.Send()&&f.mock->sends==0,"no fabricated ready owner");Check(f.transport->OpenInputChannel(L(2),&f.receiver,64),"setup owner");Check(f.Send(),"now eligible");f.Close();Check(f.mock->sends==0,"queued not executed during close");}
void InputClose(){F f;f.Init();Check(f.transport->CloseInputChannel(L(2)),"independent detach succeeds");Check(!f.transport->IsInputChannelOpen(L(2))&&f.mock->closes==0,"RX closed while TX owner retained");Check(f.Send(),"TX retained");f.loop->PumpSend();Check(f.loop->PumpReceive()!=BackendStatus::None&&f.receiver.calls==0,"gap echo not delivered");Check(f.transport->OpenInputChannel(L(2),&f.receiver,64),"same channel reopen");Check(f.Send(),"TX after reopen");f.loop->PumpSend();Check(f.loop->PumpReceive()==BackendStatus::None&&f.receiver.calls==1,"new receiver lifetime delivers");f.Close();}
void Queued(){F f;f.Init();Check(f.Send()&&f.Send(),"two queued");f.Close();Check(f.mock->sends==0&&!f.Send(),"pending canceled and future admission refused");Check(f.loop->PumpSend().status!=NativeStatus::None,"no pump after close");}
void PausedSend(){
    F f;f.Init();Event entered,release;f.mock->during_send=[&]{entered.Signal();release.Wait();};Check(f.Send(),"admit");
    std::thread send,close;std::atomic<bool> done=false;NativeSendRunResult result;
    Cleanup cleanup{[&]{release.Signal();if(send.joinable())send.join();if(close.joinable())close.join();}};
    send=std::thread([&]{result=f.loop->PumpSend();});entered.Wait();
    close=std::thread([&]{f.transport->shutdown();done=true;});
    Check(f.loop->WaitSealed(std::chrono::steady_clock::now()+3s),"observed seal boundary");
    Check(!done&&f.mock->closes==0&&!f.Send(),"wait native ticket; no new admission");
    release.Signal();send.join();close.join();Check(done&&result.status==NativeStatus::None&&f.mock->closes==1,"entered send settled before close");
    Check(f.receiver.calls==0,"post-seal ingress terminal drop");f.Close();
}
void PausedReceiver(){
    F f;f.Init();Check(f.Send(),"admit");f.loop->PumpSend();Event entered,release;f.receiver.pause=[&]{entered.Signal();release.Wait();};
    std::thread receive,close;std::atomic<bool> done=false;BackendStatus result=BackendStatus::NotImplemented;
    Cleanup cleanup{[&]{release.Signal();if(receive.joinable())receive.join();if(close.joinable())close.join();}};
    receive=std::thread([&]{result=f.loop->PumpReceive();});entered.Wait();
    close=std::thread([&]{f.transport->shutdown();done=true;});Check(f.loop->WaitSealed(std::chrono::steady_clock::now()+3s),"RX seal boundary");
    Check(!done&&f.mock->closes==0,"wait original receiver permit");release.Signal();receive.join();close.join();
    Check(result==BackendStatus::None&&f.receiver.calls==1,"original invocation completes");f.Close();
}
void Late(){F f;f.Init();auto callbacks=f.mock->callbacks;Check(f.Send(),"admit");f.loop->PumpSend();f.Close();std::uint8_t b=1;Check(callbacks.OnBytes(42,&b,1)!=BackendStatus::None&&f.receiver.calls==0,"late and queued RX never adopted");}
void Error(){F f;f.Init();f.mock->throw_send=true;Check(f.Send(),"admit error case");auto result=f.loop->PumpSend();Check(result.outcome==NativeSendOutcome::ReturnedByException&&!result.actual_return_code,"not fake SDK error");Check(f.receiver.calls==0,"no phantom delivery");f.mock->throw_close=true;f.transport->shutdown();auto first=f.loop->Observe().close;f.transport->shutdown();Check(first.status==BackendStatus::ProviderException&&f.loop->Observe().close.status==first.status&&f.mock->closes==1,"shutdown fault retained");}
void OpenException(){F f;f.Init(false);f.mock->throw_identity=true;bool escaped=false,opened=false;try{opened=f.transport->OpenInputChannel(L(2),&f.receiver,64);}catch(...){escaped=true;}Check(!escaped&&!opened&&f.mock->creates==0,"identity exception mapped before side effect");f.mock->throw_identity=false;Check(f.transport->OpenInputChannel(L(2),&f.receiver,64),"retry after untouched failure");Check(f.Send(),"send after retry");f.loop->PumpSend();Check(f.loop->PumpReceive()==BackendStatus::None,"receiver after retry");f.Close();}
void OpenAllocation(){F f;f.Init(false);const auto local=L(2);bool escaped=false,opened=false;loop_deny_allocations=true;try{opened=f.transport->OpenInputChannel(local,&f.receiver,64);}catch(...){escaped=true;}loop_deny_allocations=false;Check(!escaped&&!opened&&f.mock->creates==0,"open allocation mapped");Check(f.transport->last_error().error==FacadeError::AllocationFailure,"explicit allocation diagnostic");f.Close();}
void OpenRacingShutdown(){
    // AUDIT-C3: Shutdown overlaps an Open that is blocked inside Initialize. The
    // shutdown sees no adapter and completes; the late Open must then be refused and
    // its unpublished adapter closed, not published as a live adapter nobody drains.
    F f;f.Init(false);Event entered,release;
    f.mock->during_create=[&]{entered.Signal();release.Wait();};
    std::thread open,close;InputOpenResult opened=InputOpenResult::Opened;bool shut=false;
    Cleanup cleanup{[&]{release.Signal();if(open.joinable())open.join();if(close.joinable())close.join();}};
    open=std::thread([&]{opened=f.loop->Open(L(2),&f.receiver);});entered.Wait();
    close=std::thread([&]{shut=f.loop->Shutdown();});close.join();
    Check(f.loop->Observe().closed&&shut,"shutdown completed while the open was still blocked");
    release.Signal();open.join();
    Check(opened==InputOpenResult::Disabled,"the open that lost the race is refused");
    const auto view=f.loop->Observe();
    Check(!view.input_open&&view.adapter.phase!=AdapterPhase::Published,"no live adapter was published into a closed runtime");
    Check(f.mock->closes==1,"the unpublished adapter was closed exactly once");
}
void ObserveLifetime(){
    // AUDIT-C5: Observe and CloseAccepted read the adapter/runtime members through a snapshot
    // taken under the gate and use it after unlocking. That is sound only because those
    // objects are created once and owned by the loop state until the state itself ends:
    // never published early, never released by Shutdown. Pinned at the two interleavings the
    // contract permits: a concurrent Open that has not published, and a concurrent Shutdown
    // that is inside the adapter's own close. This is a functional lifetime check; data-race
    // freedom itself needs TSan, which is not run here.
    F f;f.Init(false);Event entered,release;
    f.mock->during_create=[&]{entered.Signal();release.Wait();};
    std::thread open,shut;InputOpenResult opened=InputOpenResult::Disabled;Event closing,go;
    Cleanup cleanup{[&]{release.Signal();go.Signal();if(open.joinable())open.join();if(shut.joinable())shut.join();}};
    open=std::thread([&]{opened=f.loop->Open(L(2),&f.receiver);});entered.Wait();
    {   // Open is inside Initialize: its adapter is a local, so nothing is published or observable.
        const auto v=f.loop->Observe();
        Check(v.started&&!v.input_open&&v.native.owners>=1,"started runtime reports its own native table");
        Check(v.adapter.phase==AdapterPhase::Constructed&&!v.adapter.input_open,"an unpublished adapter is not observable");
        Check(f.loop->CloseAccepted()==BackendStatus::InvalidState,"no published adapter to close an accepted lane on");
    }
    release.Signal();open.join();Check(opened==InputOpenResult::Opened,"the blocked open completes");
    Check(f.loop->Observe().adapter.phase==AdapterPhase::Published&&f.loop->Observe().input_open,"now published and observable");
    Check(f.loop->CloseAccepted()==BackendStatus::None,"single topology has no accepted lane: nothing to close");
    f.mock->during_close=[&]{closing.Signal();go.Wait();};
    shut=std::thread([&]{f.loop->Shutdown();});closing.Wait();
    {   // The adapter is inside its own close: its object must still be alive and observable.
        const auto v=f.loop->Observe();
        Check(v.adapter.phase==AdapterPhase::Closing,"the adapter object outlives the unlock and is mid-close");
        Check(f.loop->CloseAccepted()==BackendStatus::None,"CloseAccepted on a closing adapter is safe and idempotent");
        Check(f.mock->closes==1,"its native owner is being closed exactly once");
    }
    go.Signal();shut.join();
    const auto done=f.loop->Observe();
    Check(done.closed&&done.adapter.phase==AdapterPhase::Closed&&f.mock->closes==1,"Shutdown closes but does not release the adapter object");
    Check(f.loop->CloseAccepted()==BackendStatus::None&&f.loop->Observe().adapter.phase==AdapterPhase::Closed,"still safe to observe and call after Shutdown");
    Check(f.mock->closes==1,"no second close from the repeated calls");
}
void BackendSizeBound(){
    // AUDIT-G4: the descriptor promises 64-byte messages; a backend that can only send
    // fewer would admit the write and then refuse it at the SDK. Startup refuses it.
    F small;small.mock->max_message=32;
    Check(!small.transport->init(),"backend bound below the descriptor maximum is refused");
    Check(small.transport->last_error().error==FacadeError::NativeBackendUnavailable,"as an unavailable backend");
    Check(small.loop->Observe().closed&&small.mock->creates==0,"rolled back with no socket operation");
    F exact;exact.mock->max_message=64;exact.Init();Check(exact.Send(),"a backend bound equal to the maximum is accepted");exact.loop->PumpSend();exact.Close();
    // One accepted runtime per process: a closed record keeps its service name sealed in the
    // process-wide table, so the 'above' and 'unspecified' bounds run as their own cases.
}
void BackendSizeAbove(){
    F f;f.mock->max_message=4096;f.Init();Check(f.Send(),"a backend bound above the maximum is accepted");f.loop->PumpSend();f.Close();
}
void BackendSizeUnspecified(){
    // The default MaxMessageSize() is 0 = no bound stated. It must not be read as a capacity: the
    // loop starts (it has nothing to compare) but nothing in its view claims any backend capacity.
    NullBackend null_backend;Check(null_backend.MaxMessageSize()==0,"default: no bound stated");
    F f;Check(f.mock->MaxMessageSize()==0,"the mock states no bound by default");
    f.Init();Check(f.Send(),"an unspecified bound performs no comparison and starts as before");f.loop->PumpSend();f.Close();
}
void SharedInit(){F f;f.Init();auto second=HostFacadeTestAccess::Create(f.config,f.loop);Check(!second->init(),"second attachment rejected");second.reset();Check(f.mock->closes==0&&f.Send(),"failed duplicate must not close first live owner");f.loop->PumpSend();Check(f.loop->PumpReceive()==BackendStatus::None&&f.receiver.calls==1,"first facade continues");f.Close();}
void StartupAllocation(){F f;f.mock->deny_after_kind=true;bool escaped=false,ready=false;try{ready=f.transport->init();}catch(...){escaped=true;}loop_deny_allocations=false;Check(!escaped&&!ready,"startup allocation rejected");Check(f.loop->Observe().closed&&f.mock->creates==0&&f.mock->closes==0,"own partial startup rolled back, no socket operations");}
void SetupCleanupFailure(){F f;f.Init(false);f.mock->reject_bind=true;f.mock->throw_close=true;Check(!f.transport->OpenInputChannel(L(2),&f.receiver,64),"Bind rejection returned");Check(f.mock->creates==1&&f.mock->closes==1&&!f.transport->IsInputChannelOpen(L(2)),"pending owner cleaned once, no receiver published");f.transport->shutdown();Check(f.loop->Observe().close.status==BackendStatus::BindingRejected&&f.loop->Observe().close.cleanup_status==BackendStatus::ProviderException,"first Bind failure and failed unpublished cleanup both retained");f.transport->shutdown();Check(f.mock->closes==1&&f.loop->Observe().close.status==BackendStatus::BindingRejected&&f.loop->Observe().close.cleanup_status==BackendStatus::ProviderException,"first failure and cleanup failure stable");}
void ReceiveLimit(){F f;f.Init();std::vector<std::uint8_t> data(65,7);Check(f.transport->max_recv_buffer_size()==64,"declared maximum");f.mock->callbacks.OnBytes(42,data.data(),65);Check(f.loop->PumpReceive()==BackendStatus::NoWork&&f.receiver.calls==0,"oversized callback rejected by real attachments");f.mock->callbacks.OnBytes(42,data.data(),64);std::fill(data.begin(),data.end(),8);Check(f.loop->PumpReceive()==BackendStatus::None&&f.receiver.bytes==std::vector<std::uint8_t>(64,7),"exact limit owned and delivered");f.Close();}
}
int main(int argc,char**argv){try{Check(argc==2,"case required");std::string name=argv[1];const std::pair<const char*,void(*)()> cases[]={{"default",Defaults},{"loop",Loop},{"preopen",Preopen},{"input-close",InputClose},{"queued",Queued},{"paused-send",PausedSend},{"paused-receiver",PausedReceiver},{"late",Late},{"error",Error},{"open-exception",OpenException},{"open-allocation",OpenAllocation},{"open-racing-shutdown",OpenRacingShutdown},{"backend-size-bound",BackendSizeBound},{"backend-size-above",BackendSizeAbove},{"backend-size-unspecified",BackendSizeUnspecified},{"observe-lifetime",ObserveLifetime},{"shared-init",SharedInit},{"startup-allocation",StartupAllocation},{"setup-cleanup-failure",SetupCleanupFailure},{"receive-limit",ReceiveLimit}};for(auto c:cases)if(name==c.first){c.second();std::cout<<"PASS "<<name<<'\n';return 0;}throw std::runtime_error("unknown case");}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
