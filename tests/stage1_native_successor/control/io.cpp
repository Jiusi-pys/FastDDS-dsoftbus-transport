#include "io.hpp"
#include <mutex>
#include <condition_variable>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <cerrno>
#include <sys/stat.h>
#endif
namespace stage1_control {
namespace {
struct Flag {std::atomic_flag& f;explicit Flag(std::atomic_flag& a):f(a){}~Flag(){f.clear();}};
int remaining(Clock::time_point end){auto n=std::chrono::duration_cast<std::chrono::milliseconds>(end-Clock::now()).count();return n<=0?0:static_cast<int>(std::min<int64_t>(n+1,INT32_MAX));}
}
struct Pipe::State {
    std::mutex gate;std::condition_variable changed;size_t active=0,read_pending=0,write_pending=0;bool closing=false,retained=false,ever_io=false;
    std::atomic<bool> cancelled{false};
    std::atomic_flag read_busy=ATOMIC_FLAG_INIT,write_busy=ATOMIC_FLAG_INIT;
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE,cancel_event=nullptr;
    struct Operation {
        OVERLAPPED ov{};std::vector<uint8_t> buffer;
        explicit Operation(size_t n):buffer(n){ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!ov.hEvent)throw std::runtime_error("pipe event");}
        ~Operation(){CloseHandle(ov.hEvent);}
    };
    Operation* pending_read=nullptr;Operation* pending_write=nullptr;Operation* pending_connect=nullptr;
    ~State(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);if(cancel_event)CloseHandle(cancel_event);}
#else
    int input=-1,output=-1,cancel_read=-1,cancel_write=-1;
    ~State(){for(int fd:{input,output,cancel_read,cancel_write})if(fd>=0)::close(fd);}
#endif
};
Pipe::Pipe(std::unique_ptr<State> s):state_(std::move(s)){}
Pipe::~Pipe(){if(!close(Clock::now()+std::chrono::seconds(1)))(void)state_.release();} // explicit retained kernel/user storage until process exit
Error Pipe::take_fresh_endpoint(Endpoint& out){
    auto& s=*state_;std::lock_guard<std::mutex> l(s.gate);
    if(s.active||s.retained||s.closing||s.ever_io||s.cancelled)return Error::Closed;
#ifdef _WIN32
    if(s.handle==INVALID_HANDLE_VALUE)return Error::Closed;
    out.input=out.output=reinterpret_cast<uintptr_t>(s.handle);s.handle=INVALID_HANDLE_VALUE;
#else
    if(s.input<0||s.output<0)return Error::Closed;
    out.input=static_cast<uint64_t>(s.input);out.output=static_cast<uint64_t>(s.output);s.input=s.output=-1;
#endif
    s.closing=true;return Error::None;
}
std::shared_ptr<Pipe> Pipe::adopt_owned(Endpoint& e,uint32_t expected_creator,Error& error){
    error=Error::Invalid;
#ifdef _WIN32
    if(e.input==UINT64_MAX||e.input!=e.output||!expected_creator)return {};
    HANDLE h=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(e.input));DWORD flags=0,mode=0,server=0;
    if(GetFileType(h)!=FILE_TYPE_PIPE||!GetNamedPipeInfo(h,&flags,nullptr,nullptr,nullptr)||(flags&PIPE_TYPE_MESSAGE)||!GetNamedPipeHandleStateW(h,&mode,nullptr,nullptr,nullptr,nullptr,0)||(mode&PIPE_READMODE_MESSAGE)||!GetNamedPipeServerProcessId(h,&server)||server!=expected_creator)return {};
    auto s=std::make_unique<State>();s->cancel_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!s->cancel_event)return {};
    std::shared_ptr<Pipe> pipe(new Pipe(std::move(s))); // allocate control block BEFORE transfer
    if(!SetHandleInformation(h,HANDLE_FLAG_INHERIT,0))return {};
    pipe->state_->handle=h;
