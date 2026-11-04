// Business traffic uses only ordinary ROS APIs. The installed factory hook
// selects the Host transport; control files carry barriers, never responses.
#include <rclcpp/rclcpp.hpp>
#include <example_interfaces/srv/add_two_ints.hpp>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <vector>
#include <process.h>
#include "../ros2_transport/FailureProtocol.hpp"
using namespace std::chrono_literals;
using Service = example_interfaces::srv::AddTwoInts;
using Client = rclcpp::Client<Service>;

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::string side = argv[1], run = argv[2], peer = side == "A" ? "B" : "A";
    if ((side != "A" && side != "B") || !std::regex_match(run, std::regex("[0-9a-f]{16}"))) return 2;
    const auto pid = _getpid();
    const auto record = [&](const std::string& text) {
        std::cout << "SERVICE_ACCEPT run=" << run << " side=" << side << " pid=" << pid << " " << text << std::endl;
    };
    int result = 4;
    bool initialized = false;
    const char* stage = "RCLCPP_INIT";
    try {
        rclcpp::init(1, argv);
        initialized = true;
        stage = "NODE_CREATE";
        auto node = std::make_shared<rclcpp::Node>("service_" + side, "/svc_" + run,
            rclcpp::NodeOptions().use_intra_process_comms(false));
        stage = "SERVICE_SCENARIO";
        const auto fail = [](const char* reason) { throw std::runtime_error(reason); };
        struct Pending { std::shared_ptr<rmw_request_id_t> header; std::shared_ptr<Service::Request> request; };
        std::vector<Pending> batch;
        Pending delayed;
        std::set<std::string> identities;
        unsigned received = 0, sent = 0;
        auto service = node->create_service<Service>("add_" + side,
            [&](std::shared_ptr<rmw_request_id_t> header, std::shared_ptr<Service::Request> request) {
                std::ostringstream writer;
                for (unsigned char byte : header->writer_guid) writer << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
                if (!identities.insert(writer.str() + ":" + std::to_string(header->sequence_number)).second) fail("duplicate_request");
                ++received;
                record("event=SERVER_REQUEST id=" + std::to_string(header->sequence_number) + " writer=" + writer.str() +
                    " a=" + std::to_string(request->a) + " b=" + std::to_string(request->b));
                batch.push_back({std::move(header), std::move(request)});
            });
        auto client = node->create_client<Service>("add_" + peer);
        const auto send_response = [&](const Pending& item) {
            Service::Response response;
            response.sum = item.request->a + item.request->b;
            service->send_response(*item.header, response);
            ++sent;
            record("event=SERVER_RESPONSE id=" + std::to_string(item.header->sequence_number) + " a=" +
                std::to_string(item.request->a) + " b=" + std::to_string(item.request->b) + " sum=" + std::to_string(response.sum));
        };
        bool concurrent_replied = false;
        const auto progress = [&] {
            rclcpp::spin_some(node);
            if (!concurrent_replied && batch.size() == 4) {
                for (auto it = batch.rbegin(); it != batch.rend(); ++it) send_response(*it);
                batch.clear(); concurrent_replied = true;
            }
            if (concurrent_replied) {
                for (const auto& item : batch) {
                    if (item.request->a == -100) {
                        if (delayed.request) fail("duplicate_delayed_request");
                        delayed = item; // Deliberately deferred by the application until recovery request.
                    } else {
                        if (item.request->a == -101) {
                            if (!delayed.request) fail("recovery_before_delayed_request");
                            send_response(delayed); delayed = {};
                        } else if (item.request->a != 99) fail("unexpected_request");
                        send_response(item);
                    }
                }
                batch.clear();
            }
            std::this_thread::sleep_for(1ms);
        };
        const auto until = [&](const std::function<bool()>& ready, std::chrono::milliseconds timeout) {
            const auto end = std::chrono::steady_clock::now() + timeout;
            while (!ready() && std::chrono::steady_clock::now() < end) progress();
            if (!ready()) fail("bounded_wait_expired");
        };
        until([&] { return client->service_is_ready(); }, 30s);
        auto absent = node->create_client<Service>("absent");
        if (absent->wait_for_service(100ms)) fail("unexpected_absent_service");
        absent.reset(); record("event=ABSENT ready=0");
        const std::int64_t seed = side == "A" ? 1000 : 2000;
        const auto request = [&](std::int64_t value, unsigned generation) {
            auto q = std::make_shared<Service::Request>(); q->a = value; q->b = seed;
            auto future = client->async_send_request(q);
            record("event=CLIENT_SENT generation=" + std::to_string(generation) + " id=" + std::to_string(future.request_id) +
                " a=" + std::to_string(value) + " b=" + std::to_string(seed));
            return future;
        };
        const auto receive = [&](Client::FutureAndRequestId& future, std::int64_t value, unsigned generation) {
            until([&] { return future.wait_for(0s) == std::future_status::ready; }, 10s);
            auto response = future.get();
            if (response->sum != value + seed) fail("response_mismatch");
            record("event=CLIENT_RESPONSE generation=" + std::to_string(generation) + " id=" + std::to_string(future.request_id) +
                " a=" + std::to_string(value) + " sum=" + std::to_string(response->sum));
        };
        std::vector<Client::FutureAndRequestId> futures;
        for (int i = 1; i <= 4; ++i) futures.push_back(request(i, 1));
        record("event=CONCURRENT pending=4");
        for (int i = 1; i <= 4; ++i) receive(futures[i - 1], i, 1);
        auto timeout = request(-100, 1);
        const auto timeout_end = std::chrono::steady_clock::now() + 250ms;
        while (std::chrono::steady_clock::now() < timeout_end) progress();
        if (timeout.wait_for(0s) == std::future_status::ready || !client->remove_pending_request(timeout.request_id)) fail("timeout_not_removed");
        if (client->remove_pending_request(timeout.request_id)) fail("timeout_removed_twice");
        record("event=TIMEOUT id=" + std::to_string(timeout.request_id) + " removed=1 second_remove=0 wait_ms=250");
        auto recovery = request(-101, 1); receive(recovery, -101, 1);
        // A removed pending promise cannot later become a successful response.
        bool broken = false;
        try { (void)timeout.get(); } catch (const std::future_error& error) {
            broken = error.code() == std::make_error_code(std::future_errc::broken_promise);
        }
        if (!broken) fail("late_response_reported_success");
        record("event=LATE_IGNORED id=" + std::to_string(timeout.request_id) + " future=broken_promise");
        client.reset(); client = node->create_client<Service>("add_" + peer);
        until([&] { return client->service_is_ready(); }, 10s);
        auto recreated = request(99, 2); receive(recreated, 99, 2);
        until([&] { return received == 7 && sent == 7 && batch.empty() && !delayed.request; }, 10s);
        record("event=BUSINESS_DONE received=7 responded=7 accepted=6 timed_out=1");
        const char* release = std::getenv("FDDST_SERVICE_RELEASE");
        if (!release) fail("release_path_missing");
        until([&] { std::ifstream f(release); std::string line; return std::getline(f, line) && line == "RELEASE " + run; }, 10s);
        FastddsDsoftbusTeardownCheckpoint("BEFORE_NODE_DELETE");
        result = 0;
    } catch (const std::exception& error) {
        std::cerr << "SERVICE_EXCEPTION stage=" << stage << " message=" << error.what() << std::endl;
        if (!initialized) {
            // Context::init must succeed before shutdown or factory teardown.
            // No Node/Participant has been requested on this branch.
            record("event=INIT_FAILED result=4");
            return 4;
        }
        ros_failure_protocol::Report(run, side, pid, stage, "SCENARIO_FAILED");
    }
    FastddsDsoftbusTeardownCheckpoint("AFTER_NODE_ENTITY_DELETE");
    try { if (!rclcpp::shutdown()) result = 4; } catch (...) { result = 4; }
    FastddsDsoftbusTeardownCheckpoint("AFTER_CONTEXT_CLEANUP");
    if (FastddsDsoftbusTeardownResult() != 0) result = 4;
    record("event=CLOSED result=" + std::to_string(result));
    return result;
}
