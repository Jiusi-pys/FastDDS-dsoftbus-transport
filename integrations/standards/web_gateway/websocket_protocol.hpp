#pragma once
#include "model.hpp"
#include <map>
namespace c2 {
struct Result {std::string message;};
class Protocol {
    std::shared_ptr<c1::Gateway> gateway_;
    std::shared_ptr<xmlSchema> schema_;
    void validate(xmlDoc*) const;
public:
    Protocol(std::shared_ptr<c1::Gateway>,const std::string& schema_file);
    std::string hello(const std::string& message,std::string& key) const;
    Result process(const std::string& message,const std::string& key,std::map<std::string,c1::Binding>& bindings) const;
    std::string push(const std::string& key,const std::string& id,const c1::Binding&) const;
};
}
