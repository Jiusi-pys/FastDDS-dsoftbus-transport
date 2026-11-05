// Stage 2 feature workload: ordinary rclcpp, run on both boards with the same groups in the same order.
//
//   stage2_features <0|1> <group> [leave limit seconds]
//
// Each group runs in its own process pair, so a crash in one cannot hide another's result. Output follows
// stage2_node: S2_CASE lines, then S2_SUMMARY. Details carry measurements, and an oracle fails on any
// count of distinct samples that differs from what the case wrote, so a case cannot pass by delivering nothing.
#include "stage2_common.hpp"
#include <example_interfaces/action/fibonacci.hpp>
#include <example_interfaces/srv/add_two_ints.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <rcl_interfaces/msg/log.hpp>
#include <rcl_interfaces/msg/parameter_event.hpp>
#include <rcl_interfaces/srv/get_logger_levels.hpp>
#include <rcl_interfaces/srv/set_logger_levels.hpp>
#include <rclcpp/experimental/executors/events_executor/events_executor.hpp>
#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/generic_subscription.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/wait_set.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <rosgraph_msgs/msg/clock.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <statistics_msgs/msg/metrics_message.hpp>
#include <std_msgs/msg/string.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/static_transform_broadcaster.h>
#include <tf2_ros/transform_broadcaster.h>
#include <tf2_ros/transform_listener.h>
#include <type_description_interfaces/srv/get_type_description.hpp>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <vector>

using namespace stage2;
using Text = std_msgs::msg::String;
using Image = sensor_msgs::msg::Image;
using AddTwoInts = example_interfaces::srv::AddTwoInts;
using Fibonacci = example_interfaces::action::Fibonacci;

