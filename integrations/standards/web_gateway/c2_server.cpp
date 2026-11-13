#include "c2_network.hpp"
#include "schema_hashes.hpp"
#include "websocket_schema.hpp"
#include <csignal>
#include <filesystem>
using namespace c2;
int main(int argc,char** argv) {
    try {
        std::map<std::string,std::string> options;
        for(int i=1;i<argc;i+=2){c1::need(i+1<argc&&options.emplace(argv[i],argv[i+1]).second,500,"options invalid");}
        for(const auto& item:options)c1::need(item.first=="--policy"||item.first=="--port"||item.first=="--tls-port"||item.first=="--tls-cert"||item.first=="--tls-key"||item.first=="--representation"||item.first=="--ttl"||item.first=="--stop-file"||item.first=="--schema-dir"||item.first=="--ws-schema",500,"unknown option");
        c1::need(options.count("--policy"),500,"policy required");
        auto numeric=[&](const char* name,unsigned fallback,unsigned maximum){
            if(!options.count(name))return fallback;size_t used=0;auto n=std::stoul(options.at(name),&used);
            c1::need(used==options.at(name).size()&&n<=maximum,500,"numeric option invalid");return static_cast<unsigned>(n);
        };
        bool secure=options.count("--tls-port")!=0;
        c1::need(secure==(options.count("--tls-cert")!=0)&&secure==(options.count("--tls-key")!=0),500,"TLS port, certificate and key must be specified together");
        std::shared_ptr<ssl::context> tls;
        if(secure) {
            tls=std::make_shared<ssl::context>(ssl::context::tls_server);
            c1::need(SSL_CTX_set_min_proto_version(tls->native_handle(),TLS1_2_VERSION)==1,500,"TLS minimum version failed");
            SSL_CTX_set_options(tls->native_handle(),SSL_OP_NO_COMPRESSION|SSL_OP_NO_RENEGOTIATION);
            tls->use_certificate_chain_file(options.at("--tls-cert"));tls->use_private_key_file(options.at("--tls-key"),ssl::context::pem);
            c1::need(SSL_CTX_check_private_key(tls->native_handle())==1,500,"TLS key does not match certificate");
        }
        auto stop_file=options["--stop-file"];c1::need(stop_file.empty()||!std::filesystem::exists(stop_file),500,"stop file already exists");
        auto gateway=std::make_shared<c1::Gateway>(options.count("--schema-dir")?options.at("--schema-dir"):C1_SCHEMA_DIR,options.at("--policy"),numeric("--representation",0,2),numeric("--ttl",300,3600));
        auto protocol=std::make_shared<Protocol>(gateway,options.count("--ws-schema")?options.at("--ws-schema"):C2_SCHEMA_FILE);
        net::io_context io(1);net::thread_pool workers(8);auto state=std::make_shared<State>(gateway,protocol,workers);state->tls=tls;
        auto plain=std::make_shared<Listener>(io,state,numeric("--port",8080,65535),false);
        std::shared_ptr<Listener> encrypted;if(secure)encrypted=std::make_shared<Listener>(io,state,numeric("--tls-port",0,65535),true);
        net::steady_timer timer(io);net::signal_set signals(io,SIGINT,SIGTERM);std::atomic<bool> expiry_pending{false};std::function<void()> tick;
        auto stop=[&]{gateway->stop();state->stop();plain->stop();if(encrypted)encrypted->stop();timer.cancel();signals.cancel();io.stop();};
        tick=[&]{timer.expires_after(100ms);timer.async_wait([&](beast::error_code ec){
            if(ec||state->stopping)return;
            if(!stop_file.empty()&&std::filesystem::exists(stop_file)){stop();return;}
            if(!expiry_pending.exchange(true))net::post(workers,[&]{
                try {if(!gateway->expire())std::cerr<<"C2_EXPIRY_CLEANUP FAIL\n";}
                catch(...){gateway->stop();net::post(io,stop);}
                expiry_pending=false;
            });tick();
        });};
        signals.async_wait([&](beast::error_code ec,int){if(!ec)stop();});plain->run();if(encrypted)encrypted->run();tick();
        std::cout<<"C2_READY http_port="<<plain->port()<<" tls_port="<<(encrypted?encrypted->port():0)<<"\n"<<std::flush;
        std::exception_ptr error;
        try {io.run();}catch(...){error=std::current_exception();}
        stop();workers.join();io.restart();try {io.run();}catch(...){error=std::current_exception();}
        if(!gateway->shutdown()){std::cerr<<"C2_DDS_SHUTDOWN FAIL\n"<<std::flush;std::_Exit(1);}
        c1::need(state->count==0&&state->registry.empty(),500,"network ownership not drained");
        if(error)std::rethrow_exception(error);
        std::cout<<"C2_SHUTDOWN PASS connections=0 endpoints=0\n";return 0;
    }catch(const std::exception& e){std::cerr<<"C2_GATEWAY FAIL: "<<e.what()<<"\n";return 1;}
     catch(...){std::cerr<<"C2_GATEWAY FAIL\n";return 1;}
}
