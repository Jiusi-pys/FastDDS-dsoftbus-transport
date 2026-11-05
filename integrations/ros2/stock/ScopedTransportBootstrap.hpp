#pragma once
#include "TransportBootstrap.hpp"
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>

namespace fastdds_dsoftbus::stock_transport {
// Explicit selected-domain startup, before ordinary stock RMW/Factory startup.
// Ok means public QoS installation only, not native/domain readiness. Generic
// StockTransportBootstrap::Install still accepts a legacy unscoped descriptor.
inline BootstrapResult InstallStockTransport(StockTransportBootstrap& bootstrap,
        dsoftbus::experimental::Runtime& runtime, const dsoftbus::DSoftBusTransportDescriptor& config)
{
    if (bootstrap.installed()) return BootstrapResult::AlreadyInstalled;
    if (config.ValidateV1().code != dsoftbus::DirectoryError::None) return BootstrapResult::InvalidConfiguration;
    const auto domain = config.directory.identities.front().domain;
    auto descriptor = runtime.MakeStockDescriptor(config);
    if (!descriptor) return BootstrapResult::InvalidConfiguration;
    std::vector<fastdds_dsoftbus::binding::Locator_t> peers;
    peers.reserve(config.directory.routes.size());
    for (const auto& route : config.directory.routes) peers.push_back(route.key.remote_locator);
    return bootstrap.Install(domain, std::move(descriptor), config.default_user_unicast, peers);
}
}
