#pragma once
#include "native_files.hpp"
#include "../control/wire.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace stage1_goal {
// Test-application control only: files carry release/evidence, never replace DDS
// payload transport. This adapter requires actual current selected context.
class GoalControl final {
    Files& files_;Scalar& scalar_;NS_RunRequest request_;uint64_t entry_pid_,entry_tid_;
    std::mutex gate_,output_gate_;std::condition_variable changed_;unsigned entered_=0;
    bool sealed_=false,watch_done_=false;std::atomic<bool> stop_{false},cancelled_{false},fault_{false};
    int wake_read_=-1,wake_write_=-1;std::thread watch_;NS_RunApi api_{};NS_RunHandle* handle_=nullptr;
    uint32_t tx_=1,rx_=1;std::vector<uint8_t> journal_;
    struct Entry {GoalControl* p=nullptr;explicit Entry(GoalControl*);~Entry();};
    bool selected(bool main_only)const;
    static int32_t allow(void*,uint64_t);static int32_t emit(void*,const NS_RunEvent*);
    static int32_t wait(void*,uint32_t,uint64_t);static void abort(void*);
    void wake()noexcept;
public:
    GoalControl(Files&,Scalar&,const NS_RunRequest&);
    ~GoalControl();GoalControl(const GoalControl&)=delete;GoalControl& operator=(const GoalControl&)=delete;
    NS_RunControl callbacks();
    void start_watch(const NS_RunApi&,NS_RunHandle*);
    bool finish(Clock::time_point);
    bool fault()const{return fault_.load();}
};
}
