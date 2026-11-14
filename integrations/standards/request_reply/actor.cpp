#include "request_reply.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
namespace fs=std::filesystem;
using namespace d3;

static unsigned number(const char* value){
    std::string s=value;require(!s.empty()&&s.find_first_not_of("0123456789")==std::string::npos,"D3 numeric argument");
    auto n=std::stoull(s);require(n<=60000,"D3 numeric limit");return static_cast<unsigned>(n);
}
static std::string hex(const std::string& bytes){
    static const char digits[]="0123456789abcdef";std::string out;
    for(unsigned char c:bytes){out+=digits[c>>4];out+=digits[c&15];}return out.empty()?"-":out;
}
static void show(const char* event,const Identity& id,const Message& message){
    std::cout<<event<<" token="<<identity_text(id)<<" operation="<<message.operation()<<" status="<<message.status()
             <<" label="<<hex(message.label())<<" payload="<<hex(std::string(message.payload().begin(),message.payload().end()))<<std::endl;
}
static Message input(unsigned n,const std::string& tag){
    Message m;m.operation(n==2?UINT32_MAX:n==0?0:42);m.status(0);m.label(tag);
    if(n==1)m.label(std::string(1,char(128))+std::string(63,'x'));
    if(n==2){m.payload().resize(4096);for(unsigned i=0;i<4096;++i)m.payload()[i]=static_cast<uint8_t>(i);}
    else if(n)m.payload({0,255,127,static_cast<uint8_t>(n)});
    return m;
}
static Message transformed(Message value){value.status(value.operation()==UINT32_MAX?41:0);std::reverse(value.payload().begin(),value.payload().end());return value;}
static bool same(const Message& a,const Message& b){return a.operation()==b.operation()&&a.status()==b.status()&&a.label()==b.label()&&a.payload()==b.payload();}
template<class Predicate>static void wait(Predicate predicate,const char* what,unsigned ms=8000){
    auto end=Clock::now()+std::chrono::milliseconds(ms);
    while(!predicate()){require(Clock::now()<end,what);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
}
template<class Function>static void rejected(Function operation,const char* text){
    try{operation();}catch(const std::runtime_error& e){require(std::string(e.what())==text,"D3 wrong rejection");return;}
    throw std::runtime_error("D3 expected rejection missing");
}
static const char* outcome(Outcome value){
    switch(value){case Outcome::Reply:return "Reply";case Outcome::Timeout:return "Timeout";case Outcome::Cancelled:return "Cancelled";
        case Outcome::Closed:return "Closed";case Outcome::Pending:return "Pending";default:return "Unknown";}
}
static void client_run(Client& client,const std::string& mode,const fs::path& dir){
    if(mode=="not-ready"){
        rejected([&]{client.submit(input(0,"cold"),1000);},"D3 client not ready");
        require(client.occupied()==0,"not ready publication reservation");std::cout<<"D3_NOT_READY_REJECTED"<<std::endl;
    }
    wait([&]{return client.ready();},"D3 client discovery");std::cout<<"D3_READY role=client"<<std::endl;
    wait([&]{return fs::exists(dir/"go");},"D3 client go");
    bool guarded=false;std::thread other([&]{try{client.ready();}catch(const std::exception&){guarded=true;}});other.join();require(guarded,"D3 owner thread guard");
    auto invalid=input(0,"bad");invalid.payload().resize(4097);
    rejected([&]{client.submit(invalid,1000);},"D3 invalid request");
    rejected([&]{client.submit(input(0,"bad"),0);},"D3 deadline bound");
    unsigned count=mode=="basic"||mode=="reverse"||mode=="not-ready"?4:mode=="capacity"?2:1;
    std::vector<Identity> ids;std::vector<Message> expected;
    for(unsigned n=0;n<count;++n){auto value=input(n,dir.filename().string());auto id=client.submit(value,mode=="timeout"?200:5000);ids.push_back(id);expected.push_back(transformed(value));show("D3_SUBMITTED",id,value);}
    if(mode=="capacity"){
        rejected([&]{client.submit(input(2,"full"),5000);},"D3 client capacity");std::cout<<"D3_CAPACITY_REJECTED"<<std::endl;
    }
    if(mode=="cancel"){
        require(client.cancel(ids[0])&&!client.cancel(ids[0]),"D3 cancellation");std::cout<<"D3_CANCELLED token="<<identity_text(ids[0])<<std::endl;
    }
    if(mode=="closed"){
        require(client.close(),"D3 explicit client close");Result result;require(client.take(ids[0],result)&&result.outcome==Outcome::Closed,"D3 closed result");
        rejected([&]{client.submit(input(1,"closed"),5000);},"D3 endpoint closed/failed");
        std::cout<<"D3_RESULT token="<<identity_text(ids[0])<<" outcome=Closed"<<std::endl;return;
    }
    if(mode=="wrong"){
        wait([&]{client.poll();auto s=client.stats();return s.malformed>=1&&s.unmatched>=2;},"D3 malformed/unmatched rejection");
        require(client.state(ids[0])==Outcome::Pending,"D3 wrong identity completed call");std::cout<<"D3_REJECTIONS pending=1"<<std::endl;
    }
    if(mode=="timeout"){
        wait([&]{client.poll();return client.state(ids[0])==Outcome::Timeout;},"D3 request deadline");std::cout<<"D3_TIMEOUT token="<<identity_text(ids[0])<<std::endl;
    }
    if(mode=="timeout"||mode=="cancel"){
        wait([&]{client.poll();return client.stats().late>=1;},"D3 late reply rejection");
        Result r;require(client.take(ids[0],r)&&r.outcome==(mode=="timeout"?Outcome::Timeout:Outcome::Cancelled),"D3 terminal result changed");
        std::cout<<"D3_LATE_REJECTED"<<std::endl;std::cout<<"D3_RESULT token="<<identity_text(ids[0])<<" outcome="<<outcome(r.outcome)<<std::endl;return;
    }
    wait([&]{client.poll();for(const auto& id:ids)if(client.state(id)!=Outcome::Reply)return false;return true;},"D3 reply completion");
    if(mode=="duplicate"){
        std::cout<<"D3_REPLY_READY token="<<identity_text(ids[0])<<std::endl;
        wait([&]{client.poll();return client.stats().duplicates>=1;},"D3 duplicate reply rejection");std::cout<<"D3_DUPLICATE_REJECTED"<<std::endl;
    }
    if(mode=="capacity"){
        rejected([&]{client.submit(input(2,"still-full"),5000);},"D3 client capacity");std::cout<<"D3_COMPLETED_CAPACITY_REJECTED"<<std::endl;
        Result result;require(client.take(ids[0],result)&&same(result.message,expected[0]),"D3 first capacity result");show("D3_RESULT",ids[0],result.message);
        auto value=input(2,dir.filename().string());auto id=client.submit(value,5000);ids.push_back(id);expected.push_back(transformed(value));show("D3_SUBMITTED",id,value);
        wait([&]{client.poll();return client.state(id)==Outcome::Reply;},"D3 capacity slot reuse");
    }
    for(unsigned n=mode=="capacity"?1:0;n<ids.size();++n){
        Result result;require(client.take(ids[n],result)&&result.outcome==Outcome::Reply&&same(result.message,expected[n]),"D3 exact correlated reply");show("D3_RESULT",ids[n],result.message);
        require(client.state(ids[n])==Outcome::Unknown,"D3 result released");
    }
    require(client.occupied()==0,"D3 pending ownership");
}
static void server_run(Server& server,const std::string& mode,const fs::path& dir,unsigned expected){
    wait([&]{return server.ready();},"D3 server discovery");std::cout<<"D3_READY role=server"<<std::endl;
    wait([&]{return fs::exists(dir/"go");},"D3 server go");
    std::vector<Request> held;unsigned received=0,replied=0;bool capacity_checked=false;
    wait([&]{
        Request request;auto status=server.take(request);
        if(status==Receive::Request){held.push_back(request);++received;show("D3_RECEIVED",request.identity,request.message);}
        unsigned batch=mode=="capacity"?2:mode=="reverse"?expected:1;
        if(held.size()>=batch||received==expected){
            if(mode=="capacity"&&!capacity_checked){Request untouched;require(server.take(untouched)==Receive::Capacity,"D3 server capacity");capacity_checked=true;std::cout<<"D3_SERVER_CAPACITY_REJECTED"<<std::endl;}
            while(!held.empty()){
                auto item=held.back();held.pop_back();
                if(mode=="drop"){require(server.discard(item.identity)&&!server.discard(item.identity),"D3 discard ownership");std::cout<<"D3_DISCARDED token="<<identity_text(item.identity)<<std::endl;continue;}
                auto value=transformed(item.message);server.reply(item.identity,value);++replied;show("D3_REPLIED",item.identity,value);
                rejected([&]{server.reply(item.identity,value);},"D3 request not owned/already completed");
            }
        }
        return received==expected&&held.empty();
    },"D3 server requests");
    require(server.occupied()==0&&(mode=="drop"||replied==expected),"D3 server request ownership");
}
int main(int argc,char** argv){
    std::unique_ptr<Client> client;std::unique_ptr<Server> server;int code=0;
    try{
        require(argc==10,"usage: d3_actor ROLE DOMAIN SERVICE REP MODE DIR PEERS CAPACITY COUNT");
        std::string role=argv[1],mode=argv[5];fs::path dir=fs::absolute(argv[6]);
        Config c;c.domain=number(argv[2]);c.service=argv[3];c.representation=number(argv[4]);c.minimum_peers=number(argv[7]);c.capacity=number(argv[8]);unsigned count=number(argv[9]);
        require(count>=1&&count<=16,"D3 actor count");
        if(role=="client"){client=std::make_unique<Client>(c);client_run(*client,mode,dir);}
        else if(role=="server"){server=std::make_unique<Server>(c);server_run(*server,mode,dir,count);}
        else throw std::runtime_error("D3 role");
        std::cout<<"D3_DONE role="<<role<<std::endl;
        wait([&]{return fs::exists(dir/"stop");},"D3 actor stop",30000);
    }catch(const std::exception& error){std::cerr<<"D3_FAIL "<<error.what()<<std::endl;code=3;}
    if(client&&!client->close())code=3;if(server&&!server->close())code=3;
    bool owns=(client&&client->owns_participant())||(server&&server->owns_participant());
    std::cout<<"D3_CLOSED exit="<<code<<" owners="<<owns<<std::endl;return code;
}
