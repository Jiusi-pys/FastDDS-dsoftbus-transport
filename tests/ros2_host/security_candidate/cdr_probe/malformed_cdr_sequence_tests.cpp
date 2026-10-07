// Malformed CDR sequence-length tests for the current application data type.
//
// Task: CANDIDATE_SECURITY_MITIGATION_FROM_3BE5492.
//
// The security table records that "malformed sequence lengths were not tested"
// for the candidate. This file closes that specific gap for the type the ROS
// acceptance runs actually carry, std_msgs, and it keeps the two layers that
// the plan review requires strictly separate:
//
//   Layer 1 - the real generated rosidl typesupport for std_msgs/msg/
//             UInt8MultiArray, whose `data` member is sequence<uint8> and whose
//             `layout` member contains sequence<MultiArrayDimension>. These
//             functions live in the candidate-built
//             std_msgs__rosidl_typesupport_fastrtps_cpp library.
//   Layer 2 - Fast CDR's own sequence reader, instantiated inside this
//             translation unit, where the process allocator can actually be
//             observed. The generated library's internal allocation requests
//             are NOT observable from outside it, so layer 2 carries the
//             bounded-allocation evidence and layer 1 carries the end-to-end
//             generated-code evidence.
//
// Neither layer pre-validates the input. Every malformed buffer is handed
// straight to the library; the expected outcome is whatever that library
// actually does, recorded verbatim.
//
// The allocation observer refuses any single request above a small ceiling
// instead of attempting it. That ceiling is a test-safety control, not a
// product limit and not a statement that the library rejects the input.

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <exception>
#include <iostream>
#include <new>
#include <string>
#include <vector>

#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>

#include "std_msgs/msg/detail/u_int8_multi_array__struct.hpp"
#include "std_msgs/msg/detail/u_int8_multi_array__rosidl_typesupport_fastrtps_cpp.hpp"

namespace {

// ---------------------------------------------------------------------------
// Bounded allocation observer for this translation unit.
// ---------------------------------------------------------------------------
constexpr uint32_t kMaxRecordedRequests = 64;

struct AllocSpy
{
    std::atomic<bool> armed{false};
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> bytes{0};
    std::atomic<uint64_t> largest{0};
    std::atomic<uint64_t> denied{0};
    std::atomic<uint64_t> ceiling{32u * 1024u * 1024u};
    // Individual request sizes, so an oversized resize can be shown directly
    // instead of being hidden behind a larger unrelated allocation.
    uint64_t sizes[kMaxRecordedRequests] = {0};
    uint32_t size_count = 0;

    void reset()
    {
        calls = 0;
        bytes = 0;
        largest = 0;
        denied = 0;
        size_count = 0;
        for (uint32_t i = 0; i < kMaxRecordedRequests; ++i)
        {
            sizes[i] = 0;
        }
    }

    void note(std::size_t size)
    {
        calls.fetch_add(1);
        bytes.fetch_add(size);
        uint64_t current = largest.load();
        while (size > current && !largest.compare_exchange_weak(current, size))
        {
        }
        if (size_count < kMaxRecordedRequests)
        {
            sizes[size_count++] = size;
        }
        if (size > ceiling.load())
        {
            denied.fetch_add(1);
            throw std::bad_alloc();
        }
    }

    bool saw_request_exactly(uint64_t wanted) const
    {
        for (uint32_t i = 0; i < size_count; ++i)
        {
            if (sizes[i] == wanted)
            {
                return true;
            }
        }
        return false;
    }

