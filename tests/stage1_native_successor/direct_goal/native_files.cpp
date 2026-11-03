#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "native_files.hpp"
#include "../control/sha256_pure.hpp"
#include "query_wait.hpp"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <poll.h>
#include <dirent.h>
#include <dlfcn.h>
#include <climits>
#include <cerrno>
#include <stdexcept>
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <ctime>

namespace stage1_goal {
namespace pure_hash {std::string digest(const void* p,std::size_t n){return stage1_control::pure_hash::digest(p,n);}}
namespace {
void need(bool b,const char* code){if(!b)throw std::runtime_error(code);}
struct Fd {int n=-1;explicit Fd(int v=-1):n(v){}~Fd(){if(n>=0)::close(n);}int release(){auto v=n;n=-1;return v;}Fd(const Fd&)=delete;};
bool name(const std::string& n){return !n.empty()&&n.size()<=64&&n.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")==std::string::npos&&n!="."&&n!="..";}
int directory(const std::string& p){
    need(p.size()>1&&p[0]=='/'&&p.back()!='/',"ROOT_PATH");Fd d(open("/",O_PATH|O_DIRECTORY|O_CLOEXEC));need(d.n>=0,"ROOT_OPEN");size_t at=1;unsigned count=0;
    for(;;){auto slash=p.find('/',at);auto part=p.substr(at,slash==std::string::npos?slash:slash-at);need(name(part)&&++count<=32,"ROOT_COMPONENT");int next=openat(d.n,part.c_str(),O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);need(next>=0,"ROOT_NOFOLLOW");close(d.n);d.n=next;if(slash==std::string::npos)return d.release();at=slash+1;}
}
bool stable(const struct stat& a,const struct stat& b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec;}
std::vector<uint8_t> bytes_fd(int fd,size_t cap){struct stat a{},b{};need(fstat(fd,&a)==0&&S_ISREG(a.st_mode)&&a.st_size>0&&static_cast<uint64_t>(a.st_size)<=cap,"REGULAR_SIZE");std::vector<uint8_t> v(static_cast<size_t>(a.st_size));size_t at=0;while(at<v.size()){auto n=pread(fd,v.data()+at,std::min<size_t>(4096,v.size()-at),static_cast<off_t>(at));if(n<0&&errno==EINTR)continue;need(n>0,"EXACT_READ");at+=static_cast<size_t>(n);}need(fstat(fd,&b)==0&&stable(a,b),"CHANGED_INPUT");return v;}
std::pair<std::string,std::string> parts(const std::string& p){auto at=p.rfind('/');need(at!=std::string::npos&&at>0&&name(p.substr(at+1)),"FILE_PATH");return {p.substr(0,at),p.substr(at+1)};}
uint64_t decimal(const std::string& s){need(!s.empty(),"PROC_NUMBER");uint64_t n=0;for(char c:s){need(c>='0'&&c<='9'&&n<=(UINT64_MAX-unsigned(c-'0'))/10,"PROC_NUMBER");n=n*10+unsigned(c-'0');}return n;}
uint64_t birth(){Fd f(open("/proc/self/stat",O_RDONLY|O_CLOEXEC));need(f.n>=0,"SELF_STAT");char b[4097]{};auto n=read(f.n,b,sizeof(b)-1);need(n>0&&n<static_cast<ssize_t>(sizeof(b)-1),"SELF_STAT_BOUND");std::string s(b,static_cast<size_t>(n));auto at=s.rfind(") ");need(at!=std::string::npos,"SELF_STAT_COMM");at+=2;for(unsigned field=3;field<=22;++field){auto end=s.find(' ',at);need(end!=std::string::npos,"SELF_STAT_FIELD");if(field==22)return decimal(s.substr(at,end-at));at=end+1;}throw std::runtime_error("SELF_BIRTH");}
bool query_exclusive(){struct sigaction a{};return single_main()&&sigaction(SIGCHLD,nullptr,&a)==0&&a.sa_handler==SIG_DFL&&!(a.sa_flags&SA_NOCLDWAIT);}
}
uint64_t now_ns(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());}
bool single_main(){if(getpid()!=syscall(SYS_gettid))return false;DIR* d=opendir("/proc/self/task");if(!d)return false;unsigned n=0;errno=0;while(auto* e=readdir(d)){if(e->d_name[0]!='.')++n;}bool ok=!errno&&n==1;if(closedir(d))ok=false;return ok;}
Point native_point(){const auto wall=std::time(nullptr);need(wall>0,"SELF_WALL");return {static_cast<uint64_t>(getpid()),static_cast<uint64_t>(syscall(SYS_gettid)),static_cast<uint64_t>(getppid()),birth(),now_ns(),single_main()?1u:2u,getuid(),geteuid(),getgid(),getegid(),static_cast<uint64_t>(wall)};}
std::string image_path(){char b[PATH_MAX]{};auto n=readlink("/proc/self/exe",b,sizeof(b)-1);need(n>0&&n<static_cast<ssize_t>(sizeof(b)-1),"SELF_IMAGE");return std::string(b,static_cast<size_t>(n));}
std::string self_image_hash(){Fd f(open("/proc/self/exe",O_RDONLY|O_CLOEXEC));need(f.n>=0,"SELF_IMAGE_OPEN");auto v=bytes_fd(f.n,16*1048576);return pure_hash::digest(v.data(),v.size());}
std::string file_hash(const std::string& p,size_t cap){auto x=parts(p);Fd d(directory(x.first));Fd f(openat(d.n,x.second.c_str(),O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC));need(f.n>=0,"ARTIFACT_OPEN");struct stat s{};need(fstat(f.n,&s)==0&&s.st_nlink==1,"ARTIFACT_LINKS");auto v=bytes_fd(f.n,cap);return pure_hash::digest(v.data(),v.size());}
std::string text(const std::vector<uint8_t>& b){return std::string(b.begin(),b.end());}
struct Files::State {std::string root;Fd dir;std::map<std::string,std::pair<uint64_t,uint64_t>> published;explicit State(std::string p):root(std::move(p)),dir(directory(root)){};};
Files::Files(std::string p):s_(new State(std::move(p))){}
Files::~Files()=default;
const std::string& Files::root()const{return s_->root;}
void Files::publish(const std::string& n,const std::vector<uint8_t>& v){
    need(name(n)&&!v.empty()&&v.size()<=65536&&(s_->published.count(n)||s_->published.size()<32),"OUTPUT_BOUND");const auto tmp=n+".writing";struct stat existing{};auto old=s_->published.find(n);
    if(fstatat(s_->dir.n,n.c_str(),&existing,AT_SYMLINK_NOFOLLOW)==0){need(old!=s_->published.end()&&S_ISREG(existing.st_mode)&&existing.st_nlink==1&&old->second==std::make_pair(uint64_t(existing.st_dev),uint64_t(existing.st_ino)),"OUTPUT_UNOWNED");}else need(errno==ENOENT,"OUTPUT_STAT");
    Fd f(openat(s_->dir.n,tmp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));need(f.n>=0,"OUTPUT_TEMP");size_t at=0;while(at<v.size()){auto z=write(f.n,v.data()+at,v.size()-at);if(z<0&&errno==EINTR)continue;need(z>0,"OUTPUT_WRITE");at+=static_cast<size_t>(z);}need(fsync(f.n)==0,"OUTPUT_SYNC");struct stat made{};need(fstat(f.n,&made)==0,"OUTPUT_ID");need(renameat(s_->dir.n,tmp.c_str(),s_->dir.n,n.c_str())==0,"OUTPUT_PUBLISH");s_->published[n]={uint64_t(made.st_dev),uint64_t(made.st_ino)};
}
std::vector<uint8_t> Files::read(const std::string& n,size_t cap)const{need(name(n),"INPUT_NAME");Fd f(openat(s_->dir.n,n.c_str(),O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC));need(f.n>=0,"INPUT_OPEN");struct stat st{};need(fstat(f.n,&st)==0&&st.st_nlink==1,"INPUT_LINKS");return bytes_fd(f.n,cap);}
bool Files::wait(const std::string& n,Clock::time_point end,int cancel)const{
    need(name(n),"WAIT_NAME");Fd notify(inotify_init1(IN_CLOEXEC|IN_NONBLOCK));need(notify.n>=0,"INOTIFY_UNAVAILABLE");need(inotify_add_watch(notify.n,s_->root.c_str(),IN_CLOSE_WRITE|IN_MOVED_TO|IN_DELETE_SELF|IN_MOVE_SELF)>=0,"INOTIFY_WATCH");
    // Subscribe first, then recheck: a release between these operations is not lost.
    for(;;){struct stat st{};if(fstatat(s_->dir.n,n.c_str(),&st,AT_SYMLINK_NOFOLLOW)==0){need(S_ISREG(st.st_mode)&&st.st_nlink==1,"WAIT_INPUT_TYPE");return true;}need(errno==ENOENT,"WAIT_STAT");if(Clock::now()>=end)return false;
        auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(end-Clock::now()).count();pollfd p[2]={{notify.n,POLLIN,0},{cancel,POLLIN,0}};auto rc=poll(p,cancel>=0?2:1,static_cast<int>(std::max<int64_t>(1,ms)));if(rc<0&&errno==EINTR)continue;need(rc>=0,"WAIT_POLL");if(!rc||(cancel>=0&&p[1].revents))return false;need(!(p[0].revents&(POLLERR|POLLHUP|POLLNVAL)),"WAIT_INOTIFY_ERROR");
        alignas(inotify_event) char buf[4096];ssize_t got=0;
        while((got=::read(notify.n,buf,sizeof(buf)))>0){size_t at=0;while(at<static_cast<size_t>(got)){need(static_cast<size_t>(got)-at>=sizeof(inotify_event),"WAIT_EVENT_HEADER");const auto* e=reinterpret_cast<const inotify_event*>(buf+at);need(e->len<=static_cast<size_t>(got)-at-sizeof(*e),"WAIT_EVENT_BOUND");need(!(e->mask&(IN_Q_OVERFLOW|IN_DELETE_SELF|IN_MOVE_SELF|IN_IGNORED)),"WAIT_EVENT_LOST");at+=sizeof(*e)+e->len;}}
        need(got<0&&(errno==EAGAIN||errno==EWOULDBLOCK),"WAIT_INOTIFY_READ");
    }
}
Scalar::Scalar(const char* expected){
    const char* path="/system/lib64/platformsdk/libtokensetproc_shared.z.so";need(hash(expected)&&file_hash(path)==expected,"SCALAR_BYTES");retained=dlopen(path,RTLD_NOW|RTLD_LOCAL);need(retained,"SCALAR_LOAD");
    for(unsigned i=0;i<2;++i){void* f=dlsym(retained,i?"SetSelfTokenID":"GetSelfTokenID");Dl_info d{};char canonical[PATH_MAX]{};need(f&&dladdr(f,&d)&&d.dli_fname&&realpath(d.dli_fname,canonical)&&std::string(canonical)==path,"SCALAR_OWNER");if(i)std::memcpy(&set_fn,&f,sizeof(f));else std::memcpy(&get_fn,&f,sizeof(f));}
    // No manager query, container ABI, setter call or unload here.
}
uint64_t Scalar::get()const{return get_fn();}
int Scalar::set(uint64_t t)const{return set_fn(t);}
int sealed_state(const ResumeState& s){const auto b=encode_state(s);Fd f(memfd_create("mdds-owned-exec",MFD_ALLOW_SEALING|MFD_CLOEXEC));need(f.n>=0,"STATE_MEMFD");size_t at=0;while(at<b.size()){auto n=write(f.n,b.data()+at,b.size()-at);need(n>0,"STATE_WRITE");at+=static_cast<size_t>(n);}constexpr int seals=F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL;need(fcntl(f.n,F_ADD_SEALS,seals)==0,"STATE_ADD_SEALS");const int observed=fcntl(f.n,F_GET_SEALS);need(observed>=0&&(observed&seals)==seals,"STATE_SEALS");need(fcntl(f.n,F_SETFD,0)==0,"STATE_EXEC_INHERIT");return f.release();}
ResumeState read_state(int fd){constexpr int seals=F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL;const int observed=fcntl(fd,F_GET_SEALS);need(observed>=0&&(observed&seals)==seals,"STATE_UNSEALED");auto b=bytes_fd(fd,state_bytes);ResumeState out;need(decode_state(b.data(),b.size(),out),"STATE_LAYOUT");return out;}
int sealed_image(const std::string& path,const std::string& expected){
    auto x=parts(path);Fd d(directory(x.first));Fd input(openat(d.n,x.second.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));need(input.n>=0,"EXEC_IMAGE_OPEN");struct stat s{};need(fstat(input.n,&s)==0&&s.st_nlink==1&&!(s.st_mode&(S_ISUID|S_ISGID)),"EXEC_IMAGE_METADATA");auto b=bytes_fd(input.n,16*1048576);need(pure_hash::digest(b.data(),b.size())==expected,"EXEC_IMAGE_SHA");
    Fd f(memfd_create("mdds-goal-image",MFD_CLOEXEC|MFD_ALLOW_SEALING));need(f.n>=0,"EXEC_IMAGE_MEMFD");size_t at=0;while(at<b.size()){auto n=write(f.n,b.data()+at,std::min<size_t>(4096,b.size()-at));need(n>0,"EXEC_IMAGE_WRITE");at+=static_cast<size_t>(n);}constexpr int seals=F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL;need(fcntl(f.n,F_ADD_SEALS,seals)==0,"EXEC_IMAGE_ADD_SEALS");const int observed=fcntl(f.n,F_GET_SEALS);need(observed>=0&&(observed&seals)==seals,"EXEC_IMAGE_SEALS");return f.release();
}
Admission bound_admission(const stage1_entry::Record& r,const Point& p,const char* digest,bool actual_lookup){Admission a{};for(const char* c=r.value[stage1_entry::Run];*c;++c)a.run=(a.run<<4)|uint64_t(*c<='9'?*c-'0':*c-'a'+10);a.pid=p.pid;a.parent=p.parent;a.birth=p.birth;a.caller=r.caller;a.target=r.target;a.until=p.now+UINT64_C(90000000000);a.uid=p.uid;a.euid=p.euid;a.gid=p.gid;a.egid=p.egid;a.issued_wall=r.issued;a.until_wall=r.until;a.side=r.value[stage1_entry::Side][0]=='A'?0:1;std::memcpy(a.own,NS_ACTIONBAG,sizeof(NS_ACTIONBAG));std::memcpy(a.board,r.value[stage1_entry::Board],std::strlen(r.value[stage1_entry::Board])+1);std::memcpy(a.image_sha,r.value[stage1_entry::ConsumerSha],65);std::memcpy(a.record_sha,digest,65);a.fresh_owned_lookup=actual_lookup;a.operator_scope_bound=true;return a;}
bool query_owned_child(const stage1_entry::Record& r,uint64_t original,uint64_t until,std::string& observation){
    // Separate read-only query process BEFORE selection: its IPC workers never
    // enter the fresh selector parent. Parent is the exclusive direct waiter.
    need(query_exclusive(),"QUERY_WAIT_RIGHTS");
    int f[2];need(pipe2(f,O_CLOEXEC|O_NONBLOCK)==0,"QUERY_PIPE");Fd input(f[0]),output(f[1]);const auto parent=getpid();const auto child=fork();need(child>=0,"QUERY_FORK");
    if(child==0){
        close(input.n);input.n=-1;std::array<uint8_t,40> b{};std::memcpy(b.data(),"NSLOOK01",8);int rc=NS_STATE;NS_Observation o{};o.header={NS_VERSION,sizeof(o),NS_SCOPE,0};uint64_t flags=0;
        if(prctl(PR_SET_PDEATHSIG,SIGKILL)==0&&getppid()==parent){
            void* h=dlopen(r.value[stage1_entry::Bridge],RTLD_NOW|RTLD_LOCAL);void* raw=h?dlsym(h,"ns_identity_get_api"):nullptr;Dl_info info{};char canonical[PATH_MAX]{};NS_GetIdentityApi get{};
            if(raw&&dladdr(raw,&info)&&info.dli_fname&&realpath(info.dli_fname,canonical)&&!std::strcmp(canonical,r.value[stage1_entry::Bridge]))std::memcpy(&get,&raw,sizeof(get));NS_IdentityApi api{};
            if(get&&get(NS_VERSION,sizeof(api),&api)==NS_OK&&ns_header(&api.header,sizeof(api))==NS_OK&&api.query){
                void* query=nullptr;std::memcpy(&query,&api.query,sizeof(query));Dl_info qi{};
                if(dladdr(query,&qi)&&qi.dli_fbase==info.dli_fbase){rc=api.query(0,&o);
                    if(o.lookup==r.target)flags|=1;if(o.self==original)flags|=2;if(o.type==o.native_type&&o.info_rc==0)flags|=4;
                    if(o.apl==o.normal_apl)flags|=8;if(o.granted&&o.query_rc==0&&o.query_count==1&&(o.called&31)==31)flags|=16;
                    if(std::memchr(o.process_name,0,sizeof(o.process_name))&&!std::strcmp(o.process_name,NS_ACTIONBAG)&&o.cpp_abi[0]=='_'&&o.cpp_abi[1]=='_'&&o.cpp_abi[2]=='h'&&o.cpp_abi[3]==0)flags|=32;
                }
            }
        }
        put64(b.data()+8,static_cast<uint64_t>(static_cast<int64_t>(rc)));put64(b.data()+16,o.lookup);put64(b.data()+24,o.self);put64(b.data()+32,flags);
        auto n=write(output.n,b.data(),b.size());std::_Exit(n==static_cast<ssize_t>(b.size())?0:91);
    }
    close(output.n);output.n=-1;std::array<uint8_t,40> b{};size_t at=0;bool terminal=false,stopped=false;siginfo_t status{};int observed_errno=0;
    auto unknown=[&](unsigned phase,int error=0){observation="format=S1LOOKFAIL1\nphase="+std::to_string(phase)+"\nerrno_valid="+std::to_string(error!=0)+"\napi_errno="+std::to_string(error)+"\nsignal_sent="+std::to_string(stopped)+"\nwait_or_cleanup=UNKNOWN\n";return false;};
    auto observe=[&](){
        status={};const auto rc=waitid(P_PID,static_cast<id_t>(child),&status,WEXITED|WNOHANG|WNOWAIT);observed_errno=rc?errno:0;
        QueryObservation q;q.api_ok=rc==0;q.interrupted=rc&&observed_errno==EINTR;q.pid=status.si_pid>0?static_cast<uint64_t>(status.si_pid):0;q.empty=status.si_pid==0&&status.si_signo==0&&status.si_code==0&&status.si_status==0;q.sigchld=status.si_signo==SIGCHLD;
        q.terminal=status.si_code==CLD_EXITED?QueryTerminal::Exited:status.si_code==CLD_KILLED?QueryTerminal::Signalled:status.si_code==CLD_DUMPED?QueryTerminal::Dumped:status.si_code==0?QueryTerminal::None:QueryTerminal::Invalid;
        q.valid_status=q.terminal==QueryTerminal::Exited?(status.si_status>=0&&status.si_status<=255):(status.si_status>0&&status.si_status<NSIG);
        if(!query_exclusive()||status.si_pid<0)return QueryWait::Unknown;
        return classify_query_wait(static_cast<uint64_t>(child),q);
    };
    while(now_ns()<until){
        const auto state=observe();if(state==QueryWait::Interrupted)continue;if(state==QueryWait::Unknown)return unknown(1,observed_errno);terminal=state==QueryWait::Terminal;
        if(at<b.size()){auto n=::read(input.n,b.data()+at,b.size()-at);if(n>0)at+=static_cast<size_t>(n);else if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)return unknown(2,errno);}
        if(terminal)break;pollfd p{input.n,POLLIN,0};(void)poll(&p,1,10);
    }
    const auto reap_until=now_ns()+UINT64_C(2000000000);
    if(!terminal){
        // Fresh unconsumed wait right pins this *created direct child* PID.
        // Any ECHILD/error revokes signalling; never use PGID or descendants.
        QueryWait state=QueryWait::Interrupted;
        while(now_ns()<reap_until&&state==QueryWait::Interrupted)state=observe();
        if(state==QueryWait::Terminal)terminal=true;
        else if(state==QueryWait::Running&&query_exclusive()&&now_ns()<reap_until){if(kill(child,SIGKILL))return unknown(3,errno);stopped=true;}
        else return unknown(4,observed_errno);
    }
    int code=0;bool reaped=false;
    do{auto got=waitpid(child,&code,WNOHANG);if(got==child){reaped=true;break;}if(got<0&&errno!=EINTR)return unknown(5,errno);if(got>0)return unknown(6);poll(nullptr,0,10);}while(now_ns()<reap_until);
    if(!reaped||stopped||!WIFEXITED(code)||WEXITSTATUS(code)||at!=b.size()||std::memcmp(b.data(),"NSLOOK01",8))return unknown(7);
    const bool ok=get64(b.data()+8)==0&&get64(b.data()+16)==r.target&&get64(b.data()+24)==original&&get64(b.data()+32)==63;
    observation="format=S1LOOK1\nlookup="+std::to_string(get64(b.data()+16))+"\nself="+std::to_string(get64(b.data()+24))+"\nflags="+std::to_string(get64(b.data()+32))+"\nquery_rc="+std::to_string(static_cast<int64_t>(get64(b.data()+8)))+"\n";return ok;
}
}
