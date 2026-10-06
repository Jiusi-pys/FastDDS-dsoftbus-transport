#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>
using namespace fastdds_dsoftbus::dsoftbus;
int main() {
    DSoftBusTransportDescriptor d(64);
    if(d.ConfigureV1([&]{V1Deployment input{{"host-variant",0,"synthetic-variant"},17,v1::Profile::BytesNative,"host.variant.ns",{}}; input.native_name_scope={v1::NativeNameContract::PackageScopedV1,"example.package","example.package.v1"};for(auto& peer:input.peers)peer.native_name_scope=input.native_name_scope;return input;}()).code!=DirectoryError::None)return 1;
    DSoftBusTransport transport(d);
    return !transport.init() && transport.mode()==FacadeMode::NativeUnavailable &&
           transport.last_error().error==FacadeError::NativeBackendUnavailable ? 0 : 2;
}
