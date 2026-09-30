#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>
#include <fastdds/utils/IPLocator.hpp>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#if !defined(FASTDDS_DYN_LINK) || defined(FASTDDS_STATIC_LINK)
#error The facade consumer must inherit the actual shared Fast DDS binding.
#endif

int main()
try
{
using namespace fastdds_dsoftbus::dsoftbus;
    namespace rtps = eprosima::fastdds::rtps;
    const auto check = [](bool condition, const char* reason) {
        if (!condition) throw std::runtime_error(reason);
    };

    // Force an actual out-of-line upstream call returning a std::string across
    // the library ABI. This fixture value is not a route or networking action.
    Locator ip;
    rtps::IPLocator::setIPv4(ip, 127, 0, 0, 1);
    check(rtps::IPLocator::toIPv4string(ip) == "127.0.0.1", "upstream string ABI");
#ifdef _WIN32
    wchar_t module_path[32768]{};
    const auto module = GetModuleHandleW(L"fastdds-3.6.dll");
    check(module != nullptr, "loaded Fast DDS module");
    const auto length = GetModuleFileNameW(module, module_path, 32768);
    check(length > 0 && length < 32768, "loaded Fast DDS path");
    const auto count = WideCharToMultiByte(CP_UTF8, 0, module_path,
            static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    check(count > 0, "loaded path UTF-8 size");
    std::string path(static_cast<std::size_t>(count), '\0');
    check(WideCharToMultiByte(CP_UTF8, 0, module_path, static_cast<int>(length),
            &path[0], count, nullptr, nullptr) == count, "loaded path UTF-8 conversion");
    std::cout << "FACADE_CONSUMER_LOADED_FASTDDS=" << path << '\n';
#else
    Dl_info loaded{};
    check(dladdr(reinterpret_cast<const void*>(&rtps::IPLocator::toIPv4string), &loaded) != 0 &&
            loaded.dli_fname != nullptr, "loaded Fast DDS shared object path");
    check(std::string(loaded.dli_fname).find("libfastdds") != std::string::npos,
            "resolved symbol must belong to Fast DDS shared object");
    std::cout << "FACADE_CONSUMER_LOADED_FASTDDS=" << loaded.dli_fname << '\n';
#endif

    EndpointId local_id{}, remote_id{};
    local_id.back() = 2;
    remote_id.back() = 10;
    Locator local, remote;
    check(LocatorCodec::Encode(local_id, local) && LocatorCodec::Encode(remote_id, remote),
            "canonical fixture locators");
    DSoftBusTransportDescriptor descriptor(4096);
    descriptor.directory.locals.push_back({local, "facade.consumer.namespace", "facade.consumer.listen"});
    Route route;
    route.key = {local, remote};
    route.route_version = 1;
    route.fields = {"facade-consumer-network", "facade.remote.listen", "facade.consumer.dial",
            {"facade.remote.dial"}, RecordDataType::Bytes};
    descriptor.directory.routes.push_back(route);
    descriptor.default_metatraffic_unicast.push_back(local);
    descriptor.default_user_unicast.push_back(local);
    const rtps::TransportDescriptorInterface& base_descriptor = descriptor;
    check(base_descriptor.min_send_buffer_size() == 4096, "descriptor bound");
    {
        std::unique_ptr<rtps::TransportInterface> first(base_descriptor.create_transport());
        std::unique_ptr<rtps::TransportInterface> second(base_descriptor.create_transport());
        check(first && second && first.get() != second.get(), "independent factory objects");
        for (auto* base_transport : {first.get(), second.get()}) {
            auto* concrete = dynamic_cast<DSoftBusTransport*>(base_transport);
            check(concrete && concrete->mode() == FacadeMode::NativeUnavailable, "production factory mode");
            check(!base_transport->init(), "native unavailable init must fail");
            check(concrete->state() == FacadeState::Failed &&
                    concrete->last_error().error == FacadeError::NativeBackendUnavailable,
                    "valid configuration must reach native unavailable boundary");
        }
    } // Both actual base-owned instances are destroyed before the PASS record.
    std::cout << "FACADE_CONSUMER PASS factory_distinct=1 base_init=1 destruction=1 native_unavailable=1 communication_tested=0\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "FACADE_CONSUMER FAIL " << error.what() << '\n';
    return 1;
}
