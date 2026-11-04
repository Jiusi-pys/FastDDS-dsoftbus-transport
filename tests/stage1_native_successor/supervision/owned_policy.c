#include "owned_policy.h"
#include <limits.h>
#include <string.h>
_Static_assert(sizeof(OW_Identity)==24&&sizeof(OW_Observation)==12&&sizeof(OW_Ops)==32&&sizeof(OW_Child)==80,"64-bit Host/ARM64 C policy ABI");
static int unknown(OW_Child* c){c->phase=OW_UNKNOWN;return OW_UNKNOWN;}
int ow_begin(OW_Child* c,uint32_t pid,uint32_t ppid,uint64_t birth,int64_t start,int64_t duration){
    if(!c||!pid||pid>INT_MAX||!ppid||pid==ppid||!birth||start<0||duration<1||duration>8000||start>INT64_MAX-duration-2000)return OW_ERROR;
    memset(c,0,sizeof(*c));c->pid=pid;c->ppid=ppid;c->birth=birth;c->phase=OW_ACTIVE;c->last_ms=start;c->deadline_ms=start+duration;c->hard_ms=c->deadline_ms+2000;return OW_OK;
}
int ow_step(OW_Child* c,const OW_Ops* ops,void* context,int64_t now,int stop){
    OW_Observation o;OW_Identity id;int rc,status=0;
    if(!c)return OW_UNKNOWN;if(c->phase==OW_REAPED||c->phase==OW_UNKNOWN)return c->phase;
    if(c->phase!=OW_ACTIVE&&c->phase!=OW_SIGNALLED)return unknown(c);
    if(!ops||!ops->observe||!ops->identity||!ops->signal_one||!ops->reap||now<0||now<c->last_ms||now>=c->hard_ms||(c->cleanup_ms&&now>=c->cleanup_ms))return unknown(c);
    c->last_ms=now;memset(&o,0,sizeof(o));rc=ops->observe(context,c->pid,&o);
    if(rc==OW_INTR)return c->phase; /* Fresh observation required next step. */
    if(rc!=OW_OK||(o.terminal!=0&&o.terminal!=1)||(o.terminal?o.pid!=c->pid:o.pid!=0))return unknown(c);
    memset(&id,0,sizeof(id));if(ops->identity(context,c->pid,&id)!=OW_OK||id.pid!=c->pid||id.ppid!=c->ppid||id.birth!=c->birth||id.exclusive!=1)return unknown(c);
    if(o.terminal){
        /* Normal terminal roots are NEVER signalled, even when observed late. */
        if(!c->terminal_seen){c->terminal_seen=1;c->raw_status=o.raw_status;c->late_observation=now>=c->deadline_ms&&!c->signal_sent;if(!c->cleanup_ms)c->cleanup_ms=c->hard_ms-now<=2000?c->hard_ms:now+2000;}
        else if(o.raw_status!=c->raw_status)return unknown(c);
        rc=ops->reap(context,c->pid,&status);if(rc==OW_INTR||rc==OW_PENDING)return c->phase;
        if(rc!=OW_OK||status!=c->raw_status)return unknown(c);
        c->status_valid=1;c->phase=OW_REAPED;return OW_REAPED;
    }
    if(c->terminal_seen)return unknown(c);
    if(c->signal_sent){if(now>=c->cleanup_ms)return unknown(c);return c->phase;}
    if(stop||now>=c->deadline_ms){
        c->signal_sent=1;c->timed_out=now>=c->deadline_ms;c->cancelled=stop!=0;c->phase=OW_SIGNALLED;c->cleanup_ms=c->hard_ms-now<=2000?c->hard_ms:now+2000;
        /* The creation record, fresh wait reservation and generation matched.
         * Any failure revokes authority; never retry a numeric signal. */
        if(ops->signal_one(context,c->pid)!=OW_OK)return unknown(c);
    }
    return c->phase;
}
