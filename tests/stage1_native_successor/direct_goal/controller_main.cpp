#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "pair_controller.hpp"
#include "../control/sha256_pure.hpp"
#include <filesystem>
#include <cstdio>
#include <optional>
namespace {
namespace fs=std::filesystem;using namespace stage1_goal;
const char* safe_reason="CONTROL_EXCEPTION";unsigned safe_phase=0;
void need(bool b,const char* s){if(!b){safe_reason=s;throw std::runtime_error("CONTROL_REJECTED");}}
struct Handle{HANDLE h=INVALID_HANDLE_VALUE;explicit Handle(HANDLE v):h(v){}~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}Handle(const Handle&)=delete;};
fs::path root_path(const char* s){auto p=fs::absolute(fs::path(s)).lexically_normal();auto v=p.native();need(v.rfind(L"E:\\Codex-FastDDS-stage1-directdds",0)==0&&v.find(L"..") == std::wstring::npos,"CONTROL_ROOT_SCOPE");fs::path at=p.root_path();for(const auto& c:p.relative_path()){at/=c;auto a=GetFileAttributesW(at.c_str());need(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)&&!(a&FILE_ATTRIBUTE_REPARSE_POINT),"CONTROL_ROOT_NO_REPARSE");}return p;}
std::optional<std::vector<uint8_t>> read(const fs::path& root,const std::string& name,size_t cap){
    Handle f(CreateFileW((root/fs::path(name)).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(f.h==INVALID_HANDLE_VALUE){need(GetLastError()==ERROR_FILE_NOT_FOUND,"CONTROL_READ_OPEN");return std::nullopt;}
    BY_HANDLE_FILE_INFORMATION a{},b{};need(GetFileInformationByHandle(f.h,&a)&&GetFileType(f.h)==FILE_TYPE_DISK&&!(a.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&a.nNumberOfLinks==1&&!a.nFileSizeHigh&&a.nFileSizeLow&&a.nFileSizeLow<=cap,"CONTROL_READ_METADATA");
    std::vector<uint8_t> v(a.nFileSizeLow);DWORD done=0;need(ReadFile(f.h,v.data(),static_cast<DWORD>(v.size()),&done,nullptr)&&done==v.size(),"CONTROL_EXACT_READ");
    need(GetFileInformationByHandle(f.h,&b)&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow&&a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileSizeHigh==b.nFileSizeHigh&&a.nFileSizeLow==b.nFileSizeLow&&a.ftLastWriteTime.dwHighDateTime==b.ftLastWriteTime.dwHighDateTime&&a.ftLastWriteTime.dwLowDateTime==b.ftLastWriteTime.dwLowDateTime,"CONTROL_READ_CHANGED");return v;
}
void publish(const fs::path& root,const std::string& name,const std::vector<uint8_t>& bytes){
    need(!bytes.empty()&&bytes.size()<=65536,"CONTROL_OUTPUT_BOUND");auto tmp=root/fs::path(name+".writing"),out=root/fs::path(name);
    Handle f(CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));need(f.h!=INVALID_HANDLE_VALUE,"CONTROL_OUTPUT_EXISTS");DWORD n=0;need(WriteFile(f.h,bytes.data(),static_cast<DWORD>(bytes.size()),&n,nullptr)&&n==bytes.size()&&FlushFileBuffers(f.h),"CONTROL_OUTPUT_WRITE");need(CloseHandle(f.h),"CONTROL_OUTPUT_CLOSE");f.h=INVALID_HANDLE_VALUE;need(MoveFileExW(tmp.c_str(),out.c_str(),MOVEFILE_WRITE_THROUGH),"CONTROL_ATOMIC_PUBLISH_NO_REPLACE");
}
std::string digest(const std::vector<uint8_t>& v){return stage1_control::pure_hash::digest(v.data(),v.size());}
std::vector<uint8_t> bytes(const std::string& s){return {s.begin(),s.end()};}
}
int main(int argc,char** argv){
    // This local coordinator never invokes HDC, a token API or business image.
    // --fixture-coordinate changes only the accepted event flag in Window.
    if(argc!=4||(std::strcmp(argv[1],"--coordinate")&&std::strcmp(argv[1],"--fixture-coordinate")))return 78;
    try{
        safe_phase=1;const bool fixture=!std::strcmp(argv[1],"--fixture-coordinate");auto root=root_path(argv[2]);auto n=std::string(argv[3]);need(n.size()<=6&&!n.empty()&&n.find_first_not_of("0123456789")==std::string::npos,"CONTROL_BUDGET");auto ms=std::stoul(n);need(ms>0&&ms<=120000,"CONTROL_BUDGET");
        safe_phase=2;
        auto pair_bytes=read(root,"pair",8192);need(pair_bytes.has_value(),"CONTROL_PAIR_REQUIRED");auto pair=PairRecord::parse(std::string(pair_bytes->begin(),pair_bytes->end()));auto q=pair.request(0,digest(*pair_bytes),1);const auto end=stage1_run::Clock::now()+std::chrono::milliseconds(ms);PairCoordinator c(q.local,q.peer,end,fixture);unsigned published=0;std::array<std::vector<uint8_t>,2> frames;
        while(stage1_run::Clock::now()<end){
            safe_phase=3;if(read(root,"cancel",1)){c.cancel();break;}
            // Don't ingest next-stage A events between sequential A/B delivery.
            // Transfer evidence is supplied only after BOTH accepted operations.
            safe_phase=4;if(published){auto ack=read(root,"delivered."+std::to_string(published),256);if(ack){auto expected="S1DELIVERY1\n"+pair.f[PRun]+"\n"+std::to_string(published)+"\n"+digest(frames[0])+"\n"+digest(frames[1])+"\n";need(*ack==bytes(expected),"CONTROL_BOTH_DELIVERY_BINDING");need(c.confirm_both_delivered(published),"CONTROL_DELIVERY_ORDER");published=0;}}
            safe_phase=5;if(!published){for(unsigned s=0;s<2;++s){auto v=read(root,s?"events.B":"events.A",stage1_control::byte_limit);if(v)need(c.ingest(s,*v),c.error()?c.error():"CONTROL_EVENTS");}if(c.has_release()){published=c.delivery_sequence();frames=c.release_frames();publish(root,"release.A."+std::to_string(published),frames[0]);publish(root,"release.B."+std::to_string(published),frames[1]);}}
            if(c.complete()){publish(root,"controller-result",bytes(std::string("S1CONTROLLER1\nfixture=")+(fixture?"1":"0")+"\nrun="+pair.f[PRun]+"\nROOT_MATCH_GUID_SN_PAYLOAD_ACK_DONE_TEARDOWN=1\nNative_attestation=0\nentry_restore_and_exit_checked_separately=1\n"));return 0;}
            Sleep(10); // Local file observation only; one absolute non-reset deadline.
        }
        safe_phase=6;c.cancel();for(const char* s:{"A","B"})publish(root,std::string("cancel.")+s,bytes("1"));return 81;
    }catch(...){
        // A caller must send both native cancel files after any local failure;
        // this process has no device handles, signals or inferred ownership.
        std::printf("CONTROL_FAILED phase=%u reason=%s cancel_both_native_sides_required=1 no_device_actions=1\n",safe_phase,safe_reason);return 82;
    }
}
