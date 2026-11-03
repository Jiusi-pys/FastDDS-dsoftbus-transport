#include "v2_client.hpp"
#include "../run_protocol.hpp"
#include <future>
#include <thread>
#include <iostream>
#include <functional>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
using namespace stage1_control;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
template<class F>static F symbol(HMODULE h,const char* name){auto p=GetProcAddress(h,name);F f{};static_assert(sizeof(f)==sizeof(p),"qualified pointer size");std::memcpy(&f,&p,sizeof(f));check(f!=nullptr,"fixture symbol");return f;}
static bool owner(void* expected,const void* p){HMODULE actual=nullptr;return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,static_cast<LPCSTR>(p),&actual)&&actual==expected;}
static NS_Envelope identity(unsigned side,const char* exe_sha,const char* manifest){NS_Envelope e{};e.run=UINT64_C(0x0102030405060708);e.self_token=100+side;e.side=side;e.domain=151;e.pid=GetCurrentProcessId();const char* board=side?"3e01ff55454d202020104433991c3b00":"3e01ff55454d202020104033bf453b00";std::memcpy(e.board,board,std::strlen(board)+1);std::memcpy(e.executable_sha,exe_sha,65);std::memcpy(e.deployment_sha,manifest,65);std::memcpy(e.name_space,"host.control.fixture",21);return e;}
static NS_RunEvent event(NS_Envelope e){NS_RunEvent v{};v.header={NS_RUN_VERSION,sizeof(v),NS_SCOPE,0};v.envelope=e;v.kind=NS_ROOT_LISTENER;v.socket=100;v.flags=NS_HOST_FIXTURE_LISTEN;return v;}
static NS_RunRequest request(NS_Envelope local,NS_Envelope peer,Clock::time_point end){NS_RunRequest q{};q.header={NS_RUN_VERSION,sizeof(q),NS_SCOPE,0};q.local=local;q.peer=peer;q.sample_count=3;q.deployment.header={NS_VERSION,sizeof(NS_Request),NS_SCOPE,0};q.deployment.cookie=NS_COOKIE;q.deployment.ownership=NS_BORROWED;q.deployment.external_approval=NS_EXTERNAL_APPROVAL;q.deployment.deadline_ns=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count());return q;}
template<class F>static void join_bounded(std::future<F>& f,std::function<void()> cancel){if(f.wait_for(std::chrono::seconds(3))!=std::future_status::ready){cancel();check(f.wait_for(std::chrono::seconds(2))==std::future_status::ready,"worker cancellation completion");}}
static void codec(NS_Envelope e){
    check(codec_selftest(),"golden header");Frame f;f.kind=Kind::Release;f.direction=Direction::Coordinator;f.sequence=1;f.nonce=e.run;f.envelope=e;f.release=NS_CONNECT_RELEASE;auto b=encode(f);
    // Independent layout assertions: BE nonce, u16 string length and field
    // widths; canonical release has no C-struct padding or trailing bytes.
    const uint8_t fixed[]={0x53,0x31,0x43,0x46,0,1,2,2};check(!std::memcmp(b.data(),fixed,8),"independent golden prefix");const uint8_t nonce[]={1,2,3,4,5,6,7,8};check(!std::memcmp(b.data()+16,nonce,8),"BE nonce");check(b[52]==0&&b[53]==32,"board u16 boundary");check(b[b.size()-4]==0&&b.back()==1,"BE release");
    check(b.size()==244&&b[8]==0&&b[9]==0&&b[10]==0&&b[11]==220,"independent full canonical release length");
    Frame decoded;check(decode(b.data(),b.size(),decoded)==Error::None&&same_envelope(e,decoded.envelope)&&decoded.release==1,"roundtrip");
    for(size_t n=0;n<b.size();++n)check(decode(b.data(),n,decoded)!=Error::None,"all truncations");
    auto bad=b;bad.push_back(0);check(decode(bad.data(),bad.size(),decoded)!=Error::None,"trailing");bad=b;bad[8]=0xff;check(decode(bad.data(),bad.size(),decoded)!=Error::None,"oversize");bad=b;bad[7]=1;check(decode(bad.data(),bad.size(),decoded)!=Error::None,"wrong directionkind");bad=b;bad[53]=0;check(decode(bad.data(),bad.size(),decoded)!=Error::None,"empty string");bad=b;bad[54]=0;check(decode(bad.data(),bad.size(),decoded)!=Error::None,"NUL");
    auto a=event(e);Frame one;one.kind=Kind::Event;one.direction=Direction::Endpoint;one.sequence=1;one.nonce=e.run;one.envelope=e;one.event=a;auto x=encode(one);NS_RunEvent padded;std::memset(&padded,0xcc,sizeof(padded));padded.header=a.header;padded.envelope=a.envelope;padded.kind=a.kind;padded.ordinal=a.ordinal;padded.sequence=a.sequence;std::memcpy(padded.writer_guid,a.writer_guid,16);std::memcpy(padded.payload,a.payload,32);padded.socket=a.socket;padded.listen_rc=a.listen_rc;padded.flags=a.flags;padded.writer_matches=a.writer_matches;padded.reader_matches=a.reader_matches;one.event=padded;check(encode(one)==x,"semantic serialization only");
}
static void io_cases(NS_Envelope e){
    {auto pair=Pipe::local_pair();auto end=Clock::now()+std::chrono::seconds(2);Channel a(pair.a,e,Direction::Endpoint,end,1),b(pair.b,e,Direction::Coordinator,end,3);Frame f;f.kind=Kind::Event;f.event=event(e);auto writer=std::async(std::launch::async,[&]{return a.send(f);});Frame got;auto r=b.receive(got);if(r!=Error::None){a.cancel();b.cancel();}join_bounded(writer,[&]{a.cancel();b.cancel();});check(writer.get()==Error::None&&r==Error::None&&got.event.kind==NS_ROOT_LISTENER,"real fragment pipe");}
    {auto pair=Pipe::local_pair();auto end=Clock::now()+std::chrono::seconds(2);Channel b(pair.b,e,Direction::Coordinator,end);auto wait=std::async(std::launch::async,[&]{Frame f;return b.receive(f);});const bool pending=pair.b->wait_pending(false,end);Frame concurrent;auto busy=b.receive(concurrent);b.cancel();join_bounded(wait,[&]{b.cancel();});check(wait.get()!=Error::None&&pending&&busy==Error::Busy,"actual blocked read/concurrent refusal/cancel");}
    {auto pair=Pipe::local_pair();auto end=Clock::now()+std::chrono::seconds(2);std::vector<uint8_t> large(65536,1);auto writer=std::async(std::launch::async,[&]{size_t n=0;return pair.a->write(large.data(),large.size(),n,end);});const bool pending=pair.a->wait_pending(true,end);pair.a->cancel();join_bounded(writer,[&]{pair.a->cancel();});check(writer.get()!=Error::None&&pending,"actual blocked write cancellation");}
    {auto pair=Pipe::local_pair();auto end=Clock::now()+std::chrono::seconds(1);check(pair.a->close(end),"close peer");uint8_t v=0;size_t n=0;check(pair.b->read(&v,1,n,end)!=Error::None,"EOF read");}
    {auto pair=Pipe::local_pair();Channel expired(pair.a,e,Direction::Endpoint,Clock::now()-std::chrono::seconds(1));Frame f;f.kind=Kind::Event;f.event=event(e);check(expired.send(f)==Error::Expired,"localexpiry");}
    {auto pair=Pipe::local_pair();auto end=Clock::now()+std::chrono::seconds(1);uint8_t partial[10]={0x53,0x31};size_t n=0;check(pair.a->write(partial,10,n,end)==Error::None&&n==10,"partial header send");check(pair.a->close(end),"partial EOF");Channel in(pair.b,e,Direction::Coordinator,end);Frame f;check(in.receive(f)==Error::Eof,"truncated frame EOF");}
    {auto pair=Pipe::local_pair();auto end=Clock::now()+std::chrono::seconds(2);uint8_t partial[10]={0x53,0x31};size_t n=0;check(pair.a->write(partial,10,n,end)==Error::None&&n==10,"partial before cancel");Channel in(pair.b,e,Direction::Coordinator,end);auto reader=std::async(std::launch::async,[&]{Frame f;return in.receive(f);});const bool pending=pair.b->wait_pending(false,end);in.cancel();join_bounded(reader,[&]{in.cancel();});check(reader.get()!=Error::None&&pending,"cancel inside partial frame");}
    // Send an independently encoded duplicate sequence as real pipe bytes.
    {auto pair=Pipe::local_pair();auto end=Clock::now()+std::chrono::seconds(2);Channel b(pair.b,e,Direction::Coordinator,end);Frame f;f.kind=Kind::Event;f.direction=Direction::Endpoint;f.envelope=e;f.nonce=e.run;f.sequence=1;f.event=event(e);auto bytes=encode(f);auto send=std::async(std::launch::async,[&]{for(unsigned j=0;j<2;++j){size_t at=0;while(at<bytes.size()){size_t n=0;auto rc=pair.a->write(bytes.data()+at,bytes.size()-at,n,end);if(rc!=Error::None)return false;at+=n;}}return true;});Frame got;auto first=b.receive(got),second=b.receive(got);join_bounded(send,[&]{pair.a->cancel();pair.b->cancel();});check(send.get()&&first==Error::None&&second==Error::Sequence,"duplicate real bytes");}
}
static void module_pair(const char* path,NS_Envelope a,NS_Envelope b){
    HMODULE h=LoadLibraryA(path);check(h!=nullptr,"fixture DLL load");auto module=std::shared_ptr<void>(h,[](void* p){FreeLibrary(static_cast<HMODULE>(p));});
    auto marker=symbol<const char*(*)()>(h,"ns_control_fixture_scope");check(!std::strcmp(marker(),"HOST_FIXTURE_V2_ONLY_NO_IDENTITY_NO_DDS_NO_SDK"),"marked fixture");auto api=symbol<GetRunApi>(h,"ns_consumer_get_run_api");auto drop=symbol<int32_t(*)()>(h,"ns_control_drop_all_callbacks");auto late=symbol<int32_t(*)(unsigned,const NS_RunEvent*)>(h,"ns_control_late");
    auto end=Clock::now()+std::chrono::seconds(5);auto p0=Pipe::local_pair(),p1=Pipe::local_pair();auto ea=std::make_shared<Channel>(p0.a,a,Direction::Endpoint,end,7),eb=std::make_shared<Channel>(p1.a,b,Direction::Endpoint,end,5);auto ca=std::make_shared<Channel>(p0.b,a,Direction::Coordinator,end,3),cb=std::make_shared<Channel>(p1.b,b,Direction::Coordinator,end,11);auto pa=std::make_shared<Port>(ea,a,true),pb=std::make_shared<Port>(eb,b,true);
    stage1_run::Window window(a,b,end,true);std::mutex coordinator;bool released[5]{};std::atomic<bool> control_ok{true};
    auto controller=[&](std::shared_ptr<Channel> input){for(;;){Frame f;if(input->receive(f)!=Error::None){control_ok=false;window.cancel();ea->cancel();eb->cancel();return;}
            std::lock_guard<std::mutex> l(coordinator);if(f.kind!=Kind::Event||!window.emit(f.event)){control_ok=false;window.cancel();ea->cancel();eb->cancel();return;}
            for(uint32_t stage:{1u,2u,4u,3u})if(!released[stage]&&window.can_release(stage)){Frame token;token.kind=Kind::Release;token.release=stage;if(ca->send(token)!=Error::None||cb->send(token)!=Error::None){control_ok=false;window.cancel();ea->cancel();eb->cancel();return;}released[stage]=true;}
            if(f.event.kind==NS_TEARDOWN)return;
        }};
    {
        Client left(module,pa,end),right(module,pb,end);check(left.bind(api,owner,h)==NS_OK&&right.bind(api,owner,h)==NS_OK,"V2 owned bind");auto qa=request(a,b,end),qb=request(b,a,end);check(left.create(qa)==NS_OK&&right.create(qb)==NS_OK,"V2 create");
        auto c0=std::async(std::launch::async,controller,ca),c1=std::async(std::launch::async,controller,cb);NS_RunResult ra{},rb{};
        auto x=std::async(std::launch::async,[&]{return left.execute(ra);}),y=std::async(std::launch::async,[&]{return right.execute(rb);});
        auto cancel=[&]{ea->cancel();eb->cancel();ca->cancel();cb->cancel();(void)left.cancel();(void)right.cancel();window.cancel();};
        try{join_bounded(x,cancel);join_bounded(y,cancel);int xr=x.get(),yr=y.get();if(xr||yr)cancel();join_bounded(c0,cancel);join_bounded(c1,cancel);c0.get();c1.get();check(xr==NS_OK&&yr==NS_OK&&control_ok&&released[3]&&ra.phase==4&&rb.phase==4,"V2 module/real pipes order");}
        catch(...){cancel();for(auto* f:{&x,&y})if(f->valid()){try{f->get();}catch(...){}}for(auto* f:{&c0,&c1})if(f->valid()){try{f->get();}catch(...){}}throw;}
        // Old context stays pinned while a distinct new capsule exists. The
        // fixture deliberately violates V2-return lifetime to exercise refusal.
        auto pair=Pipe::local_pair();auto newer=a;++newer.run;auto fresh=std::make_shared<Channel>(pair.a,newer,Direction::Endpoint,end);auto freshport=std::make_shared<Port>(fresh,newer,true);auto old=event(a);
        check(pa->callbacks().context!=freshport->callbacks().context,"old context not reused");
        auto hold=symbol<void(*)()>(h,"ns_control_hold_late"),release=symbol<void(*)()>(h,"ns_control_release_late");auto entered=symbol<int32_t(*)()>(h,"ns_control_wait_late");
        hold();auto saved=std::async(std::launch::async,[&]{return late(0,&old);});const bool witnessed=entered()==NS_OK;const int refused=drop();release();join_bounded(saved,release);const int late_rc=saved.get();
        check(witnessed&&refused==NS_STATE&&late_rc==NS_CANCELLED,"copied late reference prevents drop/reclaim");check(drop()==NS_OK,"producer drop-all acknowledgement after callback return");check(pa->acknowledge_fixture_no_more_callbacks()&&pb->acknowledge_fixture_no_more_callbacks(),"fixture reclaim after ack");check(freshport->seal(end)&&freshport->acknowledge_fixture_no_more_callbacks(),"unused new capsule reclaim");
    }
    // Version/provider failure is rejected before create or callback use.
    auto pair=Pipe::local_pair();auto ep=std::make_shared<Channel>(pair.a,a,Direction::Endpoint,end);auto port=std::make_shared<Port>(ep,a,true);Client bad(module,port,end);check(bad.bind(symbol<GetRunApi>(h,"ns_control_bad_api"),owner,h)==NS_ABI,"bad ABI refused");check(bad.bind(api,[](void*,const void*){return false;},h)==NS_OWNER,"foreign provider refused");check(port->seal(end)&&drop()==NS_OK&&port->acknowledge_fixture_no_more_callbacks(),"error path reclaim");
    {auto pp=Pipe::local_pair();auto cc=std::make_shared<Channel>(pp.a,a,Direction::Endpoint,end);auto ctx=std::make_shared<Port>(cc,a,true);Client rejected(module,ctx,end);check(rejected.bind(symbol<GetRunApi>(h,"ns_control_reject_api"),owner,h)==NS_OK,"reject bind");check(rejected.create(request(a,b,end))==NS_AUTH,"create error propagate");check(ctx->seal(end)&&drop()==NS_OK&&ctx->acknowledge_fixture_no_more_callbacks(),"rejected no references");}
    {auto pp=Pipe::local_pair();auto cc=std::make_shared<Channel>(pp.a,a,Direction::Endpoint,end);auto ctx=std::make_shared<Port>(cc,a,true);Client partial(module,ctx,end);check(partial.bind(symbol<GetRunApi>(h,"ns_control_partial_api"),owner,h)==NS_OK,"partial bind");check(partial.create(request(a,b,end))==NS_UNLOAD&&partial.retained(),"uncertain partial create retains module/context");check(ctx->seal(end),"retained context sealed");}
}
static void delayed_cancel(const char* path,NS_Envelope a,NS_Envelope b){
    HMODULE h=LoadLibraryA(path);check(h!=nullptr,"cancel fixture load");auto module=std::shared_ptr<void>(h,[](void* p){FreeLibrary(static_cast<HMODULE>(p));});auto api=symbol<GetRunApi>(h,"ns_control_slow_api");auto started=symbol<int32_t(*)()>(h,"ns_control_wait_execute"),entered=symbol<int32_t(*)()>(h,"ns_control_wait_cancel");auto release=symbol<void(*)()>(h,"ns_control_release_cancel");
    auto end=Clock::now()+std::chrono::milliseconds(300);auto pair=Pipe::local_pair();auto channel=std::make_shared<Channel>(pair.a,a,Direction::Endpoint,end);auto port=std::make_shared<Port>(channel,a,true);auto client=std::make_unique<Client>(module,port,end);
    check(client->bind(api,owner,h)==NS_OK&&client->create(request(a,b,end))==NS_OK,"slow create");NS_RunResult result{};Client* raw=client.get();auto executing=std::async(std::launch::async,[raw,&result]{return raw->execute(result);});
    std::future<int> cancelling;
    try{
        check(started()==NS_OK,"positive execute entry");cancelling=std::async(std::launch::async,[raw]{return raw->cancel();});check(entered()==NS_OK,"positive cancel admission before wrapper destruction");
        join_bounded(executing,release);check(executing.get()==NS_UNLOAD&&client->retained(),"deadline retains complete cancel bookkeeping");
        client.reset(); // admitted cancel owns heap State, not this dead wrapper
        release();join_bounded(cancelling,release);check(cancelling.get()==NS_OK,"late cancellation completes after wrapper destruction");
    }catch(...){release();channel->cancel();if(cancelling.valid()){try{cancelling.get();}catch(...){}}if(executing.valid()){try{executing.get();}catch(...){}}throw;}
}
int main(int argc,char** argv){try{int guard=frontend_control(argc,argv);if(guard!=-1)return guard;check(argc==4,"args fixture/exesha/manifest");auto a=identity(0,argv[2],argv[3]),b=identity(1,argv[2],argv[3]);codec(a);io_cases(a);module_pair(argv[1],a,b);delayed_cancel(argv[1],a,b);std::cout<<"PASS CONTROL_IO_REAL_PIPES_V2_HOST_FIXTURE_ONLY no_identity=1 no_DDS=1 no_SDK=1 late_reference_fence=1 retained_cancel_heap=1\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