namespace {
struct Context {
    int side = 0, peer = 1;
    rclcpp::Node::SharedPtr node;
    Barrier* barrier = nullptr;
    int phase = 0;
    std::string me() const { return std::to_string(side); }
    std::string other() const { return std::to_string(peer); }
    std::string peer_node() const { return "s2f_node_" + other(); }
    std::string local(const std::string& name) const { return "/s2f/" + name + "_" + me(); }
    std::string remote(const std::string& name) const { return "/s2f/" + name + "_" + other(); }
    bool step() { return barrier->pass(++phase); }
};

std::string num(long long value) { return std::to_string(value); }

template <typename Publisher, typename Subscription>
bool matched(const Publisher& publisher, const Subscription& subscription, std::chrono::seconds limit = 60s)
{
    const bool ok = wait_for([&] { return publisher->get_subscription_count() >= 1 && subscription->get_publisher_count() >= 1; }, limit);
    std::this_thread::sleep_for(300ms);
    return ok;
}

struct Ints {
    std::mutex mutex;
    std::set<int> seen;
    int count = 0;
    void add(int value) { std::lock_guard<std::mutex> lock(mutex); seen.insert(value); ++count; }
    std::size_t size() { std::lock_guard<std::mutex> lock(mutex); return seen.size(); }
};

// ---- QoS ----

void deadline_case(Context& c)
{
    const auto qos = rclcpp::QoS(10).reliable().deadline(rclcpp::Duration(300ms));
    auto offered = std::make_shared<std::atomic<int>>(0);
    auto requested = std::make_shared<std::atomic<int>>(0);
    rclcpp::PublisherOptions publisher_options;
    publisher_options.event_callbacks.deadline_callback = [offered](rclcpp::QOSDeadlineOfferedInfo&) { ++*offered; };
    rclcpp::SubscriptionOptions subscription_options;
    subscription_options.event_callbacks.deadline_callback = [requested](rclcpp::QOSDeadlineRequestedInfo&) { ++*requested; };
    auto subscription = c.node->create_subscription<Int32>(c.remote("deadline"), qos, [](Int32::ConstSharedPtr) {}, subscription_options);
    auto publisher = c.node->create_publisher<Int32>(c.local("deadline"), qos, publisher_options);
    const bool ok = matched(publisher, subscription);
    c.barrier->pass(++c.phase);
    Int32 message;
    for (int i = 0; i < 40; ++i) { message.data = i; publisher->publish(message); std::this_thread::sleep_for(50ms); }
    const int during = requested->load() + offered->load(); // publishing at 20 Hz must not miss a 300 ms deadline
    const bool missed = wait_for([&] { return offered->load() >= 1 && requested->load() >= 1; }, 15s);
    report("qos_deadline", ok && during == 0 && missed,
        "during=" + num(during) + ",offered=" + num(offered->load()) + ",requested=" + num(requested->load()));
}

void liveliness_case(Context& c)
{
    const auto qos = rclcpp::QoS(10).reliable().liveliness(RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC)
        .liveliness_lease_duration(rclcpp::Duration(1s));
    auto lost = std::make_shared<std::atomic<int>>(0);
    auto alive = std::make_shared<std::atomic<int>>(0);
    auto not_alive_seen = std::make_shared<std::atomic<int>>(0);
    rclcpp::PublisherOptions publisher_options;
    publisher_options.event_callbacks.liveliness_callback = [lost](rclcpp::QOSLivelinessLostInfo&) { ++*lost; };
    rclcpp::SubscriptionOptions subscription_options;
    subscription_options.event_callbacks.liveliness_callback = [alive, not_alive_seen](rclcpp::QOSLivelinessChangedInfo& info) {
        *alive = info.alive_count;
        if (info.not_alive_count > 0) ++*not_alive_seen; };
    auto subscription = c.node->create_subscription<Int32>(c.remote("live"), qos, [](Int32::ConstSharedPtr) {}, subscription_options);
    auto publisher = c.node->create_publisher<Int32>(c.local("live"), qos, publisher_options);
    const bool ok = matched(publisher, subscription);
    c.barrier->pass(++c.phase);
    Int32 message;
    for (int i = 0; i < 10; ++i) { publisher->publish(message); publisher->assert_liveliness(); std::this_thread::sleep_for(100ms); }
    const bool was_alive = wait_for([&] { return alive->load() == 1; }, 10s);
    const bool detected = wait_for([&] { return lost->load() >= 1 && not_alive_seen->load() >= 1; }, 20s);
    report("qos_liveliness_manual", ok && was_alive && detected,
        "lost=" + num(lost->load()) + ",changes_not_alive=" + num(not_alive_seen->load()) + ",alive=" + num(alive->load()));
}

void lifespan_case(Context& c)
{
    auto expiring = rclcpp::QoS(10).reliable().transient_local().lifespan(rclcpp::Duration(1500ms));
    auto lasting = rclcpp::QoS(10).reliable().transient_local().lifespan(rclcpp::Duration(600s));
    auto short_writer = c.node->create_publisher<Int32>(c.local("span_short"), expiring);
    auto long_writer = c.node->create_publisher<Int32>(c.local("span_long"), lasting);
    Int32 message;
    for (int i = 1; i <= 5; ++i) { message.data = i; short_writer->publish(message); long_writer->publish(message); }
    c.barrier->pass(++c.phase);
    std::this_thread::sleep_for(3s);
    auto dropped = std::make_shared<Ints>();
    auto kept = std::make_shared<Ints>();
    auto short_reader = c.node->create_subscription<Int32>(c.remote("span_short"), expiring, [dropped](Int32::ConstSharedPtr m) { dropped->add(m->data); });
    auto long_reader = c.node->create_subscription<Int32>(c.remote("span_long"), lasting, [kept](Int32::ConstSharedPtr m) { kept->add(m->data); });
    const bool control = wait_for([&] { return kept->size() == 5; }, 30s);
    std::this_thread::sleep_for(2s);
    report("qos_lifespan", control && dropped->size() == 0, "expired_delivered=" + num(dropped->size()) + ",control=" + num(kept->size()));
    c.step();
}

void keep_last_case(Context& c)
{
    const auto qos = rclcpp::QoS(3).reliable().transient_local();
    auto publisher = c.node->create_publisher<Int32>(c.local("last"), qos);
    Int32 message;
    for (int i = 1; i <= 10; ++i) { message.data = i; publisher->publish(message); }
    c.barrier->pass(++c.phase);
    auto got = std::make_shared<Ints>();
    auto subscription = c.node->create_subscription<Int32>(c.remote("last"), qos, [got](Int32::ConstSharedPtr m) { got->add(m->data); });
    wait_for([&] { return got->size() >= 3; }, 30s);
    std::this_thread::sleep_for(2s);
    std::lock_guard<std::mutex> lock(got->mutex);
    report("qos_keep_last_depth", got->seen == std::set<int>({8, 9, 10}), "count=" + num(got->count));
    got->seen.clear();
}

void incompatible_case(Context& c)
{
    auto offered = std::make_shared<std::atomic<int>>(0);
    auto requested = std::make_shared<std::atomic<int>>(0);
    auto got = std::make_shared<std::atomic<int>>(0);
    rclcpp::PublisherOptions publisher_options;
    publisher_options.event_callbacks.incompatible_qos_callback = [offered](rclcpp::QOSOfferedIncompatibleQoSInfo&) { ++*offered; };
    rclcpp::SubscriptionOptions subscription_options;
    subscription_options.event_callbacks.incompatible_qos_callback = [requested](rclcpp::QOSRequestedIncompatibleQoSInfo&) { ++*requested; };
    auto subscription = c.node->create_subscription<Int32>(c.remote("incompat"), rclcpp::QoS(5).reliable(),
        [got](Int32::ConstSharedPtr) { ++*got; }, subscription_options);
    auto publisher = c.node->create_publisher<Int32>(c.local("incompat"), rclcpp::QoS(5).best_effort(), publisher_options);
    const bool events = wait_for([&] { return offered->load() >= 1 && requested->load() >= 1; }, 30s);
    Int32 message;
    for (int i = 0; i < 5; ++i) { publisher->publish(message); std::this_thread::sleep_for(50ms); }
    std::this_thread::sleep_for(1s);
    report("qos_incompatible_event", events && got->load() == 0 && publisher->get_subscription_count() == 0,
        "offered=" + num(offered->load()) + ",requested=" + num(requested->load()) + ",delivered=" + num(got->load()));
}

// ---- data paths ----

void generic_case(Context& c)
{
    const auto qos = rclcpp::QoS(10).reliable();
    const std::string type = "std_msgs/msg/String";
    rclcpp::Serialization<Text> codec;
    auto typed_got = std::make_shared<Ints>();
    auto generic_got = std::make_shared<Ints>();
    // Generic writer to typed reader on one topic, typed writer to generic reader on the other.
    auto typed_reader = c.node->create_subscription<Text>(c.remote("g_to_t"), qos, [typed_got](Text::ConstSharedPtr m) { typed_got->add(std::stoi(m->data)); });
    auto generic_reader = c.node->create_generic_subscription(c.remote("t_to_g"), type, qos, [generic_got](std::shared_ptr<rclcpp::SerializedMessage> m) {
        Text text; rclcpp::Serialization<Text>().deserialize_message(m.get(), &text); generic_got->add(std::stoi(text.data)); });
    auto generic_writer = c.node->create_generic_publisher(c.local("g_to_t"), type, qos);
    auto typed_writer = c.node->create_publisher<Text>(c.local("t_to_g"), qos);
    const bool ok = wait_for([&] { return generic_writer->get_subscription_count() == 1 && typed_writer->get_subscription_count() == 1 &&
        typed_reader->get_publisher_count() == 1 && generic_reader->get_publisher_count() == 1; }, 60s);
    std::this_thread::sleep_for(300ms);
    for (int i = 1; i <= 20; ++i) {
        Text text; text.data = std::to_string(i);
        rclcpp::SerializedMessage raw; codec.serialize_message(&text, &raw);
        generic_writer->publish(raw); typed_writer->publish(text);
        std::this_thread::sleep_for(10ms);
    }
    const bool all = wait_for([&] { return typed_got->size() == 20 && generic_got->size() == 20; }, 30s);
    report("generic_and_serialized", ok && all, "typed=" + num(typed_got->size()) + ",generic=" + num(generic_got->size()));
}

void loaned_case(Context& c)
{
    const auto qos = rclcpp::QoS(10).reliable();
    auto got = std::make_shared<Ints>();
    auto subscription = c.node->create_subscription<Int32>(c.remote("loan"), qos, [got](Int32::ConstSharedPtr m) { got->add(m->data); });
    auto publisher = c.node->create_publisher<Int32>(c.local("loan"), qos);
    const bool ok = matched(publisher, subscription);
    const bool can_loan = publisher->can_loan_messages();
    for (int i = 1; i <= 20; ++i) {
        auto loan = publisher->borrow_loaned_message();
        loan.get().data = i;
        publisher->publish(std::move(loan));
        std::this_thread::sleep_for(10ms);
    }
    const bool all = wait_for([&] { return got->size() == 20; }, 30s);
    report("loaned_message_publish", ok && all && can_loan, "can_loan=" + num(can_loan) + ",takes=" + num(got->size()));
}

void large_case(Context& c, const std::string& label, std::size_t bytes, int samples)
{
    const auto qos = rclcpp::QoS(samples).reliable();
    auto good = std::make_shared<std::atomic<int>>(0);
    auto bad = std::make_shared<std::atomic<int>>(0);
    const int peer = c.peer;
    auto subscription = c.node->create_subscription<Image>(c.remote("img_" + label), qos, [good, bad, peer, bytes](Image::ConstSharedPtr m) {
        bool same = m->data.size() == bytes && m->width == static_cast<std::uint32_t>(peer + 1);
        for (std::size_t i = 0; same && i < bytes; i += 997) same = m->data[i] == static_cast<std::uint8_t>(i * 7 + static_cast<std::size_t>(peer));
        ++*(same ? good : bad); });
    auto publisher = c.node->create_publisher<Image>(c.local("img_" + label), qos);
    const bool ok = matched(publisher, subscription);
    c.barrier->pass(++c.phase);
    Image message; message.data.resize(bytes); message.width = static_cast<std::uint32_t>(c.side + 1); message.encoding = "rgb8";
    for (std::size_t i = 0; i < bytes; ++i) message.data[i] = static_cast<std::uint8_t>(i * 7 + static_cast<std::size_t>(c.side));
    const auto start = Clock::now();
    for (int i = 0; i < samples; ++i) publisher->publish(message);
    const bool all = wait_for([&] { return good->load() + bad->load() >= samples; }, 180s);
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
    char rate[64]; std::snprintf(rate, sizeof(rate), "%.2f", static_cast<double>(good->load()) * static_cast<double>(bytes) / 1048576.0 / seconds);
    report("large_" + label, ok && all && good->load() == samples && bad->load() == 0,
        "good=" + num(good->load()) + ",bad=" + num(bad->load()) + ",of=" + num(samples) + ",MiB_per_s=" + rate);
}

void burst_case(Context& c, int samples)
{
    const auto qos = rclcpp::QoS(rclcpp::KeepAll()).reliable();
    auto got = std::make_shared<Ints>();
    auto subscription = c.node->create_subscription<Int32>(c.remote("burst"), qos, [got](Int32::ConstSharedPtr m) { got->add(m->data); });
    auto publisher = c.node->create_publisher<Int32>(c.local("burst"), qos);
    const bool ok = matched(publisher, subscription);
    c.barrier->pass(++c.phase);
    const auto start = Clock::now();
    Int32 message;
    for (int i = 1; i <= samples; ++i) { message.data = i; publisher->publish(message); }
    const bool all = wait_for([&] { return got->size() == static_cast<std::size_t>(samples); }, 120s);
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
    report("burst_reliable_keep_all", ok && all && got->count == samples, "takes=" + num(got->size()) + ",of=" + num(samples) + ",seconds=" + std::to_string(seconds).substr(0, 5));
}

void sensor_rate_case(Context& c)
{
    const auto qos = rclcpp::SensorDataQoS();
    auto got = std::make_shared<Ints>();
    auto subscription = c.node->create_subscription<Int32>(c.remote("rate"), qos, [got](Int32::ConstSharedPtr m) { got->add(m->data); });
    auto publisher = c.node->create_publisher<Int32>(c.local("rate"), qos);
    const bool ok = matched(publisher, subscription);
    c.barrier->pass(++c.phase);
    Int32 message;
    constexpr int kSent = 500;
    for (int i = 1; i <= kSent; ++i) { message.data = i; publisher->publish(message); std::this_thread::sleep_for(10ms); }
    std::this_thread::sleep_for(2s);
    report("sensor_qos_100hz", ok && got->size() >= kSent * 95 / 100, "takes=" + num(got->size()) + ",of=" + num(kSent));
}

void many_topics_case(Context& c, int topics)
{
    const auto qos = rclcpp::QoS(5).reliable();
    auto got = std::make_shared<Ints>();
    std::vector<rclcpp::Subscription<Int32>::SharedPtr> readers;
    std::vector<rclcpp::Publisher<Int32>::SharedPtr> writers;
    const auto start = Clock::now();
    for (int i = 0; i < topics; ++i) {
        readers.push_back(c.node->create_subscription<Int32>(c.remote("many" + num(i)), qos, [got, i](Int32::ConstSharedPtr) { got->add(i); }));
        writers.push_back(c.node->create_publisher<Int32>(c.local("many" + num(i)), qos));
    }
    const bool ok = wait_for([&] {
        return std::all_of(writers.begin(), writers.end(), [](const auto& w) { return w->get_subscription_count() == 1; }) &&
            std::all_of(readers.begin(), readers.end(), [](const auto& r) { return r->get_publisher_count() == 1; }); }, 120s);
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
    std::this_thread::sleep_for(300ms);
    c.barrier->pass(++c.phase);
    for (auto& w : writers) { Int32 message; w->publish(message); }
    const bool all = wait_for([&] { return got->size() == static_cast<std::size_t>(topics); }, 60s);
    report("many_topics", ok && all, "topics=" + num(topics) + ",delivered=" + num(got->size()) + ",match_seconds=" + std::to_string(seconds).substr(0, 5));
}

// ---- graph ----

void graph_detail_case(Context& c)
{
    const auto qos = rclcpp::QoS(7).reliable().transient_local();
    auto publisher = c.node->create_publisher<Text>(c.local("graph"), qos);
    auto server = c.node->create_service<AddTwoInts>(c.local("graph_srv"), [](const AddTwoInts::Request::SharedPtr, AddTwoInts::Response::SharedPtr) {});
    const bool info_ok = wait_for([&] {
        const auto list = c.node->get_publishers_info_by_topic(c.remote("graph"));
        return list.size() == 1 && list[0].node_name() == c.peer_node() && list[0].topic_type() == "std_msgs/msg/String" &&
            list[0].qos_profile().reliability() == rclcpp::ReliabilityPolicy::Reliable &&
            list[0].qos_profile().durability() == rclcpp::DurabilityPolicy::TransientLocal; }, 30s);
    const bool count_ok = wait_for([&] { return c.node->count_publishers(c.remote("graph")) == 1; }, 30s);
    const bool service_ok = wait_for([&] { return c.node->get_service_names_and_types().count(c.remote("graph_srv")) == 1; }, 30s);
    const bool by_node = wait_for([&] {
        const auto topics = c.node->get_node_graph_interface()->get_publisher_names_and_types_by_node(c.peer_node(), "/");
        return topics.count(c.remote("graph")) == 1; }, 30s);
    report("graph_endpoint_details", info_ok && count_ok && service_ok && by_node,
        "info=" + num(info_ok) + ",count=" + num(count_ok) + ",service=" + num(service_ok) + ",by_node=" + num(by_node));
    c.step();
}

void graph_leave_case(Context& c)
{
    const std::string temp = "s2f_temp_" + c.other();
    const auto has_peer_temp = [&] {
        for (const auto& name : c.node->get_node_names()) if (name == "/" + temp) return true;
        return false; };
    auto own = std::make_shared<rclcpp::Node>("s2f_temp_" + c.me(), rclcpp::NodeOptions().context(c.node->get_node_base_interface()->get_context()));
    const bool appeared = wait_for(has_peer_temp, 30s);
    c.step();
    own.reset();
    const bool left = wait_for([&] { return !has_peer_temp(); }, 30s);
    report("graph_node_leaves", appeared && left, "appeared=" + num(appeared) + ",left=" + num(left));
    c.step();
}

// Fast DDS announces a leaving Participant, so on the hub peers see it go long before the 20 s lease would end it. On the
// direct path a second Participant in a process is often not announced and goes by its lease, a limit recorded in the
// Stage 2 document, so the runner passes that path a limit just above the lease. The time is reported either way.
double g_leave_limit_seconds = 8.0;

void participant_leaves_case(Context& c)
{
    const std::string temp = "s2f_ctx_temp_" + c.other();
    const auto has_peer_temp = [&] {
        for (const auto& name : c.node->get_node_names()) if (name == "/" + temp) return true;
        return false; };
    auto context = std::make_shared<rclcpp::Context>();
    rclcpp::InitOptions init;
    init.auto_initialize_logging(false);
    context->init(0, nullptr, init);
    auto node = std::make_shared<rclcpp::Node>("s2f_ctx_temp_" + c.me(), rclcpp::NodeOptions().context(context));
    const bool appeared = wait_for(has_peer_temp, 30s);
    c.step();
    node.reset();
    context->shutdown("case finished");
    const auto start = Clock::now();
    const bool left = wait_for([&] { return !has_peer_temp(); }, 30s);
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
    report("graph_participant_leaves", appeared && left && seconds < g_leave_limit_seconds, "appeared=" + num(appeared) + ",left=" + num(left) + ",seconds=" + std::to_string(seconds).substr(0, 5));
    c.step();
}

void type_description_case(Context& c)
{
    using Query = type_description_interfaces::srv::GetTypeDescription;
    auto publisher = c.node->create_publisher<Text>(c.local("typed"), 5);
    std::string hash;
    const bool seen = wait_for([&] {
        const auto list = c.node->get_publishers_info_by_topic(c.remote("typed"));
        if (list.size() != 1) return false;
        char* text = nullptr;
        const auto value = list[0].topic_type_hash();
        if (rosidl_stringify_type_hash(&value, rcutils_get_default_allocator(), &text) != RCUTILS_RET_OK) return false;
        hash = text; rcutils_get_default_allocator().deallocate(text, rcutils_get_default_allocator().state);
        return true; }, 30s);
    auto client = c.node->create_client<Query>("/" + c.peer_node() + "/get_type_description");
    const bool ready = client->wait_for_service(30s);
    bool found = false; std::string name;
    if (seen && ready) {
        auto request = std::make_shared<Query::Request>();
        request->type_name = "std_msgs/msg/String"; request->type_hash = hash; request->include_type_sources = true;
        auto reply = client->async_send_request(request);
        if (reply.wait_for(20s) == std::future_status::ready) {
            const auto response = reply.get();
            found = response->successful; name = response->type_description.type_description.type_name;
        }
    }
    report("type_description_service", found && name == "std_msgs/msg/String", "name=" + (name.empty() ? std::string("none") : name) + ",hash=" + hash.substr(0, 12));
    c.step();
}

// ---- services and actions ----

void service_concurrent_case(Context& c)
{
    // The default service history is 10, which a burst may overwrite by design; this case tests the path.
    const auto qos = rclcpp::ServicesQoS().keep_last(400);
    auto server = c.node->create_service<AddTwoInts>(c.local("cc"), [](const AddTwoInts::Request::SharedPtr q, AddTwoInts::Response::SharedPtr r) { r->sum = q->a + q->b; }, qos);
    auto client = c.node->create_client<AddTwoInts>(c.remote("cc"), qos);
    const bool ready = client->wait_for_service(60s);
    std::vector<rclcpp::Client<AddTwoInts>::FutureAndRequestId> calls;
    for (int i = 0; ready && i < 200; ++i) {
        auto q = std::make_shared<AddTwoInts::Request>(); q->a = i; q->b = 1000;
        calls.push_back(client->async_send_request(q));
    }
    int good = 0;
    const auto end = Clock::now() + 60s; // one limit for the whole burst
    for (std::size_t i = 0; i < calls.size(); ++i)
        if (calls[i].future.wait_until(end) == std::future_status::ready && calls[i].future.get()->sum == static_cast<std::int64_t>(i) + 1000) ++good;
    report("service_200_concurrent", good == 200, "replies=" + num(good));
    c.step();
}

void service_introspection_case(Context& c)
{
    using Event = AddTwoInts::Event;
    auto server = c.node->create_service<AddTwoInts>(c.local("intro"), [](const AddTwoInts::Request::SharedPtr q, AddTwoInts::Response::SharedPtr r) { r->sum = q->a + q->b; });
    server->configure_introspection(c.node->get_clock(), rclcpp::SystemDefaultsQoS(), RCL_SERVICE_INTROSPECTION_CONTENTS);
    auto events = std::make_shared<std::atomic<int>>(0);
    auto sub = c.node->create_subscription<Event>(c.remote("intro") + "/_service_event", rclcpp::QoS(50).reliable(), [events](Event::ConstSharedPtr) { ++*events; });
    auto client = c.node->create_client<AddTwoInts>(c.remote("intro"));
    const bool ready = client->wait_for_service(60s) && wait_for([&] { return sub->get_publisher_count() == 1; }, 30s);
    c.step();
    int good = 0;
    for (int i = 0; ready && i < 5; ++i) {
        auto q = std::make_shared<AddTwoInts::Request>(); q->a = i; q->b = 1;
        auto reply = client->async_send_request(q);
        if (reply.wait_for(20s) == std::future_status::ready) ++good;
    }
    // Request received and response sent, on the server, for each of the peer's 5 calls.
    const bool seen = wait_for([&] { return events->load() >= 10; }, 30s);
    report("service_introspection", good == 5 && seen, "replies=" + num(good) + ",events_seen=" + num(events->load()));
    c.step();
}

void large_parameter_case(Context& c)
{
    auto client = std::make_shared<rclcpp::AsyncParametersClient>(c.node, c.peer_node());
    const bool ready = client->wait_for_service(60s);
    const std::string name = "s2f_blob_" + c.me();
    const std::string value = pattern(200000, c.side);
    bool stored = false, same = false;
    if (ready) {
        auto set = client->set_parameters({rclcpp::Parameter(name, value)});
        stored = set.wait_for(60s) == std::future_status::ready && set.get().at(0).successful;
        auto get = client->get_parameters({name});
        same = get.wait_for(60s) == std::future_status::ready && get.get().at(0).as_string() == value;
    }
    auto list = client->list_parameters({}, 1);
    bool listed = false;
    if (list.wait_for(20s) == std::future_status::ready) {
        const auto names = list.get().names;
        listed = std::find(names.begin(), names.end(), name) != names.end();
    }
    report("parameter_large_value", stored && same && listed, "stored=" + num(stored) + ",same=" + num(same) + ",listed=" + num(listed));
    c.step();
}

void action_many_case(Context& c)
{
    using Handle = rclcpp_action::ServerGoalHandle<Fibonacci>;
    auto server = rclcpp_action::create_server<Fibonacci>(c.node, c.local("many_fib"),
        [](const rclcpp_action::GoalUUID&, std::shared_ptr<const Fibonacci::Goal> goal) {
            return goal->order < 0 ? rclcpp_action::GoalResponse::REJECT : rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE; },
        [](std::shared_ptr<Handle>) { return rclcpp_action::CancelResponse::ACCEPT; },
        [](std::shared_ptr<Handle> handle) {
            std::thread([handle] {
                auto result = std::make_shared<Fibonacci::Result>();
                result->sequence = {handle->get_goal()->order};
                std::this_thread::sleep_for(200ms);
                handle->succeed(result);
            }).detach(); });
    auto client = rclcpp_action::create_client<Fibonacci>(c.node, c.remote("many_fib"));
    const bool ready = client->wait_for_action_server(60s);
    int succeeded = 0;
    bool rejected = false;
    if (ready) {
        std::vector<std::shared_future<rclcpp_action::ClientGoalHandle<Fibonacci>::SharedPtr>> goals;
        for (int i = 1; i <= 8; ++i) { Fibonacci::Goal goal; goal.order = i; goals.push_back(client->async_send_goal(goal)); }
        for (int i = 0; i < 8; ++i) {
            if (goals[static_cast<std::size_t>(i)].wait_for(20s) != std::future_status::ready) continue;
            auto handle = goals[static_cast<std::size_t>(i)].get();
            if (!handle) continue;
            auto result = client->async_get_result(handle);
            if (result.wait_for(30s) == std::future_status::ready && result.get().result->sequence.at(0) == i + 1) ++succeeded;
        }
        Fibonacci::Goal bad; bad.order = -1;
        auto refused = client->async_send_goal(bad);
        rejected = refused.wait_for(20s) == std::future_status::ready && !refused.get();
    }
    report("action_concurrent_and_rejected", succeeded == 8 && rejected, "succeeded=" + num(succeeded) + ",rejected=" + num(rejected));
    c.step();
}

// ---- execution models ----

template <typename Executor>
void executor_case(Context& c, const std::string& label)
{
    rclcpp::NodeOptions options;
    options.context(c.node->get_node_base_interface()->get_context());
    auto node = std::make_shared<rclcpp::Node>("s2f_exec_" + label + "_" + c.me(), options);
    auto got = std::make_shared<Ints>();
    const auto qos = rclcpp::QoS(30).reliable();
    auto subscription = node->create_subscription<Int32>(c.remote("exec_" + label), qos, [got](Int32::ConstSharedPtr m) { got->add(m->data); });
    auto publisher = node->create_publisher<Int32>(c.local("exec_" + label), qos);
    auto ticks = std::make_shared<std::atomic<int>>(0);
    auto timer = node->create_wall_timer(20ms, [publisher, ticks] {
        if (ticks->load() >= 20 || publisher->get_subscription_count() == 0) return;
        Int32 message; message.data = ++*ticks; publisher->publish(message); });
    {
        Executor executor;
        executor.add_node(node);
        std::thread spinner([&] { executor.spin(); });
        // takes counts the peer's samples and ticks this side's, so each is awaited on its own.
        const bool all = wait_for([&] { return got->size() == 20 && ticks->load() == 20; }, 60s);
        report("executor_" + label, all, "takes=" + num(got->size()) + ",ticks=" + num(ticks->load()));
        c.step();
        executor.cancel();
        spinner.join();
    }
}

void wait_set_case(Context& c)
{
    const auto qos = rclcpp::QoS(30).reliable();
    auto group = c.node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive, false);
    rclcpp::SubscriptionOptions options; options.callback_group = group;
    auto subscription = c.node->create_subscription<Int32>(c.remote("ws"), qos, [](Int32::ConstSharedPtr) {}, options);
    auto publisher = c.node->create_publisher<Int32>(c.local("ws"), qos);
    const bool ok = matched(publisher, subscription);
    c.step();
    for (int i = 1; i <= 10; ++i) { Int32 m; m.data = i; publisher->publish(m); }
    rclcpp::WaitSet wait_set({{subscription}});
    std::set<int> seen;
    const auto end = Clock::now() + 30s;
    while (seen.size() < 10 && Clock::now() < end) {
        auto result = wait_set.wait(1s);
        if (result.kind() != rclcpp::WaitResultKind::Ready) continue;
        Int32 m; rclcpp::MessageInfo info;
        while (subscription->take(m, info)) seen.insert(m.data);
    }
    report("wait_set_take", ok && seen.size() == 10, "takes=" + num(seen.size()));
    c.step();
}

