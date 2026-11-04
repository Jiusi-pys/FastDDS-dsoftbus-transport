#ifndef MDDS_OWNED_POLICY_H
#define MDDS_OWNED_POLICY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { OW_OK=0,OW_INTR=1,OW_PENDING=2,OW_ERROR=3 };
enum { OW_ACTIVE=1,OW_SIGNALLED=2,OW_REAPED=3,OW_UNKNOWN=4 };
typedef struct {uint32_t pid,ppid;uint64_t birth;int exclusive;} OW_Identity;
typedef struct {uint32_t pid;int terminal,raw_status;} OW_Observation;
typedef struct {
    int (*observe)(void*,uint32_t,OW_Observation*); /* nonconsuming WNOWAIT */
    int (*identity)(void*,uint32_t,OW_Identity*);
    int (*signal_one)(void*,uint32_t); /* positive created direct PID ONLY */
    int (*reap)(void*,uint32_t,int*); /* nonblocking, consumes wait right */
} OW_Ops;
typedef struct {
    uint32_t pid,ppid;uint64_t birth;int phase,signal_sent,raw_status,status_valid;
    int timed_out,cancelled,late_observation,terminal_seen;int64_t last_ms,deadline_ms,hard_ms,cleanup_ms;
} OW_Child;
int ow_begin(OW_Child*,uint32_t,uint32_t,uint64_t,int64_t,int64_t);
int ow_step(OW_Child*,const OW_Ops*,void*,int64_t,int);
/* Single exclusive caller. No reentry from ops. No descendant/group authority.
 * OW_REAPED means this ROOT only; descendants/SDK/identity are UNKNOWN.
 * UNKNOWN and REAPED are terminal/idempotent and perform zero target ops. */
#ifdef __cplusplus
}
#endif
#endif
