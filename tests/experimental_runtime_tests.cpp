#include <mdds/dsoftbus/ExperimentalRuntime.hpp>
#include "FakePublicSocketLink.hpp"
#include <cassert>
#include <iostream>
#include <thread>

using namespace mdds::dsoftbus;
Locator local(unsigned side) { EndpointId id{}; id[0]=10; id[15]=static_cast<std::uint8_t>(side+1); Locator l; if(!LocatorCodec::Encode(id,l)) std::abort(); return l; }
phase0::PublicSocketConfiguration config(unsigned side) {
    const std::string me=side?"host.public.B.":"host.public.A.", peer=side?"host.public.A.":"host.public.B.";
    phase0::PublicSocketConfiguration c;
    c.identity={me+"outgoing",{910+side*4},{1010+side*4}};
    c.listener_identity={me+"listener",{911+side*4},{1011+side*4}};
    c.package_name="host.public.dds";c.peer_name=peer+"listener";c.peer_network_id=side?"model.net.A":"model.net.B";
    c.listen_qos={{0,80},{1,2000}};c.bind_qos=c.listen_qos;c.max_message_size=8192;return c;
}
DSoftBusTransportDescriptor descriptor(unsigned side) {
    auto c=config(side);DSoftBusTransportDescriptor d(8192);
    d.directory.locals={{local(side),"library.test",c.listener_identity.service_name}};
    d.directory.routes={{{local(side),local(1-side)},{c.peer_network_id,c.peer_name,c.identity.service_name,{config(1-side).identity.service_name},RecordDataType::Bytes},1}};
    d.default_metatraffic_unicast={local(side)};d.default_user_unicast={local(side)};return d;
}
int main() {
    auto trace=std::make_shared<host_dds::Trace>();public_socket_fake::Configure(trace,true);
    experimental::Runtime a(config(0)),b(config(1));
    auto da=a.MakeDescriptor(descriptor(0)),db=b.MakeDescriptor(descriptor(1));
    if(!da||!db||a.MakeDescriptor(descriptor(0)))return 1;
    std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> ta(da->create_transport()),tb(db->create_transport());
    if(!ta||!tb||da->create_transport())return 2;
    bool aa=false,bb=false;std::thread x([&]{aa=ta->init();}),y([&]{bb=tb->init();});x.join();y.join();
    if(!aa||!bb)return 3;
    if(a.ObserveSocket().fake_sdk||b.ObserveSocket().fake_sdk)return 4; // actual real shim, explicitly Fake C boundary in this test only
    ta->shutdown();tb->shutdown();ta.reset();tb.reset();
    if(!a.Close()||!b.Close())return 5;
    const auto s=public_socket_fake::Observe();if(s.live||s.queued||s.bytes||s.invalid)return 6;
    std::cout<<"ACTUAL_ASSEMBLY_WITH_TEST_ONLY_FAKE_C; ONE_SHOT_AND_CLOSE_PASS\n";
}
