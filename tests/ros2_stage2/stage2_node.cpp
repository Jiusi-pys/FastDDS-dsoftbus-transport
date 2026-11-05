// Stage 2 workload: an ordinary rclcpp program. It has no Fast DDS or Transport includes; the
// autoload library selects DSoftBus from outside the process image.
//
//   stage2_node suite  <0|1> [groups]     both boards run the same cases in the same order
//   stage2_node stream <0|1> <seconds>    10 Hz RELIABLE stream both ways, for outage and restart runs
//
// Output: S2_CASE name=<case> pass=<0|1> detail=<text>, then S2_SUMMARY passed=<n> failed=<n>.
// Exit 0 only when every case passed and rclcpp shut down.
#include <action_msgs/msg/goal_status.hpp>
#include <example_interfaces/action/fibonacci.hpp>
#include <example_interfaces/srv/add_two_ints.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <lifecycle_msgs/srv/change_state.hpp>
#include <lifecycle_msgs/srv/get_state.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <std_msgs/msg/int32.hpp>
#include <std_msgs/msg/string.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using Int32 = std_msgs::msg::Int32;
using Text = std_msgs::msg::String;
using AddTwoInts = example_interfaces::srv::AddTwoInts;
using Fibonacci = example_interfaces::action::Fibonacci;
using ChangeState = lifecycle_msgs::srv::ChangeState;
using GetState = lifecycle_msgs::srv::GetState;
using Transition = lifecycle_msgs::msg::Transition;

