#pragma once
#include <string>
namespace installed_example {
// The deployment supplies the authorized package and a local name stem. Keep
// Socket names under that package's existing policy namespace and unique to
// this data window; this does not grant permission or alter platform policy.
inline std::string ServicePrefix(const std::string& package,const std::string& stem,const std::string& run) {
    return package + "." + stem + "." + run;
}
}
