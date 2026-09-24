#pragma once
#include "ExperimentalTransport.hpp"
#include "matrix/CaseTable.hpp"
#include <map>
#include <set>
namespace mdds::experiment {
struct MatrixDomainKeys {unsigned domain=0;dsoftbus::EndpointId local{},peer{};};
inline const matrix::CaseSpec* MatrixCase(const std::string& id){for(const auto& c:matrix::Cases())if(c.id==id)return &c;return nullptr;}
inline bool MatrixRunId(const std::string& run){
    if(run.size()!=16)return false;
    for(char c:run)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
inline bool ValidMatrixRequest(const std::string& id,const std::string& run,bool board_a,const std::string& directory){
    return MatrixCase(id)&&MatrixRunId(run)&&directory=="/data/local/tmp/mdds_matrix_"+run+(board_a?"_A":"_B");
}
inline bool BuildMatrixNativeConfigs(const std::string& id,const std::string& run,bool board_a,const Config& seed,
        const std::vector<MatrixDomainKeys>& keys,std::vector<Config>& output){
    const auto* spec=MatrixCase(id);if(!spec||!MatrixRunId(run))return false;
    std::map<unsigned,unsigned> locals,remotes;
    for(const auto& e:spec->endpoints){
        auto& group=e.board_a==board_a?locals:remotes;
        if(e.participant<1||e.participant>2)return false;
        const auto found=group.find(e.participant);
        if(found!=group.end()&&found->second!=e.domain)return false;
        group[e.participant]=e.domain;
    }
    if(locals.empty()||locals.size()>2||remotes.empty()||remotes.size()>2)return false;
    std::vector<Config> candidate;
    const auto slot=[](bool a,unsigned p){return 2*p-(a?1u:0u);};
    for(const auto& local:locals){
        const MatrixDomainKeys* identity=nullptr;
        for(const auto& k:keys)if(k.domain==local.second){if(identity)return false;identity=&k;}
        if(!identity||identity->local==identity->peer)return false;
        Config config=seed;config.run_id=run;config.instance_name=std::string(board_a?"A":"B")+std::to_string(local.first);
        config.additional_peers.clear();config.enable_rx_drop=false;
        const std::string space="exp-"+run;
        if(!dsoftbus::v1::AddressCodec::EncodeLocator({identity->local,slot(board_a,local.first),dsoftbus::v1::Profile::BytesNative},config.local_locator)||
           !EncodeNativeServiceName({space,local.second,slot(board_a,local.first),dsoftbus::v1::Profile::BytesNative},config.local_service))return false;
        unsigned peers=0;
        for(const auto& remote:remotes)if(remote.second==local.second){
            PeerConfig peer;peer.network_id=seed.peer_network_id;
            if(!dsoftbus::v1::AddressCodec::EncodeLocator({identity->peer,slot(!board_a,remote.first),dsoftbus::v1::Profile::BytesNative},peer.locator)||
               !EncodeNativeServiceName({space,local.second,slot(!board_a,remote.first),dsoftbus::v1::Profile::BytesNative},peer.service))return false;
            if(peers++==0){config.peer_locator=peer.locator;config.peer_service=peer.service;}
            else config.additional_peers.push_back(std::move(peer));
        }
        if(!peers||peers>2)return false;
        candidate.push_back(std::move(config));
    }
    output=std::move(candidate);return true;
}
}
