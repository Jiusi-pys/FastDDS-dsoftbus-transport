#pragma once
// Explicit native deployment at the application/public-Transport boundary.
// No DDS/RTPS implementation or DSoftBus API is changed here.
#include "../../ros2/autoload/DeviceDeployment.hpp"
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fstream>
#include <map>
#include <sstream>
#include <iostream>
#include <chrono>
#include <regex>
namespace c4 {
namespace dd=fastdds_dsoftbus::device_deployment;
namespace ds=fastdds_dsoftbus::dsoftbus;
namespace fd=eprosima::fastdds::dds;
class Transport {
    std::map<std::string,std::string> options_;
    dd::PumpedRuntime runtime_;
    ds::DSoftBusTransportDescriptor config_{65536};
    std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface> descriptor_;
    bool closed_=false;
public:
    explicit Transport(const std::string& path) {
        std::ifstream input(path);dd::Require(bool(input),"transport configuration missing");
        std::string line;while(std::getline(input,line)) {
            if(line.empty())continue;auto at=line.find('=');
            dd::Require(at!=std::string::npos&&options_.emplace(line.substr(0,at),line.substr(at+1)).second,"invalid transport option");
        }
        const std::vector<std::string> required={"token","run","local_udid","peer_udid","peer_network","side","domain","gate","stop"};
        dd::Require(options_.size()==required.size(),"transport configuration fields invalid");
        for(const auto& key:required)dd::Require(options_.count(key)&&!options_.at(key).empty(),"transport configuration incomplete");
        auto matches=[&](const char* key,const char* pattern){return std::regex_match(options_.at(key),std::regex(pattern));};
        dd::Require(matches("token","[1-9][0-9]{0,19}")&&matches("run","[0-9a-f]{16}"),"token/run invalid");
        dd::Require(matches("local_udid","[0-9A-Fa-f]{64}")&&matches("peer_udid","[0-9A-Fa-f]{64}")&&matches("peer_network","[0-9a-f]{64}"),"peer identity invalid");
        dd::Require(matches("side","[01]")&&(options_.at("domain")=="219"||options_.at("domain")=="223"),"native profile invalid");
        dd::Require(options_.at("gate").rfind("/data/local/tmp/",0)==0&&options_.at("stop").rfind("/data/local/tmp/",0)==0,"native control path invalid");
        dd::SetSelfToken(options_.at("token"));
        auto deployment=dd::MakeDeploy("com.kaihong.mdds","c4.web",std::stoul(options_.at("side")),options_.at("run"),std::stoul(options_.at("domain")),options_.at("local_udid"),options_.at("peer_udid"),options_.at("peer_network"));
        config_=dd::Configuration(deployment,true);
        runtime_.rt=std::make_unique<ds::experimental::Runtime>(dd::Backend("com.kaihong.mdds",config_),ds::experimental::RuntimeLifetime::PersistentNativeAnchor);
        auto gate=options_.at("gate"),stop=options_.at("stop");
        dd::Require(runtime_.rt->SetPreConnectGate([gate,stop] {
            std::cout<<"C4_LISTENING\n"<<std::flush;
            auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            while(std::chrono::steady_clock::now()<end){if(std::ifstream(stop).good())return false;if(std::ifstream(gate).good())return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));}return false;
        }),"native gate rejected");
        // The actual public DID callback scopes each view into the existing
        // bounded domain circuits. MakeStockDescriptor would pin one DID.
        descriptor_=runtime_.rt->MakeDescriptor(config_);dd::Require(bool(descriptor_),"native descriptor rejected");
    }
    Transport(const Transport&)=delete;
    ~Transport(){if(!closed_){std::cerr<<"C4_OWNER_NOT_CLOSED\n"<<std::flush;std::_Exit(1);}}
    bool healthy() const {return !runtime_.failed.load();}
    bool stopping() const {return std::ifstream(options_.at("stop")).good();}
    void configure(unsigned domain,fd::DomainParticipantQos& q) {
        dd::Require(healthy()&&(domain==219||domain==223),"native transport unavailable/domain outside profile");
        q.transport().use_builtin_transports=false;q.transport().user_transports={descriptor_};q.transport().max_msg_size_no_frag=config_.maxMessageSize;
        auto& w=q.wire_protocol();w.builtin.metatrafficUnicastLocatorList.clear();w.builtin.metatrafficUnicastLocatorList.push_back(config_.default_user_unicast.at(0));
        w.builtin.metatrafficMulticastLocatorList.clear();w.builtin.initialPeersList.clear();w.builtin.initialPeersList.push_back(config_.directory.routes.at(0).key.remote_locator);
        w.default_unicast_locator_list.clear();w.default_unicast_locator_list.push_back(config_.default_user_unicast.at(0));w.default_multicast_locator_list.clear();
    }
    void start() {
        runtime_.start();
        auto* factory=fd::DomainParticipantFactory::get_instance();fd::DomainParticipantQos q;configure(std::stoul(options_.at("domain")),q);
        auto* anchor=factory->create_participant(std::stoul(options_.at("domain")),q);dd::Require(anchor!=nullptr,"native bootstrap participant");
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        dd::Require(factory->delete_participant(anchor)==eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK,"bootstrap cleanup");
#else
        dd::Require(factory->delete_participant(anchor)==fd::RETCODE_OK,"bootstrap cleanup");
#endif
        dd::Require(healthy(),"native bootstrap failed");std::cout<<"C4_TRANSPORT_READY builtin=0 udp=0 shm=0\n"<<std::flush;
    }
    bool close() {
        if(closed_)return true;
        runtime_.rt->RequestPumpStop();if(runtime_.pump.joinable())runtime_.pump.join();descriptor_.reset();
        bool result=runtime_.rt->Close();
        std::cout<<"C4_PUMP healthy="<<(healthy()?1:0)<<" source="<<runtime_.failure_source.load()<<" value="<<runtime_.failure_value.load()<<"\n"<<std::flush;
        std::cout<<"C4_SOCKET sdk_sends="<<runtime_.sdk_sends.load()<<" received_records="<<runtime_.received_records.load()<<" close="<<(result?1:0)<<"\n"<<std::flush;
        if(result)closed_=true;return result;
    }
};
}
