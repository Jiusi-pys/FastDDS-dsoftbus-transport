#include "handoff_win.hpp"
#ifdef _WIN32
#include <processsnapshot.h>
#include "sha256_pure.hpp"
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <cstdio>
namespace stage1_control::handoff {
namespace {
void need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
std::wstring wide(const std::string& s){need(!s.empty()&&s.size()<1024,"path bound");for(unsigned char c:s)need(c>=32&&c<=126,"ASCII owned path required");return {s.begin(),s.end()};}
std::string narrow(const std::wstring& s){std::string out;for(wchar_t c:s){need(c>=32&&c<=126,"path encoding");out.push_back(static_cast<char>(c));}return out;}
std::wstring canonical(const std::string& input){auto w=wide(input);std::replace(w.begin(),w.end(),L'/',L'\\');need(w.size()>3&&w[1]==L':'&&w[2]==L'\\',"absolute drive path");wchar_t full[1024]{};DWORD n=GetFullPathNameW(w.c_str(),1024,full,nullptr);need(n>0&&n<1024&&CompareStringOrdinal(w.c_str(),-1,full,-1,TRUE)==CSTR_EQUAL,"noncanonical path");return {full,n};}
std::string final_path(HANDLE h){wchar_t path[1024]{};DWORD n=GetFinalPathNameByHandleW(h,path,1024,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);need(n>4&&n<1024,"handle path");std::wstring s(path,n);need(s.compare(0,4,L"\\\\?\\")==0,"unsupported namespace");return narrow(s.substr(4));}
FileId file_id(HANDLE h){BY_HANDLE_FILE_INFORMATION i{};need(GetFileInformationByHandle(h,&i)!=0,"file identity");return {i.dwVolumeSerialNumber,i.nFileIndexHigh,i.nFileIndexLow};}
void regular(HANDLE h){FILE_ATTRIBUTE_TAG_INFO tag{};need(GetFileType(h)==FILE_TYPE_DISK&&GetFileInformationByHandleEx(h,FileAttributeTagInfo,&tag,sizeof tag),"regular file attributes");need(!(tag.FileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),"nonregular/reparse file");}
DWORD remaining(Clock::time_point end){auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(end-Clock::now()).count();return ms<=0?0:static_cast<DWORD>(std::min<int64_t>(ms+1,INT32_MAX));}
}
struct LockedFile::State {
    std::vector<Handle> ancestors;Handle file;FileId identity;std::string name;bool retained=false;
    struct Read {OVERLAPPED o{};std::vector<uint8_t> bytes;explicit Read(size_t n):bytes(n){o.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);need(o.hEvent!=nullptr,"file event");}~Read(){CloseHandle(o.hEvent);}};
    Read* pending=nullptr;
};
LockedFile::LockedFile(const std::string& input):state_(new State){auto full=canonical(input);const size_t last=full.find_last_of(L'\\');need(last>=3,"file parent");
    // Every ancestor is opened before its descendant and held without DELETE
    // sharing, so ordinary path rebinding cannot race the subsequent loader.
    for(size_t pos=3;;){const auto part=full.substr(0,pos);Handle h(CreateFileW(part.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));need(h.value!=INVALID_HANDLE_VALUE,"ancestor pin");FILE_ATTRIBUTE_TAG_INFO tag{};need(GetFileInformationByHandleEx(h.value,FileAttributeTagInfo,&tag,sizeof tag)&&!(tag.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&(tag.FileAttributes&FILE_ATTRIBUTE_DIRECTORY),"ancestor reparse/refusal");state_->ancestors.push_back(std::move(h));if(pos==last)break;pos=full.find(L'\\',pos==3?3:pos+1);need(pos!=std::wstring::npos&&pos<=last,"ancestor chain");}
    state_->file=Handle(CreateFileW(full.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_OVERLAPPED,nullptr));need(state_->file.value!=INVALID_HANDLE_VALUE,"file denywrite/delete lock");regular(state_->file.value);state_->name=final_path(state_->file.value);need(CompareStringOrdinal(wide(state_->name).c_str(),-1,full.c_str(),-1,TRUE)==CSTR_EQUAL,"file path association");state_->identity=file_id(state_->file.value);
}
LockedFile::~LockedFile(){if(state_&&state_->retained)(void)state_.release();}
std::unique_ptr<LockedFile> LockedFile::adopt_readonly(HANDLE& h){regular(h);const auto path=final_path(h);const auto identity=file_id(h);auto result=std::make_unique<LockedFile>(path);need(result->id()==identity,"inherited manifest file identity");result->state_->file=Handle(h);h=INVALID_HANDLE_VALUE;return result;}
HANDLE LockedFile::handle()const{return state_->file.value;}FileId LockedFile::id()const{return state_->identity;}const std::string& LockedFile::path()const{return state_->name;}
std::vector<uint8_t> LockedFile::read(size_t maximum,Clock::time_point end)const{
    auto& s=*state_;need(!s.retained,"retained read handle");LARGE_INTEGER size{};need(GetFileSizeEx(s.file.value,&size)&&size.QuadPart>=0&&static_cast<uint64_t>(size.QuadPart)<=maximum,"file size bound");std::vector<uint8_t> out(static_cast<size_t>(size.QuadPart));size_t at=0;
    while(at<out.size()){need(Clock::now()<end,"file deadline");auto op=std::make_unique<State::Read>(std::min<size_t>(32768,out.size()-at));op->o.Offset=static_cast<DWORD>(at);op->o.OffsetHigh=static_cast<DWORD>(static_cast<uint64_t>(at)>>32);DWORD n=0;BOOL ok=ReadFile(s.file.value,op->bytes.data(),static_cast<DWORD>(op->bytes.size()),&n,&op->o);
        if(!ok){need(GetLastError()==ERROR_IO_PENDING,"file read");if(WaitForSingleObject(op->o.hEvent,remaining(end))!=WAIT_OBJECT_0){(void)CancelIoEx(s.file.value,&op->o);if(WaitForSingleObject(op->o.hEvent,1000)!=WAIT_OBJECT_0){s.retained=true;s.pending=op.release();}throw std::runtime_error("file read deadline/retained");}need(GetOverlappedResult(s.file.value,&op->o,&n,FALSE)!=0,"file completion");}
        need(n>0&&n<=op->bytes.size(),"short file");std::memcpy(out.data()+at,op->bytes.data(),n);at+=n;
    }
    need(file_id(s.file.value)==s.identity&&Clock::now()<end,"file identity/deadline after read");return out;
}
std::string sha_bytes(const void* data,size_t n){need(n<=4*1048576,"hash bound");return pure_hash::digest(data,n);}
std::string LockedFile::sha(size_t maximum,Clock::time_point end)const{auto bytes=read(maximum,end);return sha_bytes(bytes.data(),bytes.size());}
std::string current_image(){char path[1024]{};DWORD n=GetModuleFileNameA(nullptr,path,1024);need(n>0&&n<1024,"own executable path");return {path,n};}
uint64_t creation_time(HANDLE h){FILETIME a{},b{},c{},d{};need(GetProcessTimes(h,&a,&b,&c,&d)!=0,"owned process creation time");return (uint64_t(a.dwHighDateTime)<<32)|a.dwLowDateTime;}
struct ThreadObservation {DWORD capture=ERROR_INVALID_FUNCTION,marker=ERROR_INVALID_FUNCTION,walk=ERROR_INVALID_FUNCTION,marker_free=0,snapshot_free=0;unsigned count=0;bool self=false,truncated=false;PSS_THREAD_ENTRY entries[16]{};};
static ThreadObservation threads(){ThreadObservation o;HPSS snap=nullptr;HPSSWALK mark=nullptr;o.capture=PssCaptureSnapshot(GetCurrentProcess(),PSS_CAPTURE_THREADS,0,&snap);if(o.capture!=ERROR_SUCCESS)return o;o.marker=PssWalkMarkerCreate(nullptr,&mark);if(o.marker!=ERROR_SUCCESS){o.snapshot_free=PssFreeSnapshot(GetCurrentProcess(),snap);return o;}
    for(;;){PSS_THREAD_ENTRY e{};o.walk=PssWalkSnapshot(snap,PSS_WALK_THREADS,mark,&e,sizeof e);if(o.walk!=ERROR_SUCCESS)break;if(o.count==16){o.truncated=true;break;}o.entries[o.count++]=e;o.self=o.self||e.ThreadId==GetCurrentThreadId();}
    o.marker_free=PssWalkMarkerFree(mark);o.snapshot_free=PssFreeSnapshot(GetCurrentProcess(),snap);return o;
}
bool sole_main_thread(){const auto o=threads();return o.capture==ERROR_SUCCESS&&o.marker==ERROR_SUCCESS&&o.walk==ERROR_NO_MORE_ITEMS&&!o.truncated&&o.count==1&&o.self;}
void diagnostic_threads(const char* checkpoint){const auto o=threads();std::printf("THREAD_DIAG checkpoint=%s pid=%lu tid=%lu capture=%lu marker=%lu walk=%lu count=%u self=%d truncated=%d marker_free=%lu snapshot_free=%lu entry_size=%zu\n",checkpoint,GetCurrentProcessId(),GetCurrentThreadId(),o.capture,o.marker,o.walk,o.count,int(o.self),int(o.truncated),o.marker_free,o.snapshot_free,sizeof(PSS_THREAD_ENTRY));
    for(unsigned i=0;i<o.count;++i){const auto& e=o.entries[i];const auto created=(uint64_t(e.CreateTime.dwHighDateTime)<<32)|e.CreateTime.dwLowDateTime;const auto exited=(uint64_t(e.ExitTime.dwHighDateTime)<<32)|e.ExitTime.dwLowDateTime;std::printf("THREAD_ENTRY checkpoint=%s index=%u id=%lu flags=%u exit_status=%lu create=%llu exit=%llu\n",checkpoint,i,e.ThreadId,static_cast<unsigned>(e.Flags),e.ExitStatus,static_cast<unsigned long long>(created),static_cast<unsigned long long>(exited));}
}
struct Module::State {std::shared_ptr<LockedFile> file;HMODULE value=nullptr;std::wstring basename;explicit State(std::shared_ptr<LockedFile> f):file(std::move(f)){}};
static std::shared_ptr<LockedFile> qualified_file(const std::string& path,const std::string& expected){need(expected==qualified_fixture_sha,"MODULE_HASH_REJECT_BEFORE_LOAD fixture allowlist");return std::make_shared<LockedFile>(path);}
Module::Module(const std::string& path,const std::string& expected,Clock::time_point end):state_(std::make_shared<State>(qualified_file(path,expected))),pin_(new std::shared_ptr<State>(state_)){
    need(state_->file->sha(4*1048576,end)==expected,"MODULE_HASH_REJECT_BEFORE_LOAD");auto name=wide(state_->file->path());state_->basename=name.substr(name.find_last_of(L'\\')+1);need(!GetModuleHandleW(state_->basename.c_str()),"fixture basename preloaded");need(sole_main_thread(),"loader requires fresh sole-main fixture child");
    HMODULE h=LoadLibraryExW(name.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);need(h!=nullptr,"fixture LoadLibrary");state_->value=h;image_=std::shared_ptr<void>(state_,h);
    try{char loaded[1024]{};need(GetModuleFileNameA(h,loaded,1024)>0,"loaded module path");LockedFile association(loaded);need(association.id()==state_->file->id()&&association.path()==state_->file->path(),"loaded module association");}
    catch(...){(void)pin_.release();throw;}
}
Module::~Module(){if(state_->value)(void)pin_.release();} // incomplete/unknown producer lifetime: keep ALL file+image state
bool Module::release_fixture(){if(!state_->value)return true;if(state_.use_count()!=3)return false;if(!FreeLibrary(state_->value))return false;if(GetModuleHandleW(state_->basename.c_str()))return false;state_->value=nullptr;return true;}
bool Module::owns(void* expected,const void* p){HMODULE actual=nullptr;return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,static_cast<LPCSTR>(p),&actual)&&actual==expected;}
void write_manifest_new(const std::string& path,const std::string& bytes){need(bytes.size()<=8192,"manifest write bound");Handle h(CreateFileW(wide(path).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));need(h.value!=INVALID_HANDLE_VALUE,"manifest CREATE_NEW");DWORD n=0;need(WriteFile(h.value,bytes.data(),static_cast<DWORD>(bytes.size()),&n,nullptr)&&n==bytes.size()&&FlushFileBuffers(h.value),"manifest complete flush");}
}
#endif
