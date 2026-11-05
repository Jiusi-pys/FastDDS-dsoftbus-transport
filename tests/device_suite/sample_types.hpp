#pragma once
// Test sample types for the cross-board device suite, identical on Fast DDS 2.14 and 3.x.
#include "compat.hpp"
#include <array>
#include <vector>

namespace dsb {
inline std::array<std::uint8_t, 32> payload(std::uint64_t run, std::uint32_t side, std::uint32_t ordinal)
{
    std::array<std::uint8_t, 32> p{};
    for (unsigned i = 0; i < p.size(); ++i) p[i] = std::uint8_t((run >> (8 * (i % 8))) ^ side ^ ordinal ^ i);
    return p;
}

// Small RELIABLE sample: run, side, ordinal and a 32-byte check payload.
struct Layer { std::uint64_t run = 0; std::uint32_t side = 0, ordinal = 0; std::array<std::uint8_t, 32> payload{}; };
inline Layer layer(std::uint64_t run, std::uint32_t side, std::uint32_t n)
{ Layer v; v.run = run; v.side = side; v.ordinal = n; v.payload = dsb::payload(run, side, n); return v; }
inline bool layer_valid(const Layer& v, std::uint64_t run, std::uint32_t side)
{ return v.run == run && v.side == side && v.payload == dsb::payload(run, side, v.ordinal); }
class LayerType final : public SimpleType {
public:
    LayerType() : SimpleType("FastDDSDSoftBusLayerSample", 4 + 64, false) {}
protected:
    std::uint32_t encode(const void* d, std::uint8_t* o, std::uint32_t c) override
    { auto& v = *static_cast<const Layer*>(d); Out w{o, c}; w.u64(v.run); w.u32(v.side); w.u32(v.ordinal); w.bytes(v.payload.data(), 32); return w.done(); }
    bool decode(const std::uint8_t* i, std::uint32_t n, void* d) override
    { auto& v = *static_cast<Layer*>(d); In r{i, n}; v.run = r.u64(); v.side = r.u32(); v.ordinal = r.u32(); r.bytes(v.payload.data(), 32); return !r.fail; }
    std::uint32_t encoded_size(const void*) override { return 48; }
    void* make() override { return new Layer; }
    void destroy(void* d) override { delete static_cast<Layer*>(d); }
};

// Keyed sample; the 4-byte key fills the instance handle big-endian.
struct Keyed { std::uint32_t key = 0, seq = 0; std::uint64_t run = 0; std::uint32_t side = 0, tag = 0; };
class KeyedType final : public SimpleType {
public:
    KeyedType() : SimpleType("FastDDSDSoftBusKeyed", 4 + 40, true) {}
protected:
    std::uint32_t encode(const void* d, std::uint8_t* o, std::uint32_t c) override
    { auto& v = *static_cast<const Keyed*>(d); Out w{o, c}; w.u32(v.key); w.u32(v.seq); w.u64(v.run); w.u32(v.side); w.u32(v.tag); return w.done(); }
    bool decode(const std::uint8_t* i, std::uint32_t n, void* d) override
    { auto& v = *static_cast<Keyed*>(d); In r{i, n}; v.key = r.u32(); v.seq = r.u32(); v.run = r.u64(); v.side = r.u32(); v.tag = r.u32(); return !r.fail; }
    std::uint32_t encoded_size(const void*) override { return 24; }
    void* make() override { return new Keyed; }
    void destroy(void* d) override { delete static_cast<Keyed*>(d); }
    void key_of(const void* d, std::uint8_t out[16]) override
    {
        const auto k = static_cast<const Keyed*>(d)->key;
        for (unsigned i = 0; i < 16; ++i) out[i] = i < 4 ? static_cast<std::uint8_t>(k >> (8 * (3 - i))) : 0;
    }
};

// Variable-size byte sample: 150000 bytes forces DATA_FRAG; 1 MiB tests large-sample delivery.
struct Blob { std::uint64_t run = 0; std::uint32_t side = 0; std::vector<std::uint8_t> bytes; };
inline std::uint8_t blob_pattern(std::uint64_t run, std::uint32_t side, std::uint32_t i)
{ return static_cast<std::uint8_t>((i * 31u) ^ (i >> 8) ^ (side * 0x5Bu) ^ static_cast<std::uint32_t>(run)); }
inline Blob blob(std::uint64_t run, std::uint32_t side, std::uint32_t size)
{ Blob b; b.run = run; b.side = side; b.bytes.resize(size); for (std::uint32_t i = 0; i < size; ++i) b.bytes[i] = blob_pattern(run, side, i); return b; }
inline bool blob_valid(const Blob& b, std::uint64_t run, std::uint32_t side, std::uint32_t size)
{
    if (b.run != run || b.side != side || b.bytes.size() != size) return false;
    for (std::uint32_t i = 0; i < size; ++i) if (b.bytes[i] != blob_pattern(run, side, i)) return false;
    return true;
}
class BlobType final : public SimpleType {
public:
    BlobType(const char* name, std::uint32_t max_bytes) : SimpleType(name, 4 + 16 + max_bytes, false), max_(max_bytes) {}
protected:
    std::uint32_t encode(const void* d, std::uint8_t* o, std::uint32_t c) override
    {
        auto& v = *static_cast<const Blob*>(d); if (v.bytes.size() > max_) return 0;
        Out w{o, c}; w.u64(v.run); w.u32(v.side); w.u32(static_cast<std::uint32_t>(v.bytes.size()));
        w.bytes(v.bytes.data(), static_cast<std::uint32_t>(v.bytes.size())); return w.done();
    }
    bool decode(const std::uint8_t* i, std::uint32_t n, void* d) override
    {
        auto& v = *static_cast<Blob*>(d); In r{i, n}; v.run = r.u64(); v.side = r.u32(); const auto len = r.u32();
        if (r.fail || len > max_) return false; v.bytes.resize(len); return r.bytes(v.bytes.data(), len);
    }
    std::uint32_t encoded_size(const void* d) override { return 16 + static_cast<std::uint32_t>(static_cast<const Blob*>(d)->bytes.size()); }
    void* make() override { return new Blob; }
    void destroy(void* d) override { delete static_cast<Blob*>(d); }
private:
    std::uint32_t max_;
};
constexpr std::uint32_t kFragBytes = 150000;
constexpr std::uint32_t kLargeBytes = 1u << 20;

// Plain fixed-size sample for DataWriter::loan_sample (memory layout == wire layout on LE boards).
struct Plain { std::uint64_t run; std::uint32_t side, ordinal; std::uint8_t bytes[4000]; };
inline std::uint8_t plain_pattern(std::uint64_t run, std::uint32_t side, std::uint32_t ordinal, std::uint32_t i)
{ return static_cast<std::uint8_t>((i * 7u) ^ (ordinal * 13u) ^ (side * 0x5Bu) ^ static_cast<std::uint32_t>(run)); }
inline void plain_fill(Plain& s, std::uint64_t run, std::uint32_t side, std::uint32_t ordinal)
{ s.run = run; s.side = side; s.ordinal = ordinal; for (std::uint32_t i = 0; i < sizeof(s.bytes); ++i) s.bytes[i] = plain_pattern(run, side, ordinal, i); }
inline bool plain_valid(const Plain& s, std::uint64_t run, std::uint32_t side, std::uint32_t ordinal)
{
    if (s.run != run || s.side != side || s.ordinal != ordinal) return false;
    for (std::uint32_t i = 0; i < sizeof(s.bytes); ++i) if (s.bytes[i] != plain_pattern(run, side, ordinal, i)) return false;
    return true;
}
class PlainType final : public SimpleType {
public:
    PlainType() : SimpleType("FastDDSDSoftBusPlainSample", 4 + sizeof(Plain), false, true) {}
protected:
    std::uint32_t encode(const void* d, std::uint8_t* o, std::uint32_t c) override
    { if (c < sizeof(Plain)) return 0; std::memcpy(o, d, sizeof(Plain)); return sizeof(Plain); }
    bool decode(const std::uint8_t* i, std::uint32_t n, void* d) override
    { if (n != sizeof(Plain)) return false; std::memcpy(d, i, sizeof(Plain)); return true; }
    std::uint32_t encoded_size(const void*) override { return sizeof(Plain); }
    void* make() override { return new Plain(); }
    void destroy(void* d) override { delete static_cast<Plain*>(d); }
};
} // namespace dsb
