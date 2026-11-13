#pragma once
// SDK-specific close ownership. Unlike the deployment helper's best-effort
// stop_and_close, failure retains the Runtime for a later explicit close.
namespace b4_detail {
template<class Pumped, class Descriptors>
bool close_runtime(Pumped& runtime, Descriptors& descriptors) {
    if (!runtime.rt) return true;
    runtime.rt->RequestPumpStop();
    if (runtime.pump.joinable()) runtime.pump.join();
    descriptors.clear();
    return runtime.rt->Close();
}
}