void intra_process_case(Context& c)
{
    rclcpp::NodeOptions options;
    options.context(c.node->get_node_base_interface()->get_context()).use_intra_process_comms(true);
    auto node = std::make_shared<rclcpp::Node>("s2f_intra_" + c.me(), options);
    const auto qos = rclcpp::QoS(30).reliable();
    auto local_got = std::make_shared<Ints>();
    auto remote_got = std::make_shared<Ints>();
    auto local_reader = node->create_subscription<Int32>(c.local("ipc"), qos, [local_got](Int32::ConstSharedPtr m) { local_got->add(m->data); });
    auto remote_reader = node->create_subscription<Int32>(c.remote("ipc"), qos, [remote_got](Int32::ConstSharedPtr m) { remote_got->add(m->data); });
    auto publisher = node->create_publisher<Int32>(c.local("ipc"), qos);
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    std::thread spinner([&] { executor.spin(); });
    const bool ok = wait_for([&] { return publisher->get_subscription_count() == 2 && remote_reader->get_publisher_count() == 1; }, 60s);
    std::this_thread::sleep_for(300ms);
    c.step();
    for (int i = 1; i <= 20; ++i) { auto m = std::make_unique<Int32>(); m->data = i; publisher->publish(std::move(m)); std::this_thread::sleep_for(10ms); }
    const bool all = wait_for([&] { return local_got->size() == 20 && remote_got->size() == 20; }, 30s);
    report("intra_process_plus_remote", ok && all, "local=" + num(local_got->size()) + ",remote=" + num(remote_got->size()));
    c.step();
    executor.cancel();
    spinner.join();
}

