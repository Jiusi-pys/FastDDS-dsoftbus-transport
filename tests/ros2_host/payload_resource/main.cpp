// Ordinary, unmodified RMW subscriptions exercise the product pool. This bounded
// local test app observes counters, never implements a pool or rejects allocation.
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include "ReaderPayloadGuard.hpp"
#define NOMINMAX
#include <windows.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
using namespace std::chrono_literals;
using fastdds_dsoftbus::payload_resource::Snapshot;
Snapshot observation(const char* phase) {
    auto module=GetModuleHandleA("fastrtps-2.14.dll");
    auto get=reinterpret_cast<int(*)(Snapshot*)>(GetProcAddress(module,"FastddsDsoftbusPayloadSnapshot"));
    Snapshot s;if(!get||get(&s))throw std::runtime_error("actual protected DDS snapshot unavailable");
    std::cout<<"RESOURCE_SNAPSHOT phase="<<phase<<" active="<<s.active_bytes<<" retained="<<s.retained_bytes
        <<" cached="<<s.cached_payload_bytes<<" records="<<s.live_records<<" calls="<<s.allocator_calls
        <<" M_reject="<<s.size_rejections<<" B_reject="<<s.budget_rejections<<" N_reject="<<s.count_rejections
        <<" request="<<s.last_request_bytes<<" charge="<<s.last_charge_bytes<<" reason="<<s.last_rejection
        <<" high="<<s.high_water_bytes<<" frees="<<s.final_frees<<" bad_release="<<s.invalid_releases<<std::endl;
    return s;
}
bool signal(const std::string& root,const char* name) {std::ifstream file(root+"/"+name);std::string value;return std::getline(file,value)&&value==name;}
bool wait(const std::string& root,const char* name,const std::shared_ptr<rclcpp::Node>& node) {
    const auto end=std::chrono::steady_clock::now()+20s;
    while(std::chrono::steady_clock::now()<end){rclcpp::spin_some(node);if(signal(root,name))return true;std::this_thread::sleep_for(2ms);}return false;
}
int main(int argc,char** argv) {
    if(argc!=6)return 2;
    const std::string side=argv[1],run=argv[2],scene=argv[3],root=argv[4];const auto m=std::stoul(argv[5]);
    if((side!="A"&&side!="B")||m<64||m>131072||(scene!="boundary"&&scene!="huge"&&scene!="count"&&scene!="budget"))return 2;
    bool okay=false;
    try {
        rclcpp::init(1,argv);
        {
            auto node=std::make_shared<rclcpp::Node>("transport_"+side,"/fdds_"+run,rclcpp::NodeOptions().use_intra_process_comms(false));
            auto reliable=rclcpp::QoS(rclcpp::KeepLast(10)).reliable().durability_volatile();
            auto best=rclcpp::QoS(rclcpp::KeepLast(10)).best_effort().durability_volatile();
            std::map<char,unsigned> received;
            auto take=[&](const std_msgs::msg::String& value){
                if(value.data.empty())throw std::runtime_error("empty fixture");
                ++received[value.data[0]];std::cout<<"RESOURCE_TAKE label="<<value.data[0]<<" string_bytes="<<value.data.size()<<std::endl;
            };
            rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pr,pb;
            if(side=="A"){pr=node->create_publisher<std_msgs::msg::String>("reliable/from_A",reliable);pb=node->create_publisher<std_msgs::msg::String>("best_effort/from_A",best);}
            rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sr,sb;
            if(side=="B"){sr=node->create_subscription<std_msgs::msg::String>("reliable/from_A",reliable,take);sb=node->create_subscription<std_msgs::msg::String>("best_effort/from_A",best,take);}
            const auto end=std::chrono::steady_clock::now()+20s;
            auto matched=[&]{return side=="A"?(pr->get_subscription_count()&&pb->get_subscription_count()):(sr->get_publisher_count()&&sb->get_publisher_count());};
            while(std::chrono::steady_clock::now()<end){rclcpp::spin_some(node);if(matched())break;std::this_thread::sleep_for(2ms);}
            if(!matched())throw std::runtime_error("authenticated endpoint matching missing");
            std::cout<<"RESOURCE_MATCHED side="<<side<<" reliable_and_best=1"<<std::endl;
            auto start=observation("START");std::cout<<"RESOURCE_READY side="<<side<<std::endl;
            if(!wait(root,"START",node))throw std::runtime_error("start control missing");
            if(side=="A") {
                auto send=[&](char label,std::size_t count,bool rel){std_msgs::msg::String value;value.data.assign(count,label);(rel?pr:pb)->publish(value);std::cout<<"RESOURCE_WRITE label="<<label<<" string_bytes="<<count<<std::endl;std::this_thread::sleep_for(150ms);};
                if(scene=="boundary"){send('L',m-13,false);send('E',m-9,false);send('O',m-5,false);}
                else {for(unsigned i=0;i<(scene=="huge"?1u:3u);++i)send('H',70000,true);}
                std::cout<<"RESOURCE_PRE_SENT side=A"<<std::endl;
                if(!wait(root,"PROGRESS",node))throw std::runtime_error("progress control missing");
                send('P',64,false);std::cout<<"RESOURCE_SENT side=A"<<std::endl;
                okay=true;
            } else {
                for(unsigned i=0;i<200&&!signal(root,"EARLY");++i){rclcpp::spin_some(node);observation("LIVE");std::this_thread::sleep_for(50ms);}
                auto early=observation("PRE_RELEASE");
                if(scene=="count"||scene=="budget")sr.reset(); // Original RMW Reader deletion cancels incomplete history; no fake ACK.
                observation("AFTER_READER_DELETE");std::cout<<"RESOURCE_CANCELLED side=B"<<std::endl;
                for(unsigned i=0;i<200&&!signal(root,"OBSERVE");++i){rclcpp::spin_some(node);observation("PROGRESS");std::this_thread::sleep_for(50ms);}
                auto finish=observation("OBSERVE");
                okay=signal(root,"OBSERVE")&&received['P']==1&&finish.invalid_releases==0;
                if(scene=="boundary")okay=okay&&received['L']==1&&received['E']==1&&received['O']==0&&finish.size_rejections>start.size_rejections;
                else if(scene=="huge")okay=okay&&received['H']==0&&finish.size_rejections>start.size_rejections;
                else if(scene=="count")okay=okay&&received['H']==0&&early.count_rejections>start.count_rejections;
                else okay=okay&&received['H']==0&&early.budget_rejections>start.budget_rejections;
                std::cout<<"RESOURCE_ORACLE scene="<<scene<<" result="<<(okay?"PASS":"FAIL")<<" P="<<received['P']<<" H="<<received['H']<<" O="<<received['O']<<std::endl;
            }
            std::cout<<"RESOURCE_DONE side="<<side<<std::endl;
            if(!wait(root,"CLEANUP",node))throw std::runtime_error("cleanup control missing");
        }
        rclcpp::shutdown();auto final=observation("AFTER_DELETE");
        if(final.retained_bytes||final.live_records||final.invalid_releases)okay=false;
        std::cout<<"RESOURCE_EXIT side="<<side<<" result="<<(okay?"PASS":"FAIL")<<std::endl;
    }catch(const std::exception& error){std::cerr<<"RESOURCE_FAILURE reason="<<error.what()<<std::endl;try{rclcpp::shutdown();}catch(...){}return 4;}
    return okay?0:5;
}
