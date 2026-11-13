#include "xml.hpp"
#include "schema_hashes.hpp"
#include <openssl/sha.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <iomanip>
#include <sstream>
#include <algorithm>
namespace c1 {
namespace {
std::map<std::string,std::string> schema_paths;
xmlExternalEntityLoader original_loader=nullptr;
xmlParserInputPtr offline(const char* url,const char* id,xmlParserCtxtPtr ctx) {
    if(!url) return nullptr;
    auto found=schema_paths.find(url);
    return found==schema_paths.end()?nullptr:original_loader(found->second.c_str(),id,ctx);
}
xmlParserInputPtr deny_external(const char*,const char*,xmlParserCtxtPtr) { return nullptr; }
void quiet(void*,const char*,...) {}
xmlSchemaPtr load(const std::string& path) {
    std::unique_ptr<xmlSchemaParserCtxt,decltype(&xmlSchemaFreeParserCtxt)> ctx(xmlSchemaNewParserCtxt(path.c_str()),xmlSchemaFreeParserCtxt);
    need(bool(ctx),500,"schema parser allocation failed");
    xmlSchemaSetParserErrors(ctx.get(),quiet,quiet,nullptr);
    auto* schema=xmlSchemaParse(ctx.get());
    need(schema!=nullptr,500,"offline schema compilation failed"); return schema;
}
void validate(xmlSchemaPtr schema,xmlDoc* doc) {
    std::unique_ptr<xmlSchemaValidCtxt,decltype(&xmlSchemaFreeValidCtxt)> ctx(xmlSchemaNewValidCtxt(schema),xmlSchemaFreeValidCtxt);
    need(bool(ctx),500,"schema validation allocation failed");
    xmlSchemaSetValidErrors(ctx.get(),quiet,quiet,nullptr);
    need(xmlSchemaValidateDoc(ctx.get(),doc)==0,422,"XML does not match the schema profile");
}
}
Document parse(const std::string& text,size_t limit) {
    need(!text.empty() && text.size()<=limit,422,"XML body size invalid");
    // No DTD, XInclude or entity expansion is part of this protocol profile.
    need(text.find("<!DOCTYPE")==std::string::npos && text.find("<!ENTITY")==std::string::npos,422,"DTD is not supported");
    Document doc(xmlReadMemory(text.data(),static_cast<int>(text.size()),"request.xml",nullptr,
                 XML_PARSE_NONET|XML_PARSE_NOERROR|XML_PARSE_NOWARNING),xmlFreeDoc);
    need(bool(doc) && !doc->intSubset && !doc->extSubset,422,"invalid XML document");
    need(xmlDocGetRootElement(doc.get())!=nullptr,422,"empty XML document");
    return doc;
}
std::vector<xmlNode*> elements(xmlNode* node) {
    std::vector<xmlNode*> out;
    for(auto* c=node->children;c;c=c->next) if(c->type==XML_ELEMENT_NODE) out.push_back(c);
    return out;
}
std::string local(xmlNode* n) { return reinterpret_cast<const char*>(n->name); }
std::string ns(xmlNode* n) { return n->ns && n->ns->href?reinterpret_cast<const char*>(n->ns->href):""; }
std::string attribute(xmlNode* n,const char* name) {
    auto* text=xmlGetProp(n,BAD_CAST name); if(!text) return {};
    std::string result(reinterpret_cast<const char*>(text));xmlFree(text);return result;
}
std::string content(xmlNode* n) {
    auto* text=xmlNodeGetContent(n); if(!text) return {};
    std::string result(reinterpret_cast<const char*>(text));xmlFree(text);return result;
}
std::string escape(const std::string& text) {
    std::string result;
    for(char c:text) switch(c) {
        case '&':result+="&amp;";break;case '<':result+="&lt;";break;
        case '>':result+="&gt;";break;case '\"':result+="&quot;";break;
        case '\r':result+="&#13;";break;
        case '\'':result+="&apos;";break;default:result+=c;
    }
    return result;
}
std::string number_text(xmlNode* n) {
    auto s=content(n);
    auto a=s.find_first_not_of(" \r\n\t"),b=s.find_last_not_of(" \r\n\t");
    return a==std::string::npos?"":s.substr(a,b-a+1);
}
std::string entity(const std::string& tag,const std::string& attrs,const std::string& children) {
    return "<"+tag+" xmlns=\""+dds_ns+"\""+attrs+">"+children+"</"+tag+">";
}
Schemas::Schemas(const std::string& directory) {
    xmlInitParser(); original_loader=xmlGetExternalEntityLoader();
    for(const auto& item:schema_hashes) {
        auto path=std::filesystem::absolute(std::filesystem::path(directory)/item.first);
        std::ifstream in(path,std::ios::binary);
        need(bool(in),500,"schema file missing");
        std::string bytes((std::istreambuf_iterator<char>(in)),{});
        unsigned char digest[SHA256_DIGEST_LENGTH];SHA256(reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size(),digest);
        std::ostringstream hex; for(auto b:digest) hex<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
        need(hex.str()==item.second,500,"schema file hash mismatch");
        schema_paths[path.generic_string()]=path.generic_string();
        schema_paths[path.string()]=path.generic_string();
        schema_paths[std::string(item.first)]=path.generic_string();
        schema_paths["file:///"+path.generic_string()]=path.generic_string();
    }
    xmlSetExternalEntityLoader(offline);
    try {
        rest_=load((std::filesystem::path(directory)/"rest-bindings.xsd").generic_string());
        sample_=load((std::filesystem::path(directory)/"record.xsd").generic_string());
        plain_sample_=load((std::filesystem::path(directory)/"record-plain.xsd").generic_string());
    } catch (...) {
        if(rest_)xmlSchemaFree(rest_);if(sample_)xmlSchemaFree(sample_);if(plain_sample_)xmlSchemaFree(plain_sample_);
        xmlSetExternalEntityLoader(deny_external);throw;
    }
    xmlSetExternalEntityLoader(deny_external);
}
Schemas::~Schemas() { if(rest_)xmlSchemaFree(rest_);if(sample_)xmlSchemaFree(sample_);if(plain_sample_)xmlSchemaFree(plain_sample_); }
void Schemas::rest(xmlDoc* doc) const { validate(rest_,doc); }
void Schemas::sample(xmlDoc* doc) const {
    auto uri=ns(xmlDocGetRootElement(doc));
    need(uri.empty() || uri==sample_ns,422,"sample namespace unsupported");
    validate(uri.empty()?plain_sample_:sample_,doc);
}
void Schemas::response(const std::string& text) const {
    if(text.empty())return;
    try { auto doc=parse(text,1048576);rest(doc.get()); }
    catch(...) { throw Error(500,"invalid generated XML response"); }
}
}
