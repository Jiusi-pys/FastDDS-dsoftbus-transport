// Negative control for the composition's delivery-handle encapsulation check in
// CMakeLists.txt: this translation unit must NOT compile.
//
// The record-scoped composition marks the delivery context on the admitted
// execution, so the only way to run an admission is through the handle the
// composition issued. That guarantee is only worth anything if the handle has no
// execution entry of its own: if BindingDelivery exposed a Run(), a caller could
// enter a delivery without the context and make a control call the contract
// forbids. The check asserts the failure is the missing member (MSVC C2039), with
// the legal call site next to it as the positive control.
#include <mdds/dsoftbus/ServiceRecordBinding.hpp>

namespace {
using namespace mdds::dsoftbus;

// Never called: this translation unit only has to fail to compile.
void BypassTheDeliveryContext(BindingDelivery& delivery)
{
    (void)delivery.Run();
}
} // namespace
