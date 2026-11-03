#pragma once
#include "v2_client.hpp"
#include <array>
#include <string>
namespace stage1_control {
enum DeployField {DFormat,DPurpose,DRun,DParent,DImage,DImageSha,DModule,DModuleSha,DNamespace,DDomain,DPackage,DPrefix,DBudget,APid,ABoard,AToken,AUdid,ANetwork,ASlot,AInitiator,BPid,BBoard,BToken,BUdid,BNetwork,BSlot,BInitiator,DCount};
struct Deployment {
    std::array<std::string,DCount> fields;
    std::string encode()const;
    static Deployment parse(const std::string&);
    void validate()const;
    NS_RunRequest request(unsigned side,const std::string& manifest_sha,Clock::time_point end)const;
    uint64_t number(DeployField,uint64_t max=UINT64_MAX)const;
    // Preparation only: this purpose cannot authorize a loader or identity.
    std::string encode_native()const;
    static Deployment parse_native(const std::string&);
    void validate_native()const;
    NS_RunRequest request_native(unsigned,const std::string&,Clock::time_point)const;
    std::string encode_os()const;
    static Deployment parse_os(const std::string&);
    void validate_os()const;
    NS_RunRequest request_os(unsigned,const std::string&,Clock::time_point)const;
private:
    void validate_for(unsigned native)const;
    std::string encode_for(unsigned native)const;
    static Deployment parse_for(const std::string&,unsigned native);
    NS_RunRequest request_for(unsigned,const std::string&,Clock::time_point,unsigned native)const;
};
}
