#pragma once
// Exact project-private SHA algorithm reused from entry_admission.cpp.
// No OS provider, DLL, identity or business operation.
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
namespace stage1_control::pure_hash {
constexpr std::uint32_t K[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
std::uint32_t rr(std::uint32_t x,unsigned n){return(x>>n)|(x<<(32-n));}
struct Sha {
 std::uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};unsigned char b[64]{};std::uint64_t bytes=0;unsigned used=0;
 void block(){std::uint32_t w[64];for(unsigned i=0;i<16;++i)w[i]=(std::uint32_t(b[i*4])<<24)|(std::uint32_t(b[i*4+1])<<16)|(std::uint32_t(b[i*4+2])<<8)|b[i*4+3];for(unsigned i=16;i<64;++i)w[i]=w[i-16]+(rr(w[i-15],7)^rr(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(rr(w[i-2],17)^rr(w[i-2],19)^(w[i-2]>>10));
  auto a=h[0],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7],bb=h[1];for(unsigned i=0;i<64;++i){auto t=z+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+K[i]+w[i];auto u=(rr(a,2)^rr(a,13)^rr(a,22))+((a&bb)^(a&c)^(bb&c));z=g;g=f;f=e;e=d+t;d=c;c=bb;bb=a;a=t+u;}h[0]+=a;h[1]+=bb;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;}
 void add(const void* p,std::size_t n){auto* s=static_cast<const unsigned char*>(p);bytes+=n;while(n--){b[used++]=*s++;if(used==64){block();used=0;}}}
 void end(char out[65]){auto bits=bytes*8;b[used++]=128;if(used>56){while(used<64)b[used++]=0;block();used=0;}while(used<56)b[used++]=0;for(unsigned i=0;i<8;++i)b[56+i]=static_cast<unsigned char>(bits>>(56-i*8));block();static constexpr char hex[]="0123456789abcdef";for(unsigned i=0;i<32;++i){auto v=static_cast<unsigned char>(h[i/4]>>(24-(i%4)*8));out[2*i]=hex[v>>4];out[2*i+1]=hex[v&15];}out[64]=0;}
};
inline std::string digest(const void* bytes,std::size_t n){Sha s;s.add(bytes,n);char result[65]{};s.end(result);return result;}
}
