#include "c4_transport.hpp"
#include "c4_shutdown.hpp"
#include "peer_run.hpp"
#include <filesystem>
int main(int argc,char** argv) {
    std::unique_ptr<c4::Transport> transport;
    try {
        c4::dd::Require(argc==3,"usage: c4_peer TRANSPORT_CONFIG JOB_DIRECTORY");
        std::string directory=argv[2];c4::dd::Require(directory.rfind("/data/local/tmp/",0)==0,"invalid job directory");
        transport=std::make_unique<c4::Transport>(argv[1]);transport->start();
        std::cout<<"C4_PEER_READY\n"<<std::flush;
        std::string command;
        while(!transport->stopping()&&std::getline(std::cin,command)) {
            std::istringstream input(command);std::string id,role,domain,topic,rep,type,extra;
            input>>id>>role>>domain>>topic>>rep>>type;
            c4::dd::Require(!input.fail()&&!(input>>extra)&&std::regex_match(id,std::regex("[0-9a-f]{32}"))&&std::regex_match(topic,std::regex("[A-Za-z0-9_]{1,64}")),"invalid peer job");
            c4::dd::Require((role=="writer"||role=="reader"||role=="reader-ready")&&(domain=="219"||domain=="223")&&(rep=="0"||rep=="2")&&type=="Record","peer job outside profile");
            auto base=directory+"/"+id;
            std::ofstream output(base+".log");c4::dd::Require(bool(output),"peer log creation failed");
            std::vector<std::string> args={"c4_peer",role,domain,topic,rep,type};std::vector<char*> pointers;
            for(auto& arg:args)pointers.push_back(arg.data());
            PeerTransport profile{[&](unsigned d,c4::fd::DomainParticipantQos& q){transport->configure(d,q);},[&]{return transport->healthy()&&!transport->stopping();}};
            int result=run_peer(static_cast<int>(pointers.size()),pointers.data(),profile,output,output);output.flush();output.close();
            std::ofstream done(base+".exit");done<<result;done.close();
            std::cout<<"C4_JOB_DONE id="<<id<<" exit="<<result<<"\n"<<std::flush;
        }
        auto shutdown=c4::finish_transport([&]{return transport->close();},[&]{return transport->healthy();});
        if(!shutdown.success())throw std::runtime_error("peer transport close/health failed");
        std::cout<<"C4_PEER_SHUTDOWN PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<"C4_PEER FAIL "<<e.what()<<"\n"<<std::flush;if(transport&&!transport->close())std::_Exit(1);return 1;}
}