namespace {
constexpr auto kCaseTimeout = 90s;
constexpr int kSamples = 20;
constexpr std::size_t kLargeBytes = 150000;

int passed = 0, failed = 0;

void report(const std::string& name, bool ok, const std::string& detail)
{
    std::printf("S2_CASE name=%s pass=%d detail=%s\n", name.c_str(), ok ? 1 : 0, detail.c_str());
    std::fflush(stdout);
    ++(ok ? passed : failed);
}

template <typename Predicate>
bool wait_for(Predicate predicate, std::chrono::seconds limit = kCaseTimeout)
{
    const auto end = Clock::now() + limit;
    while (rclcpp::ok() && Clock::now() < end) {
        if (predicate()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return predicate();
}

std::string pattern(std::size_t size, int side)
{
    std::string value(size, 'x');
    for (std::size_t i = 0; i < size; ++i) value[i] = static_cast<char>('a' + (i * 7 + static_cast<std::size_t>(side)) % 26);
    return value;
}

struct Context {
    int side = 0, peer = 1;
    rclcpp::Node::SharedPtr node;
    rclcpp_lifecycle::LifecycleNode::SharedPtr lifecycle;
    std::string me() const { return std::to_string(side); }
    std::string other() const { return std::to_string(peer); }
    std::string peer_node() const { return "s2_node_" + other(); }
};

// Each side publishes the phase it reached on a latched topic and waits for the peer to reach it.
class Barrier {
public:
    explicit Barrier(const Context& c)
    {
        const auto qos = rclcpp::QoS(1).reliable().transient_local();
        publisher_ = c.node->create_publisher<Int32>("/s2/barrier_" + c.me(), qos);
        subscription_ = c.node->create_subscription<Int32>("/s2/barrier_" + c.other(), qos,
            [this](Int32::ConstSharedPtr m) { peer_ = m->data; });
    }
    bool pass(int phase)
    {
        Int32 message; message.data = phase; publisher_->publish(message);
        return wait_for([&] { return peer_.load() >= phase; }, 180s);
    }
private:
    rclcpp::Publisher<Int32>::SharedPtr publisher_;
    rclcpp::Subscription<Int32>::SharedPtr subscription_;
    std::atomic<int> peer_{0};
};

// Passed to the cases that own a server: it must stay alive until the peer's calls have finished.
using Step = std::function<bool()>;

bool graph_has_peer(const Context& c)
{
    for (const auto& name : c.node->get_node_names())
        if (name == "/" + c.peer_node()) return true;
    return false;
}

void graph_case(const Context& c)
{
    const bool node_seen = wait_for([&] { return graph_has_peer(c); });
    const std::string topic = "/s2/barrier_" + c.other();
    const bool topic_seen = wait_for([&] { return c.node->get_topic_names_and_types().count(topic) == 1; });
    report("graph_peer_node_and_topic", node_seen && topic_seen, "node=" + std::to_string(node_seen) + ",topic=" + std::to_string(topic_seen));
}

// Shared with subscription callbacks: the executor keeps spinning after a case returns, so a callback
// already running must not touch that case's stack frame.
struct Received {
    std::mutex mutex;
    std::set<int> seen;
    int gaps = 0, last = 0, count = 0;
    double max_silence = 0;
    Clock::time_point last_time = Clock::now();
};

void pubsub_case(const Context& c, const std::string& name, const rclcpp::QoS& qos)
{
    auto state = std::make_shared<Received>();
    auto subscription = c.node->create_subscription<Int32>("/s2/" + name + "_" + c.other(), qos, [state](Int32::ConstSharedPtr m) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (m->data != state->last + 1) ++state->gaps;
        state->last = m->data; state->seen.insert(m->data);
    });
    auto publisher = c.node->create_publisher<Int32>("/s2/" + name + "_" + c.me(), qos);
    bool matched = wait_for([&] { return publisher->get_subscription_count() == 1 && subscription->get_publisher_count() == 1; });
    std::this_thread::sleep_for(500ms);
    for (int i = 1; i <= kSamples; ++i) {
        Int32 message; message.data = i; publisher->publish(message);
        std::this_thread::sleep_for(20ms);
    }
    const bool all = wait_for([&] { std::lock_guard<std::mutex> lock(state->mutex); return state->seen.size() == kSamples; }, 30s);
    std::lock_guard<std::mutex> lock(state->mutex);
    report("pubsub_" + name, matched && all && state->gaps == 0,
        "writes=" + std::to_string(kSamples) + ",takes=" + std::to_string(state->seen.size()) + ",gaps=" + std::to_string(state->gaps));
}

void late_joiner_case(const Context& c, Barrier& barrier, int phase)
{
    const auto qos = rclcpp::QoS(1).reliable().transient_local();
    auto publisher = c.node->create_publisher<Int32>("/s2/latched_" + c.me(), qos);
    Int32 message; message.data = 4100 + c.side; publisher->publish(message);
    const bool peer_published = barrier.pass(phase);
    auto value = std::make_shared<std::atomic<int>>(0);
    auto subscription = c.node->create_subscription<Int32>("/s2/latched_" + c.other(), qos, [value](Int32::ConstSharedPtr m) { *value = m->data; });
    const bool got = wait_for([&] { return value->load() == 4100 + c.peer; }, 30s);
    report("transient_local_late_joiner", peer_published && got, "value=" + std::to_string(value->load()));
}

void fragmented_case(const Context& c)
{
    const auto qos = rclcpp::QoS(4).reliable();
    auto matches = std::make_shared<std::atomic<bool>>(false);
    auto subscription = c.node->create_subscription<Text>("/s2/large_" + c.other(), qos,
        [matches, expected = pattern(kLargeBytes, c.peer)](Text::ConstSharedPtr m) { if (m->data == expected) *matches = true; });
    auto publisher = c.node->create_publisher<Text>("/s2/large_" + c.me(), qos);
    const bool matched = wait_for([&] { return publisher->get_subscription_count() == 1 && subscription->get_publisher_count() == 1; });
    std::this_thread::sleep_for(500ms);
    Text message; message.data = pattern(kLargeBytes, c.side); publisher->publish(message);
    const bool got = wait_for([&] { return matches->load(); }, 60s);
    report("fragmented_150000_bytes", matched && got, "bytes=" + std::to_string(kLargeBytes));
}

void service_case(const Context& c, const Step& peers_done)
{
    auto server = c.node->create_service<AddTwoInts>("/s2/add_" + c.me(),
        [](const AddTwoInts::Request::SharedPtr request, AddTwoInts::Response::SharedPtr response) { response->sum = request->a + request->b; });
    auto client = c.node->create_client<AddTwoInts>("/s2/add_" + c.other());
    bool ready = client->wait_for_service(kCaseTimeout);
    int good = 0;
    std::string missing;
    for (int i = 0; ready && i < 5; ++i) {
        auto request = std::make_shared<AddTwoInts::Request>(); request->a = i; request->b = 40;
        auto future = client->async_send_request(request);
        if (future.wait_for(20s) == std::future_status::ready && future.get()->sum == i + 40) ++good;
        else missing += "_" + std::to_string(i);
    }
    report("service_request_reply", good == 5, "replies=" + std::to_string(good) + ",missing=" + missing);
    peers_done();
}

void action_case(const Context& c, const Step& peers_done)
{
    auto server = rclcpp_action::create_server<Fibonacci>(c.node, "/s2/fib_" + c.me(),
        [](const rclcpp_action::GoalUUID&, std::shared_ptr<const Fibonacci::Goal>) { return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE; },
        [](std::shared_ptr<rclcpp_action::ServerGoalHandle<Fibonacci>>) { return rclcpp_action::CancelResponse::ACCEPT; },
        [](std::shared_ptr<rclcpp_action::ServerGoalHandle<Fibonacci>> handle) {
            std::thread([handle] {
                auto feedback = std::make_shared<Fibonacci::Feedback>();
                auto result = std::make_shared<Fibonacci::Result>();
                feedback->sequence = {0, 1};
                for (int i = 2; i <= handle->get_goal()->order; ++i) {
                    if (handle->is_canceling()) { result->sequence = feedback->sequence; handle->canceled(result); return; }
                    feedback->sequence.push_back(feedback->sequence[i - 1] + feedback->sequence[i - 2]);
                    handle->publish_feedback(feedback);
                    std::this_thread::sleep_for(100ms);
                }
                result->sequence = feedback->sequence; handle->succeed(result);
            }).detach();
        });
    auto client = rclcpp_action::create_client<Fibonacci>(c.node, "/s2/fib_" + c.other());
    const bool ready = client->wait_for_action_server(kCaseTimeout);
    auto feedbacks = std::make_shared<std::atomic<int>>(0);
    rclcpp_action::Client<Fibonacci>::SendGoalOptions options;
    options.feedback_callback = [feedbacks](auto, auto) { ++*feedbacks; };
    Fibonacci::Goal goal; goal.order = 8;
    bool done = false; int last = 0;
    if (ready) {
        auto accepted = client->async_send_goal(goal, options);
        if (accepted.wait_for(20s) == std::future_status::ready && accepted.get()) {
            auto result = client->async_get_result(accepted.get());
            if (result.wait_for(30s) == std::future_status::ready) {
                const auto wrapped = result.get();
                done = wrapped.code == rclcpp_action::ResultCode::SUCCEEDED && !wrapped.result->sequence.empty();
                if (done) last = wrapped.result->sequence.back();
            }
        }
    }
    report("action_goal_feedback_result", done && last == 21 && feedbacks->load() > 0, "last=" + std::to_string(last) + ",feedback=" + std::to_string(feedbacks->load()));

    bool cancelled = false;
    std::string stage = "no_server";
    if (ready) {
        goal.order = 200;
        stage = "goal_response";
        auto accepted = client->async_send_goal(goal);
        if (accepted.wait_for(20s) == std::future_status::ready && accepted.get()) {
            auto handle = accepted.get();
            std::this_thread::sleep_for(300ms);
            auto cancel = client->async_cancel_goal(handle);
            auto result = client->async_get_result(handle);
            stage = "cancel_response";
            if (cancel.wait_for(20s) == std::future_status::ready) {
                stage = "result_response";
                if (result.wait_for(30s) == std::future_status::ready) {
                    stage = "result_code";
                    cancelled = result.get().code == rclcpp_action::ResultCode::CANCELED;
                }
            }
        }
    }
    report("action_cancel", cancelled, "canceled=" + std::to_string(cancelled) + ",stage=" + (cancelled ? std::string("done") : stage));
    peers_done();
}

void parameter_case(const Context& c)
{
    auto client = std::make_shared<rclcpp::AsyncParametersClient>(c.node, c.peer_node());
    const bool ready = client->wait_for_service(kCaseTimeout);
    const std::int64_t expected = 700 + c.side;
    bool stored = false; std::int64_t read = -1;
    if (ready) {
        // Each side writes its own parameter on the peer, so the two writers never collide.
        const std::string name = "s2_from_" + c.me();
        auto set = client->set_parameters({rclcpp::Parameter(name, expected)});
        stored = set.wait_for(20s) == std::future_status::ready && set.get().at(0).successful;
        auto get = client->get_parameters({name});
        if (get.wait_for(20s) == std::future_status::ready) read = get.get().at(0).as_int();
    }
    report("parameter_remote_set_get", stored && read == expected, "value=" + std::to_string(read));
}

void lifecycle_case(const Context& c)
{
    const std::string peer = "/s2_life_" + c.other();
    auto change = c.node->create_client<ChangeState>(peer + "/change_state");
    auto query = c.node->create_client<GetState>(peer + "/get_state");
    bool ok = change->wait_for_service(kCaseTimeout) && query->wait_for_service(kCaseTimeout);
    std::string label = "not_reached";
    for (const auto id : {Transition::TRANSITION_CONFIGURE, Transition::TRANSITION_ACTIVATE}) {
        if (!ok) break;
        auto request = std::make_shared<ChangeState::Request>(); request->transition.id = id;
        auto reply = change->async_send_request(request);
        ok = reply.wait_for(20s) == std::future_status::ready && reply.get()->success;
    }
    if (ok) {
        auto reply = query->async_send_request(std::make_shared<GetState::Request>());
        ok = reply.wait_for(20s) == std::future_status::ready;
        if (ok) label = reply.get()->current_state.label;
    }
    report("lifecycle_remote_transitions", ok && label == "active", "state=" + label);
}

bool group_on(const std::string& groups, const char* name) { return groups == "all" || groups.find(name) != std::string::npos; }

void run_suite(const Context& c, Barrier& barrier, const std::string& groups)
{
    int phase = 0;
    const auto step = [&](const char* name) {
        if (barrier.pass(++phase)) return true;
        report(std::string("barrier_") + name, false, "peer_not_reached"); return false; };
    graph_case(c);
    if (!step("start")) return;
    if (group_on(groups, "topic")) {
        pubsub_case(c, "reliable", rclcpp::QoS(kSamples).reliable());
        if (!step("reliable")) return;
        pubsub_case(c, "best_effort", rclcpp::QoS(kSamples).best_effort());
        if (!step("best_effort")) return;
        late_joiner_case(c, barrier, ++phase);
        if (!step("late_joiner")) return;
        fragmented_case(c);
        if (!step("fragmented")) return;
    }
    bool in_step = true;
    const Step finish = [&] { in_step = step("calls_finished"); return in_step; };
    if (group_on(groups, "service")) { service_case(c, finish); if (!in_step) return; }
    if (group_on(groups, "action")) { action_case(c, finish); if (!in_step) return; }
    if (group_on(groups, "parameter")) { parameter_case(c); if (!step("parameter")) return; }
    if (group_on(groups, "lifecycle")) { lifecycle_case(c); if (!step("lifecycle")) return; }
}

void run_stream(const Context& c, int seconds)
{
    const auto qos = rclcpp::QoS(rclcpp::KeepAll()).reliable();
    auto state = std::make_shared<Received>();
    auto subscription = c.node->create_subscription<Int32>("/s2/stream_" + c.other(), qos, [state](Int32::ConstSharedPtr m) {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto now = Clock::now();
        if (state->count > 0) state->max_silence = std::max(state->max_silence, std::chrono::duration<double>(now - state->last_time).count());
        state->last_time = now;
        // A volatile reader may join mid-stream, so the first sample starts anywhere, and a restarted
        // peer starts again from 1. Anything else out of order is a gap.
        if (state->count > 0 && m->data != state->last + 1 && m->data != 1) ++state->gaps;
        state->last = m->data; ++state->count;
    });
    auto publisher = c.node->create_publisher<Int32>("/s2/stream_" + c.me(), qos);
    if (!wait_for([&] { return publisher->get_subscription_count() == 1; })) { report("stream", false, "not_matched"); return; }
    std::printf("S2_STREAM_MATCHED\n"); std::fflush(stdout);
    const auto end = Clock::now() + std::chrono::seconds(seconds);
    int sequence = 0;
    while (rclcpp::ok() && Clock::now() < end) {
        Int32 message; message.data = ++sequence; publisher->publish(message);
        std::this_thread::sleep_for(100ms);
    }
    std::this_thread::sleep_for(5s); // lets acknowledged samples from the peer's tail arrive
    std::lock_guard<std::mutex> lock(state->mutex);
    std::printf("S2_STREAM sent=%d received=%d gaps=%d max_silence=%.1f\n", sequence, state->count, state->gaps, state->max_silence);
    report("stream", state->count > 0 && state->gaps == 0,
        "sent=" + std::to_string(sequence) + ",received=" + std::to_string(state->count) + ",gaps=" + std::to_string(state->gaps));
}
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: stage2_node suite|stream <0|1> [groups|seconds]\n"); return 2; }
    const std::string mode = argv[1];
    Context c;
    c.side = std::stoi(argv[2]); c.peer = c.side ? 0 : 1;
    rclcpp::InitOptions options;
    rclcpp::init(1, argv, options);
    c.node = std::make_shared<rclcpp::Node>("s2_node_" + c.me());
    c.lifecycle = std::make_shared<rclcpp_lifecycle::LifecycleNode>("s2_life_" + c.me());
    // Set remotely by the peer's parameter case.
    c.node->declare_parameter<std::int64_t>("s2_from_" + c.other(), 0);
    // Outlives the executor, whose threads can still deliver the peer's last barrier message.
    Barrier barrier(c);
    rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 4);
    executor.add_node(c.node);
    executor.add_node(c.lifecycle->get_node_base_interface());
    std::thread spinner([&] { executor.spin(); });
    if (mode == "suite") run_suite(c, barrier, argc > 3 ? argv[3] : "all");
    else run_stream(c, argc > 3 ? std::stoi(argv[3]) : 30);
    std::this_thread::sleep_for(2s); // the peer's last replies still need this process
    executor.cancel();
    spinner.join();
    executor.remove_node(c.node);
    executor.remove_node(c.lifecycle->get_node_base_interface());
    c.lifecycle.reset();
    c.node.reset();
    const bool shutdown = rclcpp::shutdown();
    std::printf("S2_SUMMARY passed=%d failed=%d shutdown=%d\n", passed, failed, shutdown ? 1 : 0);
    std::fflush(stdout);
    return failed == 0 && passed > 0 && shutdown ? 0 : 1;
}
