#pragma once
#include <cstdint>
namespace stage1_goal {
enum class QueryWait {Running,Terminal,Interrupted,Unknown};
enum class QueryTerminal {None,Exited,Signalled,Dumped,Invalid};
struct QueryObservation {bool api_ok=false,interrupted=false,empty=false,sigchld=false,valid_status=false;uint64_t pid=0;QueryTerminal terminal=QueryTerminal::Invalid;};
// A pure classification seam: no OS calls or signal authority of its own.
inline QueryWait classify_query_wait(uint64_t child,const QueryObservation& q){
    if(!child)return QueryWait::Unknown;
    if(!q.api_ok)return q.interrupted?QueryWait::Interrupted:QueryWait::Unknown;
    if(!q.pid)return q.empty&&q.terminal==QueryTerminal::None?QueryWait::Running:QueryWait::Unknown;
    if(q.pid!=child||!q.sigchld||!q.valid_status)return QueryWait::Unknown;
    return q.terminal==QueryTerminal::Exited||q.terminal==QueryTerminal::Signalled||q.terminal==QueryTerminal::Dumped?QueryWait::Terminal:QueryWait::Unknown;
}
}
