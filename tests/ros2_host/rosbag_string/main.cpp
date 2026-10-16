// Local persistence acceptance only: no Node, rclcpp::init, Participant or transport API.
#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_cpp/writer.hpp>
#include <rclcpp/serialization.hpp>
#include <std_msgs/msg/string.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

static void check(bool ok, const std::string & why)
{
  if (!ok) { throw std::runtime_error(why); }
}

static std::string quote(const std::string & text)
{
  std::ostringstream out;
  out << '"';
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') { out << '\\' << c; }
    else if (c < 32) { out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c); }
    else { out << c; }
  }
  out << '"';
  return out.str();
}

static std::string hex(const Bytes & bytes)
{
  std::ostringstream out;
  for (uint8_t c : bytes) { out << std::hex << std::setw(2) << std::setfill('0') << int(c); }
  return out.str();
}

static std::string read_file(const fs::path & path)
{
  std::ifstream input(path, std::ios::binary);
  check(input.good(), "cannot read " + path.string());
  return {std::istreambuf_iterator<char>(input), {}};
}

// Bounds main after entry. The independent runner also bounds loader/startup and HDC.
class Deadline
{
public:
  Deadline() : worker_([this]() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!condition_.wait_for(lock, std::chrono::seconds(30), [this]() { return done_; })) {
      std::cerr << "{\"status\":\"FAIL\",\"reason\":\"fixture deadline\"}" << std::endl;
      std::_Exit(124);
    }
  }) {}
  ~Deadline() {
    { std::lock_guard<std::mutex> lock(mutex_); done_ = true; }
    condition_.notify_one();
    worker_.join();
  }
private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool done_ = false;
  std::thread worker_;
};

static std::set<std::string> task_ids()
{
  std::set<std::string> ids;
  for (const auto & entry : fs::directory_iterator("/proc/self/task")) {
    ids.insert(entry.path().filename().string());
  }
  return ids;
}

static size_t bag_descriptors(const fs::path & bag)
{
  const auto prefix = bag.string() + "/";
  size_t count = 0;
  for (const auto & entry : fs::directory_iterator("/proc/self/fd")) {
    std::error_code error;
    const auto target = fs::read_symlink(entry.path(), error).string();
    check(!error || error == std::errc::no_such_file_or_directory, "cannot inspect own file descriptors");
    if (!error && (target == bag.string() || target.compare(0, prefix.size(), prefix) == 0)) {
      ++count;
    }
  }
  return count;
}

static void no_native_bridge()
{
  const auto maps = read_file("/proc/self/maps");
  std::istringstream lines(maps);
  std::string line;
  while (std::getline(lines, line)) {
    const auto slash = line.find_last_of('/');
    if (slash == std::string::npos) { continue; }
    const auto name = line.substr(slash + 1);
    for (const char * forbidden : {"dsoftbus", "runtime_token_bridge", "accesstoken", "tokensetproc", "softbus_client"}) {
      check(name.find(forbidden) == std::string::npos, std::string("unexpected native library: ") + name);
    }
  }
}

static void released(const char * phase, const fs::path & bag, const std::set<std::string> & baseline)
{
  const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while ((bag_descriptors(bag) != 0 || task_ids() != baseline) && std::chrono::steady_clock::now() < limit) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  check(bag_descriptors(bag) == 0, std::string(phase) + " retained bag file descriptor");
  check(task_ids() == baseline, std::string(phase) + " retained/replaced thread");
  no_native_bridge();
  std::cout << "{\"event\":\"released\",\"phase\":" << quote(phase)
            << ",\"bag_fds\":0,\"task_count\":" << baseline.size() << "}" << std::endl;
}

// Independent fixed String PLAIN_CDR/XCDRv1 LE byte expectation, including encapsulation/NUL.
static Bytes expected_cdr(const std::string & value)
{
  check(value.size() < 65536, "fixture payload bound");
  Bytes bytes{0, 1, 0, 0};
  const uint32_t length = static_cast<uint32_t>(value.size() + 1);
  for (unsigned shift = 0; shift < 32; shift += 8) { bytes.push_back((length >> shift) & 255); }
  bytes.insert(bytes.end(), value.begin(), value.end());
  bytes.push_back(0);
  return bytes;
}

