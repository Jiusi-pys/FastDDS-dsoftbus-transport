#pragma once
#include <libxml/parser.h>
#include <libxml/xmlschemas.h>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
namespace c1 {
inline constexpr const char* dds_ns="http://www.omg.org/dds/";
inline constexpr const char* sample_ns="ddstype://www.omg.org/Standards";
struct Error : std::runtime_error {
    int status;
    Error(int code,const char* message):std::runtime_error(message),status(code){}
};
inline void need(bool value,int code,const char* message) { if(!value) throw Error(code,message); }
using Document=std::unique_ptr<xmlDoc,decltype(&xmlFreeDoc)>;
Document parse(const std::string& text,size_t limit=65536);
std::vector<xmlNode*> elements(xmlNode* node);
std::string local(xmlNode* node);
std::string ns(xmlNode* node);
std::string attribute(xmlNode* node,const char* key);
std::string content(xmlNode* node);
std::string escape(const std::string& text);
std::string number_text(xmlNode* node);
std::string entity(const std::string& tag,const std::string& attributes,const std::string& children="");
class Schemas {
    xmlSchemaPtr rest_=nullptr,sample_=nullptr,plain_sample_=nullptr;
public:
    explicit Schemas(const std::string& directory);
    ~Schemas();
    Schemas(const Schemas&)=delete;
    void rest(xmlDoc* doc) const;
    void sample(xmlDoc* doc) const;
    void response(const std::string& text) const;
};
}
