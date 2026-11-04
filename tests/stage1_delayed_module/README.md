# Host delayed-module successor scaffold

This independent project implements a Windows Host frontend and genuine DLL
fixtures behind a versioned C/POD contract. It does not link FastDDS, DSoftBus,
permission/token libraries or the current299 executable. No real identity
adapter exists. Default mode and `--real` refuse **before DLL load**; only
explicit Host fixture cases change a local integer phase. Those changes are
not SetSelf, permission, restoration or Native profile evidence.

Fixed project labels remain actionbag
`mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16` and `com.kaihong.mdds`.
They confer no authorization. There are no target-token parameters, provisioning,
service/exec/fork facilities, identity APIs or hidden constructor switching.

`contract.h` v1 requires exact size/version/scope/reserved fields and borrowed
caller buffers/tables, valid fixture cookie, capacity and nonoverlap. Calls are
synchronous, one active invocation; callbacks borrow caller context only until
return. Function-table ownership is checked against the actual loaded DLL
before invocation. No C++ container/exception/allocator-owned object crosses
the ABI. C/C++ layout assertions qualify only this Windows x64 layout, never
OpenHarmony or cross-`__h`/`__1` ABI.

The frontend exports one preallocated Interlocked-only witness. Real DLL
attach/detach callbacks call it without allocation, mutex, IO or SDK under the
loader lock. Events demonstrate fake-select/verify before actual DLL attach,
then invocation close, actual detach/unload, then fake restore. The early-load
fixture deliberately violates ordering and is refused even after later fake
verification. Missing-export and foreign-function-owner cases fail closed.

Entry authority uses real caller thread ID and controlled fixture workers. A
real Event-gated worker causes rejection and is joined before fixture cleanup.
Windows OS thread count is separately diagnostic: this is **not** an OH
single-task check, kernel token/worker/parent inheritance or constructor safety
proof. Module code creates no background work. Cancel is requested by the Host
callback after OPEN, without Close or polling. Error plus reentry checks the
active-call refusal and close-before-unload contract.

Hard exit90 deliberately terminates without fake restore; unexpected unsafe
lifecycle or unload failures similarly produce abnormal no-restore exits.
Do not infer SIGTERM/SIGKILL/exec RAII guarantees. Real SDK callback quiet,
borrowed parameter last use, Native restoration and dual-board data remain
unimplemented/unverified. No hidden Runtime anchor/sibling is created.

Use a fresh owned E build with process-only E TEMP/TMP/cache/logs, verified VS17
2022 x64 Release/MD and singleworker/no node reuse. Configure really checks C
and CXX. Build `stage1_delayed_all`, then CTest `stage1.delayed_module.contract`
with original25s timeout. Four project TUs/three links plus Windows importlibs;
no stock SDK rebuild or full static archive. The Python driver retains actual
child RCs, including expected negative/exit cases, in a fresh child_cases.json.
Tests/expected vectors are scoped control/loader evidence, not fake Native PASS.

Initial -> actual formal independent review -> fixes/retest -> distinct NEW
final -> docs sync is required. Global/PRE_COMMIT FAIL, ReadyNO, Stage1 NOT
VERIFIED, Stage2 all37 HELD remain until their genuine applicable gates pass.
