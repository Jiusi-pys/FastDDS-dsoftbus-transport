#include "../probes/SedpEndpointObservation.hpp"
#include <iostream>
#include <limits>
#include <string>
using namespace fastdds_dsoftbus::experiment;
std::vector<std::uint8_t> Message(bool little){
    std::vector<std::uint8_t> b(44,0);b[0]='R';b[1]='T';b[2]='P';b[3]='S';b[4]=2;b[5]=3;b[6]=1;b[7]=15;
    for(unsigned i=0;i<12;++i)b[8+i]=i+1;
    b[20]=0x15;b[21]=little?5:4;b[little?26:27]=16;b[34]=4;b[35]=0xc2;b[little?40:43]=1;
    b.insert(b.end(),{0,std::uint8_t(little?3:2),0,0});
    auto u16=[&](unsigned n){b.push_back(little?n&255:n>>8);b.push_back(little?n>>8:n&255);};
    u16(0x005a);u16(16);for(unsigned i=0;i<12;++i)b.push_back(i+1);b.insert(b.end(),{0,0,1,4});
    const std::string topic="rt/fdds_model/reliable/from_A";const auto length=topic.size()+1,aligned=(length+4+3)&~std::size_t(3);
    u16(5);u16(unsigned(aligned));for(unsigned j=0;j<4;++j)b.push_back(std::uint8_t(length>>(little?j*8:(3-j)*8)));
    b.insert(b.end(),topic.begin(),topic.end());b.push_back(0);while((b.size()-48)%4)b.push_back(0);
    u16(1);u16(0);const auto body=b.size()-24;b[22]=little?body&255:body>>8;b[23]=little?body>>8:body&255;return b;
}
int main(){
    for(bool little:{true,false}){auto bytes=Message(little);auto packet=ObserveRtps(bytes.data(),bytes.size());auto result=ObserveSedpEndpoints(bytes.data(),bytes.size(),packet);
        if(!result.valid||result.count!=1||!result.values[0].reader||std::string(result.values[0].topic.data())!="rt/fdds_model/reliable/from_A"||result.values[0].guid[15]!=4)return 1;
        for(std::size_t size=24;size<bytes.size();++size){const auto bad=ObserveSedpEndpoints(bytes.data(),size,packet);if(bad.valid)return 2;}
        auto bad=packet;bad.data[0].submessage_body_bytes=std::numeric_limits<std::size_t>::max();if(ObserveSedpEndpoints(bytes.data(),bytes.size(),bad).valid)return 3;
        bad=packet;bad.data[0].submessage_offset=std::numeric_limits<std::size_t>::max();if(ObserveSedpEndpoints(bytes.data(),bytes.size(),bad).valid)return 4;
        auto malformed=bytes;malformed[little?26:27]=15;if(ObserveSedpEndpoints(malformed.data(),malformed.size(),packet).valid)return 6;
        malformed=bytes;malformed[21]|=8;if(ObserveSedpEndpoints(malformed.data(),malformed.size(),packet).valid)return 7;
        malformed=bytes;malformed[malformed.size()-(little?2:1)]=4;if(ObserveSedpEndpoints(malformed.data(),malformed.size(),packet).valid)return 8;
        malformed=bytes;malformed.push_back(1);bad=packet;++bad.data[0].submessage_body_bytes;if(ObserveSedpEndpoints(malformed.data(),malformed.size(),bad).valid)return 9;
        bytes[49]=0xff; // corrupt parameter ID; required endpoint GUID must remain missing
        if(ObserveSedpEndpoints(bytes.data(),bytes.size(),packet).valid)return 5;
    }
    std::cout<<"PASS SEDP_ENDPOINT_METADATA_BOUNDED_ENDIAN_AND_TRUNCATION\n";
}