void second_context_case(Context& c, std::size_t domain)
{
    auto context = std::make_shared<rclcpp::Context>();
    rclcpp::InitOptions init;
    init.auto_initialize_logging(false);
    init.set_domain_id(domain);
    context->init(0, nullptr, init);
    rclcpp::NodeOptions options;
    options.context(context);
    auto node = std::make_shared<rclcpp::Node>("s2f_ctx_" + c.me(), options);
    const auto qos = rclcpp::QoS(30).reliable();
    auto got = std::make_shared<Ints>();
    auto subscription = node->create_subscription<Int32>(c.remote("ctx"), qos, [got](Int32::ConstSharedPtr m) { got->add(m->data); });
    auto publisher = node->create_publisher<Int32>(c.local("ctx"), qos);
    rclcpp::ExecutorOptions executor_options; executor_options.context = context;
    rclcpp::executors::SingleThreadedExecutor executor(executor_options);
    executor.add_node(node);
    std::thread spinner([&] { executor.spin(); });
    const bool ok = matched(publisher, subscription);
    c.step();
    for (int i = 1; i <= 20; ++i) { Int32 m; m.data = i; publisher->publish(m); std::this_thread::sleep_for(10ms); }
    const bool all = wait_for([&] { return got->size() == 20; }, 30s);
    report(domain == 151 ? "second_context_same_domain" : "second_context_other_domain", ok && all, "takes=" + num(got->size()) + ",matched=" + num(ok));
    c.step();
    executor.cancel();
    spinner.join();
    subscription.reset(); publisher.reset(); node.reset();
    context->shutdown("case finished");
}

