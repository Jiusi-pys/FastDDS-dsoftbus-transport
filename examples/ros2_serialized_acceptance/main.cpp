// Two finite typed serialized request/reply exchanges on the existing Host route.
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <std_msgs/msg/string.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>
#include <process.h>
#include "../ros2_transport/FailureProtocol.hpp"
using namespace std::chrono_literals;
using Message=std_msgs::msg::String;
static void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
static std::string hex(const void* data,std::size_t size){
    const auto* bytes=static_cast<const unsigned char*>(data);std::ostringstream out;
    for(std::size_t i=0;i<size;++i)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(bytes[i]);
    return out.str();
}
static std::string gid(const rmw_gid_t& value){
    require(std::any_of(std::begin(value.data),std::end(value.data),[](auto x){return x!=0;}),"zero_gid");
    return hex(value.data,sizeof(value.data));
}
static std::string implementation(const rmw_gid_t& value){
    require(value.implementation_identifier!=nullptr,"missing_gid_implementation");
    std::string text=value.implementation_identifier;
    require(std::regex_match(text,std::regex("[A-Za-z0-9_]+")),"gid_implementation_format");return text;
}
static void qos(const rclcpp::QoS& actual){
    const auto& p=actual.get_rmw_qos_profile();
    require(p.reliability==RMW_QOS_POLICY_RELIABILITY_RELIABLE&&p.durability==RMW_QOS_POLICY_DURABILITY_VOLATILE&&
        p.history==RMW_QOS_POLICY_HISTORY_KEEP_LAST&&p.depth==10,"actual_qos");
}
static bool control(const char* key,const std::string& wanted){
    const char* path=std::getenv(key);require(path&&*path,"control_path_missing");
    std::ifstream file(path);std::string line,extra;
    return std::getline(file,line)&&line==wanted&&!std::getline(file,extra);
}
static std::string payload(const std::string& run,const std::string& role,unsigned id){
    require(id==1||id==2,"payload_id");std::string body="0123456789abcdef0";
    if(id==2){body.clear();for(unsigned i=0;i<256;++i)body+="0123456789abcdef";}
    return "run="+run+";role="+role+";id="+std::to_string(id)+";body="+body;
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    const std::string side=argv[1],run=argv[2];
    if((side!="A"&&side!="B")||!std::regex_match(run,std::regex("[0-9a-f]{16}")))return 2;
    const auto pid=_getpid();unsigned local=0;
    const auto record=[&](const std::string& text){std::cout<<"SERIALIZED_ACCEPT run="<<run<<" side="<<side<<" pid="<<pid<<" local="<<++local<<' '<<text<<std::endl;};
    int result=4;bool initialized=false;const char* stage="RCLCPP_INIT";
    try{
        rclcpp::init(1,argv);initialized=true;stage="SERIALIZED_SCENARIO";
        const std::string ns="/serialized_"+run,own_role=side=="A"?"request":"reply",peer_role=side=="A"?"reply":"request";
        auto options=rclcpp::NodeOptions().use_intra_process_comms(false).start_parameter_services(false).start_parameter_event_publisher(false);
        auto node=std::make_shared<rclcpp::Node>("participant_"+side,ns,options);
        rclcpp::executors::SingleThreadedExecutor executor;executor.add_node(node);
        const auto deadline=std::chrono::steady_clock::now()+50s;
        const auto progress=[&]{executor.spin_some();std::this_thread::sleep_for(1ms);};
        const auto until=[&](const auto& ready,auto timeout){
            const auto end=std::min(deadline,std::chrono::steady_clock::now()+timeout);
            while(!ready()){require(std::chrono::steady_clock::now()<end,"bounded_wait_expired");progress();}
        };
        auto publisher=node->create_publisher<Message>(ns+"/"+own_role,rclcpp::QoS(10).reliable().durability_volatile());
        qos(publisher->get_actual_qos());
        const std::string writer=gid(publisher->get_gid()),impl=implementation(publisher->get_gid());
        require(impl=="rmw_fastrtps_cpp","rmw_implementation");
        rclcpp::Serialization<Message> serialization;
        unsigned published=0,received=0;bool started=false;std::string peer_gid,peer_impl;
        const auto publish=[&](unsigned id){
            require(started&&id==published+1&&id<=2&&publisher->get_subscription_count()==1,"publish_sequence_match");
            require(gid(publisher->get_gid())==writer&&implementation(publisher->get_gid())==impl,"publisher_gid_changed");
            Message message;message.data=payload(run,own_role,id);
            const std::string overload=id==1?"wrapper":"raw";
            const auto identity=" role="+own_role+" id="+std::to_string(id)+" overload="+overload;
            const auto payload_bytes=message.data.size();const auto payload_hex=hex(message.data.data(),payload_bytes);
            rclcpp::SerializedMessage bytes(8192);const auto before=bytes.capacity();
            const auto diagnostic=" run="+run+" side="+side+" pid="+std::to_string(pid)+identity;
            const auto initial=" capacity_before="+std::to_string(before)+" payload_bytes="+std::to_string(payload_bytes);
            std::cerr<<"SERIALIZED_BOUNDS phase=PRE_SERIALIZE"<<diagnostic<<initial<<std::endl;
            if(before!=8192)throw std::runtime_error("initial_capacity"+initial);
            serialization.serialize_message(&message,&bytes);
            const auto length=bytes.size(),after=bytes.capacity();
            const auto bounds=initial+" capacity_after="+std::to_string(after)+" length="+std::to_string(length);
            std::cerr<<"SERIALIZED_BOUNDS phase=POST_SERIALIZE"<<diagnostic<<bounds<<" payload_hex="<<payload_hex<<std::endl;
            // rmw_fastrtps_cpp 8.4.3 assigns both exposed fields to data_length.
            if(after!=length||length<=payload_bytes||length>8192)throw std::runtime_error("serialized_bounds"+bounds);
            record("event=SERIALIZE"+identity+" payload_hex="+payload_hex+
                " length="+std::to_string(length)+" capacity_before="+std::to_string(before)+" capacity_after="+std::to_string(after));
            record("event=PUBLISH_ATTEMPT"+identity);
            // Both branches call serialized overloads; neither calls ordinary publish(Message).
            const rclcpp::SerializedMessage& view=bytes;
            if(id==1)publisher->publish(view);
            else publisher->publish(view.get_rcl_serialized_message());
            record("event=RETURN"+identity);++published;
        };
        auto subscription=node->create_subscription<Message>(ns+"/"+peer_role,rclcpp::QoS(10).reliable().durability_volatile(),
            [&](Message::ConstSharedPtr message,const rclcpp::MessageInfo& info){
                require(started&&received<2,"unexpected_receive");const unsigned id=received+1;
                require(message->data.size()<=8192,"receive_size");
                const auto boundary=message->data.find(";body=");require(boundary!=std::string::npos,"receive_body");
                const auto header=message->data.substr(0,boundary);std::smatch fields;
                require(std::regex_match(header,fields,std::regex("run=([0-9a-f]{16});role=(request|reply);id=([12])")),"receive_format");
                const auto actual=gid(info.get_rmw_message_info().publisher_gid),actual_impl=implementation(info.get_rmw_message_info().publisher_gid);
                record("event=RECEIVE role="+fields[2].str()+" id="+fields[3].str()+" payload_hex="+hex(message->data.data(),message->data.size())+" gid="+actual+" impl="+actual_impl);
                require(message->data==payload(run,peer_role,id)&&actual!=writer&&actual_impl==impl,"receive_payload_identity");
                if(peer_gid.empty()){peer_gid=actual;peer_impl=actual_impl;}
                else require(actual==peer_gid&&actual_impl==peer_impl,"peer_gid_changed");
                ++received;if(side=="B")publish(id);
            });
        qos(subscription->get_actual_qos());
        record("event=READY namespace="+ns+" pub_topic="+ns+"/"+own_role+" sub_topic="+ns+"/"+peer_role+" gid="+writer+" impl="+impl+
            " reliability=reliable durability=volatile history=keep_last depth=10");
        until([&]{return publisher->get_subscription_count()==1&&subscription->get_publisher_count()==1;},15s);
        record("event=MATCHED publishers=1 subscriptions=1");
        const auto start_end=std::min(deadline,std::chrono::steady_clock::now()+15s);
        while(!control("FDDST_LIFECYCLE_START","START "+run)){
            require(std::chrono::steady_clock::now()<start_end,"start_wait_expired");std::this_thread::sleep_for(1ms);
        }
        // Do not dispatch subscription callbacks between MATCHED and this marker.
        started=true;record("event=START peers=1");
        if(side=="A")for(unsigned id=1;id<=2;++id){publish(id);until([&]{return received==id;},3s);}
        else until([&]{return received==2&&published==2;},10s);
        const auto drain=std::min(deadline,std::chrono::steady_clock::now()+250ms);
        while(std::chrono::steady_clock::now()<drain)progress();
        require(received==2&&published==2,"final_counts");
        record("event=BUSINESS_DONE serialized=2 published=2 received=2 pending=0");
        until([&]{return control("FDDST_LIFECYCLE_RELEASE","RELEASE "+run);},10s);progress();
        require(received==2&&published==2&&gid(publisher->get_gid())==writer,"late_observation");
        record("event=OBSERVATION_END received=2 finite=1");
        executor.remove_node(node);FastddsDsoftbusTeardownCheckpoint("BEFORE_NODE_DELETE");result=0;
    }catch(const std::exception& error){
        std::cerr<<"SERIALIZED_EXCEPTION stage="<<stage<<" message="<<error.what()<<std::endl;
        if(!initialized){record("event=INIT_FAILED result=4");return 4;}
        ros_failure_protocol::Report(run,side,pid,stage,"SCENARIO_FAILED");
    }
    FastddsDsoftbusTeardownCheckpoint("AFTER_NODE_ENTITY_DELETE");
    try{if(!rclcpp::shutdown())result=4;}catch(...){result=4;}
    FastddsDsoftbusTeardownCheckpoint("AFTER_CONTEXT_CLEANUP");if(FastddsDsoftbusTeardownResult()!=0)result=4;
    record("event=CLOSED result="+std::to_string(result));return result;
}
