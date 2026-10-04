#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>
using namespace fastdds_dsoftbus::dsoftbus;
int main() {
    DSoftBusTransportDescriptor d(64);
    if(d.ConfigureV1({{"host-variant",0,"synthetic-variant"},17,v1::Profile::BytesNative,"host.variant.ns",{}}).code!=DirectoryError::None)return 1;
    DSoftBusTransport transport(d);
    return !transport.init() && transport.mode()==FacadeMode::NativeUnavailable &&
           transport.last_error().error==FacadeError::NativeBackendUnavailable ? 0 : 2;
}