void naming_case(Context& c)
{
    rclcpp::NodeOptions options;
    options.context(c.node->get_node_base_interface()->get_context());
    options.arguments({"--ros-args", "-r", "chatter:=/s2f/remapped_" + c.me()});
    auto node = std::make_shared<rclcpp::Node>("named", "/s2ns_" + c.me() + "/inner", options);
    auto got = std::make_shared<Ints>();
    const auto qos = rclcpp::QoS(30).reliable();
    auto publisher = node->create_publisher<Int32>("chatter", qos);
    auto relative = node->create_publisher<Int32>("relative", qos);
    auto reader = c.node->create_subscription<Int32>(c.remote("remapped"), qos, [got](Int32::ConstSharedPtr m) { got->add(m->data); });
    auto namespaced = c.node->create_subscription<Int32>("/s2ns_" + c.other() + "/inner/relative", qos, [got](Int32::ConstSharedPtr m) { got->add(m->data + 100); });
    const bool ok = wait_for([&] { return reader->get_publisher_count() == 1 && namespaced->get_publisher_count() == 1 &&
        publisher->get_subscription_count() == 1 && relative->get_subscription_count() == 1; }, 60s);
    std::this_thread::sleep_for(300ms);
    c.step();
    for (int i = 1; i <= 10; ++i) { Int32 m; m.data = i; publisher->publish(m); relative->publish(m); }
    const bool all = wait_for([&] { return got->size() == 20; }, 30s);
    report("namespace_and_remap", ok && all, "takes=" + num(got->size()));
    c.step();
}

