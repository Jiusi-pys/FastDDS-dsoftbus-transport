#pragma once
#include "common.hpp"
namespace d4 {
struct Service{unsigned request,response;};
// Versioned D4 profile operation identifiers, not OMG DDS-RPC generated interfaces.
inline const Service* service(unsigned operation){
    static const Service services[]={
        {UA_TYPES_BROWSEREQUEST,UA_TYPES_BROWSERESPONSE},
        {UA_TYPES_BROWSENEXTREQUEST,UA_TYPES_BROWSENEXTRESPONSE},
        {UA_TYPES_TRANSLATEBROWSEPATHSTONODEIDSREQUEST,UA_TYPES_TRANSLATEBROWSEPATHSTONODEIDSRESPONSE},
        {UA_TYPES_REGISTERNODESREQUEST,UA_TYPES_REGISTERNODESRESPONSE},
        {UA_TYPES_UNREGISTERNODESREQUEST,UA_TYPES_UNREGISTERNODESRESPONSE},
        {UA_TYPES_QUERYFIRSTREQUEST,UA_TYPES_QUERYFIRSTRESPONSE},
        {UA_TYPES_QUERYNEXTREQUEST,UA_TYPES_QUERYNEXTRESPONSE},
        {UA_TYPES_READREQUEST,UA_TYPES_READRESPONSE},
        {UA_TYPES_WRITEREQUEST,UA_TYPES_WRITERESPONSE},
        {UA_TYPES_HISTORYREADREQUEST,UA_TYPES_HISTORYREADRESPONSE},
        {UA_TYPES_HISTORYUPDATEREQUEST,UA_TYPES_HISTORYUPDATERESPONSE},
        {UA_TYPES_CALLREQUEST,UA_TYPES_CALLRESPONSE}
    };
    return operation>=1&&operation<=12?&services[operation-1]:nullptr;
}
inline void invoke(UA_Client* client,unsigned op,void* req,void* response){
    auto* descriptor=service(op);check(descriptor,"D4 service operation");
    // All selected request structures begin with RequestHeader, all responses with ResponseHeader.
    auto* header=static_cast<UA_RequestHeader*>(req);
    UA_NodeId_clear(&header->authenticationToken);header->timeoutHint=1000;
    #define D4_INVOKE(id,Type,name) case id:*static_cast<UA_##Type##Response*>(response)=UA_Client_Service_##name(client,*static_cast<UA_##Type##Request*>(req));break
    switch(op){
        D4_INVOKE(1,Browse,browse);D4_INVOKE(2,BrowseNext,browseNext);
        D4_INVOKE(3,TranslateBrowsePathsToNodeIds,translateBrowsePathsToNodeIds);
        D4_INVOKE(4,RegisterNodes,registerNodes);D4_INVOKE(5,UnregisterNodes,unregisterNodes);
        D4_INVOKE(6,QueryFirst,queryFirst);D4_INVOKE(7,QueryNext,queryNext);
        D4_INVOKE(8,Read,read);D4_INVOKE(9,Write,write);
        D4_INVOKE(10,HistoryRead,historyRead);D4_INVOKE(11,HistoryUpdate,historyUpdate);D4_INVOKE(12,Call,call);
    }
    #undef D4_INVOKE
}
}
