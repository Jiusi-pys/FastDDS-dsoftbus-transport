#pragma once
#include "../run_contract.h"
#include <array>
#include <vector>
#include <cstddef>
namespace stage1_control {
constexpr size_t header_bytes=24,body_limit=896,frame_limit=header_bytes+body_limit;
constexpr uint32_t frame_count_limit=64,byte_limit=65536;
enum class Kind:uint8_t {Event=1,Release=2,Abort=3};
enum class Direction:uint8_t {Endpoint=1,Coordinator=2};
enum class Error {None,Invalid,Bound,Sequence,Envelope,Expired,Cancelled,Eof,Io,Busy,Retained,Closed,Provider,Abi};
struct Frame {Kind kind=Kind::Abort;Direction direction=Direction::Endpoint;uint32_t sequence=0;uint64_t nonce=0;NS_Envelope envelope{};NS_RunEvent event{};uint32_t release=0;int32_t error=NS_STATE;};
bool valid_envelope(const NS_Envelope&)noexcept;
bool same_envelope(const NS_Envelope&,const NS_Envelope&)noexcept;
std::vector<uint8_t> encode(const Frame&);
Error header_size(const uint8_t*,size_t,uint32_t&)noexcept;
Error decode(const uint8_t*,size_t,Frame&)noexcept;
bool codec_selftest();
}
