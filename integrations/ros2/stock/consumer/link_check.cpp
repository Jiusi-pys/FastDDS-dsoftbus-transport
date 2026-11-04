// Installed header/link contract only. No Runtime, Factory, Participant or SDK call.
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>
#include <fastdds_dsoftbus/stock_transport/ScopedTransportBootstrap.hpp>
#include <cstdio>
#if !defined(FASTDDS_DSOFTBUS_FASTDDS_214) || FASTDDS_DSOFTBUS_FASTDDS_214 != 1
#error "Installed target must propagate the stock2.14 frontend"
#endif
#if !defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT) || FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT != 1
#error "Installed target must propagate the matching Transport layout"
#endif
using fastdds_dsoftbus::dsoftbus::experimental::Runtime;
using fastdds_dsoftbus::stock_transport::StockTransportBootstrap;
auto volatile stock_runtime_pump = &Runtime::PumpSend;
auto volatile stock_bootstrap_release = &StockTransportBootstrap::Release;
int main()
{
    std::printf("INSTALLED_CONSUMER runtime_size=%zu transport_size=%zu runtime_constructed=0 explicit_sdk_calls=0\n",
        sizeof(Runtime), sizeof(fastdds_dsoftbus::dsoftbus::DSoftBusTransport));
    return stock_runtime_pump == nullptr || stock_bootstrap_release == nullptr ? 1 : 0;
}
