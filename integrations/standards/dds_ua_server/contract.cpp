#include "profile.hpp"
#include "ua_type.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
int main(){try{
    d2::RecordType type;type.init(2);UA_Byte bytes[]={0,127,255};d2::UaRecord input{UINT32_MAX,0,{3,bytes},{3,bytes}};
    UA_Variant source;UA_Variant_init(&source);UA_Variant_setScalar(&source,&input,&type.type);source.storageType=UA_VARIANT_DATA_NODELETE;
    UA_Variant copy;UA_Variant_init(&copy);if(UA_Variant_copy(&source,&copy)!=UA_STATUSCODE_GOOD)throw std::runtime_error("type deep copy");
    auto& out=*static_cast<d2::UaRecord*>(copy.data);if(out.id!=UINT32_MAX||out.label.data==bytes||out.payload.data==bytes||out.payload.length!=3||out.label.data[2]!=255)throw std::runtime_error("type padding/ownership");UA_Variant_clear(&copy);
    auto stop=(std::filesystem::current_path()/"contract.stop").string();
    std::string valid="direction=dds_to_ua\ndomain=219\ntopic=contract\nrepresentation=2\nport=4840\nmax_instances=16\nrun_ms=5000\nstop_file="+stop+"\n";auto path=std::filesystem::current_path()/"d2-contract.cfg";
    {std::ofstream f(path);f<<valid;}auto c=d2::load(path);if(c.capacity!=16||c.rep!=2)throw std::runtime_error("profile");
    unsigned rejected=0;for(auto extra:{std::string("unknown=1\n"),std::string("direction=ua_to_dds\n"),std::string(16385,'x')}){std::ofstream f(path);f<<valid<<extra;f.close();try{d2::load(path);}catch(...){++rejected;}}
    auto relative=valid;relative.replace(relative.find(stop),stop.size(),"relative.stop");{std::ofstream f(path);f<<relative;}try{d2::load(path);}catch(...){++rejected;}
    if(rejected!=4)throw std::runtime_error("strict config");std::cout<<"D2_CONTRACT PASS"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<"D2_CONTRACT FAIL "<<e.what()<<std::endl;return 1;}}