int main(int argc, char ** argv)
{
  try {
    Deadline deadline;
    check(argc == 3, "usage: rosbag_string_persistence ABSENT_ABSOLUTE_BAG_URI ORIGINAL_STRING_MSG");
    const fs::path bag(argv[1]);
    check(bag.is_absolute() && !fs::exists(bag), "fresh absolute bag URI required");
    const auto definition = read_file(argv[2]);
    check(!definition.empty() && definition.find("string data") != std::string::npos, "real String.msg definition required");
    no_native_bridge();
    const auto baseline = task_ids();
    const std::vector<std::string> values{"", "local-string", u8"中文🙂", "last"};
    const std::string topic = "/local_string";
    const std::string type = "std_msgs/msg/String";
    const int64_t first_time = 1700000000000000000LL;
    std::vector<Bytes> expected;
    for (const auto & value : values) {
      std_msgs::msg::String message; message.data = value;
      rclcpp::SerializedMessage serialized;
      rclcpp::Serialization<std_msgs::msg::String> serialization;
      serialization.serialize_message(&message, &serialized);
      const auto & buffer = serialized.get_rcl_serialized_message();
      const Bytes actual(buffer.buffer, buffer.buffer + buffer.buffer_length);
      expected.push_back(expected_cdr(value));
      check(actual == expected.back(), "actual String CDR differs independent fixed byte expectation");
    }
    rosbag2_storage::StorageOptions options;
    options.uri = bag.string(); options.storage_id = "sqlite3";
    options.max_cache_size = 0; options.max_bagfile_size = 0; options.max_bagfile_duration = 0;
    options.snapshot_mode = false;
    const rosbag2_cpp::ConverterOptions converter{"cdr", "cdr"};
    {
      rosbag2_cpp::Writer writer;
      writer.open(options, converter);
      for (size_t i = 0; i < values.size(); ++i) {
        std_msgs::msg::String message; message.data = values[i];
        writer.write(message, topic, rclcpp::Time(first_time + int64_t(i) * 1000000, RCL_SYSTEM_TIME));
      }
      writer.close();
    }
    released("writer_close_destroy", bag, baseline);
    {
      rosbag2_cpp::Reader reader;
      reader.open(options, converter);
      check(reader.set_read_order({rosbag2_storage::ReadOrder::ReceivedTimestamp, false}), "receive-time read order unavailable");
      const auto & metadata = reader.get_metadata();
      check(metadata.storage_identifier == "sqlite3" && metadata.message_count == values.size(), "persisted count/storage mismatch");
      check(metadata.relative_file_paths.size() == 1 && metadata.compression_format.empty() && metadata.compression_mode.empty(), "unexpected split/compression");
      const auto topics = reader.get_all_topics_and_types();
      check(topics.size() == 1 && topics[0].name == topic && topics[0].type == type && topics[0].serialization_format == "cdr", "persisted topic/type/format mismatch");
      check(metadata.topics_with_message_count.size() == 1 && metadata.topics_with_message_count[0].message_count == values.size(), "topic persisted count mismatch");
      const auto & counted_topic = metadata.topics_with_message_count[0].topic_metadata;
      check(counted_topic.name == topic && counted_topic.type == type && counted_topic.serialization_format == "cdr", "metadata topic/type/format mismatch");
      std::vector<rosbag2_storage::MessageDefinition> definitions;
      reader.get_all_message_definitions(definitions);
      check(definitions.size() == 1 && definitions[0].topic_type == type && definitions[0].encoding == "ros2msg" && definitions[0].encoded_message_definition == definition, "missing/degraded/different persisted String definition");
      for (size_t i = 0; i < values.size(); ++i) {
        check(reader.has_next(), "missing persisted raw message");
        auto message = reader.read_next();
        check(message && message->serialized_data, "empty raw message");
        check(message->topic_name == topic && message->recv_timestamp == first_time + int64_t(i) * 1000000, "raw topic/order/receive-time mismatch");
        const auto & buffer = *message->serialized_data;
        const Bytes actual(buffer.buffer, buffer.buffer + buffer.buffer_length);
        check(actual == expected[i], "persisted CDR differs exact expected bytes");
        std::cout << "{\"event\":\"raw_message\",\"index\":" << i << ",\"recv_ns\":" << message->recv_timestamp
                  << ",\"cdr_hex\":" << quote(hex(actual)) << "}" << std::endl;
      }
      check(!reader.has_next(), "extra persisted raw message");
      reader.close();
    }
    released("raw_reader_close_destroy", bag, baseline);
    {
      rosbag2_cpp::Reader reader;
      reader.open(options, converter);
      check(reader.set_read_order({rosbag2_storage::ReadOrder::ReceivedTimestamp, false}), "typed receive-time read order unavailable");
      for (const auto & value : values) {
        check(reader.has_next(), "missing typed message after reopen");
        check(reader.read_next<std_msgs::msg::String>().data == value, "typed payload mismatch after reopen");
      }
      check(!reader.has_next(), "extra typed message after reopen");
      reader.close();
    }
    released("typed_reader_close_destroy", bag, baseline);
    std::cout << "{\"status\":\"PASS\",\"messages\":4,\"raw_cdr_exact\":true,\"definition_exact\":true,\"typed_reopen\":true,\"send_time_persistence\":\"NOT_CLAIMED\"}" << std::endl;
    return 0;
  } catch (const std::exception & error) {
    std::cerr << "{\"status\":\"FAIL\",\"reason\":" << quote(error.what()) << "}" << std::endl;
    return 1;
  }
}
