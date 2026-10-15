// Parameter values, service results and events travel through ordinary ROS APIs.
// Files provide only process barriers; the installed factory selects transport.
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/parameter_client.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>
#include <process.h>
#include "../ros2_transport/FailureProtocol.hpp"
using namespace std::chrono_literals;

static std::string hex(const std::string& value) {
    if (value.empty()) return "-";
    std::ostringstream out;
    for (unsigned char c : value) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
    return out.str();
}

static std::string encode(const rclcpp::Parameter& parameter) {
    const auto type = parameter.get_type();
    std::string value;
    if (type == rclcpp::ParameterType::PARAMETER_INTEGER) value = std::to_string(parameter.as_int());
    else if (type == rclcpp::ParameterType::PARAMETER_STRING) value = hex(parameter.as_string());
    else throw std::runtime_error("unexpected_parameter_type");
    return parameter.get_name() + ":" + std::to_string(static_cast<unsigned>(type)) + ":" + value;
}

static std::string pack(const std::vector<rclcpp::Parameter>& parameters) {
    std::vector<std::string> parts;
    for (const auto& parameter : parameters) parts.push_back(encode(parameter));
    std::sort(parts.begin(), parts.end());
    std::string out;
    for (const auto& part : parts) { if (!out.empty()) out += "|"; out += part; }
    return out.empty() ? "-" : out;
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::string side = argv[1], run = argv[2];
    if ((side != "A" && side != "B") || !std::regex_match(run, std::regex("[0-9a-f]{16}"))) return 2;
    const auto pid = _getpid();
    const std::string ns = "/param_" + run, server_name = ns + "/server";
    const auto record = [&](const std::string& text) {
        std::cout << "PARAM_ACCEPT run=" << run << " side=" << side << " pid=" << pid << " " << text << std::endl;
    };
    const auto require = [](bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); };
    int result = 4;
    bool initialized = false;
    const char* stage = "RCLCPP_INIT";
    try {
        rclcpp::init(1, argv);
        initialized = true;
        stage = "NODE_CREATE";
        auto options = rclcpp::NodeOptions().use_intra_process_comms(false)
            .allow_undeclared_parameters(false).automatically_declare_parameters_from_overrides(false)
            .start_parameter_services(side == "A").start_parameter_event_publisher(side == "A");
        auto node = std::make_shared<rclcpp::Node>(side == "A" ? "server" : "client", ns, options);
        const auto progress = [&] { rclcpp::spin_some(node); std::this_thread::sleep_for(1ms); };
        const auto until = [&](const auto& ready, auto timeout) {
            const auto end = std::chrono::steady_clock::now() + timeout;
            while (!ready()) { require(std::chrono::steady_clock::now() < end, "bounded_wait_expired"); progress(); }
        };
        const char* release = std::getenv("FDDST_PARAMETER_RELEASE");
        require(release != nullptr, "release_path_missing");
        const auto released = [&] { std::ifstream f(release); std::string line; return std::getline(f, line) && line == "RELEASE " + run; };
        const std::vector<std::string> names{"case.counter", "case.label", "case.readonly"};
        const auto expected = [&](int counter, const std::string& label) {
            return pack({rclcpp::Parameter(names[0], int64_t(counter)), rclcpp::Parameter(names[1], label),
                         rclcpp::Parameter(names[2], int64_t(7))});
        };
        stage = "PARAMETER_SCENARIO";
        if (side == "A") {
            node->declare_parameter<int64_t>(names[0], 10);
            node->declare_parameter<std::string>(names[1], "initial_" + run);
            rcl_interfaces::msg::ParameterDescriptor descriptor;
            descriptor.read_only = true;
            descriptor.description = "immutable acceptance value";
            node->declare_parameter<int64_t>(names[2], 7, descriptor);
            // Keep the callback handle alive for all remote requests.
            auto guard = node->add_on_set_parameters_callback([&](const std::vector<rclcpp::Parameter>& parameters) {
                rcl_interfaces::msg::SetParametersResult answer;
                answer.successful = true;
                for (const auto& parameter : parameters) {
                    if (parameter.get_name() == names[0] && parameter.get_type() == rclcpp::ParameterType::PARAMETER_INTEGER &&
                            parameter.as_int() < 0) {
                        answer.successful = false; answer.reason = "negative_counter";
                    }
                }
                return answer;
            });
            record("event=SERVER_READY node=" + server_name + " values=" + pack(node->get_parameters(names)));
            until(released, 60s);
            const auto final = pack(node->get_parameters(names));
            require(final == expected(13, "recovered_" + run), "server_final_state");
            require(!node->has_parameter("case.undeclared"), "undeclared_parameter_persisted");
            record("event=BUSINESS_DONE values=" + final + " undeclared=0");
        } else {
            auto client = std::make_shared<rclcpp::AsyncParametersClient>(node, server_name);
            std::vector<std::string> mutation_events;
            auto events = client->on_parameter_event([&](rcl_interfaces::msg::ParameterEvent::ConstSharedPtr event) {
                if (event->node != server_name) return;
                const auto convert = [](const auto& values) {
                    std::vector<rclcpp::Parameter> out;
                    for (const auto& value : values) {
                        if (value.name.rfind("case.", 0) == 0) out.push_back(rclcpp::Parameter::from_parameter_msg(value));
                    }
                    return pack(out);
                };
                const auto changed = convert(event->changed_parameters);
                const auto added = convert(event->new_parameters);
                const auto deleted = convert(event->deleted_parameters);
                if (changed == "-" && added == "-" && deleted == "-") return;
                record("event=PARAM_EVENT node=" + event->node + " changed=" + changed + " new=" + added +
                       " deleted=" + deleted + " stamp_sec=" + std::to_string(event->stamp.sec) +
                       " stamp_nsec=" + std::to_string(event->stamp.nanosec));
                if (changed != "-") {
                    require(added == "-" && deleted == "-", "mixed_mutation_event");
                    mutation_events.push_back(changed);
                }
            });
            until([&] { return client->service_is_ready() && events->get_publisher_count() >= 1; }, 30s);
            record("event=CLIENT_READY node=" + server_name);
            const auto await = [&](auto future) {
                until([&] { return future.wait_for(0s) == std::future_status::ready; }, 5s);
                return future.get();
            };
            const auto read = [&](int op, int counter, const std::string& label) {
                const auto values = await(client->get_parameters(names));
                require(values.size() == names.size(), "read_cardinality");
                for (size_t i = 0; i < names.size(); ++i) require(values[i].get_name() == names[i], "read_name_order");
                const auto encoded = pack(values);
                require(encoded == expected(counter, label), "readback_values");
                record("event=READ op=" + std::to_string(op) + " values=" + encoded);
            };
            read(1, 10, "initial_" + run);
            const auto descriptors = await(client->describe_parameters(names));
            require(descriptors.size() == 3, "descriptor_cardinality");
            std::string descriptions;
            for (size_t i = 0; i < descriptors.size(); ++i) {
                const auto& descriptor = descriptors[i];
                require(descriptor.name == names[i] && descriptor.type == (i == 1 ? 4 : 2) &&
                        descriptor.read_only == (i == 2) && !descriptor.dynamic_typing, "descriptor_value");
                if (i) descriptions += "|";
                descriptions += descriptor.name + ":" + std::to_string(descriptor.type) + ":" +
                    std::to_string(descriptor.read_only) + ":" + std::to_string(descriptor.dynamic_typing);
            }
            record("event=DESCRIBE op=2 descriptors=" + descriptions);
            auto listed = await(client->list_parameters({"case"}, 0));
            std::sort(listed.names.begin(), listed.names.end());
            require(listed.names == names, "listed_names");
            record("event=LIST op=3 names=case.counter,case.label,case.readonly");
            const auto mutate = [&](int op, bool atomic, const std::vector<rclcpp::Parameter>& values, bool success) {
                rcl_interfaces::msg::SetParametersResult answer;
                if (atomic) answer = await(client->set_parameters_atomically(values));
                else {
                    const auto answers = await(client->set_parameters(values));
                    require(answers.size() == 1, "set_result_cardinality"); answer = answers[0];
                }
                require(answer.successful == success && (success || !answer.reason.empty()), "mutation_result");
                if (op == 13) require(answer.reason == "negative_counter", "callback_reason");
                record("event=MUTATE op=" + std::to_string(op) + " atomic=" + std::to_string(atomic) +
                       " request=" + pack(values) + " ok=" + std::to_string(answer.successful) + " reason=" + hex(answer.reason));
            };
            const auto wait_event = [&](size_t count, const std::vector<rclcpp::Parameter>& values) {
                until([&] { return mutation_events.size() >= count; }, 5s);
                require(mutation_events.size() == count && mutation_events.back() == pack(values), "mutation_event_value");
            };
            std::vector<rclcpp::Parameter> values{rclcpp::Parameter(names[0], int64_t(11))};
            mutate(4, false, values, true); wait_event(1, values);
            values = {rclcpp::Parameter(names[0], int64_t(12)), rclcpp::Parameter(names[1], "accepted_" + run)};
            mutate(5, true, values, true); wait_event(2, values); read(6, 12, "accepted_" + run);
            mutate(7, true, {rclcpp::Parameter(names[0], int64_t(99)), rclcpp::Parameter(names[2], int64_t(8))}, false);
            read(8, 12, "accepted_" + run);
            mutate(9, true, {rclcpp::Parameter(names[1], "bad_type_" + run), rclcpp::Parameter(names[0], std::string("wrong_type"))}, false);
            read(10, 12, "accepted_" + run);
            mutate(11, true, {rclcpp::Parameter(names[0], int64_t(99)), rclcpp::Parameter("case.undeclared", int64_t(1))}, false);
            read(12, 12, "accepted_" + run);
            mutate(13, true, {rclcpp::Parameter(names[0], int64_t(-1)), rclcpp::Parameter(names[1], "bad_callback_" + run)}, false);
            read(14, 12, "accepted_" + run);
            values = {rclcpp::Parameter(names[0], int64_t(13)), rclcpp::Parameter(names[1], "recovered_" + run)};
            mutate(15, true, values, true); wait_event(3, values); read(16, 13, "recovered_" + run);
            auto final_list = await(client->list_parameters({"case"}, 0));
            std::sort(final_list.names.begin(), final_list.names.end());
            require(final_list.names == names, "final_listed_names");
            record("event=LIST op=17 names=case.counter,case.label,case.readonly");
            auto absent = std::make_shared<rclcpp::AsyncParametersClient>(node, ns + "/absent");
            require(!absent->wait_for_service(100ms), "absent_service_ready");
            record("event=ABSENT ready=0 wait_ms=100");
            const auto drain = std::chrono::steady_clock::now() + 250ms;
            while (std::chrono::steady_clock::now() < drain) progress();
            require(mutation_events.size() == 3, "unexpected_mutation_event");
            record("event=BUSINESS_DONE calls=17 accepted_mutations=3 rejected_mutations=4 changed_events=3");
            until(released, 10s);
        }
        FastddsDsoftbusTeardownCheckpoint("BEFORE_NODE_DELETE");
        result = 0;
    } catch (const std::exception& error) {
        std::cerr << "PARAM_EXCEPTION stage=" << stage << " message=" << error.what() << std::endl;
        if (!initialized) { record("event=INIT_FAILED result=4"); return 4; }
        ros_failure_protocol::Report(run, side, pid, stage, "SCENARIO_FAILED");
    }
    FastddsDsoftbusTeardownCheckpoint("AFTER_NODE_ENTITY_DELETE");
    try { if (!rclcpp::shutdown()) result = 4; } catch (...) { result = 4; }
    FastddsDsoftbusTeardownCheckpoint("AFTER_CONTEXT_CLEANUP");
    if (FastddsDsoftbusTeardownResult() != 0) result = 4;
    record("event=CLOSED result=" + std::to_string(result));
    return result;
}
