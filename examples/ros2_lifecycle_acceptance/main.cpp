// Real remote lifecycle services/events; files coordinate start and teardown only.
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition_event.hpp>
#include <lifecycle_msgs/srv/change_state.hpp>
#include <lifecycle_msgs/srv/get_state.hpp>
#include <lifecycle_msgs/srv/get_available_transitions.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <thread>
#include <vector>
#include <process.h>
#include "../ros2_transport/FailureProtocol.hpp"
using namespace std::chrono_literals;
using Change = lifecycle_msgs::srv::ChangeState;
using Get = lifecycle_msgs::srv::GetState;
using Available = lifecycle_msgs::srv::GetAvailableTransitions;
using Event = lifecycle_msgs::msg::TransitionEvent;
using Return = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;
using Triple = std::array<unsigned, 3>;
static_assert(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED == 1 &&
    lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE == 2 &&
    lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE == 3 &&
    lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED == 4, "state contract");
static_assert(lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS == 97 &&
    lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_FAILURE == 98 &&
    lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_ERROR == 99, "callback contract");

static void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
static bool control(const char* key, const std::string& expected) {
    const char* path = std::getenv(key);
    require(path && *path, "control_path_missing");
    std::ifstream file(path); std::string line, extra;
    return std::getline(file, line) && line == expected && !std::getline(file, extra);
}
static std::string numbers(const std::vector<unsigned>& values) {
    std::string text;
    for (auto value : values) { if (!text.empty()) text += ','; text += std::to_string(value); }
    return text.empty() ? "-" : text;
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::string side = argv[1], run = argv[2];
    if ((side != "A" && side != "B") || !std::regex_match(run, std::regex("[0-9a-f]{16}"))) return 2;
    const auto pid = _getpid();
    const std::string ns = "/lifecycle_" + run;
    unsigned local = 0;
    const auto record = [&](const std::string& text) {
        std::cout << "LIFECYCLE_ACCEPT run=" << run << " side=" << side << " pid=" << pid
                  << " local=" << ++local << ' ' << text << std::endl;
    };
    int result = 4; bool initialized = false; const char* stage = "RCLCPP_INIT";
    try {
        rclcpp::init(1, argv); initialized = true; stage = "NODE_CREATE";
        auto options = rclcpp::NodeOptions().use_intra_process_comms(false)
            .start_parameter_services(false).start_parameter_event_publisher(false);
        rclcpp::executors::SingleThreadedExecutor executor;
        const auto global_end = std::chrono::steady_clock::now() + 50s;
        const auto progress = [&] { executor.spin_some(); std::this_thread::sleep_for(1ms); };
        const auto until = [&](const auto& ready, auto timeout) {
            const auto end = std::min(global_end, std::chrono::steady_clock::now() + timeout);
            while (!ready()) { require(std::chrono::steady_clock::now() < end, "bounded_wait_expired"); progress(); }
        };
        const auto start = [&] { return control("FDDST_LIFECYCLE_START", "START " + run); };
        const auto released = [&] { return control("FDDST_LIFECYCLE_RELEASE", "RELEASE " + run); };
        stage = "LIFECYCLE_SCENARIO";
        if (side == "A") {
            auto recover = std::make_shared<rclcpp_lifecycle::LifecycleNode>("recover", ns, options);
            auto fatal = std::make_shared<rclcpp_lifecycle::LifecycleNode>("fatal", ns, options);
            std::map<std::string, unsigned> counts;
            const auto callback = [&](const std::string& node, const std::string& name,
                    const rclcpp_lifecycle::State& previous, unsigned current, Return code) {
                auto count = ++counts[node + "/" + name];
                record("event=CALLBACK node=" + node + " callback=" + name + " ordinal=" + std::to_string(count) +
                    " previous=" + std::to_string(previous.id()) + " current=" + std::to_string(current) +
                    " return=" + std::to_string(static_cast<unsigned>(code)));
                return code;
            };
            require(recover->register_on_configure([&](const auto& previous) {
                const auto attempt = counts["recover/configure"] + 1;
                require(attempt <= 4, "extra_configure");
                return callback("recover", "configure", previous, recover->get_current_state().id(),
                    attempt == 1 ? Return::FAILURE : attempt == 3 ? static_cast<Return>(99) : Return::SUCCESS);
            }), "register_configure");
            require(recover->register_on_activate([&](const auto& previous) {
                return callback("recover", "activate", previous, recover->get_current_state().id(), Return::SUCCESS);
            }), "register_activate");
            require(recover->register_on_deactivate([&](const auto& previous) {
                return callback("recover", "deactivate", previous, recover->get_current_state().id(), Return::SUCCESS);
            }), "register_deactivate");
            require(recover->register_on_cleanup([&](const auto& previous) {
                return callback("recover", "cleanup", previous, recover->get_current_state().id(), Return::SUCCESS);
            }), "register_cleanup");
            require(recover->register_on_shutdown([&](const auto& previous) {
                return callback("recover", "shutdown", previous, recover->get_current_state().id(), Return::SUCCESS);
            }), "register_shutdown");
            require(recover->register_on_error([&](const auto& previous) {
                return callback("recover", "error", previous, recover->get_current_state().id(), Return::SUCCESS);
            }), "register_error");
            require(fatal->register_on_configure([&](const auto& previous) {
                return callback("fatal", "configure", previous, fatal->get_current_state().id(), static_cast<Return>(99));
            }), "register_fatal_configure");
            require(fatal->register_on_error([&](const auto& previous) {
                return callback("fatal", "error", previous, fatal->get_current_state().id(), Return::FAILURE);
            }), "register_fatal_error");
            executor.add_node(recover->get_node_base_interface()); executor.add_node(fatal->get_node_base_interface());
            record("event=READY namespace=" + ns);
            until([&] { return recover->count_subscribers(ns + "/recover/transition_event") == 1 &&
                fatal->count_subscribers(ns + "/fatal/transition_event") == 1; }, 15s);
            record("event=MATCHED nodes=2"); until(start, 15s); record("event=START nodes=2");
            until([&] { return recover->get_current_state().id() == 4 && fatal->get_current_state().id() == 4; }, 25s);
            const std::map<std::string, unsigned> expected{{"recover/configure",4},{"recover/activate",1},
                {"recover/deactivate",1},{"recover/cleanup",1},{"recover/error",1},{"recover/shutdown",1},
                {"fatal/configure",1},{"fatal/error",1}};
            require(counts == expected, "callback_counts");
            record("event=BUSINESS_DONE callbacks=11 finalized=2 pending=0");
            until(released, 10s);
            require(counts == expected, "callback_after_done");
            executor.remove_node(recover->get_node_base_interface()); executor.remove_node(fatal->get_node_base_interface());
            FastddsDsoftbusTeardownCheckpoint("BEFORE_NODE_DELETE");
        } else {
            auto node = std::make_shared<rclcpp::Node>("client", ns, options); executor.add_node(node);
            struct Remote {
                rclcpp::Client<Change>::SharedPtr change;
                rclcpp::Client<Get>::SharedPtr get;
                rclcpp::Client<Available>::SharedPtr available;
                rclcpp::Subscription<Event>::SharedPtr events;
                std::vector<Triple> seen;
            };
            std::map<std::string, Remote> remotes;
            for (const std::string name : {"recover", "fatal"}) {
                auto& remote = remotes[name]; const auto base = ns + "/" + name;
                remote.change = node->create_client<Change>(base + "/change_state");
                remote.get = node->create_client<Get>(base + "/get_state");
                remote.available = node->create_client<Available>(base + "/get_available_transitions");
                remote.events = node->create_subscription<Event>(base + "/transition_event", rclcpp::QoS(32).reliable(),
                    [&, name](Event::ConstSharedPtr value) {
                        auto& seen = remotes.at(name).seen;
                        require(seen.size() < (name == "recover" ? 17u : 3u), "extra_transition_event");
                        seen.push_back({value->transition.id, value->start_state.id, value->goal_state.id});
                        record("event=TRANSITION node=" + name + " ordinal=" + std::to_string(seen.size()) +
                            " transition=" + std::to_string(value->transition.id) + " start=" +
                            std::to_string(value->start_state.id) + " goal=" + std::to_string(value->goal_state.id));
                    });
            }
            record("event=READY namespace=" + ns);
            until([&] {
                return std::all_of(remotes.begin(), remotes.end(), [](const auto& item) {
                    return item.second.change->service_is_ready() && item.second.get->service_is_ready() &&
                        item.second.available->service_is_ready() && item.second.events->get_publisher_count() == 1;
                });
            }, 15s);
            record("event=MATCHED nodes=2"); until(start, 15s); record("event=START nodes=2");
            const auto await = [&](auto future) {
                until([&] { return future.wait_for(0ms) == std::future_status::ready; }, 3s);
                return future.get();
            };
            const std::map<unsigned, std::string> labels{{1,"unconfigured"},{2,"inactive"},{3,"active"},{4,"finalized"}};
            const std::map<unsigned, std::vector<unsigned>> legal{{1,{1,5}},{2,{2,3,6}},{3,{4,7}},{4,{}}};
            const auto snapshot = [&](const std::string& name, unsigned operation, unsigned state) {
                auto& remote = remotes.at(name);
                auto response = await(remote.get->async_send_request(std::make_shared<Get::Request>()));
                require(response && response->current_state.id == state && response->current_state.label == labels.at(state), "remote_state");
                record("event=STATE node=" + name + " operation=" + std::to_string(operation) +
                    " service=" + ns + "/" + name + "/get_state state=" + std::to_string(state) + " label=" + labels.at(state));
                auto available = await(remote.available->async_send_request(std::make_shared<Available::Request>()));
                require(bool(available), "available_null"); std::vector<unsigned> ids;
                for (const auto& transition : available->available_transitions) {
                    require(transition.start_state.id == state, "available_start_state"); ids.push_back(transition.transition.id);
                }
                std::sort(ids.begin(), ids.end()); require(ids == legal.at(state), "available_transitions");
                record("event=AVAILABLE node=" + name + " operation=" + std::to_string(operation) +
                    " service=" + ns + "/" + name + "/get_available_transitions ids=" + numbers(ids));
            };
            std::map<std::string, std::vector<Triple>> expected;
            const auto change = [&](const std::string& name, unsigned operation, unsigned id, const std::string& label,
                    bool success, unsigned state, const std::vector<Triple>& events) {
                auto& remote = remotes.at(name);
                auto request = std::make_shared<Change::Request>(); request->transition.id = static_cast<uint8_t>(id);
                request->transition.label = label;
                record("event=REQUEST node=" + name + " operation=" + std::to_string(operation) +
                    " service=" + ns + "/" + name + "/change_state transition=" + std::to_string(id) + " label=" + (label.empty() ? "-" : label));
                auto response = await(remote.change->async_send_request(request));
                require(response && response->success == success, "change_response");
                record("event=RESPONSE node=" + name + " operation=" + std::to_string(operation) + " success=" + (success ? "1" : "0"));
                auto& wanted = expected[name]; wanted.insert(wanted.end(), events.begin(), events.end());
                until([&] { return remote.seen.size() >= wanted.size(); }, 3s);
                require(remote.seen == wanted, "transition_sequence"); snapshot(name, operation, state);
            };
            // rcl 9.2.9 populates event states only; raw transition.id remains zero.
            // Requested transition identity comes from the service, not the event.
            snapshot("recover", 0, 1);
            change("recover", 1, 0, "unknown_transition", false, 1, {});
            change("recover", 2, 1, "", false, 1, {{0,1,10},{0,10,1}});
            change("recover", 3, 1, "", true, 2, {{0,1,10},{0,10,2}});
            change("recover", 4, 3, "", true, 3, {{0,2,13},{0,13,3}});
            change("recover", 5, 4, "", true, 2, {{0,3,14},{0,14,2}});
            change("recover", 6, 2, "", true, 1, {{0,2,11},{0,11,1}});
            change("recover", 7, 1, "", false, 1, {{0,1,10},{0,10,15},{0,15,1}});
            change("recover", 8, 1, "", true, 2, {{0,1,10},{0,10,2}});
            change("recover", 9, 6, "", true, 4, {{0,2,12},{0,12,4}});
            snapshot("fatal", 0, 1);
            change("fatal", 1, 1, "", false, 4, {{0,1,10},{0,10,15},{0,15,4}});
            const auto drain = std::chrono::steady_clock::now() + 250ms;
            while (std::chrono::steady_clock::now() < drain) progress();
            for (const auto& remote : remotes) require(remote.second.seen == expected.at(remote.first), "event_drain");
            record("event=BUSINESS_DONE changes=10 states=12 events=20 pending=0");
            until(released, 10s);
            executor.remove_node(node); FastddsDsoftbusTeardownCheckpoint("BEFORE_NODE_DELETE");
        }
        result = 0;
    } catch (const std::exception& error) {
        std::cerr << "LIFECYCLE_EXCEPTION stage=" << stage << " message=" << error.what() << std::endl;
        if (!initialized) { record("event=INIT_FAILED result=4"); return 4; }
        ros_failure_protocol::Report(run, side, pid, stage, "SCENARIO_FAILED");
    }
    FastddsDsoftbusTeardownCheckpoint("AFTER_NODE_ENTITY_DELETE");
    try { if (!rclcpp::shutdown()) result = 4; } catch (...) { result = 4; }
    FastddsDsoftbusTeardownCheckpoint("AFTER_CONTEXT_CLEANUP");
    if (FastddsDsoftbusTeardownResult() != 0) result = 4;
    record("event=CLOSED result=" + std::to_string(result)); return result;
}
