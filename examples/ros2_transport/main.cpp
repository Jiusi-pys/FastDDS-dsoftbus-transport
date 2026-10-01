// Ordinary ROS API only: no Transport headers, Participant factory, Socket API,
// runtime initialization or pump management belongs in this business program.
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <example_interfaces/srv/add_two_ints.hpp>
#include <chrono>
#include <iostream>
#include <set>
#include <iomanip>
#include <sstream>
#include <regex>
#include <vector>
#include <fstream>
#include "FailureProtocol.hpp"
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace std::chrono_literals;
// Test/control metadata only. No business sample is read from this file.
bool ControlStage(const std::string& run,const std::string& side,long pid,const std::string& stage) {
    const char* prefix=std::getenv("FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX");
    if(!prefix)return true;
    std::cout<<"ROS_STAGE run="<<run<<" side="<<side<<" pid="<<pid<<" stage="<<stage<<std::endl;
    const std::string expected="ROS_RELEASE run="+run+" side="+side+" stage="+stage;
    const auto end=std::chrono::steady_clock::now()+90s;
    while(std::chrono::steady_clock::now()<end) {
        std::ifstream file(std::string(prefix)+"."+stage);std::string line;
        if(std::getline(file,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();return line==expected;}
        std::this_thread::sleep_for(2ms);
    }
    return false;
}
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    const std::string side=argv[1],run=argv[2],mode=argv[3],peer=side=="A"?"B":"A";
    if((side!="A"&&side!="B")||!std::regex_match(run,std::regex("[0-9a-f]{16}"))||
        (mode!="reliable"&&mode!="best_effort"&&mode!="dual"))return 2;
#if defined(_WIN32)
    const auto pid=_getpid();
#else
    const auto pid=getpid();
#endif
    std::cout<<"ROS_MAIN scenario=ROS2_DSOFTBUS run="<<run<<" side="<<side<<" pid="<<pid<<" mode="<<mode<<std::endl;
    int result=4;
    const char* active_stage="RCLCPP_INIT";
    try {
        rclcpp::init(1,argv);
        active_stage="NODE_CREATE";
        const auto ns="/fdds_"+run;
        auto node=std::make_shared<rclcpp::Node>("transport_"+side,ns,rclcpp::NodeOptions().use_intra_process_comms(false));
        const std::vector<std::string> phases=mode=="dual"?std::vector<std::string>{"reliable","best_effort"}:std::vector<std::string>{mode};
        for(const auto& phase:phases) {
        const auto topic_prefix=mode=="dual"?phase+"/":"";
        const auto payload_prefix=mode=="dual"?run+":"+phase+":":run+":";
        auto qos=rclcpp::QoS(rclcpp::KeepLast(10)).durability_volatile();
        if(phase=="best_effort")qos.best_effort();else qos.reliable();
        active_stage=phase=="reliable"?"RELIABLE_ENTITIES":"BEST_EFFORT_ENTITIES";
        std::set<unsigned> taken;bool bad=false;unsigned requests=0;
        auto pub=node->create_publisher<std_msgs::msg::String>(topic_prefix+"from_"+side,qos);
        std::ostringstream own_gid;for(unsigned char b:pub->get_gid().data)own_gid<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
        std::cout<<"ROS_PUBLISHER run="<<run<<" side="<<side<<" gid="<<own_gid.str()<<" intra_process="<<node->get_node_options().use_intra_process_comms()<<" phase="<<phase<<" topic="<<pub->get_topic_name()<<std::endl;
        auto sub=node->create_subscription<std_msgs::msg::String>(topic_prefix+"from_"+peer,qos,
            [&](const std_msgs::msg::String& msg,const rclcpp::MessageInfo& info) {
                const auto prefix=payload_prefix+peer+":";
                if(msg.data.rfind(prefix,0)!=0){bad=true;return;}
                unsigned ordinal=static_cast<unsigned>(std::stoul(msg.data.substr(prefix.size())));
                if(ordinal<1||ordinal>5||!taken.insert(ordinal).second){bad=true;return;}
                std::ostringstream id;for(unsigned char b:info.get_rmw_message_info().publisher_gid.data)id<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
                std::cout<<"ROS_TAKE run="<<run<<" side="<<side<<" ordinal="<<ordinal<<" gid="<<id.str()<<" payload="<<msg.data<<" phase="<<phase
                    <<" topic="<<topic_prefix<<"from_"<<peer<<" publication_sequence="<<info.get_rmw_message_info().publication_sequence_number<<std::endl;
            });
        rclcpp::Service<example_interfaces::srv::AddTwoInts>::SharedPtr service;
        rclcpp::Client<example_interfaces::srv::AddTwoInts>::SharedPtr client;
        if(side=="B")service=node->create_service<example_interfaces::srv::AddTwoInts>(topic_prefix+"add",
            [&](const std::shared_ptr<rmw_request_id_t> header,const std::shared_ptr<example_interfaces::srv::AddTwoInts::Request> q,std::shared_ptr<example_interfaces::srv::AddTwoInts::Response> a){
                a->sum=q->a+q->b;++requests;std::ostringstream writer;
                for(unsigned char byte:header->writer_guid)writer<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);
                std::cout<<"ROS_SERVICE_REQUEST run="<<run<<" a="<<q->a<<" b="<<q->b<<" sum="<<a->sum<<" phase="<<phase<<" request_id="<<header->sequence_number<<" writer="<<writer.str()<<std::endl;});
        else client=node->create_client<example_interfaces::srv::AddTwoInts>(topic_prefix+"add");
        active_stage="MATCH";
        const auto deadline=std::chrono::steady_clock::now()+30s;
        while(pub->get_subscription_count()<1&&std::chrono::steady_clock::now()<deadline){rclcpp::spin_some(node);std::this_thread::sleep_for(1ms);}
        if(pub->get_subscription_count()!=1)ros_failure_protocol::Fail(run,side,pid,"MATCH","REMOTE_MATCH_MISSING");
        const auto peer_name=ns+"/transport_"+peer;
        bool graph=false;
        while(!graph&&std::chrono::steady_clock::now()<deadline){auto names=node->get_node_names();graph=std::find(names.begin(),names.end(),peer_name)!=names.end();if(!graph){rclcpp::spin_some(node);std::this_thread::sleep_for(1ms);}}
        if(!graph)ros_failure_protocol::Fail(run,side,pid,"GRAPH","REMOTE_GRAPH_MISSING");
        std::cout<<"ROS_GRAPH run="<<run<<" side="<<side<<" peer="<<peer_name<<" visible=1 subscription_count="<<pub->get_subscription_count()<<" phase="<<phase<<std::endl;
        const auto stage_prefix=phase=="reliable"?"RELIABLE":"BEST_EFFORT";
        if(!ControlStage(run,side,pid,std::string(stage_prefix)+"_READY"))ros_failure_protocol::Fail(run,side,pid,"PHASE_READY","PHASE_RELEASE_MISSING");
        active_stage="PUBLISH";
        for(unsigned i=1;i<=5;++i){std_msgs::msg::String m;m.data=payload_prefix+side+":"+std::to_string(i);pub->publish(m);std::cout<<"ROS_WRITE run="<<run<<" side="<<side<<" ordinal="<<i<<" payload="<<m.data<<" phase="<<phase<<" topic="<<pub->get_topic_name()<<std::endl;}
        active_stage="SERVICE";
        bool service_done=false;
        if(side=="A") {
            while(!client->service_is_ready()&&std::chrono::steady_clock::now()<deadline){rclcpp::spin_some(node);std::this_thread::sleep_for(1ms);}
            if(!client->service_is_ready())ros_failure_protocol::Fail(run,side,pid,"SERVICE","SERVICE_DISCOVERY_MISSING");
            auto q=std::make_shared<example_interfaces::srv::AddTwoInts::Request>();q->a=3;q->b=4;auto future=client->async_send_request(q);
            const auto request_id=future.request_id;
            std::cout<<"ROS_SERVICE_SENT run="<<run<<" phase="<<phase<<" request_id="<<request_id<<" a=3 b=4"<<std::endl;
            while(future.wait_for(0s)!=std::future_status::ready&&std::chrono::steady_clock::now()<deadline){rclcpp::spin_some(node);std::this_thread::sleep_for(1ms);}
            if(future.wait_for(0s)!=std::future_status::ready)ros_failure_protocol::Fail(run,side,pid,"SERVICE","SERVICE_RESULT_MISSING");
            const auto response=future.get();if(response->sum!=7)ros_failure_protocol::Fail(run,side,pid,"SERVICE","SERVICE_RESULT_WRONG");
            service_done=true;std::cout<<"ROS_SERVICE_RESPONSE run="<<run<<" sum="<<response->sum<<" phase="<<phase<<" request_id="<<request_id<<std::endl;
        }
        while((taken.size()<5||(side=="B"&&requests!=1))&&std::chrono::steady_clock::now()<deadline){rclcpp::spin_some(node);std::this_thread::sleep_for(1ms);}
        if(bad||taken.size()!=5||(side=="A"&&!service_done)||(side=="B"&&requests!=1))ros_failure_protocol::Fail(run,side,pid,"LEDGER","SAMPLE_SERVICE_INCOMPLETE");
        std::cout<<"ROS_BUSINESS_DONE run="<<run<<" side="<<side<<" writes=5 takes="<<taken.size()<<" phase="<<phase<<std::endl;
        if(!ControlStage(run,side,pid,std::string(stage_prefix)+"_DONE"))ros_failure_protocol::Fail(run,side,pid,"PHASE_DONE","PHASE_COMPLETION_RELEASE_MISSING");
        }
        result=0;
    }catch(const std::exception& e){(void)e;ros_failure_protocol::Report(run,side,pid,active_stage,"ROS_OPERATION_EXCEPTION");}
    bool shutdown_return=false;
    try {shutdown_return=rclcpp::shutdown();if(!shutdown_return)ros_failure_protocol::Report(run,side,pid,"CONTEXT_SHUTDOWN","SHUTDOWN_RETURNED_FALSE",true);}
    catch(...){ros_failure_protocol::Report(run,side,pid,"CONTEXT_SHUTDOWN","SHUTDOWN_EXCEPTION",true);}
    if(!shutdown_return&&result==0)result=4;
    std::cout<<"ROS_CONTEXT_CLOSED run="<<run<<" side="<<side<<" result="<<result<<" result_scope=APPLICATION_EXIT_STATUS shutdown_return="<<int(shutdown_return)<<std::endl;return result;
}
