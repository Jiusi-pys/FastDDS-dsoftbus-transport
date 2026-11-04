#pragma once
// Ordinary rclcpp business only. Native ending/replacement belongs to the bridge.
#include <array>
#include <functional>

inline void BoundedReacceptRos(const std::shared_ptr<rclcpp::Node>& node,const std::string& side,
        const std::string& run,long pid,const std::function<bool(const std::string&,const std::function<void()>&)>& stage,
        bool route_return=false){
    using Message=std_msgs::msg::String;
    using Service=example_interfaces::srv::AddTwoInts;
    struct Entities {
        std::string qos,gid;
        rclcpp::Publisher<Message>::SharedPtr publisher;
        rclcpp::Subscription<Message>::SharedPtr reader;
        std::set<unsigned> taken;
    };
    std::array<Entities,2> entities{};const auto peer=side=="A"?"B":"A";bool bad=false;
    const auto spin=[&]{rclcpp::spin_some(node);};
    auto fail=[&](const char* point,const char* reason){ros_failure_protocol::Fail(run,side,pid,point,reason);};
    for(unsigned i=0;i<2;++i){auto& item=entities[i];item.qos=i?"best_effort":"reliable";
        auto qos=rclcpp::QoS(rclcpp::KeepLast(10)).durability_volatile();if(i)qos.best_effort();else qos.reliable();
        item.publisher=node->create_publisher<Message>(item.qos+"/from_"+side,qos);
        std::ostringstream gid;for(auto byte:item.publisher->get_gid().data)gid<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);item.gid=gid.str();
        std::cout<<"ROS_PUBLISHER run="<<run<<" side="<<side<<" gid="<<item.gid<<" intra_process=0 phase="<<item.qos<<" topic="<<item.publisher->get_topic_name()<<std::endl;
        auto* capture=&item;
        item.reader=node->create_subscription<Message>(item.qos+"/from_"+peer,qos,[&,capture](const Message& message,const rclcpp::MessageInfo& info){
            const auto prefix=run+":"+capture->qos+":"+peer+":";
            if(message.data.rfind(prefix,0)!=0){bad=true;return;}
            const auto ordinal=static_cast<unsigned>(std::stoul(message.data.substr(prefix.size())));
            if((ordinal<1||ordinal>5)&&!(route_return&&ordinal==1001)){bad=true;return;}
            if(!capture->taken.insert(ordinal).second){bad=true;return;}
            std::ostringstream source;for(auto byte:info.get_rmw_message_info().publisher_gid.data)source<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);
            std::cout<<"ROS_TAKE run="<<run<<" side="<<side<<" ordinal="<<ordinal<<" gid="<<source.str()<<" payload="<<message.data<<" phase="<<capture->qos<<" segment="<<(ordinal==1001?"OFFLINE":ordinal<=2?"PRE":"POST")
                <<" topic="<<capture->qos<<"/from_"<<peer<<" publication_sequence="<<info.get_rmw_message_info().publication_sequence_number<<std::endl;
        });
    }
    unsigned requests=0;
    rclcpp::Service<Service>::SharedPtr server;rclcpp::Client<Service>::SharedPtr client;
    if(side=="B")server=node->create_service<Service>("bounded_add",[&](const std::shared_ptr<rmw_request_id_t> id,const std::shared_ptr<Service::Request> request,std::shared_ptr<Service::Response> response){
        response->sum=request->a+request->b;++requests;
        std::ostringstream writer;for(auto byte:id->writer_guid)writer<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);
        std::cout<<"ROS_SERVICE_REQUEST run="<<run<<" phase="<<(requests==1?"PRE":"POST")<<" request_id="<<id->sequence_number<<" writer="<<writer.str()<<" a="<<request->a<<" b="<<request->b<<" sum="<<response->sum<<std::endl;
    });else client=node->create_client<Service>("bounded_add");
    const auto until=[&](const std::function<bool()>& ready,const char* point){
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(!ready()&&std::chrono::steady_clock::now()<end){spin();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        if(!ready()||bad)fail(point,"BUSINESS_OR_GRAPH_NOT_READY");
    };
    until([&]{return entities[0].publisher->get_subscription_count()==1&&entities[1].publisher->get_subscription_count()==1&&(!client||client->service_is_ready());},"PRE_MATCH");
    const auto graph=[&](const char* segment){
        const auto expected="/fdds_"+run+"/transport_"+peer;
        until([&]{const auto names=node->get_node_names();return std::find(names.begin(),names.end(),expected)!=names.end();},"GRAPH");
        for(const auto& item:entities)std::cout<<"ROS_GRAPH run="<<run<<" side="<<side<<" peer="<<expected<<" visible=1 subscription_count="<<item.publisher->get_subscription_count()<<" phase="<<item.qos<<" segment="<<segment<<std::endl;
    };
    const auto anchors=[&](const char* point){
        for(const auto& item:entities)std::cout<<"ROS_REACCEPT_ANCHOR run="<<run<<" side="<<side<<" pid="<<pid<<" point="<<point<<" phase="<<item.qos<<" node="<<node.get()<<" publisher="<<item.publisher.get()<<" subscriber="<<item.reader.get()<<" context="<<node->get_node_base_interface()->get_context().get()<<" service="<<server.get()<<" client="<<client.get()<<" gid="<<item.gid<<std::endl;
    };
    const auto business=[&](const char* segment,unsigned first,unsigned last){
        for(auto& item:entities)for(unsigned ordinal=first;ordinal<=last;++ordinal){
            Message message;message.data=run+":"+item.qos+":"+side+":"+std::to_string(ordinal);item.publisher->publish(message);
            std::cout<<"ROS_WRITE run="<<run<<" side="<<side<<" ordinal="<<ordinal<<" payload="<<message.data<<" phase="<<item.qos<<" segment="<<segment<<" topic="<<item.publisher->get_topic_name()<<std::endl;
        }
        until([&]{for(const auto& item:entities)for(unsigned ordinal=first;ordinal<=last;++ordinal)
            if(!item.taken.count(ordinal))return false;return true;},segment);
        if(client){
            until([&]{return client->service_is_ready();},"SERVICE");
            auto request=std::make_shared<Service::Request>();request->a=first==1?3:5;request->b=first==1?4:6;
            auto future=client->async_send_request(request);
            std::cout<<"ROS_SERVICE_SENT run="<<run<<" phase="<<segment<<" request_id="<<future.request_id<<" a="<<request->a<<" b="<<request->b<<std::endl;
            until([&]{return future.wait_for(std::chrono::seconds(0))==std::future_status::ready;},"SERVICE");
            const auto result=future.get();if(result->sum!=request->a+request->b)fail("SERVICE","RESULT_MISMATCH");
            std::cout<<"ROS_SERVICE_RESPONSE run="<<run<<" phase="<<segment<<" request_id="<<future.request_id<<" sum="<<result->sum<<std::endl;
        }else until([&]{return requests==(first==1?1u:2u);},"SERVICE");
        std::cout<<"ROS_SEGMENT_DONE run="<<run<<" side="<<side<<" segment="<<segment<<" writes="<<(last-first+1)*2<<" takes="<<(last-first+1)*2<<std::endl;
    };
    graph("PRE");anchors("BEFORE");
    if(!stage("PRE_READY",spin))fail("PRE_READY","CONTROL_RELEASE_MISSING");
    business("PRE",1,2);
    if(!stage("PRE_DONE",spin))fail("PRE_DONE","CONTROL_RELEASE_MISSING");
    if(route_return){
        if(!stage("OFFLINE_READY",spin))fail("OFFLINE_READY","CONTROL_RELEASE_MISSING");
        // Ordinary ROS writes, after controller observes product invalidation.
        // publish return is separate from queue/native acceptance. Reliable
        // history may deliver this sample after return; it never counts as POST.
        for(auto& item:entities){Message message;message.data=run+":"+item.qos+":"+side+":1001";
            item.publisher->publish(message);
            std::cout<<"ROS_WRITE run="<<run<<" side="<<side<<" ordinal=1001 payload="<<message.data
                <<" phase="<<item.qos<<" segment=OFFLINE topic="<<item.publisher->get_topic_name()
                <<" publish_return=OK meaning=ROS_API_NOT_NATIVE_ACCEPTANCE"<<std::endl;
        }
        if(!stage("OFFLINE_DONE",spin))fail("OFFLINE_DONE","CONTROL_RELEASE_MISSING");
    }
    if(!stage("REACCEPT_DONE",spin))fail("REACCEPT_DONE","CONTROL_RELEASE_MISSING");
    anchors("AFTER");graph("POST");
    business("POST",3,5);
    if(!stage("POST_DONE",spin))fail("POST_DONE","CONTROL_RELEASE_MISSING");
    if(!stage("TEARDOWN_READY",spin))fail("TEARDOWN_READY","CLEANUP_CONTROL_MISSING");
}
