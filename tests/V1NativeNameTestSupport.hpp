#pragma once
#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransportDescriptor.hpp>
inline fastdds_dsoftbus::dsoftbus::V1Deployment ScopedHostDeployment(fastdds_dsoftbus::dsoftbus::V1Deployment input, const char* package="host.public.dds", const char* prefix="host.public.dds.v1") {
    input.native_name_scope={fastdds_dsoftbus::dsoftbus::v1::NativeNameContract::PackageScopedV1,package,prefix};
    for(auto& peer:input.peers)peer.native_name_scope=input.native_name_scope;
    return input;
}