// ---- middleware services ----

void rosout_case(Context& c)
{
    const std::string token = "s2f_log_from_" + c.other();
    auto got = std::make_shared<std::atomic<int>>(0);
    auto subscription = c.node->create_subscription<rcl_interfaces::msg::Log>("/rosout", rclcpp::QoS(100).reliable().transient_local(),
        [got, token](rcl_interfaces::msg::Log::ConstSharedPtr m) { if (m->msg.find(token) != std::string::npos) ++*got; });
    const bool matched_peer = wait_for([&] { return subscription->get_publisher_count() >= 2; }, 30s);
    c.step();
    for (int i = 0; i < 5; ++i) RCLCPP_INFO(c.node->get_logger(), "s2f_log_from_%s %d", c.me().c_str(), i);
    const bool all = wait_for([&] { return got->load() >= 5; }, 20s);
    report("rosout_remote", matched_peer && all, "lines=" + num(got->load()));
    c.step();
}

void parameter_event_case(Context& c)
{
    auto got = std::make_shared<std::atomic<int>>(0);
    const std::string peer = "/" + c.peer_node();
    auto subscription = c.node->create_subscription<rcl_interfaces::msg::ParameterEvent>("/parameter_events", rclcpp::QoS(100).reliable(),
        [got, peer](rcl_interfaces::msg::ParameterEvent::ConstSharedPtr m) { if (m->node == peer) ++*got; });
    const bool ok = wait_for([&] { return subscription->get_publisher_count() >= 2; }, 30s);
    c.step();
    c.node->declare_parameter<int>("s2f_evented", 1);
    c.node->set_parameter(rclcpp::Parameter("s2f_evented", 2));
    const bool seen = wait_for([&] { return got->load() >= 2; }, 20s);
    report("parameter_events_remote", ok && seen, "events=" + num(got->load()));
    c.step();
}

