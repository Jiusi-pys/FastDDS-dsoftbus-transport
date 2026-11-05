#pragma once
// Helpers shared by the Stage 2 workloads. Ordinary rclcpp only: no Fast DDS or Transport includes.
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/int32.hpp>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>

namespace stage2 {
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using Int32 = std_msgs::msg::Int32;

inline constexpr auto kCaseTimeout = 90s;
inline int passed = 0, failed = 0;

inline void report(const std::string& name, bool ok, const std::string& detail)
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

inline std::string pattern(std::size_t size, int side)
{
    std::string value(size, 'x');
    for (std::size_t i = 0; i < size; ++i) value[i] = static_cast<char>('a' + (i * 7 + static_cast<std::size_t>(side)) % 26);
    return value;
}

// Each side publishes the phase it reached on a latched topic and waits for the peer to reach it.
class Barrier {
public:
    Barrier(const rclcpp::Node::SharedPtr& node, const std::string& me, const std::string& other)
    {
        const auto qos = rclcpp::QoS(1).reliable().transient_local();
        publisher_ = node->create_publisher<Int32>("/s2/barrier_" + me, qos);
        subscription_ = node->create_subscription<Int32>("/s2/barrier_" + other, qos,
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
}
