// Independent controlled UA fixture. No DDS, B4, or gateway headers/libraries.
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <stdexcept>
#include <thread>
static void good(UA_StatusCode code) { if(code!=UA_STATUSCODE_GOOD) throw std::runtime_error("UA status "+std::to_string(code)); }
int main(int argc,char** argv) {
    UA_Server* server=nullptr;bool started=false;int result=0;
    try {
        if(argc!=3) throw std::runtime_error("usage: d1_ua_server PORT CONTROL_DIRECTORY");
        unsigned port=std::stoul(argv[1]);if(port<1024 || port>65535) throw std::runtime_error("port bound");
        std::filesystem::path directory=argv[2];if(!std::filesystem::is_directory(directory)) throw std::runtime_error("control directory");
        server=UA_Server_new();if(!server) throw std::bad_alloc();
        auto* config=UA_Server_getConfig(server);good(UA_ServerConfig_setMinimal(config,static_cast<UA_UInt16>(port),nullptr));
        UA_Array_delete(config->serverUrls,config->serverUrlsSize,&UA_TYPES[UA_TYPES_STRING]);
        config->serverUrls=static_cast<UA_String*>(UA_Array_new(1,&UA_TYPES[UA_TYPES_STRING]));
        if(!config->serverUrls) { config->serverUrlsSize=0;throw std::bad_alloc(); }
        config->serverUrlsSize=1;config->serverUrls[0]=UA_STRING_ALLOC(("opc.tcp://127.0.0.1:"+std::to_string(port)).c_str());
        config->publishingIntervalLimits.min=50;config->samplingIntervalLimits.min=10;
        for(unsigned node:{1001u,1002u,2001u,2002u}) {
            UA_VariableAttributes a=UA_VariableAttributes_default; UA_UInt32 v=node==1002?8:7;UA_Int32 wrong=-1;
            if(node==2001) { UA_Variant_setScalar(&a.value,&wrong,&UA_TYPES[UA_TYPES_INT32]);a.dataType=UA_TYPES[UA_TYPES_INT32].typeId; }
            else { UA_Variant_setScalar(&a.value,&v,&UA_TYPES[UA_TYPES_UINT32]);a.dataType=UA_TYPES[UA_TYPES_UINT32].typeId; }
            a.displayName=UA_LOCALIZEDTEXT(const_cast<char*>("en"),const_cast<char*>("D1 fixture value"));
            a.accessLevel=UA_ACCESSLEVELMASK_READ;a.valueRank=UA_VALUERANK_SCALAR;
            good(UA_Server_addVariableNode(server,UA_NODEID_NUMERIC(1,node),UA_NODEID_NUMERIC(0,UA_NS0ID_OBJECTSFOLDER),UA_NODEID_NUMERIC(0,UA_NS0ID_ORGANIZES),UA_QUALIFIEDNAME(1,const_cast<char*>("value")),UA_NODEID_NUMERIC(0,UA_NS0ID_BASEDATAVARIABLETYPE),a,nullptr,nullptr));
        }
        UA_DataValue bad;UA_DataValue_init(&bad);bad.hasStatus=true;bad.status=UA_STATUSCODE_BADNOCOMMUNICATION;
        good(UA_Server_writeDataValue(server,UA_NODEID_NUMERIC(1,2002),bad));
        good(UA_Server_run_startup(server));started=true;
        std::cout<<"D1_UA_SERVER_READY version="<<UA_OPEN62541_VER_MAJOR<<'.'<<UA_OPEN62541_VER_MINOR<<'.'<<UA_OPEN62541_VER_PATCH<<" port="<<port<<std::endl;
        uint64_t serial=0;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
        while(!std::filesystem::exists(directory/"stop")) {
            if(std::chrono::steady_clock::now()>=deadline) throw std::runtime_error("fixture deadline");
            UA_Server_run_iterate(server,false);
            std::ifstream command(directory/("command-"+std::to_string(serial+1)));uint64_t next=0,value=0;unsigned node=0;
            if(command>>next>>node>>value && next>serial) {
                if((node!=1001 && node!=1002)||value>UINT32_MAX) throw std::runtime_error("fixture command bound");
                UA_UInt32 scalar=static_cast<UA_UInt32>(value);UA_Variant variant;UA_Variant_init(&variant);
                UA_Variant_setScalar(&variant,&scalar,&UA_TYPES[UA_TYPES_UINT32]);
                good(UA_Server_writeValue(server,UA_NODEID_NUMERIC(1,node),variant));serial=next;
                std::cout<<"D1_UA_WRITE serial="<<serial<<" node="<<node<<" value="<<value<<std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch(const std::exception& e) { std::cerr<<"D1_UA_SERVER_FAIL "<<e.what()<<std::endl;result=3; }
    if(server) { if(started && UA_Server_run_shutdown(server)!=UA_STATUSCODE_GOOD) result=3;UA_Server_delete(server); }
    std::cout<<"D1_UA_SERVER_CLOSED exit="<<result<<std::endl;return result;
}
