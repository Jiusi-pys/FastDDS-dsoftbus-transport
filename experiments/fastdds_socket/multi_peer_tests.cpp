#define main prior_multi_instance_main
#include "multi_instance_tests.cpp"
#undef main
#include <fastdds/rtps/common/LocatorSelector.hpp>
#include <fastdds/rtps/common/LocatorSelectorEntry.hpp>
#include <fastdds/rtps/common/PortParameters.hpp>

namespace mdds::experiment {
class MultiPeerTestAccess {
public: static void PublishFault(Control& c,std::size_t after){c.tx_publish_fault_after_=after;}
}; // No SDK generation/context is introduced.
}
namespace {
std::map<int,std::string> destinations;
std::string failing_peer;
std::vector<const void*> payload_addresses;
int MPCreate(SocketInfo info){const int id=Create(info);std::lock_guard<std::mutex> l(native.mutex);destinations[id]=info.peerName?info.peerName:"";return id;}
int MPSend(int socket,const void* data,std::uint32_t size){
    const int result=Send(socket,data,size);std::lock_guard<std::mutex> l(native.mutex);payload_addresses.push_back(data);
    return destinations[socket]==failing_peer?-99:result;
}
struct SourceReceiver:rtps::TransportReceiverInterface {
    std::mutex gate;std::condition_variable cv;std::vector<Locator> remotes;
    void OnDataReceived(const std::uint8_t*,std::uint32_t,const Locator&,const Locator& remote)override{std::lock_guard<std::mutex> l(gate);remotes.push_back(remote);cv.notify_all();}
    bool Wait(std::size_t n){std::unique_lock<std::mutex> l(gate);return cv.wait_for(l,std::chrono::seconds(5),[&]{return remotes.size()==n;});}
};
Config PairConfig(){auto c=MakeConfig(1);PeerConfig second;auto peer=MakeConfig(2);second.locator=peer.peer_locator;second.service=peer.peer_service;second.network_id=c.peer_network_id;c.additional_peers.push_back(second);return c;}
struct PairFixture {
    Config config=PairConfig();std::shared_ptr<Control> control=std::make_shared<Control>();
    ExperimentalTransport transport{ExperimentalDescriptor(config,{MPCreate,Listen,Bind,MPSend,Shutdown},control)};
    rtps::SendResourceList senders;SourceReceiver receiver;
    void Start(){Require(transport.init(),"multi peer init");Require(transport.OpenInputChannel(config.local_locator,&receiver,60000),"one local receiver");Require(transport.OpenOutputChannel(senders,config.local_locator),"aggregate output");}
    bool SendTo(const std::vector<Locator>& locators){const std::uint8_t bytes[]={1,2,3,4};std::vector<rtps::NetworkBuffer> buffers{{bytes,4}};Iterator b(locators,0),e(locators,locators.size());return senders.front()->send(buffers,4,&b,&e,Clock::now()+std::chrono::seconds(10),0);}
    ISocketListener Server(){std::lock_guard<std::mutex> l(native.mutex);for(const auto& p:native.sockets)if(p.second.server)return p.second.table;throw std::runtime_error("server missing");}
    void Finish(){transport.shutdown();Require(control->Snapshot().clean_observed,"clean multi peer stop");FinalEmpty();}
};
void Hooks(){PairFixture f;f.Start();const auto& p=f.config.additional_peers[0].locator;Locator out;
    Require(f.transport.is_locator_allowed(p)&&f.transport.is_locator_reachable(p),"second same-device slot allowed and reachable");
    Require(f.transport.transform_remote_locator(p,out)&&out==p,"second transform");Require(f.transport.RemoteToMainLocal(p)==f.config.local_locator,"second local mapping");
    Require(f.transport.NormalizeLocator(p).size()==1,"second normalize");rtps::LocatorList peers;auto copy=p;
    Require(f.transport.configureInitialPeerLocator(copy,rtps::PortParameters{},0,peers)&&peers.size()==1,"second initial peer");
    Require(f.transport.OpenOutputChannel(f.senders,p)&&f.senders.size()==1,"aggregate Sender stable across peers");f.Finish();
}
void SelectedSend(bool fail){PairFixture f;f.Start();Stage settled;ContractTestAccess::AfterTxSettle(*f.control,[&]{settled.Arrive();});
    if(fail)failing_peer=f.config.peer_service;
    Require(f.SendTo({f.config.peer_locator,f.config.additional_peers[0].locator,f.config.peer_locator}),"all distinct destinations accepted");
    Require(settled.Wait(2),"two destination tasks settled");
    {std::lock_guard<std::mutex> l(native.mutex);Require(native.sends.size()==2,"duplicate locator suppressed");Require(native.sends[0]!=native.sends[1],"different peer native owners");
      Require(destinations[native.sends[0]]==f.config.peer_service&&destinations[native.sends[1]]==f.config.additional_peers[0].service,"no cross-target ready owner fallback");
      Require(payload_addresses.size()==2&&payload_addresses[0]==payload_addresses[1],"one immutable payload shared by two tasks");
      auto& tables=native.bindings[f.config.local_service];Require(tables.size()==2&&tables[0].OnBytes==tables[1].OnBytes,"same live client table entrance");}
    const auto stats=f.control->Snapshot();Require(stats.tx_accepted==2&&stats.tx_send_returns==2&&stats.tx_send_failures==(fail?1:0),"per-target return accounting");
    f.Finish();Require(native.listens==1,"one Participant listener");
}
void Selection(){PairFixture f;f.Start();
    rtps::LocatorSelectorEntry first(2,0),second(1,0),duplicate(1,0);
    first.enabled=second.enabled=duplicate.enabled=true;
    first.unicast.push_back(f.config.peer_locator);first.unicast.push_back(f.config.additional_peers[0].locator);
    second.unicast.push_back(f.config.additional_peers[0].locator);duplicate.unicast.push_back(f.config.peer_locator);
    rtps::LocatorSelector selector(eprosima::fastdds::ResourceLimitedContainerConfig::fixed_size_configuration(3));
    Require(selector.add_entry(&first)&&selector.add_entry(&second)&&selector.add_entry(&duplicate),"selector entries");
    f.transport.select_locators(selector);
    Require(selector.selected_size()==2,"full-address two-slot selection exactly once");
    Require(first.state.unicast.size()==2&&second.state.unicast.empty()&&duplicate.state.unicast.empty(),"same device different slot retained; duplicates suppressed across entries");
    Require(f.transport.OpenOutputChannels(f.senders,first)&&f.senders.size()==1,"selected two destinations keep aggregate Sender");f.Finish();
}
void Incoming(){PairFixture f;f.Start();auto table=f.Server();
    const PeerConfig peers[]={{f.config.peer_locator,f.config.peer_service,f.config.peer_network_id},f.config.additional_peers[0]};
    for(int i=0;i<2;++i){PeerSocketInfo p{};p.name=const_cast<char*>(peers[i].service.c_str());p.networkId=const_cast<char*>(peers[i].network_id.c_str());p.dataType=DATA_TYPE_BYTES;
      Require(table.OnNegotiate(2001+i,p),"both peer accepted reservations");{std::lock_guard<std::mutex> l(native.mutex);native.sockets[2001+i]={f.config.local_service,table,true};}
      table.OnBind(2001+i,p);const std::uint8_t byte=1;table.OnBytes(2001+i,&byte,1);Require(f.receiver.Wait(i+1),"original receiver invoked");}
    {std::lock_guard<std::mutex> l(f.receiver.gate);Require(f.receiver.remotes[0]==peers[0].locator&&f.receiver.remotes[1]==peers[1].locator,"captured remote source converted to its original wire locator");}
    Stage settled;ContractTestAccess::AfterTxSettle(*f.control,[&]{settled.Arrive();});
    Require(f.SendTo({peers[0].locator,peers[1].locator})&&settled.Wait(2),"send through matching accepted routes");
    {std::lock_guard<std::mutex> l(native.mutex);Require(native.sends==std::vector<int>({2001,2002})&&native.creates==1,"accepted fallback is per route; never another peer's READY owner");}
    f.Finish();
}
void Lifetime(){PairFixture f;f.Start();Stage admitted,send,settled;std::weak_ptr<const void> payload;
    ContractTestAccess::BeforeSend(*f.control,[&]{send.Hold();});ContractTestAccess::AfterTxSettle(*f.control,[&]{settled.Arrive();});
    ContractTestAccess::AfterTxAdmit(*f.control,[&](std::weak_ptr<const void> p){payload=p;admitted.Hold();throw std::runtime_error("post-admit diagnostic");});
    auto submitting=std::async(std::launch::async,[&]{return f.SendTo({f.config.peer_locator,f.config.additional_peers[0].locator});});
    Require(admitted.Wait()&&send.Wait(),"admission and native entry barriers");const auto pending=f.control->Snapshot();
    Require(pending.tx_records==2&&pending.tx_bytes==8&&pending.live_tx_blocks==1&&pending.live_tx_payload_bytes==4,"task count distinct from shared block lifetime");
    admitted.Release();Require(submitting.get(),"diagnostic exception cannot rollback admitted tasks");Require(f.control->Snapshot().tx_records==2,"no double rollback");send.Release();Require(settled.Wait(2),"all task settlement");
    f.Finish();Require(payload.expired()&&f.control->Snapshot().live_tx_blocks==0,"final shared payload released");
}
void Capacity(){PairFixture f;f.Start();Stage held;ContractTestAccess::BeforeSend(*f.control,[&]{held.Hold();});
    Require(f.SendTo({f.config.peer_locator}),"first accepted");Require(held.Wait(),"unique TX held");
    for(int i=0;i<62;++i)Require(f.SendTo({f.config.peer_locator}),"fill 63 outstanding tasks");
    const auto before=f.control->Snapshot();Require(!f.SendTo({f.config.peer_locator,f.config.additional_peers[0].locator}),"reserve all destinations or reject whole submission");
    const auto after=f.control->Snapshot();Require(after.tx_records==63&&after.tx_bytes==before.tx_bytes&&after.live_tx_blocks==before.live_tx_blocks,"no partial task or payload admission");
    held.Release();f.Finish();
}
void PublishRollback(){PairFixture f;f.Start();MultiPeerTestAccess::PublishFault(*f.control,1);
    Require(!f.SendTo({f.config.peer_locator,f.config.additional_peers[0].locator}),"second publication allocation failure rejects complete batch");
    const auto s=f.control->Snapshot();Require(s.tx_accepted==0&&s.tx_records==0&&s.tx_bytes==0&&s.live_tx_blocks==0,"unpublished complete rollback without budget/payload leak");
    {std::lock_guard<std::mutex> l(native.mutex);Require(native.sends.empty()&&native.creates==1,"partly prepared queue never visible to SDK worker");}
    MultiPeerTestAccess::PublishFault(*f.control,static_cast<std::size_t>(-1));Stage settled;ContractTestAccess::AfterTxSettle(*f.control,[&]{settled.Arrive();});
    Require(f.SendTo({f.config.peer_locator,f.config.additional_peers[0].locator})&&settled.Wait(2),"batch remains usable after rollback");f.Finish();
}
void Configuration(){const auto api=SocketApi{MPCreate,Listen,Bind,MPSend,Shutdown};auto pair=PairConfig();
    auto bad=pair;bad.additional_peers.push_back(pair.additional_peers[0]);Require(!ProcessRuntime::Acquire({bad},api),"only one additional peer permitted");
    bad=pair;bad.additional_peers[0].locator=bad.peer_locator;Require(!ProcessRuntime::Acquire({bad},api),"duplicate full locator rejected");
    auto one=ProcessRuntime::Acquire({pair},api);Require(bool(one),"two peer runtime key");
    auto same=ProcessRuntime::Acquire({pair},api);Require(same==one,"identical peer set retains runtime");
    auto changed=pair;changed.additional_peers[0].network_id=std::string(64,'b');auto other=ProcessRuntime::Acquire({changed},api);
    Require(other&&other!=one,"additional peer identity participates in frozen runtime key");Require(one->Finalize()&&other->Finalize(),"empty configured runtimes finalized");
    Require(native.creates==0,"configuration checks have no SDK side effect");
}
}
int main(int argc,char** argv){try{Require(argc==2,"one case per fresh process");const std::string mode=argv[1];
    if(mode=="hooks")Hooks();else if(mode=="selection")Selection();else if(mode=="send")SelectedSend(false);else if(mode=="target-failure")SelectedSend(true);else if(mode=="rx-source")Incoming();else if(mode=="lifetime")Lifetime();else if(mode=="capacity")Capacity();else if(mode=="publish-rollback")PublishRollback();else if(mode=="configuration")Configuration();else Require(false,"unknown case");
    std::printf("multi_peer %s PASS\n",mode.c_str());return 0;}catch(const std::exception& e){std::fprintf(stderr,"multi_peer FAIL: %s\n",e.what());return 1;}}
