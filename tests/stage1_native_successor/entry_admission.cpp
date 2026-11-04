#include "entry_admission.hpp"
#include "native_contract.h"
#include <array>
#include <cstring>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <climits>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
namespace stage1_entry { namespace {
constexpr const char* keys[Count]={"format","intent","run","side","board","own","caller","target","issued","until","frontend","frontend_sha","bridge","bridge_sha","consumer","consumer_sha","package","namespace","prefix","domain"};
constexpr std::uint32_t K[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
std::uint32_t rr(std::uint32_t x,unsigned n){return(x>>n)|(x<<(32-n));}
struct Sha {
 std::uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};unsigned char b[64]{};std::uint64_t bytes=0;unsigned used=0;
 void block(){std::uint32_t w[64];for(unsigned i=0;i<16;++i)w[i]=(std::uint32_t(b[i*4])<<24)|(std::uint32_t(b[i*4+1])<<16)|(std::uint32_t(b[i*4+2])<<8)|b[i*4+3];for(unsigned i=16;i<64;++i)w[i]=w[i-16]+(rr(w[i-15],7)^rr(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(rr(w[i-2],17)^rr(w[i-2],19)^(w[i-2]>>10));
  auto a=h[0],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7],bb=h[1];for(unsigned i=0;i<64;++i){auto t=z+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+K[i]+w[i];auto u=(rr(a,2)^rr(a,13)^rr(a,22))+((a&bb)^(a&c)^(bb&c));z=g;g=f;f=e;e=d+t;d=c;c=bb;bb=a;a=t+u;}h[0]+=a;h[1]+=bb;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;}
 void add(const void* p,std::size_t n){auto* s=static_cast<const unsigned char*>(p);bytes+=n;while(n--){b[used++]=*s++;if(used==64){block();used=0;}}}
 void end(char out[65]){auto bits=bytes*8;b[used++]=128;if(used>56){while(used<64)b[used++]=0;block();used=0;}while(used<56)b[used++]=0;for(unsigned i=0;i<8;++i)b[56+i]=static_cast<unsigned char>(bits>>(56-i*8));block();static constexpr char hex[]="0123456789abcdef";for(unsigned i=0;i<32;++i){auto v=static_cast<unsigned char>(h[i/4]>>(24-(i%4)*8));out[2*i]=hex[v>>4];out[2*i+1]=hex[v&15];}out[64]=0;}
};
bool fail(const char*& why,const char* text){why=text;return false;}
bool hex(const char* s,std::size_t n){if(std::strlen(s)!=n)return false;for(std::size_t i=0;i<n;++i)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;return true;}
bool number(const char* s,std::uint64_t limit,std::uint64_t& out){if(!s[0])return false;out=0;for(;*s;++s){if(*s<'0'||*s>'9')return false;auto d=std::uint64_t(*s-'0');if(out>(limit-d)/10)return false;out=out*10+d;}return true;}
bool path(const char* s){return s[0]=='/'&&s[1]&&std::strlen(s)<PATH_MAX&&!std::strchr(s,'\\')&&!std::strstr(s,"//")&&!std::strstr(s,"/../")&&!std::strstr(s,"/./")&&std::strcmp(s+std::strlen(s)-1,"/")&&!(std::strlen(s)>=3&&!std::strcmp(s+std::strlen(s)-3,"/.."))&&!(std::strlen(s)>=2&&!std::strcmp(s+std::strlen(s)-2,"/."));}
bool token(const char* s,std::size_t max){if(!s[0]||std::strlen(s)>max)return false;for(;*s;++s)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='.'||*s=='_'||*s=='-'))return false;return true;}
bool stable(const struct stat&a,const struct stat&b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec;}
int open_file(const char* p,std::uint64_t max,struct stat& s){char resolved[PATH_MAX]{};if(!path(p)||!realpath(p,resolved)||std::strcmp(p,resolved))return -1;int fd=open(p,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;if(fstat(fd,&s)||!S_ISREG(s.st_mode)||s.st_size<1||std::uint64_t(s.st_size)>max){close(fd);return -1;}return fd;}
bool hash_file(const char* p,char out[65],const char*& why){struct stat a{},b{};int fd=open_file(p,4194304,a);if(fd<0)return fail(why,"ARTIFACT_FILE");Sha h;unsigned char bytes[4096];std::uint64_t total=0;bool ok=true;for(;;){auto n=read(fd,bytes,sizeof(bytes));if(n<0){ok=false;break;}if(!n)break;total+=std::uint64_t(n);if(total>4194304){ok=false;break;}h.add(bytes,std::size_t(n));}if(fstat(fd,&b)||!stable(a,b)||total!=std::uint64_t(a.st_size))ok=false;if(close(fd))ok=false;if(!ok)return fail(why,"ARTIFACT_CHANGED_OR_READ");h.end(out);return true;}
bool at(const Record&r,std::uint64_t now){return r.issued<=now&&now<r.until&&r.until>r.issued&&r.until-r.issued<=300;}
}
bool parse_admission(const char* data,std::size_t n,std::uint64_t now,Record& r,const char*& why){
 if(!data||!n||n>4096||data[n-1]!='\n')return fail(why,"RECORD_SIZE_EOL");for(std::size_t i=0;i<n;++i)if(data[i]!='\n'&&(static_cast<unsigned char>(data[i])<32||static_cast<unsigned char>(data[i])>126))return fail(why,"RECORD_ASCII");
 std::size_t p=0;Record v{};for(unsigned k=0;k<Count;++k){auto* e=static_cast<const char*>(std::memchr(data+p,'\n',n-p));if(!e)return fail(why,"RECORD_FIELD");auto len=std::size_t(e-(data+p));auto z=std::strlen(keys[k]);if(len<=z+1||std::memcmp(data+p,keys[k],z)||data[p+z]!='='||len-z-1>=sizeof(v.value[k]))return fail(why,"RECORD_ORDER_VALUE");std::memcpy(v.value[k],data+p+z+1,len-z-1);p+=len+1;}
 if(p!=n)return fail(why,"RECORD_EXTRA");if(std::strcmp(v.value[Format],"S1ENTRY1")||(std::strcmp(v.value[Intent],"inspect")&&std::strcmp(v.value[Intent],"own")&&std::strcmp(v.value[Intent],"traffic")))return fail(why,"FORMAT_INTENT");
 if(!hex(v.value[Run],16)||(std::strcmp(v.value[Side],"A")&&std::strcmp(v.value[Side],"B")))return fail(why,"RUN_SIDE");const char* board=v.value[Side][0]=='A'?"3e01ff55454d202020104033bf453b00":"3e01ff55454d202020104433991c3b00";if(std::strcmp(v.value[Board],board)||std::strcmp(v.value[Own],NS_ACTIONBAG)||std::strcmp(v.value[Package],NS_PACKAGE))return fail(why,"BOARD_OWN_PACKAGE");
 if(!number(v.value[Caller],UINT64_MAX,v.caller)||!v.caller||!number(v.value[Target],UINT32_MAX,v.target)||!v.target||!number(v.value[Issued],UINT64_MAX,v.issued)||!number(v.value[Until],UINT64_MAX,v.until)||!number(v.value[Domain],UINT32_MAX,v.domain)||!at(v,now))return fail(why,"NUMBER_TTL");
 if(!token(v.value[NameSpace],117)||!token(v.value[Prefix],96))return fail(why,"DEPLOYMENT_TEXT");for(unsigned i:{Frontend,Bridge,Consumer})if(!path(v.value[i])||!hex(v.value[i+1],64))return fail(why,"PATH_HASH_FORMAT");r=v;why="PARSED_DATA_ONLY";return true;
}
bool admission_current(const Record&r,const char*&why){auto now=std::time(nullptr);return now>=0&&at(r,std::uint64_t(now))?true:fail(why,"EXPIRED_OR_CLOCK");}
bool verify_admission(const char* file,const char* side,const char* image,Record& r,char digest[65],const char*& why){
 struct stat a{},b{};int fd=open_file(file,4096,a);if(fd<0)return fail(why,"RECORD_FILE");char buf[4097];std::size_t total=0;bool ok=true;while(total<sizeof(buf)){auto n=read(fd,buf+total,sizeof(buf)-total);if(n<0){ok=false;break;}if(!n)break;total+=std::size_t(n);}if(fstat(fd,&b)||!stable(a,b)||total!=std::size_t(a.st_size))ok=false;if(close(fd))ok=false;if(!ok)return fail(why,"RECORD_CHANGED_OR_READ");auto now=std::time(nullptr);if(now<0||!parse_admission(buf,total,std::uint64_t(now),r,why))return false;
 if(!side||std::strcmp(side,r.value[Side])||!image||std::strcmp(image,r.value[Frontend]))return fail(why,"SIDE_IMAGE_BINDING");Sha raw;raw.add(buf,total);raw.end(digest);
 for(unsigned i:{Frontend,Bridge,Consumer}){char h[65];if(!hash_file(r.value[i],h,why))return false;if(std::strcmp(h,r.value[i+1]))return fail(why,"ARTIFACT_HASH_MISMATCH");}
 if(!admission_current(r,why))return false;why="HASHED_DATA_ONLY_NOT_AUTHORIZATION";return true;
}
bool admission_selftest(){
 struct Vector{const char* text;const char* hash;};static constexpr Vector vectors[]={{"","e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},{"abc","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},{"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq","248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"}};
 bool ok=true;for(auto& v:vectors){Sha h;h.add(v.text,std::strlen(v.text));char d[65];h.end(d);ok=ok&&!std::strcmp(d,v.hash);}Sha million;char a[1000];std::memset(a,'a',sizeof(a));for(unsigned i=0;i<1000;++i)million.add(a,sizeof(a));char d[65];million.end(d);ok=ok&&!std::strcmp(d,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
 std::string v[Count]={"S1ENTRY1","inspect","0123456789abcdef","A","3e01ff55454d202020104033bf453b00",NS_ACTIONBAG,"1","2","100","200","/a","0000000000000000000000000000000000000000000000000000000000000000","/b","0000000000000000000000000000000000000000000000000000000000000000","/c","0000000000000000000000000000000000000000000000000000000000000000",NS_PACKAGE,"entry","com.kaihong.mdds.entry","151"};
 auto make=[&](){std::string s;for(unsigned i=0;i<Count;++i)s+=std::string(keys[i])+"="+v[i]+"\n";return s;};auto gold=make();Record r{};const char* why=nullptr;ok=ok&&parse_admission(gold.data(),gold.size(),150,r,why);
 for(const auto& bad:{gold+"domain=1\n",gold.substr(1),gold.substr(0,gold.size()-1),std::string(4097,'x')})ok=ok&&!parse_admission(bad.data(),bad.size(),150,r,why);
 for(unsigned i:{Own,Board,Caller,Target,Domain,Frontend,FrontendSha}){auto saved=v[i];v[i]=(i==Caller||i==Target||i==Domain)?"18446744073709551616":(i==Frontend?"/a/../b":"INVALID");auto bad=make();ok=ok&&!parse_admission(bad.data(),bad.size(),150,r,why);v[i]=saved;}
 ok=ok&&!parse_admission(gold.data(),gold.size(),200,r,why)&&!parse_admission(gold.data(),gold.size(),99,r,why);auto nul=gold;nul[3]=0;ok=ok&&!parse_admission(nul.data(),nul.size(),150,r,why);
 std::printf("ADMISSION_SELFTEST sha256_known_vectors_padding_multiblock=1 strict_parser_boundaries=1 result=%d no_identity_api=1 no_business_load=1\n",int(ok));return ok;
}
}
