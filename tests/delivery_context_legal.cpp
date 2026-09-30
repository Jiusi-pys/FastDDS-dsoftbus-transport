// The composition's delivery-handle encapsulation, checked on every build.
//
// The record-scoped composition marks the delivery context on the admitted
// execution, so the only way to run an admission is through the handle the
// composition issued. That guarantee is worth something only if the handle has no
// execution entry of its own: a public Run() would let a caller enter a delivery
// without the context and make a control call the contract forbids.
//
// This translation unit is compiled into the composition test target, so the
// property below is a build-time fact with a real dependency on the header rather
// than a configure-time one that an incremental build could skip. The same file
// is also the positive control of the CMakeLists check that compiles it against
// the bypassing call site, which additionally asserts that the failure there is
// the missing member and not a missing include or a link error.
#include <fastdds_dsoftbus/dsoftbus/ServiceRecordBinding.hpp>

#include <type_traits>
#include <utility>

namespace {
using namespace fastdds_dsoftbus::dsoftbus;

// Detect any callable execution entry on the delivery handle. A private or
// deleted member is a substitution failure here, and that is the right answer:
// what matters is whether a caller can run the delivery.
template<class T, class = void>
struct HasRun : std::false_type
{
};
template<class T>
struct HasRun<T, std::void_t<decltype(std::declval<T&>().Run())>> : std::true_type
{
};

static_assert(!HasRun<BindingDelivery>::value,
        "the composition's delivery handle must expose no execution entry of its own, "
        "so a delivery cannot be run around the context the composition arms");
static_assert(!std::is_constructible<BindingDelivery, ProcessReceiveInvocation&&>::value,
        "a delivery may only be issued by the composition that admitted it");

// Every legal way to obtain and run one admission of the composition. Never
// called: this translation unit is the compile-time control.
[[maybe_unused]] void RunAnAdmissionThroughTheComposition(ServiceRecordBinding& binding)
{
    if (auto admitted = binding.NextAdmitted()) {
        (void)binding.RunAdmitted(*admitted.value);
    }
}
[[maybe_unused]] void HoldAndRunAnAlreadyIssuedDelivery(ServiceRecordBinding& binding,
        BindingDelivery& delivery)
{
    (void)static_cast<bool>(delivery);
    (void)delivery.Observation();
    (void)binding.RunAdmitted(delivery);
}
} // namespace
