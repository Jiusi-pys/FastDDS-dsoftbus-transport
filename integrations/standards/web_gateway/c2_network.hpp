#pragma once
#include "websocket_protocol.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>
#include <type_traits>
namespace c2 {
namespace net=boost::asio;
namespace beast=boost::beast;
namespace http=beast::http;
namespace ws=beast::websocket;
namespace ssl=net::ssl;
using tcp=net::ip::tcp;
using Plain=beast::tcp_stream;
using Secure=beast::ssl_stream<Plain>;
using namespace std::chrono_literals;
inline std::string http_date(std::chrono::system_clock::time_point point) {
    auto value=std::chrono::system_clock::to_time_t(point);std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc,&value);
#else
    gmtime_r(&value,&utc);
#endif
    std::ostringstream text;text.imbue(std::locale::classic());text<<std::put_time(&utc,"%a, %d %b %Y %H:%M:%S GMT");return text.str();
}

// Observe the first data frame without interpreting payload or bypassing Beast.
// This enforces the HELLO single-frame rule even when TCP splits a frame.
template<class Stream> class TrackedStream {
    Stream stream_;
    std::array<unsigned char,14> header_{};
    size_t have_=0,need_=2;uint64_t remaining_=0;
    void observe(unsigned char byte) {
        if(first_data)return;
        if(remaining_){--remaining_;return;}
        header_[have_++]=byte;
        if(have_==2)need_=2+((header_[1]&127)==126?2:(header_[1]&127)==127?8:0)+((header_[1]&128)?4:0);
        if(have_!=need_)return;
        auto opcode=header_[0]&15;
        if(!(opcode&8)){first_data=true;single_text=(opcode==1&&(header_[0]&128));return;}
        remaining_=header_[1]&127;
        if(remaining_==126)remaining_=(uint64_t(header_[2])<<8)|header_[3];
        else if(remaining_==127){remaining_=0;for(size_t i=2;i<10;++i)remaining_=(remaining_<<8)|header_[i];}
        have_=0;need_=2;
    }
public:
    bool first_data=false,single_text=false;
    using executor_type=typename Stream::executor_type;
    explicit TrackedStream(Stream&& stream):stream_(std::move(stream)){}
    executor_type get_executor(){return stream_.get_executor();}
    Stream& next_layer(){return stream_;}
    const Stream& next_layer() const{return stream_;}
    template<class Buffers,class Handler> auto async_read_some(const Buffers& buffers,Handler&& handler) {
        return stream_.async_read_some(buffers,[this,buffers,handler=std::forward<Handler>(handler)](beast::error_code ec,size_t n) mutable {
            size_t left=n;
            for(auto it=net::buffer_sequence_begin(buffers);left&&it!=net::buffer_sequence_end(buffers);++it) {
                auto b=*it;auto used=std::min(left,b.size());auto* p=static_cast<const unsigned char*>(b.data());
                for(size_t i=0;i<used;++i)observe(p[i]);left-=used;
            }
            handler(ec,n);
        });
    }
    template<class Buffers,class Handler> auto async_write_some(const Buffers& buffers,Handler&& handler) {
        return stream_.async_write_some(buffers,std::forward<Handler>(handler));
    }
};
template<class Stream,class Handler> void async_teardown(beast::role_type role,TrackedStream<Stream>& stream,Handler&& handler) {
    using beast::websocket::async_teardown;using beast::async_teardown;
    async_teardown(role,stream.next_layer(),std::forward<Handler>(handler));
}
template<class Stream> void teardown(beast::role_type role,TrackedStream<Stream>& stream,beast::error_code& ec) {
    using beast::websocket::teardown;using beast::teardown;teardown(role,stream.next_layer(),ec);
}
struct Closable {virtual void stop()=0;virtual ~Closable()=default;};
struct State {
    std::shared_ptr<c1::Gateway> gateway;
    std::shared_ptr<Protocol> protocol;
    std::shared_ptr<ssl::context> tls;
    net::thread_pool& workers;
    std::atomic<unsigned> count{0};bool stopping=false;
    bool websocket_enabled=true;
    std::vector<std::weak_ptr<Closable>> sessions;
    std::mutex registry_mutex;
    std::map<std::string,uint64_t> registry;uint64_t next_id=0;
    State(std::shared_ptr<c1::Gateway> g,std::shared_ptr<Protocol> p,net::thread_pool& w):gateway(std::move(g)),protocol(std::move(p)),workers(w){}
    void track(const std::shared_ptr<Closable>& session) {
        sessions.erase(std::remove_if(sessions.begin(),sessions.end(),[](const auto& w){return w.expired();}),sessions.end());sessions.push_back(session);
    }
    uint64_t acquire(const std::string& name) {
        std::lock_guard<std::mutex> lock(registry_mutex);if(registry.count(name))return 0;
        auto id=++next_id;registry[name]=id;return id;
    }
    void release(const std::string& name,uint64_t id) {
        std::lock_guard<std::mutex> lock(registry_mutex);auto it=registry.find(name);if(it!=registry.end()&&it->second==id)registry.erase(it);
    }
    void stop() {stopping=true;for(auto& weak:sessions)if(auto session=weak.lock())session->stop();}
};
struct Token {std::shared_ptr<State> state;explicit Token(std::shared_ptr<State> s):state(std::move(s)){++state->count;}~Token(){--state->count;}};
template<class Stream> class WebSocketSession:public Closable,public std::enable_shared_from_this<WebSocketSession<Stream>> {
    ws::stream<TrackedStream<Stream>,false> stream_;
    beast::flat_buffer buffer_;
    std::shared_ptr<State> state_;std::shared_ptr<Token> token_;
    net::steady_timer hello_timer_,poll_timer_,write_timer_;
    std::string connection_,key_,registered_name_;uint64_t lease_=0;
    std::map<std::string,c1::Binding> bindings_;
    std::deque<std::string> inbox_;
    std::deque<std::shared_ptr<std::string>> outbox_;
    size_t inbox_bytes_=0,outbox_bytes_=0,round_robin_=0;
    bool authenticated_=false,stopped_=false,busy_=false,writing_=false,closing_=false,close_started_=false;
    ws::close_code close_code_=ws::close_code::normal;
    auto self(){return this->shared_from_this();}
    void close(ws::close_code code) {
        if(stopped_||closing_)return;closing_=true;close_code_=code;bindings_.clear();inbox_.clear();inbox_bytes_=0;
        if(writing_) {while(outbox_.size()>1){outbox_bytes_-=outbox_.back()->size();outbox_.pop_back();}}
        else {outbox_.clear();outbox_bytes_=0;write();}
    }
    void send(std::string message) {
        if(stopped_||closing_||message.empty())return;
        if(outbox_.size()>=8||outbox_bytes_+message.size()>2*1024*1024){close(ws::close_code::policy_error);return;}
        outbox_bytes_+=message.size();outbox_.push_back(std::make_shared<std::string>(std::move(message)));write();
    }
    void write() {
        if(stopped_||writing_)return;
        if(outbox_.empty()) {
            if(closing_&&!close_started_) {
                close_started_=true;write_timer_.expires_after(5s);write_timer_.async_wait([s=self()](beast::error_code ec){if(!ec)s->stop();});
                stream_.async_close(close_code_,[s=self()](beast::error_code){s->stop();});
            }
            return;
        }
        writing_=true;auto message=outbox_.front();stream_.text(true);
        write_timer_.expires_after(5s);write_timer_.async_wait([s=self()](beast::error_code ec){if(!ec)s->stop();});
        stream_.async_write(net::buffer(*message),[s=self(),message](beast::error_code ec,size_t) {
            s->write_timer_.cancel();s->writing_=false;
            if(ec){s->stop();return;}
            if(!s->outbox_.empty()){s->outbox_bytes_-=s->outbox_.front()->size();s->outbox_.pop_front();}
            s->write();s->work();
        });
    }
    void read() {
        if(stopped_||closing_)return;
        stream_.async_read(buffer_,[s=self()](beast::error_code ec,size_t) {
            if(ec){s->stop();return;}
            auto message=beast::buffers_to_string(s->buffer_.data());s->buffer_.consume(s->buffer_.size());
            if(!s->stream_.got_text()){s->close(ws::close_code::unknown_data);return;}
            if(!s->authenticated_) {
                try {
                    c1::need(s->stream_.next_layer().single_text,401,"HELLO must be a single text frame");
                    auto principal=s->state_->protocol->hello(message,s->key_);s->registered_name_=principal+":"+s->connection_;
                    s->lease_=s->state_->acquire(s->registered_name_);c1::need(s->lease_!=0,401,"connection name already active");
                    s->authenticated_=true;s->hello_timer_.cancel();s->send("HELLO_OK:bounded-c2");s->poll();
                } catch(...) {s->send("HELLO_FAIL:authentication or protocol rejected");s->close(ws::close_code::policy_error);return;}
            } else {
                if(s->inbox_.size()>=8||s->inbox_bytes_+message.size()>256*1024){s->close(ws::close_code::policy_error);return;}
                s->inbox_bytes_+=message.size();s->inbox_.push_back(std::move(message));s->work();
            }
            s->read();
        });
    }
    void work() {
        if(stopped_||closing_||busy_||inbox_.empty()||outbox_.size()>=4)return;
        busy_=true;auto message=std::move(inbox_.front());inbox_.pop_front();inbox_bytes_-=message.size();
        auto bindings=bindings_;auto key=key_;
        net::post(state_->workers,[s=self(),message=std::move(message),bindings=std::move(bindings),key]() mutable {
            Result result;int error=0;
            try {result=s->state_->protocol->process(message,key,bindings);}catch(const c1::Error& e){error=e.status;}catch(...){error=500;}
            net::post(s->stream_.get_executor(),[s,result=std::move(result),bindings=std::move(bindings),error]() mutable {
                s->busy_=false;if(s->stopped_||s->closing_)return;
                if(error){s->close(error>=500?ws::close_code::internal_error:ws::close_code::policy_error);return;}
                s->bindings_=std::move(bindings);s->send(std::move(result.message));s->work();
            });
        });
    }
    void poll() {
        if(stopped_||closing_)return;
        poll_timer_.expires_after(50ms);poll_timer_.async_wait([s=self()](beast::error_code ec) {
            if(ec||s->stopped_||s->closing_)return;
            if(!s->busy_&&s->inbox_.empty()&&!s->writing_&&s->outbox_.empty()) {
                std::vector<std::pair<std::string,c1::Binding>> readers;
                for(const auto& item:s->bindings_)if(item.second.reader)readers.push_back(item);
                if(!readers.empty()) {
                    auto item=readers[s->round_robin_++%readers.size()];auto key=s->key_;s->busy_=true;
                    net::post(s->state_->workers,[s,item,key] {
                        std::string message;int error=0;
                        try {message=s->state_->protocol->push(key,item.first,item.second);}catch(const c1::Error& e){error=e.status;}catch(...){error=500;}
                        net::post(s->stream_.get_executor(),[s,message=std::move(message),error]() mutable {
                            s->busy_=false;if(s->stopped_||s->closing_)return;
                            if(error){s->close(error>=500?ws::close_code::internal_error:ws::close_code::policy_error);return;}
                            s->send(std::move(message));s->work();
                        });
                    });
                }
            }
            s->poll();
        });
    }
public:
    WebSocketSession(Stream&& stream,std::shared_ptr<State> state,std::shared_ptr<Token> token,std::string name)
      :stream_(TrackedStream<Stream>(std::move(stream))),state_(std::move(state)),token_(std::move(token)),
       hello_timer_(stream_.get_executor()),poll_timer_(stream_.get_executor()),write_timer_(stream_.get_executor()),connection_(std::move(name)){}
    ~WebSocketSession(){state_->release(registered_name_,lease_);}
    void start(http::request<http::string_body> request) {
        state_->track(self());beast::get_lowest_layer(stream_).expires_never();
        stream_.read_message_max(65536);stream_.auto_fragment(true);stream_.write_buffer_bytes(16384);
        auto timeout=ws::stream_base::timeout::suggested(beast::role_type::server);timeout.idle_timeout=30s;timeout.handshake_timeout=5s;timeout.keep_alive_pings=true;stream_.set_option(timeout);
        stream_.set_option(ws::stream_base::decorator([](ws::response_type& res){res.set(http::field::sec_websocket_protocol,"dds-web");}));
        stream_.async_accept(request,[s=self()](beast::error_code ec) {
            if(ec){s->stop();return;}
            s->hello_timer_.expires_after(5s);s->hello_timer_.async_wait([s](beast::error_code ec){if(!ec)s->close(ws::close_code::policy_error);});s->read();
        });
    }
    void stop() override {
        if(stopped_)return;stopped_=true;bindings_.clear();inbox_.clear();hello_timer_.cancel();poll_timer_.cancel();write_timer_.cancel();
        state_->release(registered_name_,lease_);lease_=0;
        beast::error_code ignored;auto& socket=beast::get_lowest_layer(stream_).socket();socket.cancel(ignored);socket.shutdown(tcp::socket::shutdown_both,ignored);socket.close(ignored);
    }
};

