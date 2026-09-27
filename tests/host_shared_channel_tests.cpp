#include "HostProviderTestSupport.hpp"
namespace mdds::dsoftbus::host_test {
class HostProviderLoopTestAccess {
public:static void Hooks(HostProviderLoop& loop,std::function<void()> publish,std::function<void()> seal){loop.SetSharedControlHooks(std::move(publish),std::move(seal));}
};
}
namespace {
struct SharedFixture {
    DSoftBusTransportDescriptor config=Config();
    std::shared_ptr<Mock> mock=std::make_shared<Mock>();
    std::shared_ptr<HostProviderLoop> root;
    std::shared_ptr<HostProviderLoop> joined;
    std::unique_ptr<DSoftBusTransport> p,q;
    Receiver rp,rq;
    explicit SharedFixture(std::shared_ptr<ProcessReceiveObserver> observer={}):root(HostProviderLoop::CreateShared(mock,std::move(observer))) {
        p=HostFacadeTestAccess::Create(config,root);
        Check(p->init()&&p->OpenInputChannel(L(2),&rp,64),"shared creator open");
        joined=root->Join();Check(bool(joined),"explicit join from live runtime");
        q=HostFacadeTestAccess::Create(config,joined);
        Check(q->init()&&q->OpenInputChannel(L(2),&rq,64),"independent receiver on same native channel");
    }
    ~SharedFixture(){if(p)p->shutdown();if(q)q->shutdown();}
    void Bytes(){const std::uint8_t b[]={1,2,3};Check(mock->callbacks.OnBytes(42,b,3)==BackendStatus::None,"provider callback");}
    void Pump(){while(root->PumpReceive()==BackendStatus::None){}}
};
void Shared(){
    SharedFixture f;
    Check(!f.p->OpenInputChannel(L(2),&f.rq,64),"duplicate local must not replace original receiver");
    // A repeat open with the same receiver must be idempotent. The identity is compared
    // across the repeat open on purpose: an active-application count alone would also
    // read 2 if a replacement claim had been minted and the previous one released, and
    // the same receiver pointer is reused, so the delivery count cannot catch it either.
    const auto before=f.root->Observe();
    Check(f.p->OpenInputChannel(L(2),&f.rp,64),"same receiver repeated open is idempotent");
    const auto after=f.root->Observe();
    Check(after.receiver_identity==before.receiver_identity&&!(after.receiver_identity==ReceiverIdentity{}),"repeat open keeps the same active receiver identity");
    Check(after.receive.active_applications==before.receive.active_applications&&
          after.receive.retained_applications==before.receive.retained_applications,"repeat open publishes no further claim");
    f.Bytes();f.Pump();
    Check(f.rp.calls==1&&f.rq.calls==1,"separate receiver deliveries");
    Check(f.mock->creates==1,"one native owner");
}
void Creator(){SharedFixture f;f.p.reset();Check(f.mock->closes==0,"creator must not close Q owner");f.Bytes();f.Pump();Check(f.rp.calls==0&&f.rq.calls==1,"Q lives beyond creator");f.q.reset();Check(f.mock->closes==1&&f.root->Observe().closed,"last view finalizes once");}
void ControllerReleased(){SharedFixture f;f.p.reset();std::weak_ptr<HostProviderLoop> creator=f.root;f.root.reset();Check(creator.expired(),"creator view controller released");f.Bytes();while(f.joined->PumpReceive()==BackendStatus::None){}Check(f.rq.calls==1&&f.mock->closes==0,"the survivor still serves after the creator controller is gone");f.q.reset();Check(f.mock->closes==1&&f.joined->Observe().closed,"survivor finalizes");}
void Reattach(){SharedFixture f;auto old=f.root->Observe().receiver_identity;f.p.reset();auto next=f.joined->Join();Check(bool(next),"join via survivor");Receiver fresh;auto p=HostFacadeTestAccess::Create(f.config,next);Check(p->init()&&p->OpenInputChannel(L(2),&fresh,64),"same locator new P");auto now=next->Observe().receiver_identity;Check(old.application_incarnation!=now.application_incarnation&&old.receiver_incarnation!=now.receiver_incarnation&&old.logical_token!=now.logical_token,"new application receiver and token incarnations");f.Bytes();f.Pump();Check(f.rp.calls==0&&f.rq.calls==1&&fresh.calls==1,"new membership only");p.reset();}
void Queued(){SharedFixture f;f.Bytes();f.p.reset();Check(f.mock->closes==0,"queued P cancel does not close Q");const auto before_pump=f.root->Observe().receive;Check(before_pump.records==1&&before_pump.queued==1&&before_pump.retained_applications==1,"only Q queue and claim remain before any pump");f.Pump();Check(f.rp.calls==0&&f.rq.calls==1,"queued only P canceled without pump");Check(f.root->Observe().receive.records==0,"queue bytes and lease refunded");}
void Running(){SharedFixture f;Event entered,release;std::thread pump,close;std::atomic<bool> done=false;Cleanup cleanup{[&]{release.Signal();if(pump.joinable())pump.join();if(close.joinable())close.join();}};f.rp.pause=[&]{entered.Signal();release.Wait();};f.Bytes();pump=std::thread([&]{f.root->PumpReceive();});entered.Wait();close=std::thread([&]{f.p->shutdown();done=true;});Check(f.root->WaitInputClosing(std::chrono::steady_clock::now()+3s),"P close linearized");Check(!done&&f.mock->closes==0,"running P lease pins receiver and Q native owner");release.Signal();pump.join();close.join();f.Pump();Check(f.rp.calls==1&&f.rq.calls==1,"captured receivers complete");f.Bytes();f.Pump();Check(f.rp.calls==1&&f.rq.calls==2,"no later P receiver use");}
bool Submit(DSoftBusTransport& t);
// The join guard compares six views of the limits frozen by the first view. Each is
// exercised on its own, with every other field left legal, so an implementation that
// ignored any one of them fails its own case. Each case is one process with one
// runtime: an exposed record slot is never reclaimed for a service name (its SDK
// destruction is unproven), so a second runtime for the same name in one process is
// refused by design and cannot be used to build a case.
struct ForeignField {const char* name;void(*set)(DSoftBusTransportDescriptor&);};
void ForeignCase(const ForeignField& field){
    SharedFixture f;   // The creator and joiner already share one equal configuration.
    auto third=f.root->Join();Check(!third,"two simultaneous views bounded before exposure");
    f.p.reset();
    auto next=f.joined->Join();Check(bool(next),"join after the creator left");
    auto d=f.config;field.set(d);
    auto bad=HostFacadeTestAccess::Create(d,next);Check(!bad->init(),field.name);
    bad.reset();
    Check(f.mock->closes==0,"a failed join cleans up only locally");
    Check(f.root->Observe().live_views==1,"and leaves exactly the surviving membership");
    f.Bytes();f.Pump();
    Check(f.rq.calls==1,"the surviving view still receives after the refused join");
    Check(f.rp.calls==0,"and the departed view receives nothing");
}
// The record bound is the one compared field whose mutated value could plausibly be refused
// for being illegal rather than for mismatching, so it carries its own positive control: a
// 65-byte descriptor must be accepted as a first registration and as an equal second one.
// The refusal above can then only come from the frozen bound differing.
void ForeignRecordBoundLegal(){
    auto config=Config();config.maxMessageSize=65;config.send_limits.max_record_bytes=65;
    auto mock=std::make_shared<Mock>();
    auto root=HostProviderLoop::CreateShared(mock);
    auto p=HostFacadeTestAccess::Create(config,root);
    Check(p->init(),"a 65-byte descriptor is legal as the first registration");
    Receiver rp;Check(p->OpenInputChannel(L(2),&rp,65),"the first view opens at that bound");
    auto next=root->Join();Check(bool(next),"the second view is obtained");
    auto q=HostFacadeTestAccess::Create(config,next);
    Check(q->init(),"and an equal 65-byte descriptor is accepted as the second registration");
    Receiver rq;Check(q->OpenInputChannel(L(2),&rq,65),"the second view opens");
    const std::uint8_t b[]={1,2,3};Check(mock->callbacks.OnBytes(42,b,3)==BackendStatus::None,"provider callback");
    while(root->PumpReceive()==BackendStatus::None){}
    Check(rp.calls==1&&rq.calls==1,"both registrations receive at that bound");
    q.reset();p->shutdown();
    Check(mock->closes==1,"the runtime finalizes once");
}
void ForeignGlobalBytes(){ForeignCase({"global_bytes",[](DSoftBusTransportDescriptor& d){d.send_limits.global_bytes=512*1024;}});}
void ForeignPeerRecords(){ForeignCase({"peer_records",[](DSoftBusTransportDescriptor& d){d.send_limits.peer_records=33;}});}
void ForeignPeerBytes(){ForeignCase({"peer_bytes",[](DSoftBusTransportDescriptor& d){d.send_limits.peer_bytes=128*1024;}});}
void ForeignGlobalRecords(){ForeignCase({"global_records",[](DSoftBusTransportDescriptor& d){d.send_limits.global_records=129;}});}
void ForeignRecordBound(){ForeignCase({"record_bound",[](DSoftBusTransportDescriptor& d){d.maxMessageSize=65;d.send_limits.max_record_bytes=65;}});}
void Foreign(){ForeignRecordBound();}
void OldSnapshot(){SharedFixture f;f.Bytes();f.p.reset();auto next=f.joined->Join();Receiver fresh;auto p=HostFacadeTestAccess::Create(f.config,next);Check(p->init()&&p->OpenInputChannel(L(2),&fresh,64),"reopen after queued cancellation");f.Pump();Check(f.rq.calls==1&&fresh.calls==0,"new P cannot adopt old queued set");f.Bytes();f.Pump();Check(f.rq.calls==2&&fresh.calls==1,"new admission delivered");p.reset();}
void Budget(){SharedFixture f;for(int i=0;i<17;++i)f.Bytes();auto v=f.root->Observe();Check(v.receive.records==32&&v.receive.high_records==32,"one unchanged local32 budget shared by P Q");f.Pump();Check(f.rp.calls==16&&f.rq.calls==16&&f.root->Observe().receive.records==0,"separate charged deliveries and refund");}
void OpenCloseRace(){
    SharedFixture f;Check(f.p->CloseInputChannel(L(2)),"initial P input close");Receiver fresh;Event entered,release,sealed;std::thread open,close;bool opened=true;std::atomic<bool> closed=false;
    Cleanup cleanup{[&]{release.Signal();if(open.joinable())open.join();if(close.joinable())close.join();HostProviderLoopTestAccess::Hooks(*f.root,{},{});}};
    HostProviderLoopTestAccess::Hooks(*f.root,[&]{entered.Signal();release.Wait();},[&]{sealed.Signal();});
    open=std::thread([&]{opened=f.p->OpenInputChannel(L(2),&fresh,64);});entered.Wait();
    close=std::thread([&]{f.p->shutdown();closed=true;});sealed.Wait();
    Check(f.root->Observe().view_controls==1&&!closed,"shutdown waits actual pending input control");release.Signal();open.join();close.join();
    Check(!opened&&!f.root->Observe().input_open,"closed view never publishes pending receiver");f.Bytes();f.Pump();Check(fresh.calls==0&&f.rq.calls==1,"only original Q remains");
}
void ConcurrentFirst(){
    auto config=Config();auto mock=std::make_shared<Mock>();auto root=HostProviderLoop::CreateShared(mock);auto p=HostFacadeTestAccess::Create(config,root);Check(p->init(),"first init only");auto joined=root->Join();auto q=HostFacadeTestAccess::Create(config,joined);Check(q->init(),"second init only");
    Receiver rp,rq;Event entered,release,second;std::thread a,b;bool pa=false,qb=false;Cleanup cleanup{[&]{release.Signal();if(a.joinable())a.join();if(b.joinable())b.join();p->shutdown();q->shutdown();}};
    mock->during_create=[&]{entered.Signal();release.Wait();};
    a=std::thread([&]{pa=p->OpenInputChannel(L(2),&rp,64);});entered.Wait();b=std::thread([&]{second.Signal();qb=q->OpenInputChannel(L(2),&rq,64);});second.Wait();release.Signal();a.join();b.join();
    Check(pa&&qb&&mock->creates==1,"one setup owner, two independent registrations");const std::uint8_t bytes[]={1,2};mock->callbacks.OnBytes(42,bytes,2);while(root->PumpReceive()==BackendStatus::None){}Check(rp.calls==1&&rq.calls==1,"both first-open receivers");
}
void ConcurrentClose(){SharedFixture f;Event entered,release,sealed;std::thread pump,close,shutdown;std::atomic<bool> done=false;Cleanup cleanup{[&]{release.Signal();if(pump.joinable())pump.join();if(close.joinable())close.join();if(shutdown.joinable())shutdown.join();HostProviderLoopTestAccess::Hooks(*f.root,{},{});}};f.rp.pause=[&]{entered.Signal();release.Wait();};HostProviderLoopTestAccess::Hooks(*f.root,{},[&]{sealed.Signal();});f.Bytes();pump=std::thread([&]{f.root->PumpReceive();});entered.Wait();close=std::thread([&]{f.p->CloseInputChannel(L(2));});Check(f.root->WaitInputClosing(std::chrono::steady_clock::now()+3s),"original close entered");shutdown=std::thread([&]{f.p->shutdown();done=true;});sealed.Wait();Check(f.root->Observe().view_controls==1&&!done,"shutdown shares original close responsibility");release.Signal();pump.join();close.join();shutdown.join();f.Pump();Check(f.rq.calls==1&&f.mock->closes==0,"Q continues");}
struct CopyObserver final:ProcessReceiveObserver {Event entered,release;std::atomic<bool> armed{true};unsigned nth=1,seen=0;ReceiveStage selected=ReceiveStage::Captured;void OnReceive(ReceiveStage stage,const ReceiveDiagnostic&,const std::uint8_t*,std::size_t)noexcept override{if(stage!=selected||!armed)return;if(++seen!=nth)return;armed=false;entered.Signal();release.Wait();}};
void Copying(){auto observer=std::make_shared<CopyObserver>();SharedFixture f(observer);std::thread callback,close;std::atomic<bool> done=false;Cleanup cleanup{[&]{observer->release.Signal();if(callback.joinable())callback.join();if(close.joinable())close.join();}};callback=std::thread([&]{f.Bytes();});observer->entered.Wait();close=std::thread([&]{f.p->shutdown();done=true;});Check(f.root->WaitInputClosing(std::chrono::steady_clock::now()+3s),"close during actual captured copy");Check(!done&&f.root->Observe().receive.copying==1,"original capture still charged");observer->release.Signal();callback.join();close.join();f.Pump();Check(f.rp.calls==0&&f.rq.calls==1&&f.root->Observe().receive.records==0,"cancel P commit, Q snapshot survives");}
void Admitted(){auto observer=std::make_shared<CopyObserver>();observer->selected=ReceiveStage::Admitted;SharedFixture f(observer);std::thread pump,close;std::atomic<bool> done=false;Cleanup cleanup{[&]{observer->release.Signal();if(pump.joinable())pump.join();if(close.joinable())close.join();}};f.Bytes();pump=std::thread([&]{f.root->PumpReceive();});observer->entered.Wait();close=std::thread([&]{f.p->shutdown();done=true;});Check(f.root->WaitInputClosing(std::chrono::steady_clock::now()+3s),"close after execution lease before Run");Check(!done&&f.rp.calls==0&&f.root->Observe().receive.delivering==1,"admitted-not-run is charged");observer->release.Signal();pump.join();close.join();f.Pump();Check(f.rp.calls==1&&f.rq.calls==1,"old admitted work settles before detach returns");f.Bytes();f.Pump();Check(f.rp.calls==1&&f.rq.calls==2,"no old receiver after completed detach");}
void Rollback(){SharedFixture f;Check(f.p->CloseInputChannel(L(2)),"close initial P");Receiver fresh;bool failed=false,succeeded=false;for(long n=0;n<32;++n){loop_fail_after=n;const bool opened=f.p->OpenInputChannel(L(2),&fresh,64);loop_fail_after=-1;if(opened){succeeded=true;Check(f.p->CloseInputChannel(L(2)),"successful retry close");}else failed=true;Check(f.root->Observe().receive.active_applications==1&&f.root->Observe().receive.retained_applications==1&&f.mock->creates==1,"failed publication only rolls back P claim");}Check(failed&&succeeded,"allocation sweep exercised failure and success");Check(f.p->OpenInputChannel(L(2),&fresh,64),"same facade and native runtime retry");f.Bytes();f.Pump();Check(fresh.calls==1&&f.rq.calls==1,"Q and retried P valid");f.p->CloseInputChannel(L(2));}

// C1/C2: the candidate set is fixed with P first, and P is paused inside its own legal
// capture observer before the record has reached Q at all. Q's close must complete:
// Q's own admission responsibility is the only work its close may wait for. P stays
// paused and alive throughout, and a joiner created afterwards is never adopted into
// the set that was already fixed, while a later record still reaches it.
void PinnedClose(){
    auto observer=std::make_shared<CopyObserver>();SharedFixture f(observer);
    std::thread callback,close;std::atomic<bool> done=false;Event sealed;
    Cleanup cleanup{[&]{observer->release.Signal();if(callback.joinable())callback.join();if(close.joinable())close.join();HostProviderLoopTestAccess::Hooks(*f.joined,{},{});}};
    HostProviderLoopTestAccess::Hooks(*f.joined,{},[&]{sealed.Signal();});
    callback=std::thread([&]{f.Bytes();});observer->entered.Wait();
    close=std::thread([&]{f.q->shutdown();done=true;});
    sealed.Wait();
    const auto end=std::chrono::steady_clock::now()+3s;
    while(!done&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(1ms);
    Check(done,"joiner close completes while an earlier target is still paused");
    Check(f.rp.calls==0&&f.mock->closes==0,"earlier target still paused, shared runtime intact");
    const auto closed=f.joined->Observe();
    Check(closed.receiver_identity==ReceiverIdentity{}&&!(closed.retired_receiver_identity==ReceiverIdentity{}),"a closed view reports a retired identity, not a live one");
    observer->release.Signal();callback.join();close.join();
    f.Pump();
    Check(f.rp.calls==1&&f.rq.calls==0,"the earlier target settles under its own contract; the closed joiner is not re-entered");
    Receiver fresh;auto next=f.root->Join();Check(bool(next),"join after the joiner closed");
    auto q=HostFacadeTestAccess::Create(f.config,next);Check(q->init()&&q->OpenInputChannel(L(2),&fresh,64),"new joiner on the same channel");
    Check(f.root->Observe().receive.records==0,"no stale record waits for the new joiner");
    f.Bytes();f.Pump();
    Check(f.rp.calls==2&&fresh.calls==1&&f.rq.calls==0,"a later record reaches the new joiner only");
    Check(f.root->Observe().receive.records==0,"records settled");
    q.reset();
}
// C3: the converse of C1. A target that already holds its own copy/receiver lease must
// still be waited for by its own close: the close is entered, and the only work holding
// it is that target's own charged copy.
void OwnLease(){
    auto observer=std::make_shared<CopyObserver>();observer->selected=ReceiveStage::OwnedCopy;SharedFixture f(observer);
    std::thread callback,close;std::atomic<bool> done=false;Event sealed;
    Cleanup cleanup{[&]{observer->release.Signal();if(callback.joinable())callback.join();if(close.joinable())close.join();HostProviderLoopTestAccess::Hooks(*f.root,{},{});}};
    HostProviderLoopTestAccess::Hooks(*f.root,{},[&]{sealed.Signal();});
    callback=std::thread([&]{f.Bytes();});observer->entered.Wait();
    close=std::thread([&]{f.p->shutdown();done=true;});
    sealed.Wait();
    const auto held=f.root->Observe();
    Check(held.view_closing&&held.receive.copying==1&&f.rp.calls==0&&f.rq.calls==0,"the close is held by this target's own charged copy");
    Check(!done,"and its own close has not returned while that copy is still charged");
    observer->release.Signal();callback.join();close.join();
    Check(done,"the own-lease close completes once its own work settles");
    f.Pump();
    Check(f.rp.calls==0&&f.rq.calls==1&&f.root->Observe().receive.records==0,"the cancelled target delivers nothing while the other target completes normally");
}
// C4: no target has a privileged position. With the pause moved to the second
// candidate, the earlier target's close must still complete.
void CandidateOrder(){
    auto observer=std::make_shared<CopyObserver>();observer->nth=2;SharedFixture f(observer);
    std::thread callback,close;std::atomic<bool> done=false;Event sealed;
    Cleanup cleanup{[&]{observer->release.Signal();if(callback.joinable())callback.join();if(close.joinable())close.join();HostProviderLoopTestAccess::Hooks(*f.root,{},{});}};
    HostProviderLoopTestAccess::Hooks(*f.root,{},[&]{sealed.Signal();});
    callback=std::thread([&]{f.Bytes();});observer->entered.Wait();
    close=std::thread([&]{f.p->shutdown();done=true;});
    sealed.Wait();
    const auto end=std::chrono::steady_clock::now()+3s;
    while(!done&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(1ms);
    Check(done,"earlier target close completes while a later target is paused");
    Check(f.rq.calls==0&&f.mock->closes==0,"later target still paused, shared runtime intact");
    observer->release.Signal();callback.join();close.join();
    f.Pump();
    Check(f.rq.calls==1&&f.rp.calls==0,"later target settles under its own contract; the closed target delivers nothing");
}
// C5/C6: the same fan-out repeated against the same registration objects while one
// target leaves and rejoins the candidate set. Every acquired pin must be released
// exactly once: an omitted or doubled release leaves a close waiting forever, which is
// exactly how the earlier manual capture protocol failed. Admissions are separated by a
// pump so that no record is in flight and only the counting protocol is under test.
void CaptureAccounting(){
    SharedFixture f;
    constexpr unsigned rounds=200;
    int expected_p=0,expected_q=0;
    for(unsigned n=0;n<rounds;++n){
        f.Bytes();++expected_p;++expected_q;
        f.Pump();
        if(n+1==rounds/2){
            Check(f.p->CloseInputChannel(L(2)),"mid-sequence close of one target");
            f.Bytes();++expected_q;
            f.Pump();
            Check(f.p->OpenInputChannel(L(2),&f.rp,64),"the same target reopens on the same channel");
        }
    }
    Check(f.rp.calls==expected_p&&f.rq.calls==expected_q,"every admitted record is settled exactly once, with no loss");
    const auto v=f.root->Observe();
    Check(v.receive.records==0&&v.receive.active_applications==2,"queue empty and both claims intact");
}
// G1 A/E/F: closing a view must complete its own egress settlement, leave the other
// view's registration open and usable, and settle nothing twice. The strong revocation
// and the queue barrier are observed directly, not inferred from the process exiting
// normally or from a runtime-wide total reaching zero.
void EgressSettled(){
    SharedFixture f;
    const auto live=f.root->Observe();
    Check(!live.send.admission_closed&&!live.send.drained,"a live registration is open and undrained");
    f.p->shutdown();
    const auto closed=f.root->Observe();
        // The discriminating field is admission_closed: it is strong_revoked_, which only the
    // explicit BeginRevoke sets. The three below are components of `drained`, which the
    // runtime defines as their conjunction (src/NativeOwnerTable.cpp), so they are reported
    // for diagnosis rather than as independent detection.
    Check(closed.send.admission_closed,"closing a view strongly revokes that view's registration");
    Check(closed.send.queue_barrier_crossed,"and the queue barrier was crossed");
    Check(closed.send.admitted_outstanding==0&&closed.send.submission_inflight==0,"and nothing is admitted or in flight");
    Check(closed.send.drained,"so the runtime reports that registration drained");
    Check(closed.tx.records==0&&closed.tx.bytes==0&&closed.tx.closed,"that view's queue is closed and empty");
    const auto other=f.joined->Observe();
    Check(!other.send.admission_closed&&!other.tx.closed,"the other view's own registration stays open");
    Check(Submit(*f.q),"the surviving view still admits a send");
    f.joined->PumpSend();f.Pump();
    Check(f.rq.calls==1&&f.mock->sends==1,"the surviving view still executes and delivers");
    f.p->shutdown();
    Check(f.mock->closes==0,"a repeated close does not finalize the shared runtime");
    const auto again=f.root->Observe();
    Check(again.send.drained&&again.tx.records==0&&again.send.admitted_outstanding==0,"a settled registration is not settled twice");
}
// G1 B: a queued but not-entered send on a closing view must settle without ever
// reaching the provider, while the other view keeps its own capability.
void EgressQueued(){
    SharedFixture f;
    Check(Submit(*f.p),"a first view submission");
    Check(f.root->Observe().tx.records==1&&f.mock->sends==0,"the record is queued, not entered");
    f.p->shutdown();
    Check(f.mock->sends==0,"a queued record never enters the provider after the view closed");
    const auto v=f.root->Observe();
    Check(v.tx.records==0&&v.tx.bytes==0&&v.send.admitted_outstanding==0&&v.send.drained,"that view's queue and admission settle");
    Check(f.root->PumpSend().status==NativeStatus::NotFound,"the pump finds nothing to run for the closed view");
    Check(f.mock->sends==0,"and nothing reached the provider");
    Check(Submit(*f.q),"the surviving view still admits");
    f.joined->PumpSend();f.Pump();
    Check(f.mock->sends==1&&f.rq.calls==1,"the surviving view's own send executes and echoes");
}
// G1 D: a send resource obtained before the close must be refused afterwards, must not
// create a new ticket, and must not revive the revoked registration.
void EgressOldResource(){
    SharedFixture f;
    rtps::SendResourceList old;
    Check(f.p->OpenOutputChannel(old,L(3)),"first view sender resource before its close");
    Check(!old.empty(),"one attached resource");
    std::uint8_t data[]={9,9};std::vector<rtps::NetworkBuffer> buffers{{data,2}};std::vector<Locator> peers{L(3)};
    {rtps::Locators begin(peers.begin()),end(peers.end());Check(old.front()->send(buffers,2,&begin,&end,std::chrono::steady_clock::now()+5s,0),"that resource sends while its view is live");}
    f.p->shutdown();
    const int before=f.mock->sends;
    {rtps::Locators begin(peers.begin()),end(peers.end());Check(!old.front()->send(buffers,2,&begin,&end,std::chrono::steady_clock::now()+5s,0),"the old resource is refused after the close");}
    Check(f.mock->sends==before,"a refusal creates no new send");
    const auto v=f.root->Observe();
    Check(v.tx.records==0&&v.send.admitted_outstanding==0,"no ticket is created for the revoked registration");
    rtps::SendResourceList fresh;
    Check(!f.p->OpenOutputChannel(fresh,L(3)),"no new resource for a closed view");
    Check(Submit(*f.q),"the surviving view still admits");
}
// F4: driving the runtime's pumps is one runtime-level capability, and who may drive it
// is one rule for both directions. A view whose Start never succeeded is not a member of
// this runtime and must not drive its record or its queue on another view's behalf; a
// view that did start keeps that authority after it closes, which is what keeps the
// surviving view and the final drain from losing their only progress owner.
void PumpAuthority(){
    SharedFixture f;
    auto third=f.root->Join();Check(!third,"two simultaneous views bounded before exposure");
    auto d=f.config;d.maxMessageSize=65;d.send_limits.max_record_bytes=65;
    f.p.reset();
    auto next=f.joined->Join();Check(bool(next),"join after the creator left");
    auto bad=HostFacadeTestAccess::Create(d,next);Check(!bad->init(),"foreign configuration refused");
    Check(next->PumpReceive()==BackendStatus::InvalidState,"a view whose Start failed cannot drive the shared record");
    Check(next->PumpSend().status==NativeStatus::WrongState,"nor the shared send queue");
    bad.reset();
    Check(!f.joined->Observe().view_closing&&!f.joined->Observe().send.admission_closed,"the live view is untouched");
    f.Bytes();f.Pump();Check(f.rq.calls==1,"the live view still drives the runtime");
    Check(f.root->PumpReceive()!=BackendStatus::InvalidState,"a started view keeps its runtime authority after it closes");
    Check(f.mock->closes==0,"and the shared runtime is still owned");
}
// F7 positive control: the send limits are per registration, so two views hold two
// registrations, each of which admits and drives its own record. The native ceiling is
// what bounds the aggregate, and it is never widened here to make this hold.
void BudgetTwoViews(){
    SharedFixture f;
    Check(f.root->Observe().shared_runtime&&f.root->Observe().live_views==2,"two live views, one runtime");
    Check(f.mock->creates==1,"one native socket for both registrations");
    Check(Submit(*f.p)&&Submit(*f.q),"both registrations admit their own record");
    Check(f.root->Observe().tx.records==1&&f.joined->Observe().tx.records==1,"each view queues its own record");
    Check(f.root->Observe().runtime_admitted_outstanding==0,"neither view has unsettled admitted egress while both hold a registration");
    f.root->PumpSend();f.joined->PumpSend();
    Check(f.mock->sends==2,"each registration drives its own send");
}
// F7 negative control: with the per-registration limits raised to a large share of the
// native ceiling, the runtime still accepts the first registration and refuses the
// second before it is exposed, leaving no membership and no claim behind. The refusal
// is the native ceiling, not a widened budget: no authorized limit changes here.
void BudgetCeiling(){
    auto config=Config();
    config.send_limits.global_records=NativeEgressUsage::record_limit-448;
    config.send_limits.global_bytes=1024*1024;   // bytes stay far below their ceiling so only the record clause can refuse
    config.send_limits.peer_records=64;
    config.send_limits.peer_bytes=256*1024;
    auto mock=std::make_shared<Mock>();
    auto root=HostProviderLoop::CreateShared(mock);
    auto p=HostFacadeTestAccess::Create(config,root);
    Check(p->init(),"the first registration fits inside the native ceiling");
    Receiver rp;Check(p->OpenInputChannel(L(2),&rp,64),"the first view opens");
    auto joined=root->Join();Check(bool(joined),"the second view is obtained");
    auto q=HostFacadeTestAccess::Create(config,joined);
    Check(!q->init(),"the second registration is refused by the native ceiling");
    Check(root->Observe().live_views==1&&root->Observe().receive.active_applications==1,"the refused view leaves no membership or claim");
    q.reset();
    p->shutdown();
    Check(mock->closes==1,"the remaining view still finalizes exactly once");
}

bool Submit(DSoftBusTransport& t){rtps::SendResourceList list;Check(t.OpenOutputChannel(list,L(3)),"own sender resource");std::uint8_t data[]={7,8};std::vector<rtps::NetworkBuffer> buffers{{data,2}};std::vector<Locator> peers{L(3)};rtps::Locators begin(peers.begin()),end(peers.end());return list.front()->send(buffers,2,&begin,&end,std::chrono::steady_clock::now()+5s,0);}
void Sending(){SharedFixture f;Event entered,release,sealed;std::thread send,close;std::atomic<bool> done=false;Cleanup cleanup{[&]{release.Signal();if(send.joinable())send.join();if(close.joinable())close.join();HostProviderLoopTestAccess::Hooks(*f.root,{},{});}};f.mock->during_send=[&]{entered.Signal();release.Wait();};HostProviderLoopTestAccess::Hooks(*f.root,{},[&]{sealed.Signal();});Check(Submit(*f.p),"P submission");send=std::thread([&]{f.root->PumpSend();});entered.Wait();close=std::thread([&]{f.p->shutdown();done=true;});sealed.Wait();Check(!done&&f.mock->closes==0&&f.root->Observe().native.send.entered==1,"original in-Send ticket owner not released");Check(f.root->Observe().send.admitted_outstanding==1&&!f.root->Observe().send.drained,"the in-flight admission is charged and the registration is not drained");Check(!f.joined->Observe().send.admission_closed,"the other view's own registration stays open");release.Signal();send.join();close.join();Check(f.root->Observe().send.drained&&f.root->Observe().send.admitted_outstanding==0,"the closed view settles once its execution returns");f.mock->during_send={};f.Pump();Check(f.rq.calls==1&&f.rp.calls==0,"returned send echo only enters surviving Q");Check(Submit(*f.q),"Q send admission survived P revoke");f.joined->PumpSend();f.Pump();Check(f.rq.calls==2,"Q continued TX RX");}
void JoinLast(){SharedFixture f;f.q.reset();Event sealed,release;std::thread close;Cleanup cleanup{[&]{release.Signal();if(close.joinable())close.join();HostProviderLoopTestAccess::Hooks(*f.root,{},{});}};HostProviderLoopTestAccess::Hooks(*f.root,{},[&]{sealed.Signal();release.Wait();});close=std::thread([&]{f.p->shutdown();});sealed.Wait();auto next=f.root->Join();Check(bool(next),"join wins before last membership release");release.Signal();close.join();Check(f.mock->closes==0,"reserved live join keeps native runtime");Receiver fresh;auto p=HostFacadeTestAccess::Create(f.config,next);Check(p->init()&&p->OpenInputChannel(L(2),&fresh,64),"reserved view starts");p.reset();Check(f.mock->closes==1&&!f.root->Join(),"last close wins, old retained handle cannot resurrect");}
}
int main(int argc,char**argv){try{Check(argc==2,"case required");const std::pair<const char*,void(*)()> cases[]={{"shared",Shared},{"creator",Creator},{"controller-released",ControllerReleased},{"reattach",Reattach},{"queued",Queued},{"running",Running},{"foreign",Foreign},{"old-snapshot",OldSnapshot},{"budget",Budget},{"open-close-race",OpenCloseRace},{"concurrent-first",ConcurrentFirst},{"concurrent-close",ConcurrentClose},{"copying",Copying},{"admitted",Admitted},{"rollback",Rollback},{"sending",Sending},{"join-last",JoinLast},
        {"pinned-close",PinnedClose},{"own-lease",OwnLease},{"candidate-order",CandidateOrder},
        {"capture-accounting",CaptureAccounting},{"egress-settled",EgressSettled},
        {"egress-queued",EgressQueued},{"egress-old-resource",EgressOldResource},
        {"pump-authority",PumpAuthority},{"budget-two-views",BudgetTwoViews},
        {"foreign-peer-records",ForeignPeerRecords},{"foreign-peer-bytes",ForeignPeerBytes},
        {"foreign-global-records",ForeignGlobalRecords},{"foreign-global-bytes",ForeignGlobalBytes},
        {"budget-ceiling",BudgetCeiling},{"foreign-record-bound-legal",ForeignRecordBoundLegal}};for(auto c:cases)if(std::string(argv[1])==c.first){c.second();std::cout<<"PASS "<<c.first<<'\n';return 0;}throw std::runtime_error("unknown case");}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
