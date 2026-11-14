#pragma once
#include "endpoint.hpp"
#include <deque>
#include <map>

namespace d3 {
enum class Outcome{Unknown,Pending,Reply,Timeout,Cancelled,Closed};
struct Result{Outcome outcome=Outcome::Unknown;Message message;Identity reply_identity;};
struct ClientStats{uint64_t replies=0,timeouts=0,cancelled=0,late=0,duplicates=0,unmatched=0,malformed=0;};
namespace detail {
class CallTable {
    struct Call{Clock::time_point deadline;Result result;};
    size_t capacity_;
    std::map<Identity,Call> calls_;
    std::deque<std::pair<Identity,Outcome>> retired_;
public:
    ClientStats stats;
    explicit CallTable(size_t capacity):capacity_(capacity){require(capacity>=1&&capacity<=16,"D3 pending capacity");}
    size_t size()const{return calls_.size();}
    bool insert(const Identity& id,Clock::time_point deadline){
        require(valid_identity(id),"D3 invalid call identity");
        if(calls_.size()>=capacity_)return false;
        require(calls_.find(id)==calls_.end(),"D3 reused call identity");
        for(const auto& old:retired_)require(old.first!=id,"D3 reused retired call identity");
        calls_.emplace(id,Call{deadline,Result{Outcome::Pending,{},Identity::unknown()}});return true;
    }
    void expire(Clock::time_point now){
        for(auto& item:calls_)if(item.second.result.outcome==Outcome::Pending&&now>=item.second.deadline){
            item.second.result.outcome=Outcome::Timeout;++stats.timeouts;
        }
    }
    void accept(const Identity& related,const Identity& reply,const Message& data,Clock::time_point now){
        expire(now);auto found=calls_.find(related);
        if(found==calls_.end()){
            for(const auto& old:retired_)if(old.first==related){
                if(old.second==Outcome::Reply)++stats.duplicates;else ++stats.late;return;
            }
            ++stats.unmatched;return;
        }
        auto& result=found->second.result;
        if(result.outcome==Outcome::Reply){++stats.duplicates;return;}
        if(result.outcome!=Outcome::Pending){++stats.late;return;}
        result={Outcome::Reply,data,reply};++stats.replies;
    }
    Outcome state(const Identity& id,Clock::time_point now){expire(now);auto it=calls_.find(id);return it==calls_.end()?Outcome::Unknown:it->second.result.outcome;}
    bool cancel(const Identity& id,Clock::time_point now){
        expire(now);auto it=calls_.find(id);if(it==calls_.end()||it->second.result.outcome!=Outcome::Pending)return false;
        it->second.result.outcome=Outcome::Cancelled;++stats.cancelled;return true;
    }
    bool take(const Identity& id,Result& result,Clock::time_point now){
        expire(now);auto it=calls_.find(id);if(it==calls_.end()||it->second.result.outcome==Outcome::Pending)return false;
        result=it->second.result;retired_.emplace_back(id,result.outcome);if(retired_.size()>64)retired_.pop_front();calls_.erase(it);return true;
    }
    void shutdown(){for(auto& item:calls_)if(item.second.result.outcome==Outcome::Pending)item.second.result.outcome=Outcome::Closed;}
};
}

class Client {
    detail::Endpoint endpoint_;
    detail::CallTable calls_;
public:
    explicit Client(const Config& config):endpoint_(config,true),calls_(config.capacity){}
    bool ready(){return endpoint_.ready();}
    size_t occupied()const{endpoint_.thread();return calls_.size();}
    Identity submit(Message message,unsigned timeout_ms){
        endpoint_.active();require(valid_message(message)&&message.status()==0,"D3 invalid request");
        require(timeout_ms>=1&&timeout_ms<=60000,"D3 deadline bound");
        require(calls_.size()<endpoint_.config.capacity,"D3 client capacity");
        require(endpoint_.ready(),"D3 client not ready");
        auto deadline=Clock::now()+std::chrono::milliseconds(timeout_ms);
        auto id=endpoint_.send(message);require(calls_.insert(id,deadline),"D3 pending insertion");return id;
    }
    unsigned poll(){
        endpoint_.active();calls_.expire(Clock::now());unsigned read=0;
        for(;read<16;++read){
            Message data;fd::SampleInfo info;if(!endpoint_.receive(data,info))break;
            if(!info.valid_data||!valid_message(data)||!valid_identity(info.sample_identity)||!valid_identity(info.related_sample_identity)){
                ++calls_.stats.malformed;continue;
            }
            calls_.accept(info.related_sample_identity,info.sample_identity,data,Clock::now());
        }
        return read;
    }
    Outcome state(const Identity& id){endpoint_.thread();return calls_.state(id,Clock::now());}
    bool cancel(const Identity& id){endpoint_.thread();return calls_.cancel(id,Clock::now());}
    bool take(const Identity& id,Result& result){endpoint_.thread();return calls_.take(id,result,Clock::now());}
    ClientStats stats()const{endpoint_.thread();return calls_.stats;}
    bool close(){endpoint_.thread();calls_.shutdown();return endpoint_.close();}
    bool owns_participant()const noexcept{return endpoint_.owns_participant();}
};

struct Request{Identity identity;Message message;};
enum class Receive{Empty,Request,Capacity};
class Server {
    detail::Endpoint endpoint_;
    std::map<Identity,Message> active_;
    std::deque<Identity> retired_;
public:
    uint64_t malformed=0,duplicates=0;
    explicit Server(const Config& config):endpoint_(config,false){}
    bool ready(){return endpoint_.ready();}
    size_t occupied()const{endpoint_.thread();return active_.size();}
    Receive take(Request& request){
        endpoint_.active();if(active_.size()>=endpoint_.config.capacity)return Receive::Capacity;
        for(unsigned budget=0;budget<16;++budget){
            Message message;fd::SampleInfo info;if(!endpoint_.receive(message,info))return Receive::Empty;
            if(!info.valid_data||!valid_identity(info.sample_identity)||info.related_sample_identity!=Identity::unknown()||!valid_message(message)||message.status()!=0){++malformed;continue;}
            bool old=active_.find(info.sample_identity)!=active_.end();
            for(const auto& id:retired_)if(id==info.sample_identity)old=true;
            if(old){++duplicates;continue;}
            active_.emplace(info.sample_identity,message);request={info.sample_identity,message};return Receive::Request;
        }
        return Receive::Empty;
    }
    Identity reply(const Identity& request,Message message){
        endpoint_.active();require(valid_message(message),"D3 invalid reply");
        auto found=active_.find(request);require(found!=active_.end(),"D3 request not owned/already completed");
        require(endpoint_.ready(),"D3 server not ready");
        auto result=endpoint_.send(message,request);active_.erase(found);retired_.push_back(request);if(retired_.size()>64)retired_.pop_front();return result;
    }
    bool discard(const Identity& request){
        endpoint_.active();auto it=active_.find(request);if(it==active_.end())return false;
        active_.erase(it);retired_.push_back(request);if(retired_.size()>64)retired_.pop_front();return true;
    }
    bool close(){endpoint_.thread();active_.clear();return endpoint_.close();}
    bool owns_participant()const noexcept{return endpoint_.owns_participant();}
};
}
