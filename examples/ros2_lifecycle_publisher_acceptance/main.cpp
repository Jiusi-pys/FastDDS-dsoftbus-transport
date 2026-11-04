// Finite real ROS publication observation. No local lifecycle trigger or publish bypass.
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition_event.hpp>
#include <lifecycle_msgs/srv/change_state.hpp>
#include <lifecycle_msgs/srv/get_state.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/trigger.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>
#include <process.h>
#include "../ros2_transport/FailureProtocol.hpp"
using namespace std::chrono_literals;
using Message = std_msgs::msg::String;
using Change = lifecycle_msgs::srv::ChangeState;
using Get = lifecycle_msgs::srv::GetState;
using Probe = std_srvs::srv::Trigger;
using Event = lifecycle_msgs::msg::TransitionEvent;
using Return = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;
using Triple = std::array<unsigned, 3>;
using Recorder = std::function<void(const std::string&)>;
static void require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
static std::string hex(const void* data, std::size_t size) {
    auto bytes = static_cast<const unsigned char*>(data); std::ostringstream text;
    for (std::size_t i=0;i<size;++i) text << std::hex << std::setw(2) << std::setfill('0') << unsigned(bytes[i]);
    return text.str();
}
static std::string gid(const rmw_gid_t& value) {
    require(std::any_of(std::begin(value.data), std::end(value.data), [](auto x){return x!=0;}), "zero_gid");
    return hex(value.data, sizeof(value.data));
}
static std::string implementation(const rmw_gid_t& value) {
    require(value.implementation_identifier != nullptr, "missing_gid_implementation");
    std::string text = value.implementation_identifier;
    require(std::regex_match(text,std::regex("[A-Za-z0-9_]+")), "gid_implementation_format"); return text;
}
static void qos(const rclcpp::QoS& actual) {
    const auto& p=actual.get_rmw_qos_profile();
    require(p.reliability==RMW_QOS_POLICY_RELIABILITY_RELIABLE && p.durability==RMW_QOS_POLICY_DURABILITY_VOLATILE &&
            p.history==RMW_QOS_POLICY_HISTORY_KEEP_LAST && p.depth==10, "actual_qos");
}
static bool control(const char* key,const std::string& wanted) {
    const char* path=std::getenv(key); require(path&&*path,"control_path_missing");
    std::ifstream file(path); std::string line,extra;
    return std::getline(file,line)&&line==wanted&&!std::getline(file,extra);
}
static std::string payload(const std::string& run,unsigned phase,unsigned attempt,const std::string& overload) {
    return "run="+run+";phase="+std::to_string(phase)+";attempt="+std::to_string(attempt)+";overload="+overload;
}

class ManagedPublisherNode : public rclcpp_lifecycle::LifecycleNode {
public:
    using Publisher = rclcpp_lifecycle::LifecyclePublisher<Message>;
    Publisher::SharedPtr publisher;
    std::map<std::string,unsigned> callbacks;
    ManagedPublisherNode(const std::string& ns,const rclcpp::NodeOptions& options,Recorder record)
        : LifecycleNode("managed",ns,options),record_(std::move(record)) {
        publisher=create_publisher<Message>(ns+"/samples",rclcpp::QoS(10).reliable().durability_volatile());
    }
    Return on_configure(const rclcpp_lifecycle::State& previous) override {
        return note("configure",previous,LifecycleNode::on_configure(previous));
    }
    Return on_activate(const rclcpp_lifecycle::State& previous) override {
        return note("activate",previous,LifecycleNode::on_activate(previous));
    }
    Return on_deactivate(const rclcpp_lifecycle::State& previous) override {
        return note("deactivate",previous,LifecycleNode::on_deactivate(previous));
    }
    Return on_shutdown(const rclcpp_lifecycle::State& previous) override {
        return note("shutdown",previous,LifecycleNode::on_shutdown(previous));
    }
private:
    Return note(const std::string& name,const rclcpp_lifecycle::State& previous,Return result) {
        record_("event=CALLBACK callback="+name+" ordinal="+std::to_string(++callbacks[name])+" previous="+
            std::to_string(previous.id())+" current="+std::to_string(get_current_state().id())+" return="+
            std::to_string(static_cast<unsigned>(result))+" activated="+(publisher->is_activated()?"1":"0"));
        return result;
    }
    Recorder record_;
};

