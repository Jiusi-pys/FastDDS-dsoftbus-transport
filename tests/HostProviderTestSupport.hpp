#ifndef MDDS_HOST_PROVIDER_TEST_SUPPORT_HPP
#define MDDS_HOST_PROVIDER_TEST_SUPPORT_HPP
#include <mdds/dsoftbus/HostProviderLoop.hpp>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cstdlib>
#include <new>
thread_local bool loop_deny_allocations=false;
thread_local long loop_fail_after=-1;
void* operator new(std::size_t n){if(loop_deny_allocations||loop_fail_after==0)throw std::bad_alloc();if(loop_fail_after>0)--loop_fail_after;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
using namespace mdds::dsoftbus;
using namespace mdds::dsoftbus::phase0;
using namespace mdds::dsoftbus::host_test;
using namespace std::chrono_literals;
namespace rtps=eprosima::fastdds::rtps;
namespace {
void Check(bool x,const char* why){if(!x)throw std::runtime_error(why);}
struct Event {
    std::mutex mutex;std::condition_variable cv;bool ready=false;
    void Signal(){std::lock_guard<std::mutex> l(mutex);ready=true;cv.notify_all();}
    void Wait(){std::unique_lock<std::mutex> l(mutex);if(!cv.wait_for(l,5s,[&]{return ready;}))throw std::runtime_error("test barrier watchdog");}
};
struct Cleanup {std::function<void()> fn;~Cleanup(){fn();}};
Locator L(int n){EndpointId id{};id[0]=10;id[14]=127;id[15]=static_cast<std::uint8_t>(n);Locator l;Check(LocatorCodec::Encode(id,l),"locator");return l;}
DSoftBusTransportDescriptor Config(){
    DSoftBusTransportDescriptor d(64);d.directory.locals={{L(2),"host.loop.ns","host.loop.local"}};
    d.directory.routes={{{L(2),L(3)},{"model.net","model.remote","model.dial",{"model.peer"},RecordDataType::Bytes},1}};
    d.default_metatraffic_unicast={L(2)};d.default_user_unicast={L(2)};return d;
}
struct Mock final:NativeBackend {
    ProviderCallbacks callbacks;std::atomic<int> sends{0},closes{0},creates{0};
    std::function<void()> during_send;bool throw_send=false,throw_close=false,throw_identity=false,deny_after_kind=false,reject_bind=false;
    std::vector<std::uint8_t> sent;
    BackendKind Kind()const noexcept override{if(deny_after_kind)loop_deny_allocations=true;return BackendKind::HostMock;}
    BackendIdentity Identity()const override{if(throw_identity)throw std::runtime_error("mock identity exception");return {"host.facade.loop",{51},{61}};}
    BackendSocketResult Create(ProviderCallbacks c)override{callbacks=c;++creates;return {BackendStatus::None,42};}
    BackendStatus Listen(std::int32_t)override{return BackendStatus::None;}
    BackendStatus Bind(std::int32_t)override{return reject_bind?BackendStatus::BindingRejected:BackendStatus::None;}
    BackendSendResult Send(std::int32_t id,const std::uint8_t* p,std::size_t n,std::chrono::steady_clock::time_point)override{
        Check(id==42,"same owned socket");++sends;if(during_send)during_send();
        if(throw_send)throw std::runtime_error("mock send exception");
        sent.assign(p,p+n);auto temporary=sent;
        callbacks.OnBytes(id,temporary.data(),static_cast<std::uint32_t>(n));
        std::fill(temporary.begin(),temporary.end(),0xEE);return {BackendStatus::None,0};
    }
    BackendStatus Shutdown(std::int32_t id)override{Check(id==42,"close owned socket");++closes;if(throw_close)throw std::runtime_error("mock close exception");return BackendStatus::None;}
};
struct Receiver final:InputChannels::Receiver {
    std::atomic<int> calls{0};std::vector<std::uint8_t> bytes;Locator local,remote;std::function<void()> pause;
    void OnDataReceived(const rtps::octet* p,std::uint32_t n,const Locator& l,const Locator& r)override{
        ++calls;if(pause)pause();bytes.assign(p,p+n);local=l;remote=r;
    }
};
struct F {
    DSoftBusTransportDescriptor config=Config();Receiver receiver;std::shared_ptr<Mock> mock=std::make_shared<Mock>();
    std::shared_ptr<HostProviderLoop> loop=std::make_shared<HostProviderLoop>(mock);
    std::unique_ptr<DSoftBusTransport> transport=HostFacadeTestAccess::Create(config,loop);
    rtps::SendResourceList senders;
    void Init(bool input=true){Check(transport->init(),"injected actual facade init");if(input)Check(transport->OpenInputChannel(L(2),&receiver,64),"input registration");Check(transport->OpenOutputChannel(senders,L(3)),"actual output resource");}
    bool Send(){std::uint8_t data[]={1,2,3,4};std::vector<rtps::NetworkBuffer> b{{data,2},{data+2,2}};std::vector<Locator> peers{L(3)};rtps::Locators begin(peers.begin()),end(peers.end());const bool ok=senders.front()->send(b,4,&begin,&end,std::chrono::steady_clock::now()+5s,23);std::memset(data,9,4);return ok;}
    void Close(){transport->shutdown();auto v=loop->Observe();Check(v.closed&&v.sealed&&v.active_pumps==0&&v.tx.records==0&&v.tx.bytes==0,"observable work settled");Check(v.close.adapter_drained&&v.close.sdk_drain==SdkDrain::Unproven,"no SDK drain claim");}
};
}
#endif
