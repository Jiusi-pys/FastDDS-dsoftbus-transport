#pragma once
// Pure validation for the ROS 2 autoload library: flat config parsing and the ROS environment
// checks that keep rmw_fastrtps from adding UDP or shared-memory paths. No Fast DDS or SDK here.
#include <cctype>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>

namespace fastdds_dsoftbus::ros2_autoload {

struct Config {
    unsigned side = 0;
    std::string run, local_udid, peer_udid, peer_network, token, go_file;
    std::uint32_t domain = 0;
};

// Empty return means valid.
inline std::string ParseConfig(const std::string& text, Config& out)
{
    std::map<std::string, std::string> values;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos || equal == 0) return "malformed line";
        if (!values.emplace(line.substr(0, equal), line.substr(equal + 1)).second) return "duplicate key";
    }
    for (const auto* key : {"side", "run", "domain", "local_udid", "peer_udid", "peer_network", "token", "go_file"})
        if (values.count(key) == 0 || values[key].empty()) return std::string("missing ") + key;
    if (values.size() != 8) return "unknown key";
    if (values["side"] != "0" && values["side"] != "1") return "side must be 0 or 1";
    out.side = values["side"] == "1" ? 1u : 0u;
    out.run = values["run"];
    if (out.run.size() != 16) return "run must be 16 hexadecimal digits";
    for (const char c : out.run)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return "run must be 16 hexadecimal digits";
    const auto& domain = values["domain"];
    if (domain.size() > 3) return "domain out of range";
    for (const char c : domain)
        if (!std::isdigit(static_cast<unsigned char>(c))) return "domain must be a number";
    out.domain = static_cast<std::uint32_t>(std::stoul(domain));
    if (out.domain > 232) return "domain out of range";
    for (const char c : values["token"])
        if (!std::isdigit(static_cast<unsigned char>(c))) return "token must be a number";
    if (values["go_file"][0] != '/') return "go_file must be absolute";
    out.local_udid = values["local_udid"]; out.peer_udid = values["peer_udid"];
    out.peer_network = values["peer_network"]; out.token = values["token"]; out.go_file = values["go_file"];
    return {};
}

using EnvLookup = std::function<const char*(const char*)>;

// Empty return means the ROS environment cannot widen the transport set. The direct path serves one domain;
// the hub serves any, so it passes no domain.
inline std::string CheckRosEnvironment(const EnvLookup& env, std::optional<std::uint32_t> domain)
{
    const auto equals = [&](const char* name, const std::string& expected) {
        const char* value = env(name);
        return value != nullptr && expected == value;
    };
    if (!equals("RMW_IMPLEMENTATION", "rmw_fastrtps_cpp")) return "RMW_IMPLEMENTATION must be rmw_fastrtps_cpp";
    if (domain && !equals("ROS_DOMAIN_ID", std::to_string(*domain))) return "ROS_DOMAIN_ID must equal the configured domain";
    // LOCALHOST adds SHM+UDP; SUBNET with initial peers adds a UDP multicast peer.
    if (!equals("ROS_AUTOMATIC_DISCOVERY_RANGE", "SYSTEM_DEFAULT")) return "ROS_AUTOMATIC_DISCOVERY_RANGE must be SYSTEM_DEFAULT";
    // These would let an XML file or environment value change what the process builds on.
    for (const char* name : {"FASTRTPS_DEFAULT_PROFILES_FILE", "FASTDDS_DEFAULT_PROFILES_FILE", "FASTDDS_BUILTIN_TRANSPORTS",
            "FASTDDS_ENVIRONMENT_FILE", "ROS_DISCOVERY_SERVER"}) {
        const char* value = env(name);
        if (value != nullptr && value[0] != '\0') return std::string(name) + " must be unset";
    }
    // It keeps rmw_fastrtps from turning data sharing off, so same-board traffic could use shared memory instead.
    const char* from_xml = env("RMW_FASTRTPS_USE_QOS_FROM_XML");
    if (from_xml != nullptr && std::string(from_xml) == "1") return "RMW_FASTRTPS_USE_QOS_FROM_XML must not be 1";
    const char* peers = env("ROS_STATIC_PEERS");
    if (peers != nullptr && peers[0] != '\0') return "ROS_STATIC_PEERS must be unset";
    const char* localhost = env("ROS_LOCALHOST_ONLY");
    if (localhost != nullptr && std::string(localhost) == "1") return "ROS_LOCALHOST_ONLY must not be 1";
    return {};
}
}
