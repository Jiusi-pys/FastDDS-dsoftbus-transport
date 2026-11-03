#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "parent_protocol.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>
#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <cerrno>
#include <dirent.h>
#include <cstdlib>
#endif
namespace stage1_control {
#ifndef _WIN32
namespace {
void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
struct Fd {int fd=-1;Fd()=default;explicit Fd(int f):fd(f){}~Fd(){if(fd>=0)::close(fd);}Fd(const Fd&)=delete;Fd& operator=(const Fd&)=delete;};
void child_limits(){
    // Parent is a new task process; children inherit these bounds. No unsafe
    // setrlimit or C++ runs between fork and fexecve in the child stub.
    rlimit as{};need(getrlimit(RLIMIT_AS,&as)==0&&as.rlim_cur==UINT64_C(2684354560)&&as.rlim_max==UINT64_C(2684354560),"outer qualified 2.5GiB AS prerequisite");
    for(auto row:{std::pair<int,rlim_t>{RLIMIT_CPU,8},{RLIMIT_FSIZE,65536},{RLIMIT_NOFILE,256},{RLIMIT_CORE,0}}){rlimit r{row.second,row.second};need(setrlimit(row.first,&r)==0,"owned parent resource bound");}
    DIR* d=opendir("/proc/self/fd");need(d,"own descriptor inventory");bool valid=true;unsigned count=0;errno=0;
    while(auto* e=readdir(d)){if(e->d_name[0]=='.')continue;char* end=nullptr;errno=0;const auto n=std::strtoul(e->d_name,&end,10);if(errno||!end||*end||n>=256||++count>256){valid=false;break;}}
    if(errno)valid=false;if(closedir(d))valid=false;need(valid,"inherited descriptors exceed closed whitelist");
}
int directory(const std::string& p){need(p.size()>1&&p.front()=='/'&&p.back()!='/',"absolute owned output directory");Fd root(open("/",O_PATH|O_DIRECTORY|O_CLOEXEC));need(root.fd>=0,"output root");size_t at=1;unsigned count=0;while(true){const auto slash=p.find('/',at);auto part=p.substr(at,slash==std::string::npos?slash:slash-at);need(!part.empty()&&part!="."&&part!=".."&&++count<=32,"output path component");Fd next(openat(root.fd,part.c_str(),O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));need(next.fd>=0,"output non-symlink directory");::close(root.fd);root.fd=next.fd;next.fd=-1;if(slash==std::string::npos){const int result=root.fd;root.fd=-1;return result;}at=slash+1;}}
void exclusive_waiter(){struct sigaction a{};need(sigaction(SIGCHLD,nullptr,&a)==0&&a.sa_handler==SIG_DFL&&!(a.sa_flags&SA_NOCLDWAIT),"exclusive SIGCHLD/default waiter required");}
struct Child {
    pid_t pid=-1;bool done=false,unknown=false;int status=0;long rss=0;Clock::time_point* cleanup_deadline=nullptr;std::shared_ptr<Pipe> control,gate;std::array<Fd,8> high;std::array<std::string,11> words;std::array<char*,12> argv{};std::array<std::string,4> environment;std::array<char*,5> env{};
    bool collect(){
        if(done)return true;if(unknown||pid<=0)return false;exclusive_waiter();rusage u{};const auto r=wait4(pid,&status,WNOHANG,&u);
        if(r==pid){done=true;rss=u.ru_maxrss;return true;}if(r==0)return false;unknown=true;return false;
    }
    bool stop(Clock::time_point until){
        if(collect())return true;if(unknown||pid<=0)return false;
        // WNOHANG==0 and an exclusive waiter preserve this unreaped own PID.
        if(kill(pid,SIGKILL)&&errno!=ESRCH){unknown=true;return false;}
        while(Clock::now()<until){if(collect())return true;if(unknown)return false;(void)poll(nullptr,0,1);}unknown=true;return false;
    }
    ~Child(){if(pid>0&&!done&&!unknown){try{auto until=Clock::now()+std::chrono::seconds(2);if(cleanup_deadline){if(*cleanup_deadline==Clock::time_point::max())*cleanup_deadline=until;until=*cleanup_deadline;}if(!stop(until))std::fputs("OWNED_CHILD_REAP_UNPROVEN\n",stderr);}catch(...){std::fputs("OWNED_CHILD_REAP_EXCEPTION\n",stderr);}}}
};
void write_all(Pipe& p,const uint8_t* b,size_t n,Clock::time_point end){size_t at=0;while(at<n){size_t sent=0;need(p.write(b+at,n-at,sent,end)==Error::None&&sent,"owned bootstrap write");at+=sent;}}
void spawn(Child& c,int executable,int module,int manifest,int log,int nullfd,const std::string& frontend,const std::string& runtime,uint64_t run,unsigned side,uint64_t birth){
    auto ctl=Pipe::local_pair(),gate=Pipe::local_pair();Pipe::Endpoint ce,ge;need(ctl.b->take_fresh_endpoint(ce)==Error::None&&gate.b->take_fresh_endpoint(ge)==Error::None,"owned child endpoints");c.control=ctl.a;c.gate=gate.a;
    Fd child_in(static_cast<int>(ce.input)),child_out(static_cast<int>(ce.output)),boot_in(static_cast<int>(ge.input)),unused_gate_out(static_cast<int>(ge.output));
    const int sources[8]={child_in.fd,child_out.fd,manifest,module,boot_in.fd,executable,log,nullfd};
    for(unsigned i=0;i<8;++i){c.high[i].fd=fcntl(sources[i],F_DUPFD_CLOEXEC,64);need(c.high[i].fd>=64,"relocated child descriptors");}
    c.words={frontend,"--native-os-child","10","11","12","13",std::to_string(getpid()),std::to_string(birth),std::to_string(side),"14",std::to_string(run)};
    for(unsigned i=0;i<c.words.size();++i)c.argv[i]=const_cast<char*>(c.words[i].c_str());
    c.environment={"PATH=/system/bin","LANG=C","TMPDIR=/data/local/tmp","LD_LIBRARY_PATH="+runtime};for(unsigned i=0;i<4;++i)c.env[i]=const_cast<char*>(c.environment[i].c_str());
    int high[8]{};for(unsigned i=0;i<8;++i)high[i]=c.high[i].fd;char** args=c.argv.data();char** env=c.env.data();
    need(native_sole_main(),"fresh sole-main immediately before fork");exclusive_waiter();const pid_t p=fork();
    if(p==0){
        // Async-signal-safe child stub only; all arrays/strings prebuilt.
        for(int i=0;i<6;++i)if(dup2(high[static_cast<size_t>(i)],10+i)<0)_exit(126);
        if(dup2(high[6],1)<0||dup2(high[6],2)<0||dup2(high[7],0)<0)_exit(126);
        for(int fd=3;fd<256;++fd)if(fd<10||fd>15)(void)close(fd);
        if(fcntl(15,F_SETFD,FD_CLOEXEC)<0)_exit(126);
        fexecve(15,args,env);_exit(127);
    }
    need(p>0,"owned fork");c.pid=p;for(auto& f:c.high){const int n=f.fd;f.fd=-1;need(::close(n)==0,"owned relocated descriptor close uncertain");}
}
}
#endif
int native_parent(int argc,char** argv){
#ifdef _WIN32
    (void)argc;(void)argv;return 78;
#else
    try{
        need(argc==11&&!std::strcmp(argv[1],"--os-parent"),"parent: frontend SHA module SHA board nonce case outdir runtime_dir");need(native_sole_main(),"parent fresh solemain");exclusive_waiter();child_limits();
        const std::string front=argv[2],frontsha=argv[3],modulepath=argv[4],modulesha=argv[5],board=argv[6],nonce=argv[7],mode=argv[8],outdir=argv[9],runtime=argv[10];
        need(mode=="normal"||mode=="bad-bootstrap"||mode=="eof"||mode=="timeout","fixed nonbusiness parent cases");need(nonce.size()==16&&nonce.find_first_not_of("0123456789abcdef")==std::string::npos,"parent nonce");
        const auto start=Clock::now(),end=start+std::chrono::seconds(5);Fd rawfront(native_open_input(front)),rawmodule(native_open_input(modulepath));Fd exec(native_snapshot(rawfront.fd,16*1048576,frontsha,end)),module(native_snapshot(rawmodule.fd,16*1048576,modulesha,end));
        Fd manifest(memfd_create("stage1-owned-manifest",MFD_ALLOW_SEALING|MFD_CLOEXEC)),nullfd(open("/dev/null",O_RDONLY|O_CLOEXEC));need(manifest.fd>=0&&nullfd.fd>=0,"owned manifest/null descriptor");
        Fd output(directory(outdir)),runtime_directory(directory(runtime));(void)runtime_directory;
        Fd logs[2];for(unsigned i=0;i<2;++i){const auto name="child"+std::to_string(i)+".log";logs[i].fd=openat(output.fd,name.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);need(logs[i].fd>=0,"fresh owned child log");}
        auto cleanup_budget=Clock::time_point::max();Child children[2];for(auto& c:children)c.cleanup_deadline=&cleanup_budget;const auto birth=native_parent_start(static_cast<uint32_t>(getpid()));uint64_t run=0;for(char c:nonce)run=(run<<4)|static_cast<unsigned>(c<='9'?c-'0':c-'a'+10);need(run!=0,"nonzero run");
        for(unsigned side=0;side<2;++side)spawn(children[side],exec.fd,module.fd,manifest.fd,logs[side].fd,nullfd.fd,front,runtime,run,side,birth);
        Deployment d;d.fields={"S1HANDOFF1","NATIVE_OS_FIXTURE_ONLY",nonce,std::to_string(getpid()),"/proc/self/exe",frontsha,"/proc/self/fd/13",modulesha,"OS_ONLY_NO_DATA","151",NS_PACKAGE,NS_PACKAGE ".os."+nonce,"1000",std::to_string(children[0].pid),board,"1","OS_FIXTURE_A","NO_PEER_NETWORK","1","OS_A",std::to_string(children[1].pid),board,"2","OS_FIXTURE_B","NO_PEER_NETWORK","2","OS_B"};
        const auto text=d.encode_os();need(write(manifest.fd,text.data(),text.size())==static_cast<ssize_t>(text.size()),"bounded manifest write");constexpr int seals=F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL;need(fcntl(manifest.fd,F_ADD_SEALS,seals)==0,"parent manifest seal add");const int observed=fcntl(manifest.fd,F_GET_SEALS);need(observed>=0&&(observed&seals)==seals,"parent manifest seal readback");const auto sha=native_digest(text);
        if(mode=="eof")for(auto& c:children){c.control->cancel();need(c.control->close(end),"parent independent control EOF");}
        if(mode!="timeout")for(unsigned i=0;i<2;++i){ParentBoot b{static_cast<uint32_t>(getpid()),static_cast<uint32_t>(children[i].pid),i,1000,run,birth,sha};if(mode=="bad-bootstrap")b.run=run==UINT64_MAX?run-1:run+1;const auto bytes=encode_parent_boot(b);write_all(*children[i].gate,bytes.data(),bytes.size(),end);}
        const auto waitend=mode=="timeout"?Clock::now()+std::chrono::milliseconds(100):end;bool timed=false;
        while(!children[0].done||!children[1].done){for(auto& c:children){(void)c.collect();need(!c.unknown,"unknown child/reaper state");}if(children[0].done&&children[1].done)break;if(Clock::now()>=waitend){timed=true;break;}(void)poll(nullptr,0,1);}
        cleanup_budget=Clock::now()+std::chrono::seconds(2);bool clean=true;for(auto& c:children){if(!c.done)clean=c.stop(cleanup_budget)&&clean;if(!c.done||c.rss<=0||c.rss>512*1024)clean=false;std::printf("OWN_CHILD pid=%d reaped=%d status_valid=%d raw_status=%d exited=%d exit=%d signal=%d peakRSS_KiB=%ld RSS_valid=%d no_identity=1 no_SDK=1\n",int(c.pid),int(c.done),int(c.done),c.done?c.status:-1,int(c.done&&WIFEXITED(c.status)),c.done&&WIFEXITED(c.status)?WEXITSTATUS(c.status):-1,c.done&&WIFSIGNALED(c.status)?WTERMSIG(c.status):0,c.rss,int(c.done&&c.rss>0));}
        need(clean,"owned reap/RSS acceptance unproven");if(timed){need(mode=="timeout","unexpected parent deadline");return 124;}
        need(mode!="timeout","timeout case exited before owned watchdog");for(auto& c:children)need(WIFEXITED(c.status)&&WEXITSTATUS(c.status)==(mode=="normal"?0:78),"expected owned child outcome");std::puts("PARENT_OS_FIXTURE_DONE logical_roles_only=1 no_crossboard_or_DDS=1");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"PARENT_OS_FAIL %s no_identity=1 no_SDK=1\n",e.what());return 125;}
#endif
}
}
