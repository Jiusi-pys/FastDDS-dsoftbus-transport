#pragma once
#include "wire.hpp"
#include <chrono>
#include <memory>
#include <atomic>
namespace stage1_control {
using Clock=std::chrono::steady_clock;
class Pipe final {
    struct State;std::unique_ptr<State> state_;
    explicit Pipe(std::unique_ptr<State>);
public:
    struct Endpoint {uint64_t input=UINT64_MAX,output=UINT64_MAX;};
    struct Pair {std::shared_ptr<Pipe> a,b;};
    static Pair local_pair(); // owned local byte pipes only; never DSoftBus/network
    Error take_fresh_endpoint(Endpoint&); // caller owns result; only fresh unused uncancelled state
    static std::shared_ptr<Pipe> adopt_owned(Endpoint&,uint32_t expected_creator,Error&); // consumes ONLY on success
    ~Pipe();Pipe(const Pipe&)=delete;Pipe& operator=(const Pipe&)=delete;
    Error read(uint8_t*,size_t,size_t&,Clock::time_point);
    Error write(const uint8_t*,size_t,size_t&,Clock::time_point);
    void cancel()noexcept;
    bool close(Clock::time_point)noexcept;
    bool wait_pending(bool writing,Clock::time_point); // own OS operation witness, not SDK readiness
private:
    Error transfer(bool,uint8_t*,size_t,size_t&,Clock::time_point);
};
class Channel final {
    std::shared_ptr<Pipe> pipe_;NS_Envelope identity_;Direction outgoing_;
    Clock::time_point deadline_;size_t chunk_;std::atomic<bool> sealed_{false};
    std::atomic_flag tx_=ATOMIC_FLAG_INIT,rx_=ATOMIC_FLAG_INIT;
    uint32_t tx_seq_=1,rx_seq_=1;size_t tx_bytes_=0,rx_bytes_=0;
    Error exact(bool,uint8_t*,size_t);
    Error fail(Error)noexcept;
public:
    Channel(std::shared_ptr<Pipe>,NS_Envelope,Direction,Clock::time_point,size_t max_chunk=128);
    Error send(Frame);
    Error receive(Frame&);
    void cancel()noexcept;
    bool close(Clock::time_point)noexcept;
    Clock::time_point deadline()const noexcept{return deadline_;}
};
}
