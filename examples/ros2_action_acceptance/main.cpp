// Ordinary ROS action traffic. Files coordinate teardown only.
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <example_interfaces/action/fibonacci.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>
#include <process.h>
#include "../ros2_transport/FailureProtocol.hpp"
using namespace std::chrono_literals;
using Action = example_interfaces::action::Fibonacci;
using ServerHandle = rclcpp_action::ServerGoalHandle<Action>;
using ClientHandle = rclcpp_action::ClientGoalHandle<Action>;

static void require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
static std::string uuid(const rclcpp_action::GoalUUID& value) {
    require(value.size() == 16 && std::any_of(value.begin(), value.end(), [](auto x) { return x != 0; }), "zero_uuid");
    std::ostringstream out;
    for (auto x : value) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(x);
    return out.str();
}
static std::string pack(const std::vector<int32_t>& values) {
    std::string out;
    for (auto value : values) { if (!out.empty()) out += ','; out += std::to_string(value); }
    return out.empty() ? "-" : out;
}
static std::vector<int32_t> sequence(int order) {
    std::vector<int32_t> out;
    for (int i = 0; i < order; ++i) out.push_back(i < 2 ? i : out[i - 1] + out[i - 2]);
    return out;
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::string side = argv[1], run = argv[2];
    if ((side != "A" && side != "B") || !std::regex_match(run, std::regex("[0-9a-f]{16}"))) return 2;
    const auto pid = _getpid();
    const std::string ns = "/action_" + run, action_name = ns + "/fibonacci";
    unsigned local_sequence = 0;
    const auto record = [&](const std::string& text) {
        std::cout << "ACTION_ACCEPT run=" << run << " side=" << side << " pid=" << pid
                  << " local=" << ++local_sequence << " " << text << std::endl;
    };
    int result = 4;
    bool initialized = false;
    const char* stage = "RCLCPP_INIT";
    try {
        rclcpp::init(1, argv);
        initialized = true;
        stage = "NODE_CREATE";
        auto options = rclcpp::NodeOptions().use_intra_process_comms(false)
            .start_parameter_services(false).start_parameter_event_publisher(false);
        auto node = std::make_shared<rclcpp::Node>(side == "A" ? "server" : "client", ns, options);
        rclcpp::executors::SingleThreadedExecutor executor;
        executor.add_node(node);
        const auto global_end = std::chrono::steady_clock::now() + 50s;
        const auto progress = [&] { executor.spin_some(); std::this_thread::sleep_for(1ms); };
        const auto until = [&](const auto& ready, auto timeout) {
            const auto end = std::min(global_end, std::chrono::steady_clock::now() + timeout);
            while (!ready()) { require(std::chrono::steady_clock::now() < end, "bounded_wait_expired"); progress(); }
        };
        const char* release = std::getenv("FDDST_ACTION_RELEASE");
        require(release != nullptr, "release_path_missing");
        const auto released = [&] {
            std::ifstream f(release); std::string line, extra;
            return std::getline(f, line) && line == "RELEASE " + run && !std::getline(f, extra);
        };
        stage = "ACTION_SCENARIO";
        if (side == "A") {
            std::map<int, std::shared_ptr<ServerHandle>> handles;
            std::map<int, std::vector<int32_t>> emitted;
            std::set<int> terminal, requested;
            bool concurrent = false;
            unsigned cancel_calls = 0;
            auto server = rclcpp_action::create_server<Action>(node, action_name,
                [&](const auto& id, std::shared_ptr<const Action::Goal> goal) {
                    const int order = goal->order;
                    require(order == 6 || order == 40 || order == -1 || order == 0 || order == 5, "unexpected_goal");
                    require(requested.insert(order).second, "duplicate_goal");
                    record("event=GOAL order=" + std::to_string(order) + " uuid=" + uuid(id) + " accepted=" + (order == -1 ? "0" : "1"));
                    return order == -1 ? rclcpp_action::GoalResponse::REJECT : rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
                },
                [&](std::shared_ptr<ServerHandle> handle) {
                    require(handle->get_goal()->order == 40 && handles.count(6) && handles.at(6)->is_active() &&
                            handles.at(6)->is_executing() && !terminal.count(6) && ++cancel_calls == 1, "cancel_target_or_concurrency");
                    require(concurrent && emitted[40] == sequence(2), "cancel_before_feedback");
                    record("event=CANCEL order=40 uuid=" + uuid(handle->get_goal_id()) + " normal=" +
                           uuid(handles.at(6)->get_goal_id()) + " normal_active=1 accepted=1");
                    return rclcpp_action::CancelResponse::ACCEPT;
                },
                [&](std::shared_ptr<ServerHandle> handle) {
                    const int order = handle->get_goal()->order;
                    require(handle->is_executing() && handles.emplace(order, handle).second, "accepted_state");
                    record("event=ACCEPTED order=" + std::to_string(order) + " uuid=" + uuid(handle->get_goal_id()));
                });
            auto timer = node->create_wall_timer(100ms, [&] {
                if (handles.count(6) && handles.count(40) && !concurrent) {
                    require(handles.at(6)->is_executing() && handles.at(40)->is_executing(), "concurrent_state");
                    concurrent = true;
                    record("event=CONCURRENT normal=" + uuid(handles.at(6)->get_goal_id()) + " target=" +
                           uuid(handles.at(40)->get_goal_id()) + " normal_active=1 target_active=1");
                }
                for (auto& item : handles) {
                    const int order = item.first;
                    auto& handle = item.second;
                    if (terminal.count(order)) continue;
                    auto& values = emitted[order];
                    const auto identity = " order=" + std::to_string(order) + " uuid=" + uuid(handle->get_goal_id());
                    auto answer = std::make_shared<Action::Result>();
                    if (order == 40 && handle->is_canceling()) {
                        require(values == sequence(2), "canceled_values");
                        answer->sequence = values;
                        handle->canceled(answer);
                        record("event=TERMINAL" + identity + " code=5 values=" + pack(values) + " prestate=CANCELING");
                        terminal.insert(order);
                    } else if (order == 0) {
                        require(handle->is_executing(), "abort_state");
                        handle->abort(answer);
                        record("event=TERMINAL" + identity + " code=6 values=- prestate=EXECUTING");
                        terminal.insert(order);
                    } else {
                        require(handle->is_executing(), "feedback_state");
                        if ((order == 6 || order == 40) && !concurrent) continue;
                        if (order == 40 && !values.empty()) continue; // Controlled cancel target never completes naturally.
                        if (order == 6 && !values.empty() && !terminal.count(40)) continue;
                        if (values.empty()) values = sequence(2);
                        else values.push_back(values[values.size() - 1] + values[values.size() - 2]);
                        auto feedback = std::make_shared<Action::Feedback>();
                        feedback->sequence = values;
                        handle->publish_feedback(feedback);
                        record("event=FEEDBACK" + identity + " values=" + pack(values));
                        if (order != 40 && values.size() == static_cast<size_t>(order)) {
                            answer->sequence = values;
                            handle->succeed(answer);
                            record("event=TERMINAL" + identity + " code=4 values=" + pack(values) + " prestate=EXECUTING");
                            terminal.insert(order);
                        }
                    }
                }
            });
            record("event=SERVER_READY action=" + action_name);
            until([&] { return terminal.size() == 4 && requested.size() == 5; }, 40s);
            require(cancel_calls == 1 && concurrent && handles.size() == 4, "server_accounting");
            timer->cancel();
            // All owned handles reached a terminal state; no action work is left in the timer.
            record("event=BUSINESS_DONE requested=5 accepted=4 rejected=1 terminals=4 pending=0");
            until(released, 10s);
        } else {
            auto client = rclcpp_action::create_client<Action>(node, action_name);
            require(client->wait_for_action_server(10s), "action_server_unavailable");
            record("event=CLIENT_READY action=" + action_name);
            std::map<int, ClientHandle::SharedPtr> handles;
            std::map<int, std::shared_future<ClientHandle::WrappedResult>> results;
            std::map<int, size_t> feedback_sizes;
            const auto await = [&](auto future) {
                until([&] { return future.wait_for(0s) == std::future_status::ready; }, 5s);
                return future.get();
            };
            const auto send = [&](int order) {
                rclcpp_action::Client<Action>::SendGoalOptions send_options;
                send_options.feedback_callback = [&, order](ClientHandle::SharedPtr handle, const std::shared_ptr<const Action::Feedback> feedback) {
                    require(order == 6 || order == 40 || order == 5, "unexpected_feedback_order");
                    const auto& values = feedback->sequence;
                    require(values.size() >= 2 && values.size() <= static_cast<size_t>(order == 40 ? 2 : order) &&
                            values == sequence(static_cast<int>(values.size())) && values.size() > feedback_sizes[order], "feedback_value_order");
                    if (handles.count(order)) require(handle->get_goal_id() == handles.at(order)->get_goal_id(), "feedback_uuid");
                    feedback_sizes[order] = values.size();
                    record("event=FEEDBACK order=" + std::to_string(order) + " uuid=" + uuid(handle->get_goal_id()) + " values=" + pack(values));
                };
                Action::Goal goal; goal.order = order;
                record("event=SENT order=" + std::to_string(order));
                auto handle = await(client->async_send_goal(goal, send_options));
                require(bool(handle) == (order != -1), "goal_response");
                record("event=RESPONSE order=" + std::to_string(order) + " uuid=" + (handle ? uuid(handle->get_goal_id()) : "-") +
                       " accepted=" + (handle ? "1" : "0"));
                if (handle) {
                    handles.emplace(order, handle);
                    results.emplace(order, client->async_get_result(handle));
                }
            };
            const auto finish = [&](int order, rclcpp_action::ResultCode code, const std::vector<int32_t>& expected) {
                const auto answer = await(results.at(order));
                require(answer.goal_id == handles.at(order)->get_goal_id() && answer.code == code && answer.result &&
                        answer.result->sequence == expected, "wrapped_result");
                record("event=RESULT order=" + std::to_string(order) + " uuid=" + uuid(answer.goal_id) + " code=" +
                       std::to_string(static_cast<int>(answer.code)) + " values=" + pack(answer.result->sequence));
                results.erase(order);
            };
            send(6); send(40);
            until([&] { return feedback_sizes[40] == 2; }, 5s);
            record("event=CANCEL_REQUEST uuid=" + uuid(handles.at(40)->get_goal_id()) + " normal=" + uuid(handles.at(6)->get_goal_id()) + " feedback_seen=1");
            const auto canceled = await(client->async_cancel_goal(handles.at(40)));
            require(canceled && canceled->return_code == rclcpp_action::Client<Action>::CancelResponse::ERROR_NONE &&
                    canceled->goals_canceling.size() == 1 && canceled->goals_canceling[0].goal_id.uuid == handles.at(40)->get_goal_id(), "cancel_response");
            record("event=CANCEL_RESPONSE uuid=" + uuid(canceled->goals_canceling[0].goal_id.uuid) + " return_code=0 count=1");
            finish(40, rclcpp_action::ResultCode::CANCELED, sequence(2));
            finish(6, rclcpp_action::ResultCode::SUCCEEDED, sequence(6));
            send(-1);
            send(0); finish(0, rclcpp_action::ResultCode::ABORTED, {});
            send(5); finish(5, rclcpp_action::ResultCode::SUCCEEDED, sequence(5));
            const auto drain = std::chrono::steady_clock::now() + 250ms;
            while (std::chrono::steady_clock::now() < drain) progress();
            require(results.empty() && handles.size() == 4, "client_pending_work");
            for (const auto& handle : handles) client->stop_callbacks(handle.second);
            record("event=BUSINESS_DONE requested=5 accepted=4 rejected=1 terminals=4 pending=0");
            until(released, 10s);
        }
        executor.remove_node(node);
        FastddsDsoftbusTeardownCheckpoint("BEFORE_NODE_DELETE");
        result = 0;
    } catch (const std::exception& error) {
        std::cerr << "ACTION_EXCEPTION stage=" << stage << " message=" << error.what() << std::endl;
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
