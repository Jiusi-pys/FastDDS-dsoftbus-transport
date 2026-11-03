#pragma once
#include "io.hpp"
#include <mutex>
#include <condition_variable>
namespace stage1_control {
// A context is never retargeted. Native/unproved producers retain it through
// process exit. Only controlled Host fixtures can explicitly relinquish it.
class Port final {
    struct State;std::unique_ptr<State> state_;
public:
    Port(std::shared_ptr<Channel>,NS_Envelope,bool marked_fixture);
    ~Port();Port(const Port&)=delete;Port& operator=(const Port&)=delete;
    NS_RunControl callbacks()const;
    bool seal(Clock::time_point);
    bool acknowledge_fixture_no_more_callbacks();
    bool reclaimable()const;
    bool matches(const NS_Envelope&)const;
};
using OwnerCheck=bool(*)(void*,const void*);
using GetRunApi=int32_t(*)(uint32_t,uint32_t,NS_RunApi*);
class Client final {
    struct State;std::shared_ptr<State> state_;
    std::unique_ptr<std::shared_ptr<State>> pin_;
public:
    Client(std::shared_ptr<void>,std::shared_ptr<Port>,Clock::time_point);
    ~Client();Client(const Client&)=delete;Client& operator=(const Client&)=delete;
    int bind(GetRunApi,OwnerCheck,void*);
    int create(const NS_RunRequest&);
    int execute(NS_RunResult&);
    int cancel();
    bool retained()const noexcept;
    // The caller owns this wrapper through method admission. Each admitted
    // method then owns heap State, including cancellation bookkeeping. Methods
    // must never be initiated using a pointer to an already destroyed wrapper.
};
int frontend_control(int argc,char** argv); // -1:not this mode; real control-run always78
}
