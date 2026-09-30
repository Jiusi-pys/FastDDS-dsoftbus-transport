#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>
using namespace fastdds_dsoftbus::dsoftbus;
int main() {
    EndpointId id{};id[0]=10;id[14]=127;id[15]=2;Locator local;
    if(!LocatorCodec::Encode(id,local))return 1;
    DSoftBusTransportDescriptor d(64);
    d.directory.locals={{local,"host.variant.ns","host.variant.listen"}};
    d.default_metatraffic_unicast={local};d.default_user_unicast={local};
    DSoftBusTransport transport(d);
    return !transport.init() && transport.mode()==FacadeMode::NativeUnavailable &&
           transport.last_error().error==FacadeError::NativeBackendUnavailable ? 0 : 2;
}
