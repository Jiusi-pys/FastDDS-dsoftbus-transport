// Host tests for the autoload library's config and ROS environment validation.
#include "AutoloadConfig.hpp"
#include <cstdio>
#include <map>
#include <string>
using namespace fastdds_dsoftbus::ros2_autoload;
static int failures = 0;
static void check(bool value, const char* name) { if (!value) { std::printf("FAIL %s\n", name); ++failures; } }
static std::string good(const std::string& replace_from = "", const std::string& replace_to = "")
{
    std::string text = "side=1\nrun=0123456789abcdef\ndomain=151\nlocal_udid=A\npeer_udid=B\npeer_network=N\ntoken=42\ngo_file=/data/go\n";
    if (!replace_from.empty()) text.replace(text.find(replace_from), replace_from.size(), replace_to);
    return text;
}
static EnvLookup env(std::map<std::string, std::string> values)
{
    return [values = std::move(values)](const char* name) -> const char* {
        const auto it = values.find(name); return it == values.end() ? nullptr : it->second.c_str(); };
}
int main()
{
    Config c;
    check(ParseConfig(good(), c).empty() && c.side == 1 && c.domain == 151 && c.token == "42", "valid config");
    check(ParseConfig("# note\r\n" + good(), c).empty(), "comment and CRLF");
    check(!ParseConfig(good("side=1", "side=2"), c).empty(), "bad side");
    check(!ParseConfig(good("run=0123456789abcdef", "run=xyz"), c).empty(), "bad run");
    check(!ParseConfig(good("domain=151", "domain=233"), c).empty(), "domain range");
    check(!ParseConfig(good("domain=151", "domain=9999999999999"), c).empty(), "huge domain does not throw");
    check(!ParseConfig(good("token=42", "token=4a"), c).empty(), "bad token");
    check(!ParseConfig(good("go_file=/data/go", "go_file=go"), c).empty(), "relative go file");
    check(!ParseConfig(good("peer_network=N\n", ""), c).empty(), "missing key");
    check(!ParseConfig(good() + "extra=1\n", c).empty(), "unknown key");
    check(!ParseConfig(good() + "side=0\n", c).empty(), "duplicate key");
    check(!ParseConfig(good() + "broken\n", c).empty(), "malformed line");

    std::map<std::string, std::string> ok{{"RMW_IMPLEMENTATION", "rmw_fastrtps_cpp"}, {"ROS_DOMAIN_ID", "151"},
        {"ROS_AUTOMATIC_DISCOVERY_RANGE", "SYSTEM_DEFAULT"}};
    check(CheckRosEnvironment(env(ok), 151).empty(), "valid environment");
    check(!CheckRosEnvironment(env(ok), 152).empty(), "wrong domain");
    for (const auto& change : std::map<std::string, std::string>{{"RMW_IMPLEMENTATION", "rmw_cyclonedds_cpp"},
            {"ROS_AUTOMATIC_DISCOVERY_RANGE", "LOCALHOST"}, {"ROS_STATIC_PEERS", "10.0.0.1"}, {"ROS_LOCALHOST_ONLY", "1"},
            {"FASTRTPS_DEFAULT_PROFILES_FILE", "/a.xml"}, {"FASTDDS_DEFAULT_PROFILES_FILE", "/a.xml"}, {"FASTDDS_BUILTIN_TRANSPORTS", "UDPv4"},
            {"FASTDDS_ENVIRONMENT_FILE", "/a.json"}, {"ROS_DISCOVERY_SERVER", "10.0.0.1:11811"}}) {
        auto bad = ok; bad[change.first] = change.second;
        check(!CheckRosEnvironment(env(bad), 151).empty(), change.first.c_str());
    }
    auto subnet = ok; subnet["ROS_AUTOMATIC_DISCOVERY_RANGE"] = "SUBNET";
    check(!CheckRosEnvironment(env(subnet), 151).empty(), "SUBNET adds a UDP multicast peer");
    auto missing = ok; missing.erase("ROS_DOMAIN_ID");
    check(!CheckRosEnvironment(env(missing), 151).empty(), "missing domain");
    std::printf(failures ? "autoload config tests FAILED\n" : "autoload config tests PASS\n");
    return failures ? 1 : 0;
}