template<class Stream> class HttpSession:public Closable,public std::enable_shared_from_this<HttpSession<Stream>> {
    Stream stream_;beast::flat_buffer buffer_;http::request_parser<http::string_body> parser_;
    std::shared_ptr<State> state_;std::shared_ptr<Token> token_;bool stopped_=false;unsigned port_;
    auto self(){return this->shared_from_this();}
    void reply(int status,c1::Response result={},bool head=false) {
        if(stopped_||state_->stopping)return;
        auto response=std::make_shared<http::response<http::string_body>>(static_cast<http::status>(status),11);
        response->set(http::field::cache_control,"no-store");response->set(http::field::content_type,"application/dds-web+xml");
        response->set(http::field::date,http_date(std::chrono::system_clock::now()));
        if(result.modified!=std::chrono::system_clock::time_point{})response->set(http::field::last_modified,http_date(result.modified));
        if(status==401)response->set(http::field::www_authenticate,"APIKey realm=\"DDS-WEB\"");
        if(status==405)response->set(http::field::allow,"GET, HEAD, POST, PUT, DELETE");
        if(!result.location.empty())response->set(http::field::location,std::string(std::is_same_v<Stream,Secure>?"https":"http")+"://127.0.0.1:"+std::to_string(port_)+result.location);
        auto length=result.body.size();if(!head)response->body()=std::move(result.body);response->keep_alive(false);if(status!=204)response->content_length(length);
        beast::get_lowest_layer(stream_).expires_after(5s);
        http::async_write(stream_,*response,[s=self(),response](beast::error_code ec,size_t){
            if constexpr(std::is_same_v<Stream,Secure>) {
                if(!ec&&!s->stopped_){s->stream_.async_shutdown([s](beast::error_code){s->stop();});return;}
            }
            s->stop();
        });
    }
    void read() {
        beast::get_lowest_layer(stream_).expires_after(5s);
        http::async_read(stream_,buffer_,parser_,[s=self()](beast::error_code ec,size_t) {
            if(s->stopped_||s->state_->stopping)return;
            if(ec){if(ec==http::error::body_limit)s->reply(413);else if(ec==http::error::header_limit)s->reply(431);else s->stop();return;}
            auto request=s->parser_.release();
            if(s->state_->websocket_enabled&&ws::is_upgrade(request)) {
                auto target=std::string(request.target());auto name=target.rfind("/dds/v1/",0)==0?target.substr(8):"";
                bool valid=!name.empty()&&name.size()<=48&&std::all_of(name.begin(),name.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';});
                auto protocols=std::string(request[http::field::sec_websocket_protocol]);bool offered=false;std::istringstream in(protocols);std::string p;
                while(std::getline(in,p,',')){auto a=p.find_first_not_of(" \t"),b=p.find_last_not_of(" \t");if(a!=std::string::npos&&p.substr(a,b-a+1)=="dds-web")offered=true;}
                if(!valid||!offered||request.version()!=11||request.method()!=http::verb::get||request.base().count(http::field::host)!=1||request[http::field::host].empty()||s->buffer_.size()!=0){s->reply(400);return;}
                auto socket=std::make_shared<WebSocketSession<Stream>>(std::move(s->stream_),s->state_,s->token_,name);s->stopped_=true;socket->start(std::move(request));return;
            }
            net::post(s->state_->workers,[s,request=std::move(request)]() mutable {
                c1::Response result;
                try {
                    c1::need(request.version()==11&&request.base().count(http::field::host)==1&&!request[http::field::host].empty(),400,"HTTP headers invalid");
                    c1::need(request.base().count("OMG-DDS-API-Key")==1,401,"API key required");
                    c1::need(request[http::field::upgrade].empty(),501,"upgrade unsupported");
                    c1::need(request.method()==http::verb::get||request.method()==http::verb::head||request.base().count(http::field::content_length)==1,411,"Content-Length required");
                    result=s->state_->gateway->handle({std::string(request.method_string()),std::string(request.target()),std::move(request.body()),std::string(request["OMG-DDS-API-Key"]),std::string(request[http::field::accept]),std::string(request[http::field::content_type]),std::string(request[http::field::cache_control])});
                }catch(const c1::Error& e){result.status=e.status;}catch(...){result.status=500;}
                bool head=request.method()==http::verb::head;
                net::post(s->stream_.get_executor(),[s,result=std::move(result),head]() mutable {s->reply(result.status,std::move(result),head);});
            });
        });
    }
public:
    HttpSession(Stream&& stream,std::shared_ptr<State> state,std::shared_ptr<Token> token,unsigned port)
      :stream_(std::move(stream)),state_(std::move(state)),token_(std::move(token)),port_(port){parser_.body_limit(65536);parser_.header_limit(8192);}
    void start() {
        state_->track(self());
        if constexpr(std::is_same_v<Stream,Secure>) {
            beast::get_lowest_layer(stream_).expires_after(5s);
            stream_.async_handshake(ssl::stream_base::server,[s=self()](beast::error_code ec){if(ec){s->stop();return;}s->read();});
        } else read();
    }
    void stop() override {
        if(stopped_)return;stopped_=true;beast::error_code ec;auto& socket=beast::get_lowest_layer(stream_).socket();socket.cancel(ec);socket.shutdown(tcp::socket::shutdown_both,ec);socket.close(ec);
    }
};
class Listener:public std::enable_shared_from_this<Listener> {
    tcp::acceptor acceptor_;std::shared_ptr<State> state_;bool secure_;
public:
    Listener(net::io_context& io,std::shared_ptr<State> state,unsigned port,bool secure)
      :acceptor_(io),state_(std::move(state)),secure_(secure) {
        tcp::endpoint address(net::ip::make_address("127.0.0.1"),static_cast<unsigned short>(port));acceptor_.open(address.protocol());acceptor_.bind(address);acceptor_.listen(32);
    }
    unsigned port() const{return acceptor_.local_endpoint().port();}
    void run() {
        acceptor_.async_accept([s=shared_from_this()](beast::error_code ec,tcp::socket socket) {
            if(!ec&&!s->state_->stopping&&s->state_->count<32) {
                auto token=std::make_shared<Token>(s->state_);
                if(s->secure_)std::make_shared<HttpSession<Secure>>(Secure(Plain(std::move(socket)),*s->state_->tls),s->state_,token,s->port())->start();
                else std::make_shared<HttpSession<Plain>>(Plain(std::move(socket)),s->state_,token,s->port())->start();
            }
            if(s->acceptor_.is_open()&&!s->state_->stopping)s->run();
        });
    }
    void stop(){beast::error_code ec;acceptor_.cancel(ec);acceptor_.close(ec);}
};
}
