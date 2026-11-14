#include "mapping.hpp"
#include <open62541/client_config_default.h>
#include <open62541/client_subscriptions.h>
#include <chrono>
#include <iostream>
#include <thread>

namespace d1 {
using Clock=std::chrono::steady_clock;
class UaDdsGateway {
    Config config_; Queue queue_; std::atomic<Failure> failure_{Failure::none};
    b4_endpoint* endpoint_=nullptr; b4_sample* sample_=nullptr; UA_Client* client_=nullptr;
    std::thread worker_; std::mutex log_; bool deleting_=false,subscription_lost_=false;
    uint64_t accepted_=0,published_=0,connections_=0,attempts_=0,discarded_=0;
    struct Context { UaDdsGateway* gateway; const Item* item; };
    std::array<Context,8> contexts_{};
    void fail(Failure value) noexcept { auto expected=Failure::none; failure_.compare_exchange_strong(expected,value); }
    void state(const char* text) { std::lock_guard<std::mutex> lock(log_); std::cout<<"D1_STATE "<<text<<" attempts="<<attempts_<<" connections="<<connections_<<std::endl; }
    static void changed(UA_Client*,UA_UInt32,void*,UA_UInt32,void* context,UA_DataValue* value) noexcept {
        auto& c=*static_cast<Context*>(context); auto& self=*c.gateway;
        try {
            if(self.deleting_ || self.failure_!=Failure::none) return;
            b4_record record{}; auto result=map_value(*c.item,*value,record);
            if(result!=Failure::none) { self.fail(result); return; }
            if(!self.queue_.push(record)) { self.fail(Failure::overflow); return; }
            ++self.accepted_;
        } catch(...) { self.fail(Failure::callback); }
    }
    static void deleted(UA_Client*,UA_UInt32,void* context) noexcept {
        auto& self=*static_cast<UaDdsGateway*>(context); if(!self.deleting_) self.subscription_lost_=true;
    }
    void delete_client() {
        if(!client_) return;
        deleting_=true;
        auto code=UA_Client_disconnect(client_);
        { std::lock_guard<std::mutex> lock(log_); std::cout<<"D1_UA_DISCONNECT code="<<code<<std::endl; }
        UA_Client_delete(client_); client_=nullptr; deleting_=false; subscription_lost_=false;
    }
    bool setup_failed(UA_StatusCode code) {
        UA_SecureChannelState channel; UA_SessionState session; UA_StatusCode recovery;
        UA_Client_getState(client_,&channel,&session,&recovery);
        // Service creation can lose its transport/session just like run_iterate.
        // Healthy-session item/configuration rejections remain terminal errors.
        bool retry=channel!=UA_SECURECHANNELSTATE_OPEN || session!=UA_SESSIONSTATE_ACTIVATED ||
            recovery!=UA_STATUSCODE_GOOD || code==UA_STATUSCODE_BADTIMEOUT ||
            code==UA_STATUSCODE_BADREQUESTTIMEOUT || code==UA_STATUSCODE_BADCONNECTIONCLOSED ||
            code==UA_STATUSCODE_BADNOTCONNECTED || code==UA_STATUSCODE_BADSESSIONCLOSED ||
            code==UA_STATUSCODE_BADSESSIONIDINVALID || code==UA_STATUSCODE_BADSECURECHANNELCLOSED;
        { std::lock_guard<std::mutex> lock(log_); std::cout<<"D1_SETUP_FAILED code="<<code<<" retry="<<retry<<std::endl; }
        if(!retry) fail(Failure::ua_setup);
        return false;
    }
    bool connect() {
        ++attempts_; state("CONNECTING"); client_=UA_Client_new();
        if(!client_) { fail(Failure::ua_setup); return false; }
        auto* cc=UA_Client_getConfig(client_);
        if(UA_ClientConfig_setDefault(cc)!=UA_STATUSCODE_GOOD) { fail(Failure::ua_setup); return false; }
        cc->timeout=config_.timeout; cc->connectivityCheckInterval=200;
        cc->requestedSessionTimeout=2000; cc->noReconnect=true; cc->noNewSession=true;
        cc->securityMode=UA_MESSAGESECURITYMODE_NONE;
        // Cap individual incoming messages and outstanding Publish requests too.
        cc->localConnectionConfig.recvBufferSize=65536; cc->localConnectionConfig.sendBufferSize=65536;
        cc->localConnectionConfig.localMaxMessageSize=262144; cc->localConnectionConfig.localMaxChunkCount=4;
        cc->outStandingPublishRequests=2;
        auto result=UA_Client_connect(client_,config_.endpoint.c_str());
        if(result!=UA_STATUSCODE_GOOD) {
            std::lock_guard<std::mutex> lock(log_); std::cout<<"D1_CONNECT_FAILED code="<<result<<std::endl; return false;
        }
        auto request=UA_CreateSubscriptionRequest_default(); request.requestedPublishingInterval=config_.publish;
        request.requestedLifetimeCount=30; request.requestedMaxKeepAliveCount=5; request.maxNotificationsPerPublish=256;
        auto response=UA_Client_Subscriptions_create(client_,request,this,nullptr,deleted);
        auto code=response.responseHeader.serviceResult; auto subscription=response.subscriptionId;
        auto revised=response.revisedPublishingInterval;
        UA_CreateSubscriptionResponse_clear(&response);
        if(code!=UA_STATUSCODE_GOOD) return setup_failed(code);
        for(size_t i=0;i<config_.items.size();++i) {
            const auto& item=config_.items[i]; contexts_[i]={this,&item};
            auto monitor=UA_MonitoredItemCreateRequest_default(UA_NODEID_NUMERIC(static_cast<UA_UInt16>(item.ns),item.node));
            monitor.requestedParameters.samplingInterval=config_.sampling;
            monitor.requestedParameters.queueSize=config_.ua_queue;
            monitor.requestedParameters.discardOldest=false;
            auto reply=UA_Client_MonitoredItems_createDataChange(client_,subscription,UA_TIMESTAMPSTORETURN_BOTH,monitor,&contexts_[i],changed,nullptr);
            code=reply.statusCode;
            { std::lock_guard<std::mutex> lock(log_); std::cout<<"D1_MONITOR key="<<item.key<<" code="<<code<<" sampling="<<reply.revisedSamplingInterval<<" queue="<<reply.revisedQueueSize<<std::endl; }
            UA_MonitoredItemCreateResult_clear(&reply);
            if(code!=UA_STATUSCODE_GOOD) return setup_failed(code);
        }
        ++connections_;
        { std::lock_guard<std::mutex> lock(log_); std::cout<<"D1_SUBSCRIBED generation="<<connections_<<" publishing="<<revised<<" items="<<config_.items.size()<<std::endl; }
        state("SUBSCRIBED"); return true;
    }
    bool stopped() const { return std::filesystem::exists(config_.stop); }
    void consume() noexcept {
        try {
            b4_record value{};
            while(queue_.pop(value)) {
                if(failure_!=Failure::none) { ++discarded_; continue; }
                if(b4_sample_set(sample_,&value)!=B4_OK || b4_write(endpoint_,sample_)!=B4_OK) { fail(Failure::dds); ++discarded_; continue; }
                ++published_; std::lock_guard<std::mutex> lock(log_);
                std::cout<<"D1_WRITE key="<<value.id<<" value="<<value.sequence_number<<std::endl;
            }
        } catch(...) { fail(Failure::dds); }
    }
    void cleanup() {
        state("STOPPING"); delete_client(); queue_.close();
        if(worker_.joinable()) worker_.join();
        if(endpoint_ && published_) {
            // Stock 2.14.5 periodic heartbeat is 3s; use the existing B3/B4/B5
            // acceptance budget, once, without altering DDS reliability/QoS.
            auto began=Clock::now(); auto code=b4_wait_ack(endpoint_,5000);
            std::cout<<"D1_DDS_ACK code="<<code<<" elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-began).count()<<std::endl;
            if(code!=B4_OK) fail(Failure::dds);
        }
        if(sample_) { if(b4_sample_destroy(sample_)==B4_OK) sample_=nullptr; else fail(Failure::cleanup); }
        if(endpoint_) {
            auto result=b4_close(endpoint_);
            if(result!=B4_OK) result=b4_close(endpoint_); // one explicit close-only retry
            if(result==B4_OK) endpoint_=nullptr; else fail(Failure::cleanup);
        }
        state("STOPPED");
    }
public:
    explicit UaDdsGateway(Config config):config_(std::move(config)),queue_(config_.capacity) {}
    int run() {
        state("STARTING");
        try {
            if(!stopped()) {
                b4_options options{config_.domain,config_.topic.c_str(),1,static_cast<int>(config_.representation)};
                if(b4_open(&options,&endpoint_)!=B4_OK || b4_sample_create(&sample_)!=B4_OK) fail(Failure::dds);
                if(failure_==Failure::none) {
                    // Match waits are cancellable in short bounded slices.
                    auto until=Clock::now()+std::chrono::milliseconds(config_.match); bool matched=false;
                    while(!stopped() && Clock::now()<until) {
                        auto code=b4_wait_match(endpoint_,100); if(code==B4_OK) { matched=true; break; }
                        if(code!=B4_TIMEOUT) { fail(Failure::dds); break; }
                    }
                    if(!matched && !stopped()) fail(Failure::dds);
                }
                if(failure_==Failure::none && !stopped()) {
                    state("DDS_READY"); worker_=std::thread(&UaDdsGateway::consume,this);
                    auto deadline=Clock::now()+std::chrono::milliseconds(config_.run);
                    auto retry_at=Clock::now();
                    while(!stopped() && failure_==Failure::none) {
                        if(Clock::now()>=deadline) { fail(Failure::deadline); break; }
                        if(!client_) {
                            if(Clock::now()<retry_at) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
                            if(attempts_>=config_.retries) { fail(Failure::retry_limit); break; }
                            if(!connect()) { delete_client(); state("RETRY_WAIT"); retry_at=Clock::now()+std::chrono::milliseconds(config_.reconnect); continue; }
                        }
                        auto code=UA_Client_run_iterate(client_,20);
                        UA_SecureChannelState channel; UA_SessionState session; UA_StatusCode recovery;
                        UA_Client_getState(client_,&channel,&session,&recovery);
                        if(code!=UA_STATUSCODE_GOOD || recovery!=UA_STATUSCODE_GOOD || session!=UA_SESSIONSTATE_ACTIVATED || subscription_lost_) {
                            delete_client(); state("RETRY_WAIT"); retry_at=Clock::now()+std::chrono::milliseconds(config_.reconnect);
                        }
                    }
                }
            }
        } catch(const std::exception& error) { std::cerr<<"D1_EXCEPTION "<<error.what()<<std::endl; fail(Failure::callback); }
        catch(...) { fail(Failure::callback); }
        cleanup();
        std::cout<<"D1_RESULT status="<<(failure_==Failure::none?"PASS":"FAIL")<<" reason="<<name(failure_)<<" accepted="<<accepted_<<" published="<<published_<<" discarded="<<discarded_<<" peak="<<queue_.peak()<<" connections="<<connections_<<" attempts="<<attempts_<<" ua_owners="<<(client_?1:0)<<" dds_owners="<<(endpoint_?1:0)<<" sample_owners="<<(sample_?1:0)<<std::endl;
        return failure_==Failure::none?0:3;
    }
};
}
int main(int argc,char** argv) {
    try { if(argc!=2) throw std::runtime_error("usage: ua_dds_gateway CONFIG"); d1::UaDdsGateway gateway(d1::load(argv[1])); return gateway.run(); }
    catch(const std::exception& e) { std::cerr<<"D1_CONFIG_FAIL "<<e.what()<<std::endl; return 2; }
}
