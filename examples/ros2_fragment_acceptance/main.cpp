// Fixed typed ROS String delivery on the unchanged Windows Host transport seam.
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialization.hpp>
#include <std_msgs/msg/string.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
#include <process.h>
#include "../ros2_transport/FailureProtocol.hpp"
using namespace std::chrono_literals;
using Message=std_msgs::msg::String;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
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
    require(std::string(value.implementation_identifier)=="rmw_fastrtps_cpp","gid_implementation");
    return value.implementation_identifier;
}
static void qos(const rclcpp::QoS& actual,bool reliable){
    const auto& p=actual.get_rmw_qos_profile();
    require(p.reliability==(reliable?RMW_QOS_POLICY_RELIABILITY_RELIABLE:RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT)&&
        p.durability==RMW_QOS_POLICY_DURABILITY_VOLATILE&&p.history==RMW_QOS_POLICY_HISTORY_KEEP_LAST&&p.depth==10,"actual_qos");
}
static bool control(const char* key,const std::string& wanted){
    const char* path=std::getenv(key);require(path&&*path,"control_path_missing");
    std::ifstream file(path);std::string line,extra;
    return std::getline(file,line)&&line==wanted&&!std::getline(file,extra);
}
static std::string payload(const std::string& run,const std::string& reliability,
        const std::string& side,const std::string& phase,unsigned id){
    require(id>=1&&id<=5,"payload_id");
    const std::size_t size=phase=="small"?1024:66000;
    std::string value="run="+run+";qos="+reliability+";side="+side+";phase="+phase+";id="+std::to_string(id)+";body=";
    const unsigned salt=id+(side=="A"?0:7)+(phase=="small"?0:phase=="large"?11:19);
    for(std::size_t i=value.size();i<size;++i)value+=char('a'+(i+salt)%26);
    require(value.size()==size,"payload_size");return value;
}
static std::string ids(const std::set<unsigned>& values,bool missing=false){
    std::string result;for(unsigned i=1;i<=5;++i)if(bool(values.count(i))!=missing){
        if(!result.empty())result+=',';result+=std::to_string(i);
    }return result.empty()?"-":result;
}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    const std::string side=argv[1],run=argv[2],reliability=argv[3],peer=side=="A"?"B":"A";
    if((side!="A"&&side!="B")||!std::regex_match(run,std::regex("[0-9a-f]{16}"))||
        (reliability!="reliable"&&reliability!="best_effort"))return 2;
    const auto pid=_getpid();unsigned local=0;
    const auto record=[&](const std::string& text){std::cout<<"FRAGMENT_ACCEPT run="<<run<<" side="<<side<<" pid="<<pid<<" local="<<++local<<' '<<text<<std::endl;};
    int result=4;bool initialized=false;const char* stage="RCLCPP_INIT";
    std::string phase="none";std::set<unsigned> sent,received;
    try{
        rclcpp::init(1,argv);initialized=true;stage="FRAGMENT_SCENARIO";
        const auto deadline=std::chrono::steady_clock::now()+50s;
        const std::string ns="/fragment_"+run;
        auto options=rclcpp::NodeOptions().use_intra_process_comms(false).start_parameter_services(false).start_parameter_event_publisher(false);
        auto node=std::make_shared<rclcpp::Node>("participant_"+side,ns,options);
        rclcpp::executors::SingleThreadedExecutor executor;executor.add_node(node);
        const auto progress=[&]{executor.spin_some();std::this_thread::sleep_for(1ms);};
        const auto until=[&](const auto& ready,auto timeout){
            const auto end=std::min(deadline,std::chrono::steady_clock::now()+timeout);
            while(!ready()){require(std::chrono::steady_clock::now()<end,"bounded_wait_expired");progress();}
        };
        const bool reliable=reliability=="reliable";
        auto requested=rclcpp::QoS(10).durability_volatile();
        if(reliable)requested.reliable();else requested.best_effort();
        auto publisher=node->create_publisher<Message>(ns+"/from_"+side,requested);
        qos(publisher->get_actual_qos(),reliable);
        const std::string writer=gid(publisher->get_gid()),impl=implementation(publisher->get_gid());
        rclcpp::Serialization<Message> serialization;bool started=false;
        unsigned total_published=0,total_received=0;std::string peer_gid;std::set<uint64_t> sequences;
        const auto cdr_length=[&](const Message& message){
            rclcpp::SerializedMessage bytes;serialization.serialize_message(&message,&bytes);
            require(bytes.size()>message.data.size()&&bytes.size()<=message.data.size()+32,"cdr_length");return bytes.size();
        };
        const auto publish=[&](unsigned id){
            require(started&&id==sent.size()+1&&id<=5&&publisher->get_subscription_count()==1,"publish_sequence_match");
            require(gid(publisher->get_gid())==writer&&implementation(publisher->get_gid())==impl,"publisher_gid_changed");
            Message message;message.data=payload(run,reliability,side,phase,id);
            const auto identity=" phase="+phase+" id="+std::to_string(id);
            record("event=PUBLISH_ATTEMPT"+identity+" bytes="+std::to_string(message.data.size())+" cdr_length="+std::to_string(cdr_length(message)));
            publisher->publish(message);
            sent.insert(id);++total_published;record("event=RETURN"+identity);
        };
        auto subscription=node->create_subscription<Message>(ns+"/from_"+peer,requested,
            [&](Message::ConstSharedPtr message,const rclcpp::MessageInfo& info){
                require(started&&message->data.size()<=66000,"unexpected_receive_size");
                const auto boundary=message->data.find(";body=");require(boundary!=std::string::npos,"receive_body");
                std::smatch fields;const auto header=message->data.substr(0,boundary);
                require(std::regex_match(header,fields,std::regex("run=([0-9a-f]{16});qos=(reliable|best_effort);side=(A|B);phase=(small|large|burst);id=([1-5])")),"receive_format");
                const unsigned id=unsigned(std::stoul(fields[5].str()));
                const auto& raw=info.get_rmw_message_info();const auto actual=gid(raw.publisher_gid),actual_impl=implementation(raw.publisher_gid);
                const auto sequence=raw.publication_sequence_number;
                record("event=RECEIVE phase="+fields[4].str()+" id="+fields[5].str()+" payload_hex="+hex(message->data.data(),message->data.size())+
                    " gid="+actual+" impl="+actual_impl+" sequence="+std::to_string(sequence)+" cdr_length="+std::to_string(cdr_length(*message)));
                require(fields[1]==run&&fields[2]==reliability&&fields[3]==peer&&fields[4]==phase&&
                    message->data==payload(run,reliability,peer,phase,id),"receive_payload_identity");
                require(actual!=writer&&actual_impl==impl&&(peer_gid.empty()||peer_gid==actual),"peer_gid_changed");peer_gid=actual;
                require(sequence!=0&&sequence!=std::numeric_limits<uint64_t>::max()&&sequences.insert(sequence).second,"publication_sequence");
                require(received.insert(id).second,"duplicate_payload_id");++total_received;
                if(phase!="burst"){
                    require(id==received.size(),"sequential_receive_id");
                    if(side=="B")publish(id);
                }
            });
        qos(subscription->get_actual_qos(),reliable);
        record("event=READY namespace="+ns+" pub_topic="+ns+"/from_"+side+" sub_topic="+ns+"/from_"+peer+" gid="+writer+" impl="+impl+
            " reliability="+reliability+" durability=volatile history=keep_last depth=10");
        until([&]{return publisher->get_subscription_count()==1&&subscription->get_publisher_count()==1;},15s);
        record("event=MATCHED publishers=1 subscriptions=1");
        const auto start_end=std::min(deadline,std::chrono::steady_clock::now()+15s);
        while(!control("FDDST_LIFECYCLE_START","START "+run)){
            require(std::chrono::steady_clock::now()<start_end,"start_wait_expired");std::this_thread::sleep_for(1ms);
        }
        started=true;record("event=START peers=1");
        const char* start=std::getenv("FDDST_LIFECYCLE_START");require(start&&*start,"phase_control_path");
        int peer_pid=0;
        const auto barrier=[&](const std::string& state){
            const std::string base=std::string(start)+".fragment."+run+"."+reliability+"."+phase+"."+state+".";
            const auto path=base+side,tmp=path+".tmp."+std::to_string(pid);
            require(!std::filesystem::exists(path)&&!std::filesystem::exists(tmp),"phase_control_reuse");
            {std::ofstream file(tmp);require(bool(file),"phase_control_create");
                file<<"FRAGMENT_PHASE run="<<run<<" qos="<<reliability<<" phase="<<phase<<" side="<<side<<" pid="<<pid<<" state="<<state<<'\n';
                file.close();require(bool(file),"phase_control_write");}
            std::filesystem::rename(tmp,path);
            const auto end=std::min(deadline,std::chrono::steady_clock::now()+10s);
            for(;;){
                std::ifstream file(base+peer);std::string line,extra;
                if(file){
                    require(bool(std::getline(file,line))&&!std::getline(file,extra),"phase_control_lines");std::smatch match;
                    require(std::regex_match(line,match,std::regex("FRAGMENT_PHASE run="+run+" qos="+reliability+" phase="+phase+" side="+peer+" pid=([1-9][0-9]*) state="+state)),"phase_control_identity");
                    const auto found=std::stoul(match[1].str());require(found<=std::numeric_limits<int>::max()&&found!=unsigned(pid),"phase_peer_pid");
                    require(peer_pid==0||peer_pid==int(found),"phase_peer_changed");peer_pid=int(found);return;
                }
                require(std::chrono::steady_clock::now()<end,"phase_barrier_expired");
                // No executor dispatch until the local PHASE_START marker.
                std::this_thread::sleep_for(1ms);
            }
        };
        for(const std::string next:{"small","large","burst"}){
            phase=next;sent.clear();received.clear();record("event=PHASE_READY phase="+phase);
            barrier("ready");record("event=PHASE_START phase="+phase+" peer_pid="+std::to_string(peer_pid));
            if(phase=="burst"){
                // Exactly five contiguous application publishes, with no spin/ack between them.
                for(unsigned id=1;id<=5;++id)publish(id);
                until([&]{return received.size()==5;},10s);
            }else if(side=="A"){
                for(unsigned id=1;id<=5;++id){publish(id);until([&]{return received.count(id)!=0;},3s);}
            }else until([&]{return sent.size()==5&&received.size()==5;},10s);
            require(sent.size()==5&&received.size()==5,"phase_counts");
            record("event=PHASE_DONE phase="+phase+" published=5 received=5 missing=-");
            barrier("done");record("event=PHASE_CONFIRM phase="+phase+" peer_pid="+std::to_string(peer_pid));
        }
        const auto drain=std::min(deadline,std::chrono::steady_clock::now()+250ms);
        while(std::chrono::steady_clock::now()<drain)progress();
        require(total_received==15&&total_published==15,"final_counts");
        record("event=BUSINESS_DONE published=15 received=15 pending=0");
        until([&]{return control("FDDST_LIFECYCLE_RELEASE","RELEASE "+run);},10s);progress();
        require(total_received==15&&total_published==15,"late_observation");
        record("event=OBSERVATION_END received=15 finite=1");
        executor.remove_node(node);FastddsDsoftbusTeardownCheckpoint("BEFORE_NODE_DELETE");result=0;
    }catch(const std::exception& error){
        std::cerr<<"FRAGMENT_PROGRESS run="<<run<<" side="<<side<<" pid="<<pid<<" qos="<<reliability<<" phase="<<phase<<
            " published_ids="<<ids(sent)<<" received_ids="<<ids(received)<<" missing_ids="<<ids(received,true)<<std::endl;
        std::cerr<<"FRAGMENT_EXCEPTION stage="<<stage<<" message="<<error.what()<<std::endl;
        if(!initialized){record("event=INIT_FAILED result=4");return 4;}
        ros_failure_protocol::Report(run,side,pid,stage,"SCENARIO_FAILED");
    }
    FastddsDsoftbusTeardownCheckpoint("AFTER_NODE_ENTITY_DELETE");
    try{if(!rclcpp::shutdown())result=4;}catch(...){result=4;}
    FastddsDsoftbusTeardownCheckpoint("AFTER_CONTEXT_CLEANUP");if(FastddsDsoftbusTeardownResult()!=0)result=4;
    record("event=CLOSED result="+std::to_string(result));return result;
}
