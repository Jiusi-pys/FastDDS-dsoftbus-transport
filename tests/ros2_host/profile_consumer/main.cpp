// New profile-qualification scenario, NOT the historical A/04 workload.
// Ordinary ROS entities exercise lazy RMW/type-support loading and discovery.
// No application sample or service request is sent; control files only release
// the observation/teardown barrier after the tester captures actual modules.
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <example_interfaces/srv/add_two_ints.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <process.h>
#include "../../../examples/ros2_transport/FailureProtocol.hpp"

using namespace std::chrono_literals;
using Service = example_interfaces::srv::AddTwoInts;

static void RequireEnvironment(const char* name, const std::string& value)
{
    const char* actual = std::getenv(name);
    if (!actual || actual != value) throw std::runtime_error("qualification environment mismatch");
}

static void WaitRelease(const std::filesystem::path& path, const std::string& expected,
        const std::shared_ptr<rclcpp::Node>& node)
{
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (std::filesystem::exists(path))
        {
            if (std::filesystem::file_size(path) > 256) throw std::runtime_error("oversized release");
            std::ifstream input(path, std::ios::binary);
            std::string line, extra;
            if (!std::getline(input, line)) throw std::runtime_error("empty release");
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line != expected || std::getline(input, extra)) throw std::runtime_error("invalid release identity");
            return;
        }
        if (node) rclcpp::spin_some(node);
        std::this_thread::sleep_for(5ms);
    }
    throw std::runtime_error("module observation release timed out");
}

int main(int argc, char** argv)
{
    if (argc != 4) return 2;
    const std::string side = argv[1], mode = argv[2];
    const std::filesystem::path release = std::filesystem::u8path(argv[3]);
    if ((side != "A" && side != "B") || (mode != "success" && mode != "fail-create") ||
            !release.is_absolute() || std::filesystem::exists(release)) return 2;
    const std::string run = "0123456789abcdef", peer = side == "A" ? "B" : "A";
    const auto pid = _getpid();
    const auto record = [&](const char* event) {
        std::cout << "PROFILE_QUALIFICATION run=" << run << " side=" << side << " pid=" << pid
                  << " mode=" << mode << " event=" << event << " business_sent=0" << std::endl;
    };
    bool initialized = false;
    const char* stage = "ENVIRONMENT";
    try
    {
        RequireEnvironment("RMW_IMPLEMENTATION", "rmw_fastrtps_cpp");
        RequireEnvironment("ROS_DOMAIN_ID", "201");
        RequireEnvironment("ROS_SECURITY_ENABLE", "true");
        RequireEnvironment("ROS_SECURITY_STRATEGY", "Enforce");
        RequireEnvironment("ROS_SECURITY_ENCLAVE_OVERRIDE", "/" + side);
        const char* keystore = std::getenv("ROS_SECURITY_KEYSTORE");
        if (!keystore || !std::filesystem::path(keystore).is_absolute() ||
                !std::filesystem::is_directory(keystore)) throw std::runtime_error("keystore missing");
        stage = "RCLCPP_INIT";
        rclcpp::init(1, argv);
        initialized = true;
        stage = "NODE_CREATE";
        std::shared_ptr<rclcpp::Node> node;
        try
        {
            node = std::make_shared<rclcpp::Node>("transport_" + side, "/fdds_" + run,
                    rclcpp::NodeOptions().use_intra_process_comms(false));
        }
        catch (const std::exception&)
        {
            if (mode != "fail-create") throw;
            // Tester must additionally prove the predeclared occupied-port
            // failure, Runtime close and original factory's retained-module
            // marker. Merely catching an exception does not qualify that path.
            record("EXPECTED_NODE_REJECTION_MODULES_READY");
            FastddsDsoftbusFailureSnapshot(1);
            WaitRelease(release, "PROFILE_RELEASE run=" + run + " side=" + side +
                    " pid=" + std::to_string(pid) + " mode=" + mode, {});
            if (!rclcpp::shutdown()) throw std::runtime_error("failed-create context shutdown rejected");
            initialized = false;
            record("EXPECTED_NODE_REJECTION_EXIT");
            return 0;
        }
        if (mode == "fail-create") throw std::runtime_error("expected participant rejection did not occur");
        stage = "ENTITY_CREATE";
        unsigned unexpected_business = 0;
        const auto topic = "/fdds_" + run + "/reliable/";
        auto publisher = node->create_publisher<std_msgs::msg::String>(topic + "from_" + side, rclcpp::QoS(10).reliable());
        auto subscription = node->create_subscription<std_msgs::msg::String>(topic + "from_" + peer,
                rclcpp::QoS(10).reliable(), [&](std_msgs::msg::String::ConstSharedPtr) { ++unexpected_business; });
        rclcpp::Service<Service>::SharedPtr service;
        rclcpp::Client<Service>::SharedPtr client;
        if (side == "B")
            service = node->create_service<Service>(topic + "add",
                    [&](std::shared_ptr<Service::Request>, std::shared_ptr<Service::Response>) { ++unexpected_business; });
        else client = node->create_client<Service>(topic + "add");
        stage = "DISCOVERY";
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        const auto ready = [&] {
            return publisher->get_subscription_count() == 1 && subscription->get_publisher_count() == 1 &&
                    (!client || client->service_is_ready());
        };
        while (!ready() && std::chrono::steady_clock::now() < deadline)
        {
            rclcpp::spin_some(node);
            std::this_thread::sleep_for(5ms);
        }
        if (!ready()) throw std::runtime_error("profile discovery timeout");
        record("ENTITIES_DISCOVERED_MODULES_READY");
        stage = "MODULE_OBSERVATION";
        WaitRelease(release, "PROFILE_RELEASE run=" + run + " side=" + side +
                " pid=" + std::to_string(pid) + " mode=" + mode, node);
        if (unexpected_business) throw std::runtime_error("unexpected application business traffic");
        stage = "ENTITY_DELETE";
        FastddsDsoftbusTeardownCheckpoint("BEFORE_PHASE_ENTITY_DELETE");
        client.reset(); service.reset(); subscription.reset(); publisher.reset();
        FastddsDsoftbusTeardownCheckpoint("BEFORE_NODE_DELETE");
        node.reset();
        stage = "CONTEXT_SHUTDOWN";
        if (!rclcpp::shutdown()) throw std::runtime_error("profile context shutdown rejected");
        initialized = false;
        if (FastddsDsoftbusTeardownResult() != 0) throw std::runtime_error("profile teardown evidence incomplete");
        record("CLOSED");
        return 0;
    }
    catch (const std::exception&)
    {
        ros_failure_protocol::Report(run, side, pid, stage, "PROFILE_QUALIFICATION_FAILED");
        if (initialized)
        {
            try { rclcpp::shutdown(); } catch (...) {}
        }
        return 4;
    }
}