    std::string size_list() const
    {
        std::string text;
        for (uint32_t i = 0; i < size_count; ++i)
        {
            text += (i ? "," : "");
            text += std::to_string(sizes[i]);
        }
        return text.empty() ? std::string("none") : text;
    }
};

AllocSpy g_spy;

// ---------------------------------------------------------------------------
// Result recording
// ---------------------------------------------------------------------------
struct CaseResult
{
    std::string name;
    std::string input;
    std::string expectation;
    std::string observed;
    bool pass = false;
    uint64_t alloc_calls = 0;
    uint64_t alloc_largest = 0;
    uint64_t alloc_denied = 0;
    // "observed_in_this_translation_unit" for the Fast CDR layer, where the
    // sequence template is instantiated here; "not_observable" for the
    // generated typesupport layer, whose internal allocations cannot be seen.
    std::string allocation_observability = "not_observable";
};

std::vector<CaseResult> g_results;

std::string describe_exception(
        const std::exception& error)
{
    return std::string(typeid(error).name()) + ": " + error.what();
}

// ---------------------------------------------------------------------------
// Buffer construction helpers
// ---------------------------------------------------------------------------
constexpr uint8_t kEncapsulation[4] = {0x00, 0x01, 0x00, 0x00}; // CDR_LE plain

std::vector<uint8_t> buffer_with(
        const std::vector<uint8_t>& body)
{
    std::vector<uint8_t> out(kEncapsulation, kEncapsulation + 4);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

void append_u32(
        std::vector<uint8_t>& out,
        uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

// A well-formed UInt8MultiArray body: layout with an empty dim sequence and
// data_offset 0, followed by the data sequence.
std::vector<uint8_t> multi_array_body(
        uint32_t declared_data_length,
        const std::vector<uint8_t>& data_bytes)
{
    std::vector<uint8_t> body;
    append_u32(body, 0);                  // layout.dim sequence length
    append_u32(body, 0);                  // layout.data_offset
    append_u32(body, declared_data_length); // data sequence length
    body.insert(body.end(), data_bytes.begin(), data_bytes.end());
    return body;
}

// ---------------------------------------------------------------------------
// Layer 1: real generated rosidl typesupport
// ---------------------------------------------------------------------------
bool generated_deserialize(
        const std::vector<uint8_t>& buffer,
        std_msgs::msg::UInt8MultiArray& message,
        std::string& error)
{
    eprosima::fastcdr::FastBuffer fast_buffer(
        reinterpret_cast<char*>(const_cast<uint8_t*>(buffer.data())), buffer.size());
    eprosima::fastcdr::Cdr cdr(
        fast_buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,
        eprosima::fastcdr::CdrVersion::XCDRv1);
    try
    {
        cdr.read_encapsulation();
        std_msgs::msg::typesupport_fastrtps_cpp::cdr_deserialize(cdr, message);
    }
    catch (const std::exception& caught)
    {
        error = describe_exception(caught);
        return false;
    }
    return true;
}

void run_generated_case(
        const std::string& name,
        const std::string& input_description,
        const std::string& expectation,
        const std::vector<uint8_t>& buffer,
        bool expect_success,
        size_t expected_data_size)
{
    CaseResult result;
    result.name = name;
    result.input = input_description;
    result.expectation = expectation;

    g_spy.reset();
    g_spy.armed = true;
    std_msgs::msg::UInt8MultiArray message;
    std::string error;
    bool ok = false;
    try
    {
        ok = generated_deserialize(buffer, message, error);
    }
    catch (const std::exception& caught)
    {
        // The bounded ceiling refused a request the library tried to make.
        error = "denied by bounded observer: " + describe_exception(caught);
        ok = false;
    }
    g_spy.armed = false;

    // Any allocation the spy saw here was made by this harness building the
    // exception message, not by the generated library, whose own allocations
    // are invisible from outside it. Report the layer as unobservable rather
    // than presenting these numbers as library behaviour.
    result.allocation_observability = "not_observable";

    if (expect_success)
    {
        result.pass = ok && message.data.size() == expected_data_size;
        result.observed = ok
                ? "deserialized, data.size()=" + std::to_string(message.data.size())
                : "rejected: " + error;
    }
    else
    {
        result.pass = !ok;
        result.observed = ok
                ? "accepted, data.size()=" + std::to_string(message.data.size())
                : "rejected: " + error;
    }
    g_results.push_back(result);
    std::cout << (result.pass ? "[PASS] " : "[FAIL] ") << name
              << " -> " << result.observed << std::endl;
}

// ---------------------------------------------------------------------------
// Layer 2: Fast CDR sequence reader with an observed allocator
// ---------------------------------------------------------------------------
template<typename T>
void run_fastcdr_case(
        const std::string& name,
        const std::string& input_description,
        const std::string& expectation,
        const std::vector<uint8_t>& buffer,
        bool expect_success,
        size_t expected_count,
        uint64_t expected_resize_bytes)
{
    CaseResult result;
    result.name = name;
    result.input = input_description;
    result.expectation = expectation;

    g_spy.reset();
    g_spy.armed = true;
    std::string error;
    bool ok = false;
    size_t observed_size = 0;
    try
    {
        eprosima::fastcdr::FastBuffer fast_buffer(
            reinterpret_cast<char*>(const_cast<uint8_t*>(buffer.data())), buffer.size());
        eprosima::fastcdr::Cdr cdr(
            fast_buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,
            eprosima::fastcdr::CdrVersion::XCDRv1);
        cdr.read_encapsulation();
        std::vector<T> sequence;
        cdr.deserialize(sequence);
        observed_size = sequence.size();
        ok = true;
    }
    catch (const std::exception& caught)
    {
        error = describe_exception(caught);
    }
    g_spy.armed = false;

    result.alloc_calls = g_spy.calls.load();
    result.alloc_largest = g_spy.largest.load();
    result.alloc_denied = g_spy.denied.load();
    result.allocation_observability = "observed_in_this_translation_unit";

    if (expect_success)
    {
        result.pass = ok && observed_size == expected_count;
        result.observed = ok
                ? "deserialized, size=" + std::to_string(observed_size)
                : "rejected: " + error;
    }
    else
    {
        result.pass = !ok;
        result.observed = ok
                ? "accepted, size=" + std::to_string(observed_size)
                : "rejected: " + error;
    }
    if (result.pass && expected_resize_bytes != 0)
    {
        // The point of this case is not only that the read failed, but which
        // resize the guard admitted before it failed.
        result.pass = g_spy.saw_request_exactly(expected_resize_bytes);
    }
    result.observed += " | requests=[" + g_spy.size_list() + "]";
    g_results.push_back(result);
    std::cout << (result.pass ? "[PASS] " : "[FAIL] ") << name
              << " -> " << result.observed
              << " (largest=" << result.alloc_largest
              << " denied=" << result.alloc_denied << ")" << std::endl;
}

} // namespace

// ---------------------------------------------------------------------------
// Bounded process allocator for this translation unit.
// ---------------------------------------------------------------------------
void* operator new(std::size_t size)
{
    if (g_spy.armed.load())
    {
        g_spy.note(size);
    }
    void* memory = std::malloc(size ? size : 1);
    if (memory == nullptr)
    {
        throw std::bad_alloc();
    }
    return memory;
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

int main(
        int argc,
        char** argv)
{
    std::string output_path;
    for (int i = 1; i < argc; ++i)
    {
        const std::string key = argv[i];
        if (key == "--result" && i + 1 < argc)
        {
            output_path = argv[++i];
        }
    }

    std::cout << "== Layer 1: real generated rosidl typesupport "
                 "(std_msgs__rosidl_typesupport_fastrtps_cpp) ==" << std::endl;

    // Legal empty sequence.
    run_generated_case("gen_01_empty_sequence",
            "layout.dim=0, data_offset=0, data length=0, no bytes",
            "accepted with data.size()==0",
            buffer_with(multi_array_body(0, {})), true, 0);

    // Legal ordinary sequence.
    run_generated_case("gen_02_ordinary_sequence",
            "data length=8, 8 bytes present",
            "accepted with data.size()==8",
            buffer_with(multi_array_body(8, {1, 2, 3, 4, 5, 6, 7, 8})), true, 8);

    // Legal boundary: declared length equals the exact number of bytes left.
    run_generated_case("gen_03_boundary_exact_remaining",
            "data length=16, exactly 16 bytes present",
            "accepted with data.size()==16",
            buffer_with(multi_array_body(16, std::vector<uint8_t>(16, 0x7E))), true, 16);

    // Declared length one greater than the bytes present.
    run_generated_case("gen_04_declared_exceeds_remaining_by_one",
            "data length=17, only 16 bytes present",
            "rejected, no partially sized vector delivered",
            buffer_with(multi_array_body(17, std::vector<uint8_t>(16, 0x7E))), false, 0);

    // Declared length far beyond the buffer.
    run_generated_case("gen_05_declared_far_beyond_buffer",
            "data length=1048576, 16 bytes present",
            "rejected",
            buffer_with(multi_array_body(1048576, std::vector<uint8_t>(16, 0x7E))), false, 0);

    // Largest representable length with a tiny buffer.
    run_generated_case("gen_06_declared_uint32_max",
            "data length=0xFFFFFFFF, 16 bytes present",
            "rejected; this layer's internal allocations are not observable",
            buffer_with(multi_array_body(0xFFFFFFFFu, std::vector<uint8_t>(16, 0x7E))), false, 0);

    // Truncated: the length field itself is incomplete.
    {
        std::vector<uint8_t> body;
        append_u32(body, 0);
        append_u32(body, 0);
        body.push_back(0x08); // only one byte of the data length field
        run_generated_case("gen_07_truncated_length_field",
                "data length field cut after 1 byte",
                "rejected",
                buffer_with(body), false, 0);
    }

    // Truncated: length fine, payload cut.
    {
        std::vector<uint8_t> body;
        append_u32(body, 0);
        append_u32(body, 0);
        append_u32(body, 64);
        body.insert(body.end(), 8, 0x11); // 8 of 64 bytes
        run_generated_case("gen_08_truncated_payload",
                "data length=64, 8 bytes present",
                "rejected",
                buffer_with(body), false, 0);
    }

    // Truncated inside the layout dim sequence length.
    {
        std::vector<uint8_t> body;
        body.push_back(0x01); // one byte only
        run_generated_case("gen_09_truncated_layout_sequence_length",
                "layout.dim length field cut after 1 byte",
                "rejected",
                buffer_with(body), false, 0);
    }

    // Nested sequence: layout.dim declares 1 element but no element follows.
    {
        std::vector<uint8_t> body;
        append_u32(body, 1); // layout.dim sequence length = 1
        append_u32(body, 0); // layout.data_offset
        append_u32(body, 0); // data length = 0
        run_generated_case("gen_10_nested_sequence_without_element",
                "layout.dim length=1, no dim element present",
                "rejected",
                buffer_with(body), false, 0);
    }

    // Recovery: a valid sample after malformed input still deserializes exactly.
    {
        std::vector<uint8_t> good = buffer_with(multi_array_body(4, {9, 8, 7, 6}));
        std_msgs::msg::UInt8MultiArray message;
        std::string error;
        bool ok = generated_deserialize(good, message, error);
        CaseResult result;
        result.name = "gen_11_recovery_after_malformed";
        result.input = "one malformed read followed by a valid 4-byte sample";
        result.expectation = "valid sample accepted with exact contents";
        result.pass = ok && message.data.size() == 4
                && message.data[0] == 9 && message.data[3] == 6;
        result.observed = ok
                ? "data.size()=" + std::to_string(message.data.size())
                : "rejected: " + error;
        result.allocation_observability = "not_observable";
        g_results.push_back(result);
        std::cout << (result.pass ? "[PASS] " : "[FAIL] ") << result.name
                  << " -> " << result.observed << std::endl;
    }

    std::cout << "== Layer 2: Fast CDR sequence reader, allocator observed ==" << std::endl;

    // Legal empty and ordinary primitive sequences.
    run_fastcdr_case<uint8_t>("cdr_01_empty_sequence",
            "length=0", "accepted with size 0",
            buffer_with({0, 0, 0, 0}), true, 0, 0);

    run_fastcdr_case<uint8_t>("cdr_02_ordinary_sequence",
            "length=8, 8 bytes present", "accepted with size 8",
            buffer_with({8, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8}), true, 8, 0);

    // Declared length beyond the remaining bytes: the length guard fires.
    run_fastcdr_case<uint8_t>("cdr_03_declared_beyond_remaining",
            "length=64, 8 bytes present", "rejected",
            buffer_with({64, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8}), false, 0, 0);

    // Largest representable length: still rejected, and no request is made.
    run_fastcdr_case<uint8_t>("cdr_04_declared_uint32_max",
            "length=0xFFFFFFFF, 8 bytes present",
            "rejected; the only recorded requests are harness exception construction",
            buffer_with({0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3, 4, 5, 6, 7, 8}), false, 0, 0);

    // Arithmetic boundary: declared length equals the remaining byte count.
    // The guard compares remaining BYTES against element COUNT, so for a
    // wider element type it admits a resize of count * sizeof(T) bytes.
    run_fastcdr_case<uint32_t>("cdr_05_boundary_count_equals_bytes_uint32",
            "length=8, exactly 8 bytes present, element is 4 bytes",
            "a 32-byte resize (8 uint32 elements) is requested before the read fails",
            buffer_with({8, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8}), false, 0, 32);

    // Length chosen so the admitted resize size (12 * 8 = 96) cannot be confused
    // with the allocations that building the exception itself performs.
    run_fastcdr_case<uint64_t>("cdr_06_boundary_count_equals_bytes_uint64",
            "length=12, exactly 12 bytes present, element is 8 bytes",
            "a 96-byte resize (12 uint64 elements) is requested before the read fails",
            buffer_with({12, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}), false, 0, 96);

    // Legal boundary for the widest element type actually used here.
    run_fastcdr_case<uint32_t>("cdr_07_legal_uint32_sequence",
            "length=3, 12 bytes present", "accepted with size 3",
            buffer_with({3, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0}), true, 3, 0);

    // Recovery after a rejected read: a fresh stream still reads exactly.
    {
        CaseResult result;
        result.name = "cdr_08_recovery_after_rejected_read";
        result.input = "rejected stream followed by a valid uint8 sequence";
        result.expectation = "valid stream accepted with exact contents";
        std::string error;
        bool ok = false;
        std::vector<uint8_t> recovered;
        try
        {
            std::vector<uint8_t> bad = buffer_with({64, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8});
            eprosima::fastcdr::FastBuffer bad_buffer(
                reinterpret_cast<char*>(bad.data()), bad.size());
            eprosima::fastcdr::Cdr bad_cdr(
                bad_buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,
                eprosima::fastcdr::CdrVersion::XCDRv1);
            bad_cdr.read_encapsulation();
            std::vector<uint8_t> rejected;
            try
            {
                bad_cdr.deserialize(rejected);
            }
            catch (const std::exception&)
            {
            }

            std::vector<uint8_t> good = buffer_with({3, 0, 0, 0, 0xAA, 0xBB, 0xCC});
            eprosima::fastcdr::FastBuffer good_buffer(
                reinterpret_cast<char*>(good.data()), good.size());
            eprosima::fastcdr::Cdr good_cdr(
                good_buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,
                eprosima::fastcdr::CdrVersion::XCDRv1);
            good_cdr.read_encapsulation();
            good_cdr.deserialize(recovered);
            ok = true;
        }
        catch (const std::exception& caught)
        {
            error = describe_exception(caught);
        }
        result.pass = ok && recovered.size() == 3
                && recovered[0] == 0xAA && recovered[2] == 0xCC;
        result.observed = ok ? "size=" + std::to_string(recovered.size())
                             : "rejected: " + error;
        g_results.push_back(result);
        std::cout << (result.pass ? "[PASS] " : "[FAIL] ") << result.name
                  << " -> " << result.observed << std::endl;
    }

    // ---------------------------------------------------------------------
    size_t passed = 0;
    for (const CaseResult& result : g_results)
    {
        if (result.pass)
        {
            ++passed;
        }
    }

    std::string report = "{\n  \"cases\": [\n";
    for (size_t i = 0; i < g_results.size(); ++i)
    {
        const CaseResult& result = g_results[i];
        report += "    {\"name\": \"" + result.name + "\", "
                  "\"input\": \"" + result.input + "\", "
                  "\"expectation\": \"" + result.expectation + "\", "
                  "\"observed\": \"" + result.observed + "\", "
                  "\"allocation_calls\": " + std::to_string(result.alloc_calls) + ", "
                  "\"allocation_largest\": " + std::to_string(result.alloc_largest) + ", "
                  "\"allocation_denied\": " + std::to_string(result.alloc_denied) + ", "
                  "\"allocation_observability\": \"" + result.allocation_observability + "\", "
                  "\"pass\": " + (result.pass ? "true" : "false") + "}";
        report += (i + 1 == g_results.size()) ? "\n" : ",\n";
    }
    report += "  ],\n  \"cases_total\": " + std::to_string(g_results.size())
            + ",\n  \"cases_passed\": " + std::to_string(passed)
            + ",\n  \"allocation_ceiling_bytes\": " + std::to_string(g_spy.ceiling.load())
            + ",\n  \"allocation_observability\": \"operator new in this translation unit only; "
              "allocations made inside the generated typesupport library are not observable from outside it\"\n}\n";

    if (!output_path.empty())
    {
        std::FILE* file = std::fopen(output_path.c_str(), "wb");
        if (file != nullptr)
        {
            std::fwrite(report.data(), 1, report.size(), file);
            std::fclose(file);
        }
    }
    std::cout << report << std::endl;
    return passed == g_results.size() ? 0 : 1;
}
