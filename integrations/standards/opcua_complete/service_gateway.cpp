#include "services.hpp"
#include "request_reply.hpp"
namespace d4 {
class ServiceGateway{
    Config c_;
    struct Session{
        UA_Client* client=nullptr;
        ~Session(){if(client)UA_Client_delete(client);}
        void open(const std::string& endpoint){client=UA_Client_new();check(client,"D4 client");auto* c=UA_Client_getConfig(client);c->timeout=1000;c->noReconnect=true;c->localConnectionConfig.localMaxMessageSize=65536;c->localConnectionConfig.localMaxChunkCount=16;good(UA_Client_connect(client,endpoint.c_str()));}
    };
    std::map<std::string,std::unique_ptr<Session>> sessions_;
public:
    explicit ServiceGateway(Config c):c_(std::move(c)){}
    int run(){
        std::unique_ptr<d3::Server> server;bool failed=false;unsigned completed=0;
        try{
            d3::Config d;d.domain=c_.domain;d.representation=c_.rep;d.service=c_.service;d.capacity=c_.capacity;server=std::make_unique<d3::Server>(d);
            auto end=Clock::now()+std::chrono::milliseconds(c_.run_ms);bool ready=false;
            while(!fs::exists(c_.stop)){
                check(Clock::now()<end,"D4 run deadline");
                if(!ready&&server->ready()){ready=true;std::cout<<"D4_SERVICE_READY"<<std::endl;}
                d3::Request request;
                if(server->take(request)==d3::Receive::Request){
                    auto answer=request.message;answer.payload().clear();UA_StatusCode status=UA_STATUSCODE_GOOD;
                    auto* descriptor=service(request.message.operation());
                    if(!descriptor)status=UA_STATUSCODE_BADSERVICEUNSUPPORTED;
                    else if(request.message.label()!="local")status=UA_STATUSCODE_BADSERVERNAMEMISSING;
                    else{
                        UaValue req(&UA_TYPES[descriptor->request]),response(&UA_TYPES[descriptor->response]);
                        status=decode(request.message.payload(),req.data,&UA_TYPES[descriptor->request]);
                        if(status==UA_STATUSCODE_GOOD){
                            auto writer=d3::identity_text(request.identity).substr(0,32);auto it=sessions_.find(writer);
                            if(it==sessions_.end()){
                                if(sessions_.size()>=4)status=UA_STATUSCODE_BADTOOMANYSESSIONS;
                                else{auto session=std::make_unique<Session>();session->open(c_.endpoint);it=sessions_.emplace(writer,std::move(session)).first;}
                            }
                            if(status==UA_STATUSCODE_GOOD){
                                invoke(it->second->client,request.message.operation(),req.data,response.data);
                                size_t size=UA_calcSizeBinary(response.data,&UA_TYPES[descriptor->response]);
                                if(!size||size>4096)status=UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED;
                                else answer.payload(encode(response.data,&UA_TYPES[descriptor->response]));
                            }
                        }
                    }
                    answer.status(status);server->reply(request.identity,answer);++completed;
                    std::cout<<"D4_SERVICE_REPLY token="<<d3::identity_text(request.identity)<<" op="<<answer.operation()<<" status="<<status<<" bytes="<<answer.payload().size()<<std::endl;
                }
                for(auto& item:sessions_)good(UA_Client_run_iterate(item.second->client,0));
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }catch(const std::exception& e){std::cerr<<"D4_SERVICE_FAIL "<<e.what()<<std::endl;failed=true;}
        for(auto& item:sessions_)if(UA_Client_disconnect(item.second->client)!=UA_STATUSCODE_GOOD)failed=true;
        sessions_.clear();if(server&&!server->close())failed=true;
        std::cout<<"D4_SERVICE_CLOSED exit="<<(failed?3:0)<<" owners="<<(server&&server->owns_participant())<<" completed="<<completed<<std::endl;return failed?3:0;
    }
};
}
int main(int argc,char** argv){try{d4::check(argc==2,"usage: d4_service_gateway CONFIG");d4::ServiceGateway gateway(d4::config(argv[1]));return gateway.run();}catch(const std::exception& e){std::cerr<<"D4_CONFIG_FAIL "<<e.what()<<std::endl;return 2;}}
