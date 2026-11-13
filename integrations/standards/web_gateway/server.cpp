#include "model.hpp"
#include "schema_hashes.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <locale>
namespace net=boost::asio;
namespace beast=boost::beast;
namespace http=beast::http;
using tcp=net::ip::tcp;
using namespace std::chrono_literals;
namespace {
std::string http_date(std::chrono::system_clock::time_point point) {
    auto seconds=std::chrono::system_clock::to_time_t(point);std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc,&seconds);
#else
    gmtime_r(&seconds,&utc);
#endif
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::put_time(&utc,"%a, %d %b %Y %H:%M:%S GMT");return out.str();
}
struct Session:std::enable_shared_from_this<Session> {
    beast::tcp_stream stream;
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    std::shared_ptr<c1::Gateway> gateway;
    net::thread_pool& workers;
    std::shared_ptr<std::atomic<unsigned>> connections;
    unsigned port;
    Session(tcp::socket socket,std::shared_ptr<c1::Gateway> g,net::thread_pool& w,
            std::shared_ptr<std::atomic<unsigned>> count,unsigned p)
        :stream(std::move(socket)),gateway(std::move(g)),workers(w),connections(std::move(count)),port(p) {
        parser.body_limit(65536);parser.header_limit(8192);
    }
    ~Session(){--*connections;}
    void run() {
        stream.expires_after(5s);
        http::async_read(stream,buffer,parser,[self=shared_from_this()](beast::error_code ec,std::size_t) {
            if(ec) {
                if(ec==http::error::body_limit)self->reply(413,{},false);
                else if(ec==http::error::header_limit)self->reply(431,{},false);
                else if(ec!=beast::error::timeout&&ec!=net::error::operation_aborted)self->reply(400,{},false);
                return;
            }
            auto request=self->parser.release();
            net::post(self->workers,[self,request=std::move(request)]() mutable {
                c1::Response response;bool head=request.method()==http::verb::head;
                try {
                    c1::need(request.version()==11,505,"HTTP version unsupported");
                    c1::need(request.base().count(http::field::host)==1&&!request[http::field::host].empty(),400,"Host required");
                    c1::need(request.base().count("OMG-DDS-API-Key")==1,401,"exactly one API key required");
                    c1::need(request[http::field::upgrade].empty(),501,"protocol upgrade outside C1");
                    c1::need(request.method()==http::verb::get||head||request.base().count(http::field::content_length)==1,411,"Content-Length required");
                    c1::Request r{std::string(request.method_string()),std::string(request.target()),std::move(request.body()),
                        std::string(request["OMG-DDS-API-Key"]),std::string(request[http::field::accept]),
                        std::string(request[http::field::content_type]),std::string(request[http::field::cache_control])};
                    response=self->gateway->handle(r);
                } catch(const c1::Error& error) {response.status=error.status;response.body.clear();}
                  catch(...) {response.status=500;response.body.clear();}
                net::post(self->stream.get_executor(),[self,response=std::move(response),head]() mutable {
                    self->reply(response.status,std::move(response),head);
                });
            });
        });
    }
    void reply(int code,c1::Response value,bool head) {
        auto response=std::make_shared<http::response<http::string_body>>(static_cast<http::status>(code),11);
        response->set(http::field::server,"standards-c1");
        response->set(http::field::date,http_date(std::chrono::system_clock::now()));
        if(value.modified!=std::chrono::system_clock::time_point{})response->set(http::field::last_modified,http_date(value.modified));
        response->set(http::field::cache_control,"no-store");
        response->set(http::field::content_type,"application/dds-web+xml");
        if(code==401)response->set(http::field::www_authenticate,"APIKey realm=\"DDS-WEB\"");
        if(code==405)response->set(http::field::allow,"GET, HEAD, POST, PUT, DELETE");
        if(!value.location.empty())response->set(http::field::location,"http://127.0.0.1:"+std::to_string(port)+value.location);
        response->keep_alive(false);auto length=value.body.size();
        if(!head)response->body()=std::move(value.body);
        if(code!=204)response->content_length(length);
        stream.expires_after(5s);
        http::async_write(stream,*response,[self=shared_from_this(),response](beast::error_code,std::size_t) {
            beast::error_code ignored;self->stream.socket().shutdown(tcp::socket::shutdown_both,ignored);self->stream.socket().close(ignored);
        });
    }
};
struct Listener:std::enable_shared_from_this<Listener> {
    tcp::acceptor acceptor;
    std::shared_ptr<c1::Gateway> gateway;
    net::thread_pool& workers;
    std::shared_ptr<std::atomic<unsigned>> connections=std::make_shared<std::atomic<unsigned>>(0);
    Listener(net::io_context& io,std::shared_ptr<c1::Gateway> g,net::thread_pool& w,unsigned port)
      :acceptor(io),gateway(std::move(g)),workers(w) {
        tcp::endpoint address(net::ip::make_address("127.0.0.1"),static_cast<unsigned short>(port));
        acceptor.open(address.protocol());acceptor.bind(address);acceptor.listen(32);
    }
    void run() {
        acceptor.async_accept([self=shared_from_this()](beast::error_code ec,tcp::socket socket) {
            if(!ec) {
                if(self->connections->load()<32) {
                    ++*self->connections;
                    std::make_shared<Session>(std::move(socket),self->gateway,self->workers,self->connections,self->acceptor.local_endpoint().port())->run();
                } else {beast::error_code ignored;socket.close(ignored);}
            }
            if(self->acceptor.is_open())self->run();
        });
    }
};
}
int main(int argc,char** argv) {
    try {
        std::map<std::string,std::string> options;
        for(int i=1;i<argc;i+=2) {
            c1::need(i+1<argc,500,"options require values");
            c1::need(options.emplace(argv[i],argv[i+1]).second,500,"duplicate option");
        }
        for(const auto& item:options)c1::need(item.first=="--policy"||item.first=="--port"||item.first=="--representation"||item.first=="--ttl"||item.first=="--stop-file"||item.first=="--schema-dir",500,"unknown option");
        c1::need(options.count("--policy"),500,"--policy required");
        auto numeric=[&](const char* key,unsigned fallback,unsigned maximum) {
            if(!options.count(key))return fallback;
            auto text=options.at(key);size_t used=0;auto n=std::stoul(text,&used);
            c1::need(used==text.size()&&n<=maximum,500,"option integer invalid");return static_cast<unsigned>(n);
        };
        auto gateway=std::make_shared<c1::Gateway>(options.count("--schema-dir")?options["--schema-dir"]:C1_SCHEMA_DIR,
            options["--policy"],numeric("--representation",0,2),numeric("--ttl",300,3600));
        net::thread_pool workers(8);net::io_context io(1);
        auto listener=std::make_shared<Listener>(io,gateway,workers,numeric("--port",8080,65535));
        auto stop_file=options["--stop-file"];
        c1::need(stop_file.empty()||!std::filesystem::exists(stop_file),500,"stop file already exists");
        std::atomic<bool> expire_pending{false};
        std::atomic<bool> expire_reported{false};
        net::steady_timer timer(io);std::function<void()> tick;
        tick=[&] {
            timer.expires_after(100ms);timer.async_wait([&](beast::error_code ec) {
                if(ec)return;
                if(!stop_file.empty()&&std::filesystem::exists(stop_file)) {gateway->stop();io.stop();return;}
                if(!expire_pending.exchange(true))net::post(workers,[&] {
                    try {
                        if(!gateway->expire()) {if(!expire_reported.exchange(true))std::cerr<<"C1_EXPIRY_CLEANUP FAIL\n";}
                        else expire_reported=false;
                    } catch(...) {
                        std::cerr<<"C1_EXPIRY_EXCEPTION FAIL\n";gateway->stop();net::post(io,[&]{io.stop();});
                    }
                    expire_pending=false;
                });
                tick();
            });
        };
        net::signal_set signals(io,SIGINT,SIGTERM);
        signals.async_wait([&](beast::error_code,int){gateway->stop();io.stop();});
        listener->run();tick();
        std::cout<<"C1_READY port="<<listener->acceptor.local_endpoint().port()<<"\n"<<std::flush;
        try {io.run();}
        catch(...) {
            gateway->stop();workers.join();
            if(!gateway->shutdown()){std::cerr<<"C1_SHUTDOWN FAIL\n"<<std::flush;std::_Exit(1);}
            throw;
        }
        gateway->stop();workers.join();
        if(!gateway->shutdown()) {std::cerr<<"C1_SHUTDOWN FAIL\n"<<std::flush;std::_Exit(1);}
        std::cout<<"C1_SHUTDOWN PASS\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"C1_STARTUP FAIL: "<<e.what()<<"\n";return 1;}
      catch(...) {std::cerr<<"C1_STARTUP FAIL\n";return 1;}
}
