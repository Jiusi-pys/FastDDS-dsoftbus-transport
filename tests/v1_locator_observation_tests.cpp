#include "../probes/V1LocatorObservation.hpp"
#include <vector>
#include <stdexcept>
#include <iostream>
using namespace fastdds_dsoftbus::experiment;
using Bytes=std::vector<std::uint8_t>;
void require(bool value){if(!value)throw std::runtime_error("SPDP locator assertion");}
void word(Bytes& b,unsigned value,unsigned n,bool le){for(unsigned j=0;j<n;++j)b.push_back(std::uint8_t(value>>(8*(le?j:n-1-j))));}
Bytes packet(bool sub_le,bool payload_le,bool qos=false,unsigned repeats=1){
 Bytes b{'R','T','P','S',2,3,1,15};for(unsigned j=0;j<12;++j)b.push_back(std::uint8_t(j+1));
 Bytes body;word(body,0,2,sub_le);word(body,16,2,sub_le);word(body,0,4,sub_le);body.insert(body.end(),{0,1,0,0xc2});word(body,0,4,sub_le);word(body,7,4,sub_le);
 if(qos){word(body,0x70,2,sub_le);word(body,1,2,sub_le);body.insert(body.end(),{4,0,0,0});word(body,1,2,sub_le);word(body,0,2,sub_le);}
 body.insert(body.end(),{0,std::uint8_t(payload_le?3:2),0,0});
 for(unsigned r=0;r<repeats;++r)for(unsigned pid:{0x31u,0x32u}) {word(body,pid,2,payload_le);word(body,24,2,payload_le);word(body,0x40000000,4,payload_le);word(body,0x01000001,4,payload_le);for(unsigned j=0;j<16;++j)body.push_back(std::uint8_t(j+1));}
 word(body,1,2,payload_le);word(body,0,2,payload_le);
 b.push_back(0x15);b.push_back(std::uint8_t((sub_le?1:0)|4|(qos?2:0)));word(b,unsigned(body.size()),2,sub_le);b.insert(b.end(),body.begin(),body.end());return b;
}
int main(){try{
 for(bool sub:{false,true})for(bool payload:{false,true})for(bool qos:{false,true}) {
  auto b=packet(sub,payload,qos);const auto out=ObserveSpdpLocators(b.data(),b.size());require(out.valid&&out.count==2&&!out.overflow);
  require(out.values[0].pid==0x31&&out.values[1].pid==0x32&&out.values[0].serialized_bytes==24&&out.values[1].serialized_bytes==24);
  require(out.values[0].kind==0x40000000&&out.values[0].port==0x01000001&&out.values[0].writer[12]==0&&out.values[0].writer[13]==1&&out.values[0].writer[15]==0xc2&&out.values[0].sequence_low==7);
  require(out.values[0].address==out.values[1].address&&out.values[0].writer==out.values[1].writer);
  require(out.values[0].data_submessage_offset==20&&out.values[0].data_submessage_offset==out.values[1].data_submessage_offset);
  for(std::size_t n=0;n<20;++n)require(!ObserveSpdpLocators(b.data(),n).valid);
  for(std::size_t n=21;n<b.size();++n)require(!ObserveSpdpLocators(b.data(),n).valid);
 }
 auto duplicate=packet(true,true,false,2);duplicate[24+20+4+28+4+8]^=1;auto out=ObserveSpdpLocators(duplicate.data(),duplicate.size());require(out.valid&&out.count==4);require(out.values[0].address!=out.values[1].address);
 require(out.values[0].data_submessage_offset==out.values[1].data_submessage_offset&&out.values[0].data_submessage_offset==out.values[2].data_submessage_offset);
 auto user_only=packet(true,true);user_only.erase(user_only.begin()+76,user_only.begin()+104);user_only[22]=std::uint8_t(user_only.size()-24);
 auto meta_only=packet(true,true);meta_only.erase(meta_only.begin()+48,meta_only.begin()+76);meta_only[22]=std::uint8_t(meta_only.size()-24);
 user_only.insert(user_only.end(),meta_only.begin()+20,meta_only.end());out=ObserveSpdpLocators(user_only.data(),user_only.size());
 require(out.valid&&out.count==2&&out.values[0].pid==0x31&&out.values[1].pid==0x32&&out.values[0].writer==out.values[1].writer&&out.values[0].sequence_low==out.values[1].sequence_low&&out.values[0].data_submessage_offset!=out.values[1].data_submessage_offset);
 auto overflow=packet(true,true,false,17);out=ObserveSpdpLocators(overflow.data(),overflow.size());require(!out.valid&&out.overflow);
 auto b=packet(true,true);b[21]&=std::uint8_t(~4);out=ObserveSpdpLocators(b.data(),b.size());require(out.valid&&out.count==0);
 b=packet(true,true);b[21]|=8;require(!ObserveSpdpLocators(b.data(),b.size()).valid);
 b=packet(true,true);b[24+20+4+2]=23;require(!ObserveSpdpLocators(b.data(),b.size()).valid);
 b=packet(true,true);b[b.size()-2]=1;require(!ObserveSpdpLocators(b.data(),b.size()).valid);
 b=packet(true,true);b[b.size()-4]=0;require(!ObserveSpdpLocators(b.data(),b.size()).valid);
 b=packet(true,true);b[26]=15;require(!ObserveSpdpLocators(b.data(),b.size()).valid);
 b=packet(true,true);b[44]=0;b[45]=1;out=ObserveSpdpLocators(b.data(),b.size());require(!out.valid&&out.unsupported);
 b=packet(true,true);b[20]=0x16;out=ObserveSpdpLocators(b.data(),b.size());require(!out.valid&&out.unsupported);
 b=packet(true,true);b[22]=b[23]=0;require(ObserveSpdpLocators(b.data(),b.size()).valid);
 b=packet(true,true);Bytes info{0x0c,1,20,0};info.insert(info.end(),8,0);info.insert(info.end(),12,0xa7);b.insert(b.begin()+20,info.begin(),info.end());out=ObserveSpdpLocators(b.data(),b.size());require(out.valid&&out.values[0].writer[0]==0xa7);
 std::cout<<"SPDP_LOCATOR_OBSERVATION PASS synthetic_only=1\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
