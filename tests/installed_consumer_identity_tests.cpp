#include "token_first_entry.hpp"
#include "../examples/installed_runtime_dual_board/ServiceNames.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
using namespace fastdds_dsoftbus_probe;
struct Operations : TokenEntryOperations {
    std::uint64_t current=0x100000001ULL,expected=700000123;
    int selected=0,restored=0,providers=0;bool deny=false;
    TaskCheckpoint ObserveTasks() override {return {true,true,1,1,1,1};}
    std::uint64_t GetSelf() override{return current;}
    int SetSelf(std::uint64_t v) override{current=v;if(v==expected)++selected;else ++restored;return 0;}
    OwnedTokenObservation VerifyOwned(std::uint64_t v,const char*) override{return {v,1,1,0,"fastdds_dsoftbus_probe",2,2,0,{0},deny?1:0};}
    void Diagnostic(const char*,std::uint64_t,std::uint64_t,int) override{}
    bool EnterProvider() override{++providers;return current==expected;}
    bool CloseProvider() override{return current==expected;}
};
int main(){
    const auto name=installed_example::ServicePrefix("com.kaihong.mdds","mdds.installed","0123456789abcdef");
    if(name!="com.kaihong.mdds.mdds.installed.0123456789abcdef"||
       name==installed_example::ServicePrefix("com.kaihong.mdds","mdds.installed","fedcba9876543210"))return 8;
    Operations op;
    {
        TokenFirstEntry identity(op);
        if(!identity.Begin({TokenEntryMode::Record,true,true,op.expected}))return 1;
        if(op.current!=op.expected||op.restored||op.providers)return 2;
        // Actual public begin/restore API brackets the complete provider scope.
        bool destroyed=false;
        {struct Provider {Operations& op;bool& closed;~Provider(){if(op.current!=op.expected)std::abort();closed=true;}} provider{op,destroyed};
            if(identity.Begin({TokenEntryMode::Record,true,true,op.expected}))return 3;
            if(op.current!=op.expected||op.restored)return 4;
        }
        if(!destroyed||!identity.Restore()||op.current!=0x100000001ULL||op.restored!=1)return 5;
    }
    if(op.restored!=1)return 6;
    Operations denied;denied.deny=true;TokenFirstEntry guard(denied);
    if(guard.Begin({TokenEntryMode::Record,true,true,denied.expected})||denied.current!=0x100000001ULL||denied.providers)return 7;
    std::cout<<"INSTALLED_IDENTITY_BEGIN_PROVIDER_LIFETIME_RESTORE_PASS\n";return 0;
}
