#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "native_ingress.hpp"
#include "parent_protocol.hpp"
#include <cstdio>
#include <cstring>
#include <climits>
#include <stdexcept>
#include <algorithm>
#ifndef _WIN32
#include "sha256_pure.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <cerrno>
#include <poll.h>
#endif
namespace stage1_control {
namespace {
void need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
uint64_t decimal(const char* p,uint64_t maximum){need(p&&*p&&(!p[1]||*p!='0'),"native canonical decimal");uint64_t n=0;for(;*p;++p){need(*p>='0'&&*p<='9',"native decimal bytes");const auto x=static_cast<uint64_t>(*p-'0');need(x<=maximum&&n<=(maximum-x)/10,"native decimal overflow");n=n*10+x;}return n;}
}
int native_fd_number(const char* p){auto n=decimal(p,INT_MAX);need(n>=3,"native FD excludes stdio");return static_cast<int>(n);}
size_t native_file_size(int64_t bytes,size_t cap,bool regular){need(regular&&bytes>0&&static_cast<uint64_t>(bytes)<=cap,"native exact stat allocation bound");return static_cast<size_t>(bytes);}
NS_RunRequest native_bound_request(const Deployment& d,unsigned side,uint32_t pid,uint32_t parent,const std::string& digest,const std::string& image,Clock::time_point end){
    d.validate_native();need(side<2&&parent==d.number(DParent,UINT32_MAX),"native actual parent");need(image==d.fields[DImageSha],"native actual executable hash");need(Clock::now()<end,"native expired");
    auto q=d.request_native(side,digest,end);need(q.local.pid==pid,"native actual child PID");return q;
}
#ifndef _WIN32
namespace {
struct Fd {
    int n=-1;Fd()=default;explicit Fd(int f):n(f){}~Fd(){if(n>=0)::close(n);}Fd(const Fd&)=delete;Fd& operator=(const Fd&)=delete;
    int release(){int f=n;n=-1;return f;}
};
struct PrepControl {
    int input,output;
    void check()const{
        pollfd f[2]={{input,POLLIN,0},{output,0,0}};need(poll(f,2,0)>=0,"native control poll error");
        need(!((f[0].revents|f[1].revents)&(POLLERR|POLLHUP|POLLNVAL)),"native control EOF/error cancellation before READY");
        // No control frame is legal before this preparation-only route has
        // published anything. Refuse without consuming/validating an Abort.
        need(!(f[0].revents&POLLIN),"native unexpected control bytes before READY");
    }
};
void live(Clock::time_point end,const PrepControl* control=nullptr){need(Clock::now()<end,"native preparation deadline");if(control)control->check();}
size_t regular(int fd,size_t cap){struct stat s{};need(fstat(fd,&s)==0,"native source stat");return native_file_size(static_cast<int64_t>(s.st_size),cap,S_ISREG(s.st_mode));}
std::vector<uint8_t> read_fd(int fd,size_t cap,Clock::time_point end,const PrepControl* control=nullptr){
    live(end,control);const size_t bytes=regular(fd,cap);std::vector<uint8_t> b(bytes);size_t at=0;
    while(at<b.size()){live(end,control);const auto n=pread(fd,b.data()+at,std::min<size_t>(4096,b.size()-at),static_cast<off_t>(at));need(n>0,"native bounded pread");at+=static_cast<size_t>(n);}
    live(end,control);return b;
}
std::string hash_fd(int fd,size_t cap,Clock::time_point end,const PrepControl* control=nullptr){auto b=read_fd(fd,cap,end,control);return pure_hash::digest(b.data(),b.size());}
bool same_file(int a,int b){struct stat x{},y{};return fstat(a,&x)==0&&fstat(b,&y)==0&&x.st_dev==y.st_dev&&x.st_ino==y.st_ino;}
int open_nofollow(const std::string& path){
    need(path.size()>1&&path.front()=='/'&&path.back()!='/',"native absolute input path");Fd root(open("/",O_PATH|O_DIRECTORY|O_CLOEXEC));need(root.n>=0,"native root directory");size_t at=1;unsigned count=0;
    while(true){const auto slash=path.find('/',at);const auto part=path.substr(at,slash==std::string::npos?slash:slash-at);need(!part.empty()&&part!="."&&part!=".."&&++count<=32,"native path component");
        // Metadata-only O_PATH avoids opening a FIFO/device for read at all.
        // All bytes come from the already-validated private regular-file FD.
        Fd next(openat(root.n,part.c_str(),O_PATH|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW|(slash==std::string::npos?0:O_DIRECTORY)));need(next.n>=0,"native nofollow metadata path");if(slash==std::string::npos){regular(next.n,16*1048576);return next.release();}::close(root.n);root.n=next.release();at=slash+1;}
}
uint64_t parent_start(uint32_t parent){
    const auto path="/proc/"+std::to_string(parent)+"/stat";Fd f(open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));need(f.n>=0,"native parent stat open");char b[4097]{};const auto n=read(f.n,b,sizeof(b)-1);need(n>0&&n<static_cast<ssize_t>(sizeof(b)-1),"native parent stat bounded");
    std::string text(b,static_cast<size_t>(n));const auto close=text.rfind(") ");need(close!=std::string::npos,"native parent stat comm");size_t at=close+2;
    for(unsigned field=3;field<=22;++field){const auto end=text.find(' ',at);need(end!=std::string::npos,"native parent stat fields");if(field==22)return decimal(text.substr(at,end-at).c_str(),UINT64_MAX);at=end+1;}throw std::runtime_error("native parent start missing");
}
bool sole_main(){
    if(static_cast<uint64_t>(getpid())!=static_cast<uint64_t>(syscall(SYS_gettid)))return false;DIR* d=opendir("/proc/self/task");if(!d)return false;unsigned count=0;bool ok=true;errno=0;
    while(auto* e=readdir(d)){if(e->d_name[0]=='.')continue;if(++count>1){ok=false;break;}}if(errno)ok=false;if(closedir(d))ok=false;return ok&&count==1;
}
void pipe_fd(int fd,bool output){struct stat s{};const int flags=fcntl(fd,F_GETFL);need(fstat(fd,&s)==0&&S_ISFIFO(s.st_mode)&&flags>=0&&(flags&O_ACCMODE)==(output?O_WRONLY:O_RDONLY)&&(flags&O_NONBLOCK),"native private pipe direction/nonblock");}
int snapshot(int source,size_t cap,const std::string& expected,Clock::time_point end,const PrepControl* control){
    const auto b=read_fd(source,cap,end,control);need(pure_hash::digest(b.data(),b.size())==expected,"native consumed input SHA");live(end,control);Fd f(memfd_create("fastdds-stage1-prepared",MFD_CLOEXEC|MFD_ALLOW_SEALING));need(f.n>=0,"native memfd unsupported/failure");
    size_t at=0;while(at<b.size()){live(end,control);auto n=write(f.n,b.data()+at,std::min<size_t>(4096,b.size()-at));need(n>0,"native snapshot write");at+=static_cast<size_t>(n);}
    constexpr int seals=F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL;need(fcntl(f.n,F_ADD_SEALS,seals)==0,"native seals unsupported/failure");const int observed=fcntl(f.n,F_GET_SEALS);need(observed>=0&&(observed&seals)==seals,"native all seals readback");
    need(hash_fd(f.n,cap,end,control)==expected,"native sealed descriptor SHA");return f.release();
}
struct Prepared {
    Fd manifest,module;std::shared_ptr<Pipe> pipe;std::shared_ptr<Channel> channel;NS_RunRequest request{};
    // There are no business owners/callbacks. No SDK quiet or identity effect
    // follows from closing this local preparation-only channel.
};
}
#endif
static int prepare_impl(int argc,char** argv,bool os,const ParentBoot* boot=nullptr){
#ifdef _WIN32
    (void)argc;(void)argv;(void)os;(void)boot;std::puts("NATIVE_FD_UNSUPPORTED_HOST no_identity=1 no_business=1");return 78;
#else
    try{
        need(argc==10,"native prepare argv: in out manifest module parent birth side manifestSHA");need(sole_main(),"native fresh sole-main preparation");
        // The fresh caller exclusively owns these numbers through return:
        // no concurrent close/dup/rebind/use. Preparation creates no worker.
        int borrowed[4]={native_fd_number(argv[2]),native_fd_number(argv[3]),native_fd_number(argv[4]),native_fd_number(argv[5])};
        for(unsigned i=0;i<4;++i)for(unsigned j=0;j<i;++j)need(borrowed[i]!=borrowed[j],"native duplicate FD number");
        for(int fd:borrowed)need(fcntl(fd,F_GETFD)>=0,"native inherited FD valid before any duplication");
        Fd in(fcntl(borrowed[0],F_DUPFD_CLOEXEC,3)),out(fcntl(borrowed[1],F_DUPFD_CLOEXEC,3)),manifest(fcntl(borrowed[2],F_DUPFD_CLOEXEC,3)),module(fcntl(borrowed[3],F_DUPFD_CLOEXEC,3));need(in.n>=0&&out.n>=0&&manifest.n>=0&&module.n>=0,"native private descriptor duplicates");
        pipe_fd(in.n,false);pipe_fd(out.n,true);need(!same_file(in.n,out.n),"native separate pipe objects");const PrepControl control{in.n,out.n};control.check();
        const auto parent=decimal(argv[6],UINT32_MAX),birth=decimal(argv[7],UINT64_MAX),side=decimal(argv[8],1);need(parent&&birth&&getppid()==static_cast<pid_t>(parent)&&parent_start(static_cast<uint32_t>(parent))==birth,"native sampled direct parent");
        const auto started=Clock::now(),limit=started+std::chrono::seconds(5);const auto bytes=read_fd(manifest.n,8192,limit,&control);const auto digest=pure_hash::digest(bytes.data(),bytes.size());need(digest==argv[9],"native manifest exact SHA");
        auto d=os?Deployment::parse_os(std::string(bytes.begin(),bytes.end())):Deployment::parse_native(std::string(bytes.begin(),bytes.end()));const auto end=started+std::chrono::milliseconds(d.number(DBudget,5000));live(end);
        if(os)need(d.fields[DImage]=="/proc/self/exe"&&d.fields[DModule]=="/proc/self/fd/13","OS fixture kernel image/opaque module carrier");
        else {Fd named_module(open_nofollow(d.fields[DModule]));need(same_file(named_module.n,module.n),"native module path/descriptor association");}
        Fd own(open("/proc/self/exe",O_RDONLY|O_CLOEXEC));need(own.n>=0,"native kernel own executable");if(!os){Fd named_image(open_nofollow(d.fields[DImage]));need(same_file(own.n,named_image.n),"native own image inode/path");}
        auto prepared=std::make_unique<Prepared>();const auto actual_image=hash_fd(own.n,16*1048576,end,&control);
        if(os){need(boot&&d.number(DParent,UINT32_MAX)==parent&&d.fields[DImageSha]==actual_image&&d.number(DBudget,5000)==boot->budget_ms,"OS fixture actual parent/image/budget");prepared->request=d.request_os(static_cast<unsigned>(side),digest,end);need(prepared->request.local.pid==static_cast<uint32_t>(getpid())&&prepared->request.local.run==boot->run,"OS fixture actual child/run");}
        else prepared->request=native_bound_request(d,static_cast<unsigned>(side),static_cast<uint32_t>(getpid()),static_cast<uint32_t>(parent),digest,actual_image,end);
        prepared->manifest.n=snapshot(manifest.n,8192,digest,end,&control);prepared->module.n=snapshot(module.n,16*1048576,d.fields[DModuleSha],end,&control);
        const auto elf=read_fd(prepared->module.n,16*1048576,end,&control);need(elf.size()>=64&&elf[0]==127&&elf[1]=='E'&&elf[2]=='L'&&elf[3]=='F'&&elf[4]==2&&elf[5]==1&&elf[16]==3&&elf[17]==0&&elf[18]==183&&elf[19]==0,"native sealed AArch64 ET_DYN bytes only");
        need(sole_main()&&getppid()==static_cast<pid_t>(parent)&&parent_start(static_cast<uint32_t>(parent))==birth,"native parent/entry recheck before commit");
        live(end,&control);Pipe::Endpoint ep{static_cast<uint64_t>(in.n),static_cast<uint64_t>(out.n)};Error error;auto pipe=Pipe::adopt_owned(ep,static_cast<uint32_t>(parent),error);need(pipe&&error==Error::None,"native validated private FD adoption");in.release();out.release();prepared->pipe=std::move(pipe);
        // The two same-target OS logical roles are not business wire peers.
        if(!os)prepared->channel=std::make_shared<Channel>(prepared->pipe,prepared->request.local,Direction::Endpoint,end);
        // All validation/owner allocation succeeded. Transfer originals once;
        // never retry close on an unknown EINTR outcome or signal another PID.
        live(end,&control);bool close_ok=true;for(int& fd:borrowed){if(::close(fd))close_ok=false;fd=-1;}need(close_ok,"native transferred original close uncertain");
        std::printf("NATIVE_PREPARED_ONLY side=%u pid=%u parent=%llu manifest_sha=%s sealed_module_sha=%s borrowed_transferred_once=1 parent_binding=SAMPLED_NOT_PIDFD no_getter=1 no_module_load=1 no_identity=1 no_SDK=1\n",static_cast<unsigned>(side),prepared->request.local.pid,static_cast<unsigned long long>(parent),digest.c_str(),d.fields[DModuleSha].c_str());
        if(os){prepared->pipe->cancel();need(prepared->pipe->close(end),"OS local pipe close uncertain");std::puts("NATIVE_OS_FD_PREPARATION_OK no_business_Channel=1 board_binding=EXTERNAL_ATTESTATION logical_roles_only=1");return 0;}
        prepared->channel->cancel();need(prepared->channel->close(end),"native local channel close uncertain");
        std::puts("NATIVE_BUSINESS_LOADER_UNSUPPORTED_PRECONSTRUCTOR_BINDING dependency_constructor_binding=UNKNOWN V2_business_route=DISABLED not_READY=1");return 78;
    }catch(const std::exception& e){std::fprintf(stderr,"NATIVE_PREPARE_REFUSED %s no_getter=1 no_module_load=1 no_identity=1\n",e.what());return 78;}
#endif
}
int native_fd_prepare(int argc,char** argv){return prepare_impl(argc,argv,false);}
#ifndef _WIN32
int native_snapshot(int fd,size_t cap,const std::string& sha,Clock::time_point end){return snapshot(fd,cap,sha,end,nullptr);}
int native_open_input(const std::string& path){Fd meta(open_nofollow(path));const auto carrier="/proc/self/fd/"+std::to_string(meta.n);Fd data(open(carrier.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC));need(data.n>=0&&same_file(meta.n,data.n),"native metadata/data descriptor binding");return data.release();}
uint64_t native_parent_start(uint32_t pid){return parent_start(pid);}
bool native_sole_main(){return sole_main();}
std::string native_digest(const std::string& text){return pure_hash::digest(text.data(),text.size());}
#endif
int native_os_child(int argc,char** argv){
#ifdef _WIN32
    (void)argc;(void)argv;return 78;
#else
    try{
        need(argc==11&&sole_main(),"OS child argc/solemain");const int gate=native_fd_number(argv[9]);const std::array<int,4> data={native_fd_number(argv[2]),native_fd_number(argv[3]),native_fd_number(argv[4]),native_fd_number(argv[5])};validate_os_fd_set(gate,data);
        need(fcntl(gate,F_GETFD)>=0,"OS gate valid before any duplication");for(int fd:data)need(fcntl(fd,F_GETFD)>=0,"OS data valid before gate duplication");
        struct stat st{};const int flags=fcntl(gate,F_GETFL);need(fstat(gate,&st)==0&&S_ISFIFO(st.st_mode)&&flags>=0&&(flags&O_ACCMODE)==O_RDONLY&&(flags&O_NONBLOCK),"OS bootstrap readonly nonblocking pipe");for(int fd:data){struct stat x{};need(fstat(fd,&x)==0&&!(st.st_dev==x.st_dev&&st.st_ino==x.st_ino),"OS gate kernel-object alias/unknown refused");}
        Fd private_gate(fcntl(gate,F_DUPFD_CLOEXEC,3));need(private_gate.n>=0,"OS private gate duplicate");const auto run=decimal(argv[10],UINT64_MAX);const auto until=Clock::now()+std::chrono::seconds(5);std::array<uint8_t,parent_boot_bytes> bytes{};size_t at=0;
        while(at<bytes.size()){live(until);pollfd p{private_gate.n,POLLIN,0};const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(until-Clock::now()).count();need(poll(&p,1,static_cast<int>(std::max<int64_t>(1,ms)))>0,"OS bootstrap timeout");const auto n=read(private_gate.n,bytes.data()+at,bytes.size()-at);need(n>0,"OS bootstrap EOF/error");at+=static_cast<size_t>(n);}
        const auto b=decode_parent_boot(bytes.data(),bytes.size(),static_cast<uint32_t>(getpid()),static_cast<uint32_t>(getppid()),run);need(b.side==decimal(argv[8],1)&&b.parent==decimal(argv[6],UINT32_MAX)&&b.birth==decimal(argv[7],UINT64_MAX),"OS bootstrap exact argv binding");need(close(gate)==0,"OS bootstrap close");
        char* args[10];for(unsigned i=0;i<9;++i)args[i]=argv[i];args[9]=const_cast<char*>(b.manifest_sha.c_str());return prepare_impl(10,args,true,&b);
    }catch(const std::exception& e){std::fprintf(stderr,"OS_CHILD_REFUSED %s no_identity=1 no_business=1\n",e.what());return 78;}
#endif
}
}