void sim_time_case(Context& c)
{
    // Board 0 owns /clock; board 1 runs on it.
    using TimeMessage = rosgraph_msgs::msg::Clock;
    rclcpp::NodeOptions options;
    options.context(c.node->get_node_base_interface()->get_context());
    options.parameter_overrides({rclcpp::Parameter("use_sim_time", c.side == 1)});
    auto node = std::make_shared<rclcpp::Node>("s2f_sim_" + c.me(), options);
    if (c.side == 0) {
        auto publisher = c.node->create_publisher<TimeMessage>("/clock", rclcpp::ClockQoS());
        c.step();
        bool ok = wait_for([&] { return publisher->get_subscription_count() >= 1; }, 30s);
        for (int i = 1; i <= 50; ++i) { TimeMessage m; m.clock.sec = 5000 + i; publisher->publish(m); std::this_thread::sleep_for(20ms); }
        report("sim_time_clock", ok, "published=50");
        c.step();
    } else {
        rclcpp::executors::SingleThreadedExecutor executor;
        executor.add_node(node);
        std::thread spinner([&] { executor.spin(); });
        c.step();
        const bool followed = wait_for([&] { return node->now().seconds() > 5040.0; }, 30s);
        report("sim_time_clock", followed && node->get_clock()->get_clock_type() == RCL_ROS_TIME, "now=" + std::to_string(node->now().seconds()));
        c.step();
        executor.cancel();
        spinner.join();
    }
}

void statistics_case(Context& c)
{
    using Metrics = statistics_msgs::msg::MetricsMessage;
    auto metrics = std::make_shared<std::atomic<int>>(0);
    auto stats = c.node->create_subscription<Metrics>("/s2f/stats_" + c.other(), rclcpp::QoS(50).reliable(), [metrics](Metrics::ConstSharedPtr) { ++*metrics; });
    rclcpp::SubscriptionOptions options;
    options.topic_stats_options.state = rclcpp::TopicStatisticsState::Enable;
    options.topic_stats_options.publish_topic = "/s2f/stats_" + c.me();
    options.topic_stats_options.publish_period = 1s;
    auto subscription = c.node->create_subscription<Text>(c.remote("statted"), 10, [](Text::ConstSharedPtr) {}, options);
    auto publisher = c.node->create_publisher<Text>(c.local("statted"), 10);
    const bool ok = matched(publisher, subscription);
    c.step();
    for (int i = 0; i < 40; ++i) { Text m; m.data = "x"; publisher->publish(m); std::this_thread::sleep_for(100ms); }
    const bool seen = wait_for([&] { return metrics->load() >= 2; }, 20s);
    report("topic_statistics_remote", ok && seen, "metrics=" + num(metrics->load()));
    c.step();
}