#else
    (void)expected_creator;
    if(e.input>static_cast<uint64_t>(INT32_MAX)||e.output>static_cast<uint64_t>(INT32_MAX)||e.input==e.output)return {};
    const int in=static_cast<int>(e.input),out=static_cast<int>(e.output);struct stat a{},b{};const int af=fcntl(in,F_GETFL),bf=fcntl(out,F_GETFL);
    if(fstat(in,&a)||fstat(out,&b)||!S_ISFIFO(a.st_mode)||!S_ISFIFO(b.st_mode)||af<0||bf<0||(af&O_ACCMODE)!=O_RDONLY||(bf&O_ACCMODE)!=O_WRONLY||!(af&O_NONBLOCK)||!(bf&O_NONBLOCK))return {};
    auto s=std::make_unique<State>();int stop[2];if(::pipe(stop))return {};
    s->cancel_read=stop[0];s->cancel_write=stop[1];for(int fd:stop)if(fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK)||fcntl(fd,F_SETFD,FD_CLOEXEC))return {};
    std::shared_ptr<Pipe> pipe(new Pipe(std::move(s)));
    if(fcntl(in,F_SETFD,FD_CLOEXEC)||fcntl(out,F_SETFD,FD_CLOEXEC))return {};
    pipe->state_->input=in;pipe->state_->output=out;
#endif
    e={};error=Error::None;return pipe;
}
Pipe::Pair Pipe::local_pair(){
    auto a=std::make_unique<State>(),b=std::make_unique<State>();
#ifdef _WIN32
    static std::atomic<unsigned> serial{0};wchar_t name[160]{};
    if(swprintf(name,160,L"\\\\.\\pipe\\fastdds-owned-control-%lu-%u",GetCurrentProcessId(),++serial)<0)throw std::runtime_error("pipe name");
    a->handle=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,256,256,0,nullptr);
    if(a->handle==INVALID_HANDLE_VALUE)throw std::runtime_error("CreateNamedPipe");
    b->handle=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    if(b->handle==INVALID_HANDLE_VALUE)throw std::runtime_error("CreateFile pipe");
    auto connect=std::make_unique<State::Operation>(1);
    const BOOL connected=ConnectNamedPipe(a->handle,&connect->ov);const DWORD error=connected?0:GetLastError();
    if(!connected&&error==ERROR_IO_PENDING){
        if(WaitForSingleObject(connect->ov.hEvent,1000)!=WAIT_OBJECT_0){
            (void)CancelIoEx(a->handle,&connect->ov);
            if(WaitForSingleObject(connect->ov.hEvent,1000)!=WAIT_OBJECT_0){a->retained=true;a->pending_connect=connect.release();(void)a.release();}
            throw std::runtime_error("owned connect completion timeout");
        }
        DWORD ignored=0;if(!GetOverlappedResult(a->handle,&connect->ov,&ignored,FALSE))throw std::runtime_error("owned connect failure");
    }else if(!connected&&error!=ERROR_PIPE_CONNECTED)throw std::runtime_error("local connect not established");
    a->cancel_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);b->cancel_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!a->cancel_event||!b->cancel_event)throw std::runtime_error("cancel event");
#else
    auto pipes=[](int f[2]){if(::pipe(f))throw std::runtime_error("pipe");for(unsigned i=0;i<2;++i){if(fcntl(f[i],F_SETFL,fcntl(f[i],F_GETFL)|O_NONBLOCK)||fcntl(f[i],F_SETFD,FD_CLOEXEC)){::close(f[0]);::close(f[1]);throw std::runtime_error("owned nonblock");}}};
    int ab[2];pipes(ab);a->output=ab[1];b->input=ab[0];int ba[2];pipes(ba);b->output=ba[1];a->input=ba[0];int ac[2];pipes(ac);a->cancel_read=ac[0];a->cancel_write=ac[1];int bc[2];pipes(bc);b->cancel_read=bc[0];b->cancel_write=bc[1];