int main(int argc,char** argv) {
    if(argc!=3)return 2;
    const std::string side=argv[1],run=argv[2];
    if((side!="A"&&side!="B")||!std::regex_match(run,std::regex("[0-9a-f]{16}")))return 2;
    const auto pid=_getpid(); const std::string ns="/publisher_"+run;
    unsigned local=0;
    Recorder record=[&](const std::string& text){std::cout<<"PUBLISHER_ACCEPT run="<<run<<" side="<<side<<" pid="<<pid<<" local="<<++local<<' '<<text<<std::endl;};
    int result=4;bool initialized=false;const char* stage="RCLCPP_INIT";
    try {
        rclcpp::init(1,argv);initialized=true;stage="PUBLISHER_SCENARIO";
        auto options=rclcpp::NodeOptions().use_intra_process_comms(false).start_parameter_services(false).start_parameter_event_publisher(false);
        rclcpp::executors::SingleThreadedExecutor executor;
        const auto global_end=std::chrono::steady_clock::now()+50s;
        const auto progress=[&]{executor.spin_some();std::this_thread::sleep_for(1ms);};
        const auto until=[&](const auto& ready,auto timeout){
            const auto end=std::min(global_end,std::chrono::steady_clock::now()+timeout);
            while(!ready()){require(std::chrono::steady_clock::now()<end,"bounded_wait_expired");progress();}
        };
        const auto start=[&]{return control("FDDST_LIFECYCLE_START","START "+run);};
        const auto await_start=[&]{
            const auto end=std::min(global_end,std::chrono::steady_clock::now()+15s);
            // Discovery has qualified; do not dispatch business callbacks before START is logged.
            while(!start()){require(std::chrono::steady_clock::now()<end,"start_wait_expired");std::this_thread::sleep_for(1ms);}
            record("event=START nodes=1");
        };
        const auto released=[&]{return control("FDDST_LIFECYCLE_RELEASE","RELEASE "+run);};
        const std::string qos_fields=" reliability=reliable durability=volatile history=keep_last depth=10";
        if(side=="A") {
            auto node=std::make_shared<ManagedPublisherNode>(ns,options,record);
            auto publisher=node->publisher; qos(publisher->get_actual_qos());
            const std::string writer=gid(publisher->get_gid()),impl=implementation(publisher->get_gid());
            unsigned phase=0,attempts=0;
            auto probe=node->create_service<Probe>(ns+"/probe",[&](Probe::Request::SharedPtr,Probe::Response::SharedPtr answer){
                require(start()&&phase<4,"probe_phase"); ++phase;
                const unsigned expected_state=(phase%2)?2:3; const bool active=(phase%2)==0;
                require(node->get_current_state().id()==expected_state && publisher->is_activated()==active,"probe_state");
                require(publisher->get_subscription_count()==1 && gid(publisher->get_gid())==writer && implementation(publisher->get_gid())==impl,"probe_match_gid");
                const auto identity=" phase="+std::to_string(phase)+" state="+std::to_string(expected_state)+" activated="+(active?"1":"0");
                record("event=PROBE"+identity+" matched=1 gid="+writer+" impl="+impl);
                for(const std::string overload:{"ref","unique"}) {
                    const auto id=++attempts; auto data=payload(run,phase,id,overload);
                    const auto fields=" phase="+std::to_string(phase)+" attempt="+std::to_string(id)+" overload="+overload+" activated="+(active?"1":"0")+" payload_hex="+hex(data.data(),data.size());
                    record("event=ATTEMPT"+fields);
                    // Always call the managed API, even when inactive. No application gating.
                    if(overload=="ref"){Message message;message.data=data;publisher->publish(message);}
                    else {auto message=std::make_unique<Message>();message->data=data;publisher->publish(std::move(message));require(!message,"unique_ownership");}
                    record("event=RETURN phase="+std::to_string(phase)+" attempt="+std::to_string(id)+" overload="+overload);
                }
                answer->success=true;
                answer->message="phase="+std::to_string(phase)+" state="+std::to_string(expected_state)+" activated="+(active?"1":"0")+" first="+std::to_string(phase*2-1)+" last="+std::to_string(phase*2)+" gid="+writer+" impl="+impl;
            });
            executor.add_node(node->get_node_base_interface());
            record("event=READY namespace="+ns+" gid="+writer+" impl="+impl+qos_fields);
            until([&]{return publisher->get_subscription_count()==1 && node->count_subscribers(ns+"/managed/transition_event")==1;},15s);
            record("event=MATCHED nodes=1 samples=1 transitions=1");await_start();
            until([&]{return node->get_current_state().id()==4;},25s);
            const std::map<std::string,unsigned> expected{{"configure",1},{"activate",2},{"deactivate",2},{"shutdown",1}};
            require(phase==4&&attempts==8&&node->callbacks==expected&&!publisher->is_activated()&&gid(publisher->get_gid())==writer,"server_final");
            record("event=BUSINESS_DONE probes=4 attempts=8 callbacks=6 finalized=1 activated=0 gid="+writer);
            until(released,10s);require(phase==4&&attempts==8&&node->callbacks==expected,"extra_server_work");
            executor.remove_node(node->get_node_base_interface());FastddsDsoftbusTeardownCheckpoint("BEFORE_NODE_DELETE");
        } else {
            auto node=std::make_shared<rclcpp::Node>("client",ns,options);executor.add_node(node);
            auto change=node->create_client<Change>(ns+"/managed/change_state");auto get=node->create_client<Get>(ns+"/managed/get_state");auto probe=node->create_client<Probe>(ns+"/probe");
            std::vector<Triple> events;std::vector<unsigned> received;std::string writer,impl;
            auto transition=node->create_subscription<Event>(ns+"/managed/transition_event",rclcpp::QoS(32).reliable(),[&](Event::ConstSharedPtr event){
                require(events.size()<12,"extra_event");events.push_back({event->transition.id,event->start_state.id,event->goal_state.id});
                record("event=TRANSITION ordinal="+std::to_string(events.size())+" transition="+std::to_string(event->transition.id)+" start="+std::to_string(event->start_state.id)+" goal="+std::to_string(event->goal_state.id));
            });
            auto subscription=node->create_subscription<Message>(ns+"/samples",rclcpp::QoS(10).reliable().durability_volatile(),[&](Message::ConstSharedPtr message,const rclcpp::MessageInfo& info){
                require(message->data.size()<=256,"oversize_sample");std::smatch fields;
                require(std::regex_match(message->data,fields,std::regex("run=([0-9a-f]{16});phase=([1-4]);attempt=([1-8]);overload=(ref|unique)")),"payload_format");
                const unsigned phase=unsigned(std::stoul(fields[2].str())),attempt=unsigned(std::stoul(fields[3].str()));
                const auto actual=gid(info.get_rmw_message_info().publisher_gid),actual_impl=implementation(info.get_rmw_message_info().publisher_gid);
                record("event=RECEIVE phase="+std::to_string(phase)+" attempt="+std::to_string(attempt)+" overload="+fields[4].str()+" gid="+actual+" impl="+actual_impl+" payload_hex="+hex(message->data.data(),message->data.size()));
                const std::vector<unsigned> expected{3,4,7,8};
                require(fields[1].str()==run && received.size()<expected.size() && attempt==expected[received.size()] && phase==((attempt<=4)?2u:4u) && fields[4].str()==((attempt%2)?"ref":"unique"),"received_script");
                require(!writer.empty()&&actual==writer&&actual_impl==impl,"received_writer_gid");received.push_back(attempt);
            });
            qos(subscription->get_actual_qos());record("event=READY namespace="+ns+qos_fields);
            until([&]{return change->service_is_ready()&&get->service_is_ready()&&probe->service_is_ready()&&subscription->get_publisher_count()==1&&transition->get_publisher_count()==1;},15s);
            record("event=MATCHED nodes=1 samples=1 transitions=1");await_start();
            const auto await=[&](auto future){until([&]{return future.wait_for(0ms)==std::future_status::ready;},3s);return future.get();};
            const std::map<unsigned,std::string> labels{{1,"unconfigured"},{2,"inactive"},{3,"active"},{4,"finalized"}};
            const auto snapshot=[&](unsigned operation,unsigned state){auto answer=await(get->async_send_request(std::make_shared<Get::Request>()));
                require(answer&&answer->current_state.id==state&&answer->current_state.label==labels.at(state),"state_response");
                record("event=STATE operation="+std::to_string(operation)+" service="+ns+"/managed/get_state state="+std::to_string(state)+" label="+labels.at(state));};
            std::vector<Triple> expected_events;
            const auto move=[&](unsigned operation,unsigned id,unsigned state,const std::vector<Triple>& wanted){
                auto request=std::make_shared<Change::Request>();request->transition.id=static_cast<uint8_t>(id);
                record("event=CHANGE_REQUEST operation="+std::to_string(operation)+" service="+ns+"/managed/change_state transition="+std::to_string(id));
                auto answer=await(change->async_send_request(request));require(answer&&answer->success,"change_response");
                record("event=CHANGE_RESPONSE operation="+std::to_string(operation)+" success=1");
                expected_events.insert(expected_events.end(),wanted.begin(),wanted.end());until([&]{return events.size()>=expected_events.size();},3s);require(events==expected_events,"event_sequence");snapshot(operation,state);
            };
            const auto publish_probe=[&](unsigned phase){
                require(subscription->get_publisher_count()==1,"probe_subscriber_match");
                record("event=PROBE_REQUEST phase="+std::to_string(phase)+" service="+ns+"/probe matched=1");
                auto answer=await(probe->async_send_request(std::make_shared<Probe::Request>()));require(answer&&answer->success,"probe_response");
                std::smatch fields;require(std::regex_match(answer->message,fields,std::regex("phase=([1-4]) state=([23]) activated=([01]) first=([1357]) last=([2468]) gid=([0-9a-f]+) impl=([A-Za-z0-9_]+)")),"probe_response_format");
                require(unsigned(std::stoul(fields[1].str()))==phase&&unsigned(std::stoul(fields[2].str()))==((phase%2)?2u:3u)&&fields[3].str()==((phase%2)?"0":"1")&&unsigned(std::stoul(fields[4].str()))==phase*2-1&&unsigned(std::stoul(fields[5].str()))==phase*2,"probe_response_values");
                require(fields[6].length()==2*RMW_GID_STORAGE_SIZE&&fields[6].str()!=std::string(2*RMW_GID_STORAGE_SIZE,'0'),"probe_gid_format");
                if(writer.empty()){writer=fields[6].str();impl=fields[7].str();}else require(writer==fields[6].str()&&impl==fields[7].str(),"probe_gid_changed");
                record("event=PROBE_RESPONSE "+answer->message+" success=1");
                if(phase%2==0)until([&]{return received.size()>=(phase==2?2u:4u);},3s);
            };
            snapshot(0,1);move(1,1,2,{{0,1,10},{0,10,2}});publish_probe(1);
            move(2,3,3,{{0,2,13},{0,13,3}});publish_probe(2);
            move(3,4,2,{{0,3,14},{0,14,2}});publish_probe(3);
            move(4,3,3,{{0,2,13},{0,13,3}});publish_probe(4);
            move(5,4,2,{{0,3,14},{0,14,2}});move(6,6,4,{{0,2,12},{0,12,4}});
            const auto end=std::chrono::steady_clock::now()+250ms;while(std::chrono::steady_clock::now()<end)progress();
            require(received==std::vector<unsigned>({3,4,7,8})&&events==expected_events,"final_observation");
            record("event=BUSINESS_DONE changes=6 states=7 probes=4 events=12 received=4 pending=0");
            until(released,10s);progress();require(received==std::vector<unsigned>({3,4,7,8})&&events==expected_events,"late_observation");
            record("event=OBSERVATION_END received=4 inactive_received=0 finite=1");
            executor.remove_node(node);FastddsDsoftbusTeardownCheckpoint("BEFORE_NODE_DELETE");
        }
        result=0;
    } catch(const std::exception& error) {
        std::cerr<<"PUBLISHER_EXCEPTION stage="<<stage<<" message="<<error.what()<<std::endl;
        if(!initialized){record("event=INIT_FAILED result=4");return 4;}
        ros_failure_protocol::Report(run,side,pid,stage,"SCENARIO_FAILED");
    }
    FastddsDsoftbusTeardownCheckpoint("AFTER_NODE_ENTITY_DELETE");
    try{if(!rclcpp::shutdown())result=4;}catch(...){result=4;}
    FastddsDsoftbusTeardownCheckpoint("AFTER_CONTEXT_CLEANUP");if(FastddsDsoftbusTeardownResult()!=0)result=4;
    record("event=CLOSED result="+std::to_string(result));return result;
}
