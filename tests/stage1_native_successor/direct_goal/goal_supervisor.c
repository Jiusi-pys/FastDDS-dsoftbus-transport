/* New direct-DDS window: at most120s + common2s. Historical8s supervisor immutable. No added signalling authority. */
#define _GNU_SOURCE 1
#include "../supervision/owned_policy.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
static volatile sig_atomic_t stop_requested=0;
static void control_signal(int sig){if(sig==SIGALRM)_exit(125);stop_requested=1;} /* NO signals/loggers/waits in handler. */
static int64_t now_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int exclusive(void){struct sigaction a;DIR* d;struct dirent* e;unsigned count=0;int ok=1;if(sigaction(SIGCHLD,NULL,&a)||a.sa_handler!=SIG_DFL||(a.sa_flags&SA_NOCLDWAIT))return 0;d=opendir("/proc/self/task");if(!d)return 0;errno=0;while((e=readdir(d))){if(e->d_name[0]=='.')continue;if(++count!=1){ok=0;break;}}if(errno)ok=0;if(closedir(d))ok=0;return ok&&count==1;}
static int identity_read(uint32_t pid,OW_Identity* out){
    char path[80],buf[4097],*end,*at;ssize_t n;unsigned field;uint64_t ppid=0,birth=0;int fd;if(!pid||!out)return OW_ERROR;
    if(snprintf(path,sizeof(path),"/proc/%u/stat",pid)>=(int)sizeof(path))return OW_ERROR;fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return OW_ERROR;n=read(fd,buf,sizeof(buf)-1);if(close(fd)||n<=0||n>=(ssize_t)sizeof(buf)-1)return OW_ERROR;buf[n]=0;at=strrchr(buf,')');if(!at||at[1]!=' ')return OW_ERROR;at+=2;
    for(field=3;field<=22;++field){end=strchr(at,' ');if(!end)return OW_ERROR;if(field==4||field==22){char saved=*end,*last;unsigned long long value;*end=0;errno=0;value=strtoull(at,&last,10);if(errno||last==at||*last){*end=saved;return OW_ERROR;}*end=saved;if(field==4)ppid=value;else birth=value;}at=end+1;}
    if(ppid>UINT32_MAX||!birth)return OW_ERROR;out->pid=pid;out->ppid=(uint32_t)ppid;out->birth=birth;out->exclusive=exclusive();return out->exclusive?OW_OK:OW_ERROR;
}
typedef struct {uint32_t parent;int last_errno;long rss;} Native;
static int observe(void* p,uint32_t pid,OW_Observation* out){Native* n=p;siginfo_t i;if(!exclusive())return OW_ERROR;memset(&i,0,sizeof(i));if(waitid(P_PID,(id_t)pid,&i,WEXITED|WNOHANG|WNOWAIT)){n->last_errno=errno;return errno==EINTR?OW_INTR:OW_ERROR;}out->pid=(uint32_t)i.si_pid;out->terminal=i.si_pid!=0;out->raw_status=0;if(!i.si_pid)return OW_OK;if(i.si_pid!=(pid_t)pid)return OW_ERROR;if(i.si_code==CLD_EXITED)out->raw_status=(i.si_status&255)<<8;else if(i.si_code==CLD_KILLED)out->raw_status=i.si_status&127;else if(i.si_code==CLD_DUMPED)out->raw_status=(i.si_status&127)|128;else return OW_ERROR;return OW_OK;}
static int identity(void* p,uint32_t pid,OW_Identity* out){Native* n=p;int r=identity_read(pid,out);return r==OW_OK&&out->ppid==n->parent?OW_OK:OW_ERROR;}
static int signal_one(void* p,uint32_t pid){Native* n=p;if(!pid||pid>INT32_MAX||!exclusive())return OW_ERROR;if(kill((pid_t)pid,SIGKILL)){n->last_errno=errno;return OW_ERROR;}return OW_OK;}
static int reap(void* p,uint32_t pid,int* status){Native* n=p;struct rusage u;pid_t r;if(!exclusive())return OW_ERROR;memset(&u,0,sizeof(u));r=wait4((pid_t)pid,status,WNOHANG,&u);if(r==0)return OW_PENDING;if(r<0){n->last_errno=errno;return errno==EINTR?OW_INTR:OW_ERROR;}if(r!=(pid_t)pid)return OW_ERROR;n->rss=u.ru_maxrss;return OW_OK;}
int main(int argc,char** argv){
    char* end;long seconds;int out,err,ready[2];pid_t creator,child;int64_t start;struct sigaction a;OW_Child owned;OW_Identity initial;Native context;const OW_Ops ops={observe,identity,signal_one,reap};
    if(argc<6)return 90;errno=0;seconds=strtol(argv[1],&end,10);if(errno||!end||*end||seconds<1||seconds>120||argv[2][0]!='/'||argv[3][0]!='/'||argv[4][0]!='/'||argv[5][0]!='/')return 90;if(!exclusive())return 90;
    out=open(argv[2],O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(out<0)return 90;err=open(argv[3],O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(err<0){close(out);return 90;}if(pipe2(ready,O_CLOEXEC|O_NONBLOCK))return 90;
    memset(&a,0,sizeof(a));a.sa_handler=control_signal;sigemptyset(&a.sa_mask);if(sigaction(SIGALRM,&a,NULL)||sigaction(SIGTERM,&a,NULL)||sigaction(SIGHUP,&a,NULL)||sigaction(SIGINT,&a,NULL))return 90;
    {sigset_t unblocked;sigemptyset(&unblocked);sigaddset(&unblocked,SIGALRM);sigaddset(&unblocked,SIGTERM);sigaddset(&unblocked,SIGHUP);sigaddset(&unblocked,SIGINT);if(sigprocmask(SIG_UNBLOCK,&unblocked,NULL))return 90;}
    creator=getpid();start=now_ms();if(start<0)return 90;alarm((unsigned)seconds+2);child=fork();if(child<0){printf("DIRECT_SUP_UNKNOWN stage=FORK parent=%d child=-1 no_signal=1\n",(int)creator);fflush(stdout);alarm(0);return 90;}
    if(child==0){
        /* memfd image sealing also grows a regular file. The frozen app is
         * 302104B; admission must reject images above this finite 1MiB bound. */
        const struct rlimit core={0,0},fsize={1048576,1048576},as={2684354560ULL,2684354560ULL};char temporary[4096];char* env[4];close(ready[0]);
        if(setsid()!=getpid()||prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=creator||setrlimit(RLIMIT_CORE,&core)||setrlimit(RLIMIT_FSIZE,&fsize)||setrlimit(RLIMIT_AS,&as)||dup2(out,1)<0||dup2(err,2)<0)_exit(93);close(out);close(err);
        if(write(ready[1],"R",1)!=1)_exit(94);close(ready[1]);if(snprintf(temporary,sizeof(temporary),"TMPDIR=%s",argv[4])>=(int)sizeof(temporary))_exit(95);env[0]="PATH=/system/bin:/vendor/bin";env[1]="LANG=C";env[2]=temporary;env[3]=NULL;execve(argv[5],argv+5,env);_exit(96);
    }
    close(out);close(err);close(ready[1]);memset(&context,0,sizeof(context));context.parent=(uint32_t)creator;
    printf("DIRECT_SUP_CREATED parent=%d child=%d generation=UNVERIFIED no_signal_authority=1\n",(int)creator,(int)child);fflush(stdout);
    /* Root is still ours/unreaped; unavailable initial identity seals UNKNOWN.
     * No fallback numeric signal, process-name kill or group cleanup. */
    if(identity_read((uint32_t)child,&initial)!=OW_OK||initial.ppid!=(uint32_t)creator||ow_begin(&owned,(uint32_t)child,(uint32_t)creator,initial.birth,start,seconds*1000)!=OW_OK){printf("DIRECT_SUP_UNKNOWN stage=INITIAL_IDENTITY_OR_ADMISSION parent=%d child=%d no_signal=1 descendants=UNKNOWN\n",(int)creator,(int)child);fflush(stdout);close(ready[0]);alarm(0);return 125;}
    printf("DIRECT_SUP_BEGIN parent=%d child=%d birth=%llu scope=DIRECT_CHILD_ONLY descendants=UNKNOWN\n",(int)creator,(int)child,(unsigned long long)initial.birth);fflush(stdout);
    while(owned.phase!=OW_REAPED&&owned.phase!=OW_UNKNOWN){struct timespec pause={0,10000000};(void)ow_step(&owned,&ops,&context,now_ms(),stop_requested!=0);if(owned.phase!=OW_REAPED&&owned.phase!=OW_UNKNOWN)(void)nanosleep(&pause,NULL);}
    alarm(0);close(ready[0]);printf("DIRECT_SUP_RESULT child=%u phase=%d status_valid=%d raw_status=%d signal_sent=%d timeout=%d cancelled=%d late=%d errno=%d root_reaped=%d peak_rss_kib=%ld descendants=UNKNOWN no_group_signal=1\n",owned.pid,owned.phase,owned.status_valid,owned.status_valid?owned.raw_status:-1,owned.signal_sent,owned.timed_out,owned.cancelled,owned.late_observation,context.last_errno,owned.phase==OW_REAPED,context.rss);fflush(stdout);
    if(owned.phase!=OW_REAPED||!owned.status_valid||context.rss<=0||context.rss>524288||owned.late_observation)return 125;if(owned.timed_out)return 124;if(owned.cancelled)return 125;return WIFEXITED(owned.raw_status)?WEXITSTATUS(owned.raw_status):WIFSIGNALED(owned.raw_status)?128+WTERMSIG(owned.raw_status):125;
}
