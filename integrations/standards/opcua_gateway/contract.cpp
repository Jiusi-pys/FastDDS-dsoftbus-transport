#include "mapping.hpp"
#include <future>
#include <iostream>
#include <thread>
static void check(bool ok,const char* what) { if(!ok) throw std::runtime_error(what); }
int main() {
    try {
        using namespace d1; unsigned checks=0;
        auto expect=[&](bool ok,const char* text){check(ok,text);++checks;};
        b4_record value{},copy{}; value.id=7; value.sequence_number=UINT32_MAX;
        Queue queue(1); expect(queue.push(value),"queue push"); value.id=9;
        expect(!queue.push(value),"overflow explicit rejection");
        expect(queue.pop(copy) && copy.id==7 && copy.sequence_number==UINT32_MAX,"copied FIFO ownership");
        queue.close(); expect(!queue.pop(copy) && !queue.push(value) && queue.peak()==1,"closed queue and highwater");
        Queue waiting(2); auto waiter=std::async(std::launch::async,[&]{b4_record r{};return waiting.pop(r);});
        waiting.close(); expect(waiter.wait_for(std::chrono::seconds(1))==std::future_status::ready && !waiter.get(),"close wakes consumer");
        Queue ordered(256); for(unsigned i=0;i<128;++i) { value.id=i;expect(ordered.push(value),"bounded fill"); }
        ordered.close(); std::thread consumer([&]{for(unsigned i=0;i<128;++i){b4_record r{};if(!ordered.pop(r)||r.id!=i) std::terminate();}}); consumer.join();
        expect(ordered.peak()==128,"FIFO across thread ownership");
        Item item{1,1001,17,"sensor"}; UA_DataValue data;UA_DataValue_init(&data); UA_UInt32 scalar=UINT32_MAX;
        UA_Variant_setScalar(&data.value,&scalar,&UA_TYPES[UA_TYPES_UINT32]);data.hasValue=true;
        expect(map_value(item,data,copy)==Failure::none && copy.id==17 && copy.sequence_number==UINT32_MAX && copy.label_size==6 && copy.payload_size==0,"UInt32 mapping");
        Item oversized{1,1001,17,std::string(65,'x')};expect(map_value(oversized,data,copy)==Failure::type,"mapping rejects oversized label independently");
        data.hasStatus=true;data.status=UA_STATUSCODE_BADNOTCONNECTED;auto before=copy;
        expect(map_value(item,data,copy)==Failure::quality && std::memcmp(&before,&copy,sizeof(copy))==0,"bad quality unchanged");
        data.status=UA_STATUSCODE_UNCERTAIN;expect(map_value(item,data,copy)==Failure::quality,"uncertain rejected");
        data.hasStatus=false;UA_Int32 wrong=-1;UA_Variant_setScalar(&data.value,&wrong,&UA_TYPES[UA_TYPES_INT32]);
        expect(map_value(item,data,copy)==Failure::type,"unsafe numeric cast rejected");
        UA_UInt32 array[2]={1,2};UA_Variant_setArray(&data.value,array,2,&UA_TYPES[UA_TYPES_UINT32]);
        expect(map_value(item,data,copy)==Failure::type,"array rejected");data.hasValue=false;
        expect(map_value(item,data,copy)==Failure::type,"empty value rejected");
        std::string valid="direction=ua_to_dds\nendpoint=opc.tcp://127.0.0.1:4840\ndomain=219\ntopic=d1_contract\nrepresentation=2\nqueue_capacity=16\nrequest_timeout_ms=500\nreconnect_ms=200\nretry_limit=5\npublish_ms=50\nsampling_ms=10\nua_queue_size=16\nmatch_timeout_ms=500\nrun_ms=5000\nstop_file=d1_contract.stop\nitem=1;1001;17;sensor\n";
        auto path=std::filesystem::current_path()/"d1_contract.cfg";
        auto write=[&](const std::string& text){std::ofstream f(path,std::ios::binary);f<<text;f.close();check(bool(f),"test config write");};
        write(valid);auto c=load(path);expect(c.items.size()==1 && c.capacity==16 && c.domain==219,"valid directional profile");
        auto reject=[&](std::string text){write(text);bool failed=false;try{load(path);}catch(const std::exception&){failed=true;}expect(failed,"invalid config must reject");};
        reject(valid+"unknown=1\n");reject(valid+"direction=ua_to_dds\n");reject(valid+"item=1;1001;18;duplicate\n");
        reject(valid+"item=1;1002;17;duplicatekey\n");
        for(auto pair:{std::pair<const char*,const char*>{"direction=ua_to_dds","direction=dds_to_ua"},{"queue_capacity=16","queue_capacity=257"},{"endpoint=opc.tcp://127.0.0.1:4840","endpoint=opc.tcp://example.com:4840"},{"representation=2","representation=1"},{"domain=219","domain=226"}}) {
            auto changed=valid;changed.replace(changed.find(pair.first),std::strlen(pair.first),pair.second);reject(changed);
        }
        reject(valid+"item=1;1002;18;"+std::string(65,'x')+"\n");
        write(std::string(16385,'x'));bool large=false;try{load(path);}catch(...){large=true;}expect(large,"config size bound");
        std::cout<<"D1_CONTRACT PASS checks="<<checks<<std::endl;return 0;
    } catch(const std::exception& e) { std::cerr<<"D1_CONTRACT FAIL "<<e.what()<<std::endl;return 1; }
}