void logger_service_case(Context& c)
{
    using Set = rcl_interfaces::srv::SetLoggerLevels;
    using Get = rcl_interfaces::srv::GetLoggerLevels;
    rclcpp::NodeOptions options;
    options.context(c.node->get_node_base_interface()->get_context()).enable_logger_service(true);
    auto node = std::make_shared<rclcpp::Node>("s2f_logsvc_" + c.me(), options);
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    std::thread spinner([&] { executor.spin(); });
    const std::string peer = "/s2f_logsvc_" + c.other();
    auto set = c.node->create_client<Set>(peer + "/set_logger_levels");
    auto get = c.node->create_client<Get>(peer + "/get_logger_levels");
    bool ok = set->wait_for_service(60s) && get->wait_for_service(60s);
    int level = -1;
    if (ok) {
        auto q = std::make_shared<Set::Request>();
        rcl_interfaces::msg::LoggerLevel row; row.name = "s2f_logsvc_" + c.other(); row.level = 10; q->levels.push_back(row);
        auto reply = set->async_send_request(q);
        ok = reply.wait_for(20s) == std::future_status::ready && reply.get()->results.at(0).successful;
        auto ask = std::make_shared<Get::Request>(); ask->names.push_back(row.name);
        auto answer = get->async_send_request(ask);
        if (ok && answer.wait_for(20s) == std::future_status::ready) level = static_cast<int>(answer.get()->levels.at(0).level);
    }
    report("logger_service_remote", ok && level == 10, "level=" + num(level));
    c.step();
    executor.cancel();
    spinner.join();
}

void tf_case(Context& c)
{
    const auto now = c.node->get_clock()->now();
    geometry_msgs::msg::TransformStamped stamp;
    stamp.header.stamp = now; stamp.header.frame_id = "world_" + c.me(); stamp.child_frame_id = "static_" + c.me();
    stamp.transform.translation.x = 1.5 + c.side; stamp.transform.rotation.w = 1.0;
    tf2_ros::StaticTransformBroadcaster fixed(c.node);
    fixed.sendTransform(stamp);
    tf2_ros::TransformBroadcaster moving(c.node);
    tf2_ros::Buffer buffer(c.node->get_clock());
    tf2_ros::TransformListener listener(buffer, c.node, false);
    c.step();
    std::atomic<bool> running{true};
    std::thread sender([&] {
        while (running.load()) {
            geometry_msgs::msg::TransformStamped t;
            t.header.stamp = c.node->get_clock()->now(); t.header.frame_id = "world_" + c.me(); t.child_frame_id = "moving_" + c.me();
            t.transform.rotation.w = 1.0; t.transform.translation.y = 2.0 + c.side;
            moving.sendTransform(t);
            std::this_thread::sleep_for(50ms);
        } });
    const std::string world = "world_" + c.other();
    double fixed_x = 0, moving_y = 0;
    const bool got = wait_for([&] {
        if (!buffer.canTransform(world, "static_" + c.other(), tf2::TimePointZero) || !buffer.canTransform(world, "moving_" + c.other(), tf2::TimePointZero)) return false;
        fixed_x = buffer.lookupTransform(world, "static_" + c.other(), tf2::TimePointZero).transform.translation.x;
        moving_y = buffer.lookupTransform(world, "moving_" + c.other(), tf2::TimePointZero).transform.translation.y;
        return true; }, 30s);
    report("tf2_static_and_dynamic", got && fixed_x == 1.5 + c.peer && moving_y == 2.0 + c.peer, "x=" + std::to_string(fixed_x) + ",y=" + std::to_string(moving_y));
    c.step();
    running = false;
    sender.join();
}

void run_group(Context& c, const std::string& group)
{
    const auto next = [&] { return c.step(); };
    if (group == "qos") {
        deadline_case(c); next(); liveliness_case(c); next(); lifespan_case(c); keep_last_case(c); next(); incompatible_case(c); next();
    } else if (group == "data") {
        generic_case(c); next(); loaned_case(c); next(); burst_case(c, 2000); next(); sensor_rate_case(c); next();
    } else if (group == "large") {
        large_case(c, "1MiB", 1u << 20, 3); next(); large_case(c, "1080p", 1920u * 1080u * 3u, 3); next(); large_case(c, "16MiB", 16u << 20, 2); next();
    } else if (group == "scale") {
        many_topics_case(c, 40); next();
    } else if (group == "graph") {
        graph_detail_case(c); graph_leave_case(c); participant_leaves_case(c); type_description_case(c);
    } else if (group == "calls") {
        service_concurrent_case(c); service_introspection_case(c); large_parameter_case(c); action_many_case(c);
    } else if (group == "exec") {
        executor_case<rclcpp::executors::SingleThreadedExecutor>(c, "single");
        executor_case<rclcpp::executors::StaticSingleThreadedExecutor>(c, "static");
        executor_case<rclcpp::executors::MultiThreadedExecutor>(c, "multi");
        executor_case<rclcpp::experimental::executors::EventsExecutor>(c, "events");
        wait_set_case(c); intra_process_case(c); naming_case(c);
    } else if (group == "context") {
        second_context_case(c, 151);
    } else if (group == "domain2") {
        second_context_case(c, 152);
    } else if (group == "services") {
        rosout_case(c); parameter_event_case(c); sim_time_case(c); statistics_case(c); logger_service_case(c); tf_case(c);
    } else {
        report("unknown_group", false, group);
    }
}
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: stage2_features <0|1> <group> [leave limit seconds]\n"); return 2; }
    if (argc > 3) g_leave_limit_seconds = std::stod(argv[3]);
    Context c;
    c.side = std::stoi(argv[1]); c.peer = c.side ? 0 : 1;
    rclcpp::init(1, argv);
    c.node = std::make_shared<rclcpp::Node>("s2f_node_" + c.me(), rclcpp::NodeOptions().allow_undeclared_parameters(true));
    Barrier barrier(c.node, c.me(), c.other());
    c.barrier = &barrier;
    rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 4);
    executor.add_node(c.node);
    std::thread spinner([&] { executor.spin(); });
    const bool started = c.step();
    if (!started) report("barrier_start", false, "peer_not_reached");
    else run_group(c, argv[2]);
    c.step(); // the peer's last replies still need this process
    std::this_thread::sleep_for(1s);
    executor.cancel();
    spinner.join();
    executor.remove_node(c.node);
    c.node.reset();
    const bool shutdown = rclcpp::shutdown();
    std::printf("S2_SUMMARY passed=%d failed=%d shutdown=%d\n", passed, failed, shutdown ? 1 : 0);
    std::fflush(stdout);
    return failed == 0 && passed > 0 && shutdown ? 0 : 1;
}
