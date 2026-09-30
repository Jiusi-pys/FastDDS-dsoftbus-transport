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

using namespace std::chrono_literals;
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    const std::string side=argv[1],run=argv[2],mode=argv[3],peer=side=="A"?"B":"A";
    if(side!="A"&&side!="B")return 2;
    rclcpp::init(1,argv);
    int result=4;
    try {
        const auto ns="/fdds_"+run;
        auto node=std::make_shared<rclcpp::Node>("transport_"+side,ns,rclcpp::NodeOptions().use_intra_process_comms(false));
        auto qos=rclcpp::QoS(rclcpp::KeepLast(10)).durability_volatile();
        if(mode=="best_effort")qos.best_effort();else if(mode=="reliable")qos.reliable();else throw std::runtime_error("mode");
        std::set<unsigned> taken;bool bad=false;unsigned requests=0;
        auto pub=node->create_publisher<std_msgs::msg::String>("from_"+side,qos);
        std::ostringstream own_gid;for(unsigned char b:pub->get_gid().data)own_gid<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
        std::cout<<"ROS_PUBLISHER run="<<run<<" side="<<side<<" gid="<<own_gid.str()<<" intra_process="<<node->get_node_options().use_intra_process_comms()<<std::endl;
        auto sub=node->create_subscription<std_msgs::msg::String>("from_"+peer,qos,
            [&](const std_msgs::msg::String& msg,const rclcpp::MessageInfo& info) {
                const auto prefix=run+":"+peer+":";
                if(msg.data.rfind(prefix,0)!=0){bad=true;return;}
                unsigned ordinal=static_cast<unsigned>(std::stoul(msg.data.substr(prefix.size())));
                if(ordinal<1||ordinal>5||!taken.insert(ordinal).second){bad=true;return;}
                std::ostringstream id;for(unsigned char b:info.get_rmw_message_info().publisher_gid.data)id<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
                std::cout<<"ROS_TAKE run="<<run<<" side="<<side<<" ordinal="<<ordinal<<" gid="<<id.str()<<" payload="<<msg.data<<std::endl;
            });
        rclcpp::Service<example_interfaces::srv::AddTwoInts>::SharedPtr service;
        rclcpp::Client<example_interfaces::srv::AddTwoInts>::SharedPtr client;
        if(side=="B")service=node->create_service<example_interfaces::srv::AddTwoInts>("add",
            [&](const std::shared_ptr<example_interfaces::srv::AddTwoInts::Request> q,std::shared_ptr<example_interfaces::srv::AddTwoInts::Response> a){a->sum=q->a+q->b;++requests;std::cout<<"ROS_SERVICE_REQUEST run="<<run<<" a="<<q->a<<" b="<<q->b<<" sum="<<a->sum<<std::endl;});
        else client=node->create_client<example_interfaces::srv::AddTwoInts>("add");
        const auto deadline=std::chrono::steady_clock::now()+30s;
        while(pub->get_subscription_count()<1&&std::chrono::steady_clock::now()<deadline){rclcpp::spin_some(node);std::this_thread::sleep_for(1ms);}
        if(pub->get_subscription_count()!=1)throw std::runtime_error("remote match missing");
        const auto peer_name=ns+"/transport_"+peer;
        bool graph=false;
        while(!graph&&std::chrono::steady_clock::now()<deadline){auto names=node->get_node_names();graph=std::find(names.begin(),names.end(),peer_name)!=names.end();if(!graph){rclcpp::spin_some(node);std::this_thread::sleep_for(1ms);}}
        if(!graph)throw std::runtime_error("ROS graph peer missing");
        std::cout<<"ROS_GRAPH run="<<run<<" side="<<side<<" peer="<<peer_name<<" visible=1 subscription_count="<<pub->get_subscription_count()<<std::endl;
        for(unsigned i=1;i<=5;++i){std_msgs::msg::String m;m.data=run+":"+side+":"+std::to_string(i);pub->publish(m);std::cout<<"ROS_WRITE run="<<run<<" side="<<side<<" ordinal="<<i<<" payload="<<m.data<<std::endl;}
        bool service_done=false;
        if(side=="A") {
            while(!client->service_is_ready()&&std::chrono::steady_clock::now()<deadline){rclcpp::spin_some(node);std::this_thread::sleep_for(1ms);}
            if(!client->service_is_ready())throw std::runtime_error("service discovery missing");
            auto q=std::make_shared<example_interfaces::srv::AddTwoInts::Request>();q->a=3;q->b=4;auto future=client->async_send_request(q);
            while(future.wait_for(0s)!=std::future_status::ready&&std::chrono::steady_clock::now()<deadline){rclcpp::spin_some(node);std::this_thread::sleep_for(1ms);}
            if(future.wait_for(0s)!=std::future_status::ready)throw std::runtime_error("service result missing");
            const auto response=future.get();if(response->sum!=7)throw std::runtime_error("service result wrong");
            service_done=true;std::cout<<"ROS_SERVICE_RESPONSE run="<<run<<" sum="<<response->sum<<std::endl;
        }
        while((taken.size()<5||(side=="B"&&requests!=1))&&std::chrono::steady_clock::now()<deadline){rclcpp::spin_some(node);std::this_thread::sleep_for(1ms);}
        if(bad||taken.size()!=5||(side=="A"&&!service_done)||(side=="B"&&requests!=1))throw std::runtime_error("sample/service ledger incomplete");
        result=0;
        std::cout<<"ROS_BUSINESS_DONE run="<<run<<" side="<<side<<" writes=5 takes="<<taken.size()<<std::endl;
    }catch(const std::exception& e){std::cerr<<"ROS_FAILURE "<<e.what()<<std::endl;}
    rclcpp::shutdown();std::cout<<"ROS_CONTEXT_CLOSED run="<<run<<" side="<<side<<" result="<<result<<std::endl;return result;
}
