#include "model.hpp"
#include "profile.hpp"
#include B3_HEADER
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/core/LoanableSequence.hpp>
#include <fastdds/dds/core/condition/WaitSet.hpp>
#include <fastdds/dds/core/condition/StatusCondition.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
static const auto ok=eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK;
static const auto no_data=eprosima::fastrtps::types::ReturnCode_t::RETCODE_NO_DATA;
static const auto timeout_code=eprosima::fastrtps::types::ReturnCode_t::RETCODE_TIMEOUT;
#else
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
static const auto ok=eprosima::fastdds::dds::RETCODE_OK;
static const auto no_data=eprosima::fastdds::dds::RETCODE_NO_DATA;
static const auto timeout_code=eprosima::fastdds::dds::RETCODE_TIMEOUT;
#endif
#include <openssl/crypto.h>
#include <openssl/sha.h>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <functional>
#include <iomanip>
namespace c1 {
namespace fd=eprosima::fastdds::dds;
using Sample=Standards::BoundedRecord;
using Clock=std::chrono::steady_clock;
struct Node {
    std::string category,name,owner,wire_name,type_ref,condition_ref;
    bool closing=false,dead=false,status_condition=false,registered=false;
    unsigned domain=0;
    Clock::time_point expires;
    std::chrono::system_clock::time_point modified=std::chrono::system_clock::now();
    std::weak_ptr<Node> parent,topic;
    std::map<std::string,std::shared_ptr<Node>> children;
    fd::DomainParticipant* participant=nullptr;
    fd::Publisher* publisher=nullptr;fd::Subscriber* subscriber=nullptr;
    fd::Topic* topic_dds=nullptr;fd::DataWriter* writer=nullptr;fd::DataReader* reader=nullptr;
    fd::TypeSupport type;
};
namespace {
const std::map<std::string,std::string> tags={{"applications","application"},{"domain_participants","domain_participant"},
 {"registered_types","register_type"},{"topics","topic"},{"publishers","publisher"},{"subscribers","subscriber"},
 {"data_writers","data_writer"},{"data_readers","data_reader"},{"waitsets","waitset"}};
std::string trim(std::string s) {
    auto a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");
    return a==std::string::npos?"":s.substr(a,b-a+1);
}
bool collection_allowed(const std::string& parent,const std::string& category) {
    return (parent.empty()&&category=="applications")||(parent=="applications"&&(category=="domain_participants"||category=="waitsets"))||
      (parent=="domain_participants"&&(category=="topics"||category=="registered_types"||category=="publishers"||category=="subscribers"))||
      (parent=="publishers"&&category=="data_writers")||(parent=="subscribers"&&category=="data_readers");
}
std::string lower(std::string s) {for(auto& c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return s;}
bool accepts_xml(const std::string& header) {
    std::istringstream in(header);std::string part;
    while(std::getline(in,part,',')) {
        auto at=part.find(';');if(lower(trim(part.substr(0,at)))!="application/dds-web+xml")continue;
        bool enabled=true;
        if(at!=std::string::npos) {
            std::istringstream params(part.substr(at+1));std::string param;
            while(std::getline(params,param,';')) {
                param=lower(trim(param));if(param.rfind("q=",0)==0) {
                    size_t used=0;double q=0;try{q=std::stod(param.substr(2),&used);}catch(...){return false;}
                    if(used!=param.size()-2||!std::isfinite(q)||q<=0||q>1)enabled=false;
                }
            }
        }
        if(enabled)return true;
    }
    return false;
}
std::vector<std::string> split(const std::string& s,char separator) {
    std::vector<std::string> v;std::istringstream in(s);std::string item;
    while(std::getline(in,item,separator))v.push_back(item);return v;
}
bool name_ok(const std::string& n) {
    return !n.empty() && n.size()<=48 && std::all_of(n.begin(),n.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');});
}
unsigned integer(const std::string& s,unsigned max) {
    unsigned n=0;auto r=std::from_chars(s.data(),s.data()+s.size(),n);
    need(!s.empty() && r.ec==std::errc{} && r.ptr==s.data()+s.size() && n<=max,422,"integer outside profile");return n;
}
double seconds(const std::string& s) {
    size_t end=0;double value=0;
    try {value=std::stod(s,&end);}catch(...) {throw Error(422,"invalid timeout");}
    need(end==s.size() && std::isfinite(value) && value>=0 && value<=2,422,"timeout outside profile");return value;
}
std::string decode(const std::string& s,bool query) {
    std::string result;
    for(size_t i=0;i<s.size();++i) {
        unsigned char c=s[i];
        if(c=='%') {
            need(i+2<s.size(),422,"invalid URI escape");
            auto digit=[](char d)->int {return d>='0'&&d<='9'?d-'0':d>='A'&&d<='F'?d-'A'+10:d>='a'&&d<='f'?d-'a'+10:-1;};
            int a=digit(s[i+1]),b=digit(s[i+2]);need(a>=0 && b>=0,422,"invalid URI escape");
            c=static_cast<unsigned char>((a<<4)|b);i+=2;
        } else if(query && c=='+')c=' ';
        need(c>=32 && c<127 && c!='#' && (query || c!='/'),422,"URI outside profile");result+=char(c);
    }
    return result;
}
struct Target {std::vector<std::string> path;std::map<std::string,std::string> query;};
Target target(const std::string& raw) {
    need(raw.size()<=2048 && raw.find('#')==std::string::npos,422,"invalid target");
    auto at=raw.find('?');auto path=raw.substr(0,at);
    constexpr const char* prefix="/dds/rest1/";
    need(path.rfind(prefix,0)==0,404,"resource not found");path=path.substr(std::char_traits<char>::length(prefix));
    if(!path.empty()&&path.back()=='/')path.pop_back();
    Target t;
    for(const auto& part:split(path,'/')) {auto d=decode(part,false);need(name_ok(d)||d=="domain_participants"||d=="registered_types"||d=="data_writers"||d=="data_readers",422,"resource name outside profile");t.path.push_back(d);}
    need(!t.path.empty(),404,"resource not found");
    // Tables 4 and 5 disagree on this segment. Canonical locations use Table 4.
    if(t.path.size()>2 && t.path[0]=="applications" && t.path[2]=="participants")t.path[2]="domain_participants";
    if(at!=std::string::npos) for(const auto& part:split(raw.substr(at+1),'&')) {
        auto eq=part.find('=');need(eq!=std::string::npos,422,"query requires value");
        auto k=decode(part.substr(0,eq),true),v=decode(part.substr(eq+1),true);
        need(t.query.emplace(k,v).second,422,"duplicate query parameter");
    }
    return t;
}
bool glob(const std::string& text,const std::string& pattern) {
    need(pattern.find_first_of("[]\\")==std::string::npos,422,"pattern outside profile");
    size_t i=0,j=0,star=std::string::npos,saved=0;
    while(i<text.size()) {
        if(j<pattern.size()&&(pattern[j]=='?'||pattern[j]==text[i])){++i;++j;}
        else if(j<pattern.size()&&pattern[j]=='*'){star=j++;saved=i;}
        else if(star!=std::string::npos){j=star+1;i=++saved;}
        else return false;
    }
    while(j<pattern.size()&&pattern[j]=='*')++j;return j==pattern.size();
}
std::string path_for(std::shared_ptr<Node> n) {
    if(n->category.empty())return "/dds/rest1";
    return path_for(n->parent.lock())+"/"+n->category+"/"+n->name;
}
unsigned count(const std::shared_ptr<Node>& node,const std::string& category="") {
    unsigned total=category.empty()||node->category==category?1:0;
    for(const auto& item:node->children)total+=count(item.second,category);return total;
}
unsigned entity_count(xmlNode* xml,bool participants=false) {
    unsigned total=0;
    for(const auto& tag:tags)if(local(xml)==tag.second&&(!participants||tag.first=="domain_participants"))++total;
    for(auto* child:elements(xml))total+=entity_count(child,participants);
    return total;
}
bool domains_allowed(const std::shared_ptr<Node>& node,const Principal& principal) {
    if(node->category=="domain_participants"&&!principal.domains.count(node->domain))return false;
    for(const auto& child:node->children)if(!domains_allowed(child.second,principal))return false;
    return true;
}
bool has_closing(const std::shared_ptr<Node>& node) {
    if(node->closing)return true;
    for(const auto& child:node->children)if(has_closing(child.second))return true;
    return false;
}
void mark_modified(std::shared_ptr<Node> node) {
    auto now=std::chrono::system_clock::now();
    while(node){node->modified=now;node=node->parent.lock();}
}
bool same_representation(const std::shared_ptr<Node>& node,xmlNode* xml) {
    if(node->name!=attribute(xml,"name"))return false;
    if(node->category=="domain_participants"&&node->domain!=integer(attribute(xml,"domain_id"),232))return false;
    if(node->category=="topics"&&node->type_ref!=attribute(xml,"register_type_ref"))return false;
    if((node->category=="data_writers"||node->category=="data_readers")&&node->type_ref!=attribute(xml,"topic_ref"))return false;
    bool status=false;size_t children=0;
    for(auto* child:elements(xml)) {
        if(local(child)=="status_condition"){status=true;continue;}
        if(node->category=="waitsets") {
            if(attribute(child,"condition_ref")!=node->condition_ref)return false;
            continue;
        }
        for(const auto& tag:tags)if(local(child)==tag.second) {
            auto found=node->children.find(tag.first+"/"+attribute(child,"name"));
            if(found==node->children.end()||!same_representation(found->second,child))return false;
            ++children;
        }
    }
    if(status!=node->status_condition)return false;
    size_t expected=0;
    for(const auto& child:node->children)if(!(node->category=="applications"&&child.second->category=="waitsets"))++expected;
    return children==expected;
}
std::shared_ptr<Node> participant_of(std::shared_ptr<Node> n) {
    while(n && n->category!="domain_participants")n=n->parent.lock();
    need(bool(n),422,"participant context missing");return n;
}
std::string query_value(const std::map<std::string,std::string>& q,const std::string& key,const std::string& fallback) {
    auto it=q.find(key);return it==q.end()?fallback:it->second;
}
std::string state_metadata(const fd::SampleInfo& info) {
    std::ostringstream handle;
    for(size_t i=0;i<16;++i)handle<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(info.instance_handle.value[i]);
    auto source=info.source_timestamp;
    return "<read_sample_info><source_timestamp><sec>"+std::to_string(source.seconds())+"</sec><nanosec>"+std::to_string(source.nanosec())+
        "</nanosec></source_timestamp><valid_data>"+(info.valid_data?"true":"false")+"</valid_data><instance_handle>"+handle.str()+
        "</instance_handle><instance_state>"+(info.instance_state==fd::ALIVE_INSTANCE_STATE?"ALIVE":info.instance_state==fd::NOT_ALIVE_DISPOSED_INSTANCE_STATE?"NOT_ALIVE_DISPOSED":"NOT_ALIVE_NO_WRITERS")+
        "</instance_state><sample_state>"+(info.sample_state==fd::READ_SAMPLE_STATE?"READ":"NOT_READ")+"</sample_state><view_state>"+
        (info.view_state==fd::NEW_VIEW_STATE?"NEW":"NOT_NEW")+"</view_state></read_sample_info>";
}
std::string sample_xml(const Sample& value) {
    std::string result="<BoundedRecord xmlns=\""+std::string(sample_ns)+"\"><id>"+std::to_string(value.id())+
        "</id><sequence_number>"+std::to_string(value.sequence_number())+"</sequence_number><label>"+escape(value.label())+"</label><payload>";
    for(auto b:value.payload())result+="<item>"+std::to_string(unsigned(b))+"</item>";
    return result+"</payload></BoundedRecord>";
}
Sample sample_value(xmlNode* root) {
    Sample value;auto parts=elements(root);need(parts.size()==4,422,"sample field count invalid");
    auto sample_integer=[](xmlNode* node,unsigned maximum) {
        auto text=number_text(node);
        if(!text.empty()&&text[0]=='+')text.erase(0,1);
        else if(!text.empty()&&text[0]=='-') {
            text.erase(0,1);need(!text.empty()&&text.find_first_not_of('0')==std::string::npos,422,"unsigned value is negative");
        }
        return integer(text,maximum);
    };
    value.id(sample_integer(parts[0],UINT32_MAX));
    value.sequence_number(sample_integer(parts[1],UINT32_MAX));
    auto label=content(parts[2]);need(label.size()<=64 && label.find('\0')==std::string::npos,422,"label byte bound exceeded");value.label(label);
    std::vector<uint8_t> payload;
    for(auto* item:elements(parts[3]))payload.push_back(static_cast<uint8_t>(sample_integer(item,255)));
    need(payload.size()<=256,422,"payload bound exceeded");value.payload(std::move(payload));return value;
}
std::string filter_name(const std::string& category) {
    static const std::map<std::string,std::string> names={{"applications","applicationNameExpression"},{"domain_participants","participantNameExpression"},
     {"topics","topicNameExpression"},{"publishers","publisherNameExpression"},{"subscribers","subscriberNameExpression"},
     {"data_writers","datawriterNameExpression"},{"data_readers","datareaderNameExpression"},{"registered_types","registeredTypeNameExpression"},{"waitsets","waitsetNameExpression"}};
    return names.at(category);
}
}
Gateway::Gateway(const std::string& schemas,const std::string& policy,unsigned representation,unsigned ttl)
    :schemas_(schemas),root_(std::make_shared<Node>()),representation_(representation),ttl_(ttl) {
    need((representation==0||representation==2)&&ttl>=5&&ttl<=3600,500,"invalid gateway profile");
    quarantine_.reserve(128);
    std::ifstream in(policy,std::ios::binary);need(bool(in),500,"policy file missing");
    auto doc=parse(std::string((std::istreambuf_iterator<char>(in)),{}));auto* top=xmlDocGetRootElement(doc.get());
    need(local(top)=="gateway"&&ns(top).empty(),500,"policy root invalid");
    std::set<std::string> names;
    for(auto* p:elements(top)) {
        need(local(p)=="principal"&&ns(p).empty()&&elements(p).empty()&&principals_.size()<8,500,"policy principal invalid");
        Principal principal;principal.name=attribute(p,"name");principal.owner=attribute(p,"owner");if(principal.owner.empty())principal.owner=principal.name;
        need(name_ok(principal.name)&&name_ok(principal.owner)&&names.insert(principal.name).second,500,"policy identity invalid");
        auto env_name=attribute(p,"key_env");need(!env_name.empty()&&env_name.size()<=128,500,"policy key environment missing");
        auto* env=std::getenv(env_name.c_str());need(env!=nullptr,500,"API-key environment missing");std::string key(env);
        need(key.size()>=24 && key.size()<=128,500,"API-key length invalid");
        SHA256(reinterpret_cast<const unsigned char*>(key.data()),key.size(),principal.digest);
        for(const auto& old:principals_)need(CRYPTO_memcmp(old.digest,principal.digest,32)!=0,500,"duplicate API key");
        for(const auto& d:split(attribute(p,"domains"),','))principal.domains.insert(integer(trim(d),232));
        for(const auto& role:split(attribute(p,"permissions"),' ')) {
            need(role=="read"||role=="write"||role=="manage",500,"policy permission invalid");principal.permissions.insert(role);
        }
        need(!principal.domains.empty()&&!principal.permissions.empty(),500,"policy grants missing");principals_.push_back(principal);
    }
    need(!principals_.empty(),500,"policy principals missing");
    schemas_.response(type_catalog_xml);
}
Principal Gateway::authenticate(const std::string& key) const {
    need(key.size()>=24 && key.size()<=128,401,"API key required");unsigned char digest[32];SHA256(reinterpret_cast<const unsigned char*>(key.data()),key.size(),digest);
    const Principal* match=nullptr;for(const auto& p:principals_)if(CRYPTO_memcmp(p.digest,digest,32)==0)match=&p;
    need(match!=nullptr,401,"API key invalid");return *match;
}
std::shared_ptr<Node> Gateway::lookup(const std::vector<std::string>& path,bool closing) {
    need(path.size()%2==0,404,"resource not found");auto node=root_;
    for(size_t i=0;i<path.size();i+=2) {
        auto found=node->children.find(path[i]+"/"+path[i+1]);need(found!=node->children.end(),404,"resource not found");node=found->second;
        need(!node->dead && (closing||!node->closing),404,"resource closed");
    }
    return node;
}
void Gateway::check_tree(xmlNode* node,const std::string& category,const Principal& principal) {
    need(ns(node)==dds_ns&&local(node)==tags.at(category),422,"wrong entity representation");
    need(name_ok(attribute(node,"name")),422,"entity name outside profile");
    std::set<std::string> attrs={"name"};
    if(category=="domain_participants")attrs.insert("domain_id");
    if(category=="topics")attrs.insert("register_type_ref");
    if(category=="registered_types")attrs.insert("type_ref");
    if(category=="data_writers"||category=="data_readers")attrs.insert("topic_ref");
    for(auto* a=node->properties;a;a=a->next) {
        if(a->ns && std::string(reinterpret_cast<const char*>(a->ns->href))=="http://www.w3.org/2001/XMLSchema-instance")continue;
        need(!a->ns&&attrs.count(reinterpret_cast<const char*>(a->name)),422,"entity attribute outside profile");
    }
    if(category=="domain_participants")need(principal.domains.count(integer(attribute(node,"domain_id"),232)),403,"domain not permitted");
    if(category=="registered_types")need(attribute(node,"type_ref")=="Standards::BoundedRecord",422,"only catalog type Standards::BoundedRecord is supported");
    std::set<std::string> seen;
    for(auto* c:elements(node)) {
        auto tag=local(c);std::string child;
        if(category=="applications"&&tag=="domain_participant")child="domain_participants";
        if(category=="domain_participants") {
            if(tag=="register_type")child="registered_types";if(tag=="topic")child="topics";
            if(tag=="publisher")child="publishers";if(tag=="subscriber")child="subscribers";
        }
        if(category=="publishers"&&tag=="data_writer")child="data_writers";
        if(category=="subscribers"&&tag=="data_reader")child="data_readers";
        if(!child.empty()) {
            need(seen.insert(child+"/"+attribute(c,"name")).second,422,"duplicate nested entity");check_tree(c,child,principal);
        } else if(category=="data_readers"&&tag=="status_condition") {
            need(seen.insert(tag).second,422,"duplicate condition");auto v=elements(c);
            need(v.size()==1 && local(v[0])=="status" && content(v[0])=="DATA_AVAILABLE",422,"status condition outside profile");
        } else if(category=="waitsets"&&tag=="condition") {
            need(seen.insert(tag).second,422,"only one wait condition is supported");
        } else need(false,422,"entity child or QoS outside profile");
    }
}
void Gateway::check_capacity(xmlNode* xml,std::shared_ptr<Node> replaced) {
    unsigned nodes=count(root_),participants=count(root_,"domain_participants");
    for(const auto& node:quarantine_){nodes+=count(node);participants+=count(node,"domain_participants");}
    if(replaced){nodes-=count(replaced);participants-=count(replaced,"domain_participants");}
    need(nodes+entity_count(xml)<=128,429,"resource limit reached");
    need(participants+entity_count(xml,true)<=8,429,"participant limit reached");
}
std::shared_ptr<Node> Gateway::create(std::shared_ptr<Node> parent,const std::string& category,xmlNode* xml,const Principal& principal) {
    auto name=attribute(xml,"name"),key=category+"/"+name;
    need(!parent->children.count(key),409,"object already exists");
    need(count(root_)+quarantine_.size()<128,429,"resource limit reached");
    auto n=std::make_shared<Node>();n->category=category;n->name=name;n->parent=parent;n->owner=principal.owner;
    if(category=="applications") {
        unsigned owned=0;for(const auto& x:root_->children)if(x.second->owner==principal.owner)++owned;
        need(owned<4&&root_->children.size()<8,429,"application limit reached");n->expires=Clock::now()+std::chrono::seconds(ttl_);
    } else if(category=="waitsets") {
        n->condition_ref=attribute(elements(xml).at(0),"condition_ref");
        auto ref=split(n->condition_ref,':');std::vector<std::string> clean;
        for(const auto& x:ref)if(!x.empty())clean.push_back(x);
        need(clean.size()==4 && clean[3]=="status_condition",422,"condition reference outside profile");
        auto reader=lookup({"applications",parent->name,"domain_participants",clean[0],"subscribers",clean[1],"data_readers",clean[2]});
        need(reader->status_condition,422,"reader has no status condition");n->topic=reader;
    } else if(category=="domain_participants") {
        need(count(root_,category)<8,429,"participant limit reached");n->domain=integer(attribute(xml,"domain_id"),232);
    }
    try {
        if(category=="domain_participants") {
            fd::DomainParticipantQos q;q.transport().use_builtin_transports=false;
            q.transport().user_transports.push_back(std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());
            n->participant=fd::DomainParticipantFactory::get_instance()->create_participant(n->domain,q);
            need(n->participant!=nullptr,500,"DDS participant creation failed");
        } else if(category!="applications" && category!="waitsets") {
            auto part=participant_of(parent);n->participant=part->participant;
            if(category=="registered_types") {
                n->type=fd::TypeSupport(new Standards::BoundedRecordPubSubType);n->type_ref="Standards::BoundedRecord";n->wire_name=name;
                need(n->type.register_type(n->participant,n->wire_name)==ok,500,"DDS type registration failed");
                n->registered=true;
            } else if(category=="topics") {
                n->type_ref=attribute(xml,"register_type_ref");
                auto reg=part->children.find("registered_types/"+n->type_ref);
                need(reg!=part->children.end()&&!reg->second->closing,422,"registered type reference invalid");
                n->wire_name=reg->second->wire_name;
                n->topic_dds=n->participant->create_topic(name,n->wire_name,fd::TOPIC_QOS_DEFAULT);
                need(n->topic_dds!=nullptr,500,"DDS topic creation failed");
            } else if(category=="publishers") {
                n->publisher=n->participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT);need(n->publisher!=nullptr,500,"DDS publisher creation failed");
            } else if(category=="subscribers") {
                n->subscriber=n->participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);need(n->subscriber!=nullptr,500,"DDS subscriber creation failed");
            } else if(category=="data_writers"||category=="data_readers") {
                n->type_ref=attribute(xml,"topic_ref");auto t=part->children.find("topics/"+n->type_ref);
                need(t!=part->children.end()&&!t->second->closing,422,"topic reference invalid");n->topic=t->second;
                if(category=="data_writers") {
                    fd::DataWriterQos q;q.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;q.history().depth=16;q.data_sharing().off();
                    q.representation().m_value={static_cast<fd::DataRepresentationId_t>(representation_)};
                    n->writer=parent->publisher->create_datawriter(t->second->topic_dds,q);need(n->writer!=nullptr,500,"DDS writer creation failed");
                } else {
                    fd::DataReaderQos q;q.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;q.history().depth=16;q.data_sharing().off();
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
                    q.type_consistency().representation.m_value={static_cast<fd::DataRepresentationId_t>(representation_)};
#else
                    q.representation().m_value={static_cast<fd::DataRepresentationId_t>(representation_)};
#endif
                    n->reader=parent->subscriber->create_datareader(t->second->topic_dds,q);need(n->reader!=nullptr,500,"DDS reader creation failed");
                    for(auto* c:elements(xml))if(local(c)=="status_condition")n->status_condition=true;
                    if(n->status_condition)need(n->reader->get_statuscondition().set_enabled_statuses(fd::StatusMask::data_available())==ok,500,"DDS condition configuration failed");
                }
            }
        }
        for(auto* c:elements(xml))for(const auto& tag:tags)if(local(c)==tag.second)create(n,tag.first,c,principal);
        parent->children.emplace(key,n);
        mark_modified(n);
    } catch(...) {
        bool cleaned=false;try {cleaned=close(n);}catch(...){}
        if(!cleaned){quarantine_.push_back(n);poisoned_=true;}
        throw;
    }
    return n;
}
bool Gateway::close(std::shared_ptr<Node> n) {
    n->closing=true;
    std::vector<std::string> keys;for(const auto& c:n->children)keys.push_back(c.first);
    auto rank=[](const std::string& s) {return s.rfind("topics/",0)==0?2:s.rfind("registered_types/",0)==0?3:1;};
    std::stable_sort(keys.begin(),keys.end(),[&](const auto& a,const auto& b){return rank(a)<rank(b);});
    for(const auto& key:keys){if(!close(n->children.at(key)))return false;n->children.erase(key);}
    auto p=n->parent.lock();
    if(n->writer){if(!p||p->publisher->delete_datawriter(n->writer)!=ok)return false;n->writer=nullptr;}
    if(n->reader){if(!p||p->subscriber->delete_datareader(n->reader)!=ok)return false;n->reader=nullptr;}
    if(n->publisher){if(n->participant->delete_publisher(n->publisher)!=ok)return false;n->publisher=nullptr;}
    if(n->subscriber){if(n->participant->delete_subscriber(n->subscriber)!=ok)return false;n->subscriber=nullptr;}
    if(n->topic_dds){if(n->participant->delete_topic(n->topic_dds)!=ok)return false;n->topic_dds=nullptr;}
    if(n->category=="registered_types"&&n->participant&&n->registered) {
        if(n->participant->unregister_type(n->wire_name)!=ok)return false;n->registered=false;n->wire_name.clear();
    }
    if(n->category=="domain_participants"&&n->participant) {
        if(fd::DomainParticipantFactory::get_instance()->delete_participant(n->participant)!=ok)return false;
    }
    n->participant=nullptr;n->dead=true;return true;
}
std::string Gateway::serialize(const std::shared_ptr<Node>& n) const {
    std::string attrs=" name=\""+escape(n->name)+"\"",children;
    if(n->category=="domain_participants")attrs+=" domain_id=\""+std::to_string(n->domain)+"\"";
    if(n->category=="registered_types")attrs+=" type_ref=\"Standards::BoundedRecord\"";
    if(n->category=="topics")attrs+=" register_type_ref=\""+escape(n->type_ref)+"\"";
    if(n->category=="data_writers"||n->category=="data_readers")attrs+=" topic_ref=\""+escape(n->type_ref)+"\"";
    if(n->status_condition)children+="<status_condition><status>DATA_AVAILABLE</status></status_condition>";
    if(n->category=="waitsets")children+="<condition condition_ref=\""+escape(n->condition_ref)+"\"/>";
    for(const auto& child:n->children)if(!child.second->closing && !(n->category=="applications"&&child.second->category=="waitsets"))children+=serialize(child.second);
    return entity(tags.at(n->category),attrs,children);
}
Response Gateway::read(std::shared_ptr<Node> n,const std::map<std::string,std::string>& q,std::unique_lock<std::mutex>& lock) {
    for(const auto& x:q)need(x.first=="removeFromReaderCache"||x.first=="minSamples"||x.first=="maxSamples"||x.first=="maxWait"||x.first=="sampleSelector",422,"unknown read parameter");
    need(query_value(q,"sampleSelector","").empty(),422,"sample selector outside profile");
    auto remove=query_value(q,"removeFromReaderCache","true");need(remove=="true"||remove=="false",422,"invalid cache selection");
    unsigned minimum=integer(query_value(q,"minSamples","1"),64),maximum=integer(query_value(q,"maxSamples","64"),64);
    need(maximum>0&&minimum<=maximum,422,"invalid sample range");
    auto deadline=Clock::now()+std::chrono::milliseconds(static_cast<int>(seconds(query_value(q,"maxWait","0"))*1000));
    std::vector<std::string> result;std::set<std::string> seen;
    do {
        need(!stopping_,503,"gateway stopping");need(!n->closing&&!n->dead&&n->reader,404,"reader closed");
        auto t=n->topic.lock();need(t&&!t->closing,404,"topic closed");
        fd::LoanableSequence<Sample> values;fd::SampleInfoSeq infos;
        int limit=static_cast<int>(remove=="true"?maximum-result.size():maximum);
        auto code=remove=="true"?n->reader->take(values,infos,limit,fd::ANY_SAMPLE_STATE,fd::ANY_VIEW_STATE,fd::ANY_INSTANCE_STATE):
                                  n->reader->read(values,infos,limit,fd::ANY_SAMPLE_STATE,fd::ANY_VIEW_STATE,fd::ANY_INSTANCE_STATE);
        need(code==ok||code==no_data,500,"DDS read failed");
        if(code==ok) {
            try {
                need(values.length()==infos.length(),500,"DDS loan lengths differ");
                for(int i=0;i<values.length()&&result.size()<maximum;++i) {
                    if(remove=="false") {
                        // SampleIdentity distinguishes repeated equal values and equal timestamps.
                        // Fast DDS synthesizes no-writers notifications with a fresh timestamp
                        // on every read: their instance/generation/state is the stable event key.
                        std::ostringstream key;key<<infos[i].valid_data<<':'<<infos[i].sample_identity;
                        if(infos[i].sample_identity==decltype(infos[i].sample_identity)::unknown()) {
                            need(!infos[i].valid_data,500,"DDS data sample identity unavailable");
                            for(size_t j=0;j<16;++j)key<<':'<<unsigned(infos[i].instance_handle.value[j]);
                            key<<':'<<infos[i].instance_state<<':'<<infos[i].disposed_generation_count<<':'<<infos[i].no_writers_generation_count;
                        }
                        if(!seen.insert(key.str()).second)continue;
                    }
                    result.push_back("<sample>"+state_metadata(infos[i])+(infos[i].valid_data?"<data>"+sample_xml(values[i])+"</data>":"")+"</sample>");
                }
            } catch(...) {need(n->reader->return_loan(values,infos)==ok,500,"DDS loan return failed");throw;}
            need(n->reader->return_loan(values,infos)==ok,500,"DDS loan return failed");
        }
        if(result.size()>=minimum||result.size()>=maximum||Clock::now()>=deadline)break;
        lock.unlock();std::this_thread::sleep_for(std::chrono::milliseconds(10));lock.lock();
    } while(true);
    std::string body;for(const auto& s:result)body+=s;return {200,entity("read_sample_seq","",body),"",std::chrono::system_clock::now()};
}
Response Gateway::wait(std::shared_ptr<Node> n,const std::map<std::string,std::string>& q,std::unique_lock<std::mutex>& lock) {
    for(const auto& x:q)need(x.first=="timeout",422,"unknown wait parameter");
    auto until=Clock::now()+std::chrono::milliseconds(static_cast<int>(seconds(query_value(q,"timeout","0"))*1000));
    bool active=false;
    do {
        need(!stopping_,503,"gateway stopping");auto reader=n->topic.lock();
        need(!n->closing&&!n->dead&&reader&&!reader->closing&&reader->reader,404,"condition closed");
        fd::WaitSet ws;auto& condition=reader->reader->get_statuscondition();
        need(ws.attach_condition(condition)==ok,500,"DDS waitset attach failed");
        fd::ConditionSeq conditions;auto code=ws.wait(conditions,{0,0});
        need(ws.detach_condition(condition)==ok,500,"DDS waitset detach failed");
        need(code==ok||code==timeout_code,500,"DDS wait failed");active=code==ok&&!conditions.empty();
        if(active||Clock::now()>=until)break;
        lock.unlock();std::this_thread::sleep_for(std::chrono::milliseconds(10));lock.lock();
    } while(true);
    return {200,entity("condition_name_list","",active?"<item>"+escape(n->condition_ref)+"</item>":""),"",std::chrono::system_clock::now()};
}
Response Gateway::handle(const Request& req) {
    auto principal=authenticate(req.key);
    need(accepts_xml(req.accept),406,"DDS-WEB XML required");
    need(!trim(req.cache_control).empty(),400,"Cache-Control required");
    need(req.body.empty()||lower(trim(req.content_type.substr(0,req.content_type.find(';'))))=="application/dds-web+xml",415,"DDS-WEB XML required");
    auto t=target(req.target);auto method=req.method=="HEAD"?"GET":req.method;
    need(method=="GET"||method=="POST"||method=="PUT"||method=="DELETE",405,"method unsupported");
    bool writing=method=="POST"&&t.path.size()==8&&t.path[6]=="data_writers";
    auto permission=method=="GET"?"read":writing?"write":"manage";
    need(principal.permissions.count(permission),403,"operation not permitted");
    Document doc(nullptr,xmlFreeDoc);
    if(method=="POST"||method=="PUT") {
        doc=parse(req.body);if(writing)schemas_.sample(doc.get());else schemas_.rest(doc.get());
    } else need(req.body.empty(),422,"request body not allowed");
    std::unique_lock<std::mutex> lock(mutex_);need(!stopping_,503,"gateway stopping");
    need(!poisoned_||method=="DELETE",503,"cleanup retry required");
    Response out;
    if(t.path[0]=="types") {
        need(method=="GET",422,"type creation outside fixed catalog profile");
        need(t.path.size()==1||(t.path.size()==2&&t.path[1]=="BoundedRecord"),404,"type not found");
        need(t.query.empty(),422,"type query outside profile");
        return {200,type_catalog_xml,"",catalog_modified_};
    }
    need(t.path[0]=="applications",404,"resource not found");
    if(t.path.size()>1) {
        auto app=root_->children.find("applications/"+t.path[1]);need(app!=root_->children.end(),404,"application not found");
        need(app->second->owner==principal.owner,403,"application not permitted");
        // The bounded policy grants an application as one ownership/domain unit.
        need(domains_allowed(app->second,principal),403,"application domains not permitted");
        app->second->expires=Clock::now()+std::chrono::seconds(ttl_);
    }
    if(method=="POST"&&!writing) {
        need(t.query.empty(),422,"create query unsupported");
        need(t.path.size()%2==1,405,"POST requires collection");auto category=t.path.back();
        need(tags.count(category),404,"collection not found");auto path=t.path;path.pop_back();auto parent=lookup(path);
        need(collection_allowed(parent->category,category),404,"collection not found");auto* xml=xmlDocGetRootElement(doc.get());check_tree(xml,category,principal);
        check_capacity(xml);auto node=create(parent,category,xml,principal);out={201,"",path_for(node)};
    } else if(method=="DELETE") {
        need(t.query.empty(),422,"delete query unsupported");auto n=lookup(t.path,true);auto p=n->parent.lock();
        if(!close(n)){poisoned_=true;throw Error(500,"DDS cleanup failed; retry DELETE");}
        p->children.erase(n->category+"/"+n->name);mark_modified(p);poisoned_=!quarantine_.empty()||has_closing(root_);out.status=204;
    } else if(method=="PUT") {
        need(t.query.empty(),422,"update query unsupported");auto n=lookup(t.path);auto p=n->parent.lock();auto category=n->category;
        auto* xml=xmlDocGetRootElement(doc.get());check_tree(xml,category,principal);need(attribute(xml,"name")==n->name,422,"update name differs from URI");
        check_capacity(xml,n);
        need(same_representation(n,xml),422,"PUT changes outside fixed-QoS profile; use explicit create/delete");
        // With no supported QoS changes this is a no-op, preserving DDS identity and history.
        out.status=204;
    } else if(writing) {
        need(t.query.empty(),422,"write query unsupported");auto writer=lookup(t.path);auto topic=writer->topic.lock();need(writer->writer&&topic&&!topic->closing,404,"writer closed");
        auto sample=sample_value(xmlDocGetRootElement(doc.get()));
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        need(writer->writer->write(&sample),500,"DDS write failed");
#else
        need(writer->writer->write(&sample)==ok,500,"DDS write failed");
#endif
    } else if(t.path.size()%2==1) {
        auto category=t.path.back();need(tags.count(category),404,"collection not found");auto path=t.path;path.pop_back();auto parent=lookup(path);
        need(collection_allowed(parent->category,category),404,"collection not found");
        auto filter=filter_name(category);for(const auto& q:t.query)need(q.first==filter,422,"unknown name filter");
        auto pattern=query_value(t.query,filter,"*");std::string body;
        for(const auto& child:parent->children)if(child.second->category==category&&!child.second->closing&&
             (category!="applications"||(child.second->owner==principal.owner&&domains_allowed(child.second,principal)))&&glob(child.second->name,pattern))body+=serialize(child.second);
        out.body=entity(tags.at(category)+"_list","",body);out.modified=parent->modified;
    } else {
        auto n=lookup(t.path);
        if(n->reader)out=read(n,t.query,lock);
        else if(n->category=="waitsets")out=wait(n,t.query,lock);
        else {need(t.query.empty(),422,"resource query unsupported");out.body=serialize(n);out.modified=n->modified;}
    }
    lock.unlock();schemas_.response(out.body);return out;
}
bool Gateway::expire() {
    std::lock_guard<std::mutex> lock(mutex_);
    bool success=true;
    for(auto it=root_->children.begin();it!=root_->children.end();) {
        if(it->second->expires<=Clock::now()) {if(close(it->second)){it=root_->children.erase(it);mark_modified(root_);continue;}success=false;}++it;
    }
    for(auto it=quarantine_.begin();it!=quarantine_.end();)if(close(*it))it=quarantine_.erase(it);else{success=false;++it;}
    poisoned_=!quarantine_.empty()||has_closing(root_);
    return success;
}
bool Gateway::shutdown() {
    stopping_=true;std::lock_guard<std::mutex> lock(mutex_);bool success=true;
    for(auto it=root_->children.begin();it!=root_->children.end();)if(close(it->second))it=root_->children.erase(it);else{success=false;++it;}
    for(auto it=quarantine_.begin();it!=quarantine_.end();)if(close(*it))it=quarantine_.erase(it);else{success=false;++it;}
    return success;
}
}
