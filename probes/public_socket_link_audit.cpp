#include <mdds/dsoftbus/PublicSocketBackend.hpp>

// Static link witness only. Never deployed or executed by this work package.
// Constructing the concrete class roots its vtable and actual five API methods;
// Kind/Observe are inert and no public SDK operation is called by main.
int main()
{
    using namespace mdds::dsoftbus::phase0;
    PublicSocketBackend backend(PublicSocketConfiguration{});
    return backend.Kind() == BackendKind::DSoftBusProvider &&
           !backend.Observe().configured ? 0 : 1;
}