#endif
    return {std::shared_ptr<Pipe>(new Pipe(std::move(a))),std::shared_ptr<Pipe>(new Pipe(std::move(b)))};
}
void Pipe::cancel()noexcept{
    if(!state_)return;
    {std::lock_guard<std::mutex> l(state_->gate);state_->cancelled=true;}
#ifdef _WIN32
    SetEvent(state_->cancel_event);
#else
    const uint8_t byte=1;(void)::write(state_->cancel_write,&byte,1); // own nonblocking cancel pipe; already-readable/full is cancellation too
#endif
}
bool Pipe::close(Clock::time_point end)noexcept{
    if(!state_)return true;auto& s=*state_;
    {std::lock_guard<std::mutex> l(s.gate);s.closing=true;}cancel();
    std::unique_lock<std::mutex> l(s.gate);if(!s.changed.wait_until(l,end,[&]{return s.active==0;})||s.retained)return false;
#ifdef _WIN32
    if(s.handle!=INVALID_HANDLE_VALUE){CloseHandle(s.handle);s.handle=INVALID_HANDLE_VALUE;}
#else
    for(int* p:{&s.input,&s.output})if(*p>=0){::close(*p);*p=-1;}
#endif
    return true;
}
Error Pipe::read(uint8_t* p,size_t n,size_t& actual,Clock::time_point end){return transfer(false,p,n,actual,end);}
Error Pipe::write(const uint8_t* p,size_t n,size_t& actual,Clock::time_point end){return transfer(true,const_cast<uint8_t*>(p),n,actual,end);}
bool Pipe::wait_pending(bool write,Clock::time_point end){auto& s=*state_;std::unique_lock<std::mutex> l(s.gate);return s.changed.wait_until(l,end,[&]{return (write?s.write_pending:s.read_pending)>0||s.closing;})&&(write?s.write_pending:s.read_pending)>0;}
Error Pipe::transfer(bool write,uint8_t* data,size_t n,size_t& actual,Clock::time_point end){
    actual=0;if(!data||!n||n>65536||!state_)return Error::Bound;auto& s=*state_;auto& flag=write?s.write_busy:s.read_busy;
    if(flag.test_and_set())return Error::Busy;Flag done(flag);
    {std::lock_guard<std::mutex> l(s.gate);if(s.closing||s.retained)return s.retained?Error::Retained:Error::Closed;if(s.cancelled)return Error::Cancelled;s.ever_io=true;++s.active;}
    struct Active {State& s;~Active(){std::lock_guard<std::mutex> l(s.gate);--s.active;s.changed.notify_all();}} active{s};
    if(Clock::now()>=end)return Error::Expired;
#ifdef _WIN32
    if(WaitForSingleObject(s.cancel_event,0)==WAIT_OBJECT_0)return Error::Cancelled;
    auto op=std::make_unique<State::Operation>(n);if(write)std::memcpy(op->buffer.data(),data,n);DWORD bytes=0;
    BOOL ok=write?WriteFile(s.handle,op->buffer.data(),static_cast<DWORD>(n),&bytes,&op->ov):ReadFile(s.handle,op->buffer.data(),static_cast<DWORD>(n),&bytes,&op->ov);
    if(!ok){const auto error=GetLastError();if(error!=ERROR_IO_PENDING)return error==ERROR_BROKEN_PIPE||error==ERROR_PIPE_NOT_CONNECTED?Error::Eof:Error::Io;
        {std::lock_guard<std::mutex> l(s.gate);++(write?s.write_pending:s.read_pending);s.changed.notify_all();}
        HANDLE waits[]={op->ov.hEvent,s.cancel_event};const DWORD w=WaitForMultipleObjects(2,waits,FALSE,static_cast<DWORD>(remaining(end)));
        if(w!=WAIT_OBJECT_0){const Error reason=w==WAIT_OBJECT_0+1?Error::Cancelled:w==WAIT_TIMEOUT?Error::Expired:Error::Io;
            const bool cancellation=CancelIoEx(s.handle,&op->ov)!=0;const auto ce=cancellation?0:GetLastError();
            if((!cancellation&&ce!=ERROR_NOT_FOUND)||WaitForSingleObject(op->ov.hEvent,1000)!=WAIT_OBJECT_0){std::lock_guard<std::mutex> l(s.gate);s.retained=true;(write?s.pending_write:s.pending_read)=op.release();return Error::Retained;}
            (void)GetOverlappedResult(s.handle,&op->ov,&bytes,FALSE);return reason;
        }
        if(!GetOverlappedResult(s.handle,&op->ov,&bytes,FALSE)){auto e=GetLastError();return e==ERROR_BROKEN_PIPE?Error::Eof:e==ERROR_OPERATION_ABORTED?Error::Cancelled:Error::Io;}
    }
    if(Clock::now()>=end)return Error::Expired;if(WaitForSingleObject(s.cancel_event,0)==WAIT_OBJECT_0)return Error::Cancelled;
    actual=bytes;if(!write&&bytes)std::memcpy(data,op->buffer.data(),bytes);return bytes?Error::None:Error::Eof;
#else
    for(;;){int timeout=remaining(end);if(!timeout)return Error::Expired;pollfd fds[]={{write?s.output:s.input,static_cast<short>(write?POLLOUT:POLLIN),0},{s.cancel_read,POLLIN,0}};
        int rc=::poll(fds,2,0);if(!rc){{std::lock_guard<std::mutex> l(s.gate);++(write?s.write_pending:s.read_pending);s.changed.notify_all();}rc=::poll(fds,2,timeout);}if(rc<0){if(errno==EINTR)continue;return Error::Io;}if(!rc)return Error::Expired;if(fds[1].revents)return Error::Cancelled;
        if(fds[0].revents&POLLNVAL)return Error::Io;
        ssize_t got;
        if(write){sigset_t block,old,pending;sigemptyset(&block);sigaddset(&block,SIGPIPE);if(pthread_sigmask(SIG_BLOCK,&block,&old))return Error::Io;
            if(sigpending(&pending)){pthread_sigmask(SIG_SETMASK,&old,nullptr);return Error::Io;}const bool before=sigismember(&pending,SIGPIPE)==1;
            got=::write(s.output,data,n);int saved=errno;if(got<0&&saved==EPIPE&&!before){timespec zero{};while(sigtimedwait(&block,nullptr,&zero)<0&&errno==EINTR){}}
            const int restored=pthread_sigmask(SIG_SETMASK,&old,nullptr);errno=saved;if(restored)return Error::Io;
        }else got=::read(s.input,data,n);
        if(got<0){if(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK)continue;return errno==EPIPE?Error::Eof:Error::Io;}if(!got)return Error::Eof;
        if(Clock::now()>=end)return Error::Expired;actual=static_cast<size_t>(got);return Error::None;
    }
#endif
}
Channel::Channel(std::shared_ptr<Pipe> p,NS_Envelope e,Direction d,Clock::time_point end,size_t chunk):pipe_(std::move(p)),identity_(e),outgoing_(d),deadline_(end),chunk_(chunk){if(!pipe_||!valid_envelope(e)||chunk==0||chunk>frame_limit)throw std::invalid_argument("channel input");}
Error Channel::fail(Error e)noexcept{sealed_=true;pipe_->cancel();return e;}
void Channel::cancel()noexcept{sealed_=true;pipe_->cancel();}
bool Channel::close(Clock::time_point end)noexcept{cancel();return pipe_->close(end);}
Error Channel::exact(bool write,uint8_t* data,size_t n){size_t at=0;while(at<n){if(sealed_)return Error::Cancelled;if(Clock::now()>=deadline_)return fail(Error::Expired);size_t done=0;auto r=write?pipe_->write(data+at,std::min(chunk_,n-at),done,deadline_):pipe_->read(data+at,std::min(chunk_,n-at),done,deadline_);if(r!=Error::None)return fail(r);if(!done||done>n-at)return fail(Error::Io);at+=done;}return Error::None;}
Error Channel::send(Frame f){
    if(tx_.test_and_set())return Error::Busy;Flag guard(tx_);if(sealed_)return Error::Closed;if(tx_seq_>frame_count_limit)return fail(Error::Bound);
    f.sequence=tx_seq_;f.nonce=identity_.run;f.envelope=identity_;f.direction=outgoing_;
    std::vector<uint8_t> bytes;try{bytes=encode(f);}catch(...){return fail(Error::Invalid);}if(tx_bytes_+bytes.size()>byte_limit)return fail(Error::Bound);
    auto e=exact(true,bytes.data(),bytes.size());if(e==Error::None){++tx_seq_;tx_bytes_+=bytes.size();}return e;
}
Error Channel::receive(Frame& out){
    if(rx_.test_and_set())return Error::Busy;Flag guard(rx_);if(sealed_)return Error::Closed;
    std::array<uint8_t,header_bytes> head{};auto e=exact(false,head.data(),head.size());if(e!=Error::None)return e;uint32_t n=0;e=header_size(head.data(),head.size(),n);if(e!=Error::None)return fail(e);
    if(rx_seq_>frame_count_limit||rx_bytes_+header_bytes+n>byte_limit)return fail(Error::Bound);std::vector<uint8_t> all(head.begin(),head.end());all.resize(header_bytes+n);e=exact(false,all.data()+header_bytes,n);if(e!=Error::None)return e;
    Frame f;e=decode(all.data(),all.size(),f);if(e!=Error::None)return fail(e);const auto incoming=outgoing_==Direction::Endpoint?Direction::Coordinator:Direction::Endpoint;
    if(f.direction!=incoming||!same_envelope(f.envelope,identity_))return fail(Error::Envelope);if(f.sequence!=rx_seq_)return fail(Error::Sequence);if(Clock::now()>=deadline_)return fail(Error::Expired);++rx_seq_;rx_bytes_+=all.size();out=f;return Error::None;
}
}
