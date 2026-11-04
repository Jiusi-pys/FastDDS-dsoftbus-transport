#pragma once
#include "ExperimentalTransport.hpp"
namespace fastdds_dsoftbus::experiment {
// Dedicated keyed fixture admission; it does not inspect or route by business key.
inline bool NativeKeyedCase(const std::string& id) {
    for(const char* allowed:{"K1","K2","K3","K4_TRUE","K4_FALSE","H1","H2","H3","R2","R3"})
        if(id==allowed)return true;
    return false; // R1 is explicitly a host-only public API consistency test.
}
inline bool KeyedHex(const std::string& value,std::size_t size) {
    if(value.size()!=size)return false;
    for(char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
inline bool ValidKeyedRequest(const std::string& id,const std::string& run,bool board_a,const std::string& directory) {
    return NativeKeyedCase(id)&&KeyedHex(run,16)&&directory=="/data/local/tmp/mdds_keyed_"+run+(board_a?"_A":"_B");
}
inline bool BuildKeyedNativeConfig(const std::string& id,const std::string& run,bool board_a,const Config& seed,Config& output) {
    if(!NativeKeyedCase(id)||!KeyedHex(run,16)||seed.run_id!=run||seed.enable_rx_drop||
            !seed.additional_peers.empty()||seed.max_message_size!=60000||!KeyedHex(seed.peer_network_id,64))return false;
    dsoftbus::v1::EndpointAddress local,peer;
    if(!dsoftbus::v1::AddressCodec::DecodeLocator(seed.local_locator,local)||
            !dsoftbus::v1::AddressCodec::DecodeLocator(seed.peer_locator,peer)||
            local.profile!=dsoftbus::v1::Profile::BytesNative||peer.profile!=dsoftbus::v1::Profile::BytesNative||
            local.slot!=(board_a?1u:2u)||peer.slot!=(board_a?2u:1u)||local.device_key==peer.device_key)return false;
    std::string local_service,peer_service;
    if(!EncodeNativeServiceName({"exp-"+run,73,local.slot,dsoftbus::v1::Profile::BytesNative},local_service)||
            !EncodeNativeServiceName({"exp-"+run,73,peer.slot,dsoftbus::v1::Profile::BytesNative},peer_service)||
            seed.local_service!=local_service||seed.peer_service!=peer_service)return false;
    Config candidate=seed;candidate.instance_name=board_a?"A1":"B1";
    output=std::move(candidate);return true;
}
}
