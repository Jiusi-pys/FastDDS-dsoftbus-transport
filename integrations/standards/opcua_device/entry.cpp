// Select one unchanged application entry point and supply only its public
// participant transport. All application DDS entities die before Runtime close.
#include "../cpp_backend/participant_transport.hpp"
#include "../web_gateway/c4_transport.hpp"
#include "../web_gateway/c4_shutdown.hpp"
#define main d5_application_main
#include D5_APP_SOURCE
#undef main
int main(int argc,char** argv){
    std::unique_ptr<c4::Transport> native;
    try{
        if(argc<3)throw std::runtime_error("usage: D5_EXEC TRANSPORT_CONFIG APP_ARGS...");
        native=std::make_unique<c4::Transport>(argv[1]);native->start();
        int result;
        {
            standards_transport::Scope scope([&](unsigned domain,eprosima::fastdds::dds::DomainParticipantQos& q){native->configure(domain,q);});
            // Keep the conventional argv[0] for the selected application.
            std::vector<char*> args{argv[0]};for(int i=2;i<argc;++i)args.push_back(argv[i]);args.push_back(nullptr);
            result=d5_application_main(static_cast<int>(args.size()-1),args.data());
        }
        auto shutdown=c4::finish_transport([&]{return native->close();},[&]{return native->healthy();});
        if(!shutdown.closed||!shutdown.healthy||result){std::cerr<<"D5_SHUTDOWN FAIL application="<<result<<std::endl;return 1;}
        std::cout<<"D5_SHUTDOWN PASS application=0"<<std::endl;return 0;
    }catch(const std::exception& e){std::cerr<<"D5_FAIL "<<e.what()<<std::endl;if(native&&!native->close())std::_Exit(1);return 1;}
    catch(...){std::cerr<<"D5_FAIL unknown"<<std::endl;if(native&&!native->close())std::_Exit(1);return 1;}
}
