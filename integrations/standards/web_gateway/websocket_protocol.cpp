#include "websocket_protocol.hpp"
#include "websocket_schema.hpp"
#include <openssl/sha.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string_view>
namespace c2 {
using namespace c1;
namespace {
bool identifier(const std::string& value) {
    return !value.empty()&&std::all_of(value.begin(),value.end(),[](unsigned char c){
        return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||std::string_view("!#$%&'*+-.^_`|~").find(c)!=std::string_view::npos;});
}
std::string trim(std::string s) {
    auto a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");return a==std::string::npos?"":s.substr(a,b-a+1);
}
std::string lower(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return s;}
std::string child(xmlNode* parent,const char* tag) {
    // All envelope simple-content fields use XSD whitespace-collapse. Body data does not.
    for(auto* n:elements(parent))if(local(n)==tag) {
        std::string normalized;bool space=false;
        for(char c:content(n)) {
            if(c==' '||c=='\t'||c=='\r'||c=='\n'){space=!normalized.empty();continue;}
            if(space)normalized+=' ';normalized+=c;space=false;
        }
        return normalized;
    }
    return {};
}
std::string xml(xmlNode* n) {
    Document doc(xmlNewDoc(BAD_CAST "1.0"),xmlFreeDoc);need(bool(doc),500,"XML allocation failed");
    auto* copied=xmlDocCopyNode(n,doc.get(),1);need(copied,500,"XML copy failed");xmlDocSetRootElement(doc.get(),copied);
    need(xmlReconciliateNs(doc.get(),copied)>=0,500,"XML namespace copy failed");
    std::unique_ptr<xmlBuffer,decltype(&xmlBufferFree)> buffer(xmlBufferCreate(),xmlBufferFree);need(bool(buffer),500,"XML allocation failed");
    need(xmlNodeDump(buffer.get(),doc.get(),copied,0,0)>=0,500,"XML serialization failed");
    return reinterpret_cast<const char*>(xmlBufferContent(buffer.get()));
}
std::string body(xmlNode* node) {
    for(auto* n:elements(node))if(local(n)=="body")return xml(elements(n).at(0));return {};
}
std::string uri(std::string value) {
    need(!value.empty()&&value.size()<=2048,422,"URI outside profile");
    if(value[0]!='/')value='/'+value;
    if(value.rfind("/applications",0)==0||value.rfind("/types",0)==0)value="/dds/rest1"+value;
    need(value.rfind("/dds/rest1/",0)==0,422,"URI outside REST mapping");return value;
}
const char* code(int status) {
    if(status>=200&&status<300)return "OK";
    if(status==401)return "ACCESS_DENIED";if(status==403)return "PERMISSIONS_ERROR";
    if(status==404)return "INVALID_OBJECT";if(status==500)return "DDS_ERROR";
    if(status>=500)return "GENERIC_SERVICE_ERROR";return "INVALID_INPUT";
}
Request request(const std::string& key,std::string method,std::string target,std::string body={}) {
    return {std::move(method),std::move(target),std::move(body),key,"application/dds-web+xml","application/dds-web+xml","no-cache"};
}
void quiet(void*,const char*,...){}
}
Protocol::Protocol(std::shared_ptr<Gateway> gateway,const std::string& path):gateway_(std::move(gateway)) {
    std::ifstream in(path,std::ios::binary);need(bool(in),500,"WebSocket schema missing");
    std::string bytes((std::istreambuf_iterator<char>(in)),{});unsigned char digest[32];
    SHA256(reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size(),digest);std::ostringstream hex;
    for(auto b:digest)hex<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
    need(hex.str()==C2_SCHEMA_HASH,500,"WebSocket schema hash mismatch");
    std::unique_ptr<xmlSchemaParserCtxt,decltype(&xmlSchemaFreeParserCtxt)> ctx(xmlSchemaNewMemParserCtxt(bytes.data(),static_cast<int>(bytes.size())),xmlSchemaFreeParserCtxt);
    need(bool(ctx),500,"WebSocket schema allocation failed");xmlSchemaSetParserErrors(ctx.get(),quiet,quiet,nullptr);
    schema_={xmlSchemaParse(ctx.get()),xmlSchemaFree};need(bool(schema_),500,"WebSocket schema compilation failed");
}
void Protocol::validate(xmlDoc* doc) const {
    std::unique_ptr<xmlSchemaValidCtxt,decltype(&xmlSchemaFreeValidCtxt)> ctx(xmlSchemaNewValidCtxt(schema_.get()),xmlSchemaFreeValidCtxt);
    need(bool(ctx),500,"WebSocket validator allocation failed");xmlSchemaSetValidErrors(ctx.get(),quiet,quiet,nullptr);
    need(xmlSchemaValidateDoc(ctx.get(),doc)==0,422,"WebSocket XML envelope invalid");
}
std::string Protocol::hello(const std::string& message,std::string& key) const {
    need(message.size()<=2048&&!message.empty(),401,"HELLO size invalid");
    std::map<std::string,std::string> fields;size_t start=0;
    while(start<message.size()) {
        auto end=message.find("\r\n",start);need(end!=std::string::npos,401,"HELLO requires CRLF");
        auto line=message.substr(start,end-start);start=end+2;
        if(line.empty()){need(start==message.size(),401,"HELLO trailing content");break;}
        auto colon=line.find(':');need(colon!=std::string::npos,401,"HELLO field invalid");
        auto name=lower(line.substr(0,colon));need(identifier(name),401,"HELLO field invalid");
        need(fields.emplace(name,trim(line.substr(colon+1))).second,401,"HELLO duplicate field");
    }
    need(fields["version"]=="1",401,"HELLO version unsupported");
    need(lower(fields["content-type"])=="application/dds-web+xml"&&lower(fields["accept"])=="application/dds-web+xml",401,"HELLO media type unsupported");
    key=fields["omg-dds-api-key"];return gateway_->principal_name(key);
}
Result Protocol::process(const std::string& message,const std::string& key,std::map<std::string,Binding>& bindings) const {
    auto doc=parse(message);validate(doc.get());auto* root=xmlDocGetRootElement(doc.get());auto name=local(root);
    if(name=="request") {
        auto id=child(root,"id");bool has_id=false;for(auto* n:elements(root))if(local(n)=="id")has_id=true;
        Response result;
        try {result=gateway_->handle(request(key,child(root,"method"),uri(child(root,"uri")),body(root)));}
        catch(const Error& error){result.status=error.status;}catch(...){result.status=500;}
        auto response=std::string("<response>")+(has_id?"<id>"+escape(id)+"</id>":"")+"<return_code>"+code(result.status)+"</return_code>"+
            (result.body.empty()?"":"<body>"+result.body+"</body>")+"</response>";
        auto check=parse(response,1048576);validate(check.get());return {response};
    }
    if(name=="bind") {
        auto updated=bindings;
        for(auto* n:elements(root)) {
            auto id=child(n,"bind_id"),target=child(n,"uri");
            if(target.empty()){updated.erase(id);continue;}
            auto binding=gateway_->bind(key,uri(target),local(n)=="bind_datareader");
            updated[id]=std::move(binding);need(updated.size()<=16,429,"connection binding limit");
        }
        bindings=std::move(updated);return {};
    }
    if(name=="b_req") {
        bool has_id=false;for(auto* n:elements(root))if(local(n)=="bind_id")has_id=true;
        need(has_id,422,"B_REQUEST requires a bind_id element");
        auto id=child(root,"bind_id");auto found=bindings.find(id);
        need(found!=bindings.end()&&!found->second.reader,422,"writer binding missing");
        gateway_->handle(request(key,"POST",found->second.uri,body(root)),&found->second);return {};
    }
    throw Error(422,"WebSocket message kind unsupported");
}
std::string Protocol::push(const std::string& key,const std::string& id,const Binding& binding) const {
    auto response=gateway_->handle(request(key,"GET",binding.uri+"?removeFromReaderCache=true&minSamples=0&maxSamples=16"),&binding);
    auto doc=parse(response.body,1048576);if(elements(xmlDocGetRootElement(doc.get())).empty())return {};
    auto message="<b_push><bind_id>"+escape(id)+"</bind_id><body>"+response.body+"</body></b_push>";
    auto check=parse(message,1048576);validate(check.get());return message;
}
}
