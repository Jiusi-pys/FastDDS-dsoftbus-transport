#include "native_contract.h"
#include <accesstoken_kit.h>
#include <token_setproc.h>
#include <cstring>
#include <vector>
static constexpr char cpp_abi[]=NS_STR(_LIBCPP_ABI_NAMESPACE);
static_assert(sizeof(cpp_abi)==4&&cpp_abi[2]=='h',"Use actual platform __h headers; no namespace repair");
static_assert(sizeof(NS_Observation)==208&&sizeof(NS_IdentityApi)==24,"C++ platform POD");
static int32_t query(uint32_t target,NS_Observation* out) {
    if(!out)return NS_BOUND;
    const int check=ns_header(&out->header,sizeof(*out));if(check)return check;
    NS_Observation o{};o.header=out->header;o.info_rc=-1;o.query_rc=-1;o.query_state=-1;
    using namespace OHOS::Security::AccessToken;
    o.native_type=TOKEN_NATIVE;o.normal_apl=APL_NORMAL;
    try {
        o.self=GetSelfTokenID();o.called|=16;
        o.lookup=AccessTokenKit::GetNativeTokenId(NS_ACTIONBAG);o.called|=1;
        if(!o.lookup||(target&&target!=o.lookup)){*out=o;return NS_OWNER;}
        o.type=AccessTokenKit::GetTokenTypeFlag(o.lookup);o.called|=2;
        NativeTokenInfo info{};o.info_rc=AccessTokenKit::GetNativeTokenInfo(o.lookup,info);o.called|=4;
        if(info.processName.size()>=sizeof(o.process_name)||info.processName.find('\0')!=std::string::npos){*out=o;return NS_BOUND;}
        std::memcpy(o.process_name,info.processName.data(),info.processName.size());o.apl=info.apl;
        std::vector<int32_t> states;
        o.query_rc=AccessTokenKit::VerifyAccessToken(o.lookup,std::vector<std::string>{"ohos.permission.DISTRIBUTED_DATASYNC"},states,true);o.called|=8;
        if(states.size()>UINT32_MAX){*out=o;return NS_BOUND;}
        o.query_count=static_cast<uint32_t>(states.size());if(states.size()==1)o.query_state=states[0];
        o.granted=o.info_rc==0&&info.processName==NS_ACTIONBAG&&o.apl==APL_NORMAL&&o.type==TOKEN_NATIVE&&o.query_rc==0&&states.size()==1&&states[0]==PERMISSION_GRANTED;
        std::memcpy(o.cpp_abi,cpp_abi,sizeof(cpp_abi));*out=o;return o.granted?NS_OK:NS_QUERY;
    } catch(...) {*out=o;return NS_QUERY;}
}
extern "C" NS_EXPORT int32_t ns_identity_get_api(uint32_t version,uint32_t capacity,NS_IdentityApi* out) {
    if(version!=NS_VERSION)return NS_ABI;if(capacity!=sizeof(NS_IdentityApi))return NS_SIZE;if(!out)return NS_BOUND;
    const NS_IdentityApi api={{NS_VERSION,sizeof(NS_IdentityApi),NS_SCOPE,0},query};*out=api;return NS_OK;
}
/* No setters/provisioning/Reload/Delete, constructors or C++ ownership escape. */
