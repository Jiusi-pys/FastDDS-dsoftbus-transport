#ifndef FASTDDS_DSOFTBUS_TESTS_TRANSPORT_FACADE_SELECTOR_CASES_HPP
#define FASTDDS_DSOFTBUS_TESTS_TRANSPORT_FACADE_SELECTOR_CASES_HPP

#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>

#include <fastdds/rtps/common/LocatorSelector.hpp>
#include <fastdds/rtps/common/LocatorSelectorEntry.hpp>

#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <vector>

// FCD-07 uses the real Fast DDS selector and its bounded index containers.
// The caller supplies an explicitly initialized HostModel with these two routes.
// No native connection, generation, queue pump, or participant is created here.
inline void RunFacadeSelectorCases(
fastdds_dsoftbus::dsoftbus::DSoftBusTransport& facade,
const fastdds_dsoftbus::dsoftbus::Locator& remote10,
const fastdds_dsoftbus::dsoftbus::Locator& remote11)
{
using fastdds_dsoftbus::dsoftbus::Locator;
    using eprosima::fastdds::ResourceLimitedContainerConfig;
    using eprosima::fastdds::rtps::LocatorSelector;
    using eprosima::fastdds::rtps::LocatorSelectorEntry;

    const auto require = [](bool condition, const char* message)
            {
                if (!condition)
                {
                    throw std::runtime_error(message);
                }
            };
    const auto add_unicast = [&require](
            LocatorSelectorEntry& entry, std::initializer_list<Locator> locators)
            {
                for (const auto& locator : locators)
                {
                    require(entry.unicast.push_back(locator) != nullptr,
                            "FCD-07 fixture unicast capacity");
                }
            };
    const auto selected_values = [](const LocatorSelector& selector)
            {
                std::vector<Locator> values;
                selector.for_each([&values](const Locator& locator)
                        {
                            values.push_back(locator);
                        });
                return values;
            };

    require(remote10 != remote11 && facade.is_locator_allowed(remote10) &&
            facade.is_locator_allowed(remote11), "FCD-07 routed input precondition");
    Locator other_kind = remote10;
    other_kind.kind = LOCATOR_KIND_UDPv4;
    other_kind.port = 7400;
    Locator malformed = remote10;
    malformed.port = 0;
    Locator unowned = remote10;
    unowned.address[15] ^= 0x40;
    require(facade.IsLocatorSupported(unowned) && !facade.is_locator_allowed(unowned),
            "FCD-07 unowned canonical input precondition");

    {
        LocatorSelectorEntry disabled(1, 0);
        LocatorSelectorEntry disallowed(1, 0);
        LocatorSelectorEntry mixed(6, 0);
        LocatorSelectorEntry repeated(2, 0);
        LocatorSelectorEntry multicast_only(0, 1);
        add_unicast(disabled, {remote10});
        add_unicast(disallowed, {remote11});
        add_unicast(mixed, {other_kind, remote10, remote10, malformed, unowned, remote11});
        add_unicast(repeated, {remote10, remote11});
        require(multicast_only.multicast.push_back(remote10) != nullptr,
                "FCD-07 fixture multicast capacity");

        // Set actual public flags, without inventing a GUID or enabling a
        // disabled entry through a mock selection implementation.
        disabled.enabled = false;
        disallowed.enabled = true;
        disallowed.allowed_to_send = false;
        mixed.enabled = true;
        repeated.enabled = true;
        multicast_only.enabled = true;

        LocatorSelector selector(ResourceLimitedContainerConfig::fixed_size_configuration(5));
        require(selector.add_entry(&disabled), "FCD-07 add disabled entry");
        require(selector.add_entry(&disallowed), "FCD-07 add disallowed entry");
        require(selector.add_entry(&mixed), "FCD-07 add mixed entry");
        require(selector.add_entry(&repeated), "FCD-07 add repeated entry");
        require(selector.add_entry(&multicast_only), "FCD-07 add multicast entry");
        selector.selection_start();
        require(mixed.state.unicast.push_back(0) != nullptr,
                "FCD-07 seed earlier transport selection");
        selector.select(2);
        const std::vector<Locator> advertised(mixed.unicast.begin(), mixed.unicast.end());
        const std::vector<Locator> expected{other_kind, remote10, remote11};

        facade.select_locators(selector);
        require(!disabled.enabled && disabled.state.unicast.empty(),
                "FCD-07 disabled entry must remain disabled and unselected");
        require(disallowed.enabled && !disallowed.allowed_to_send &&
                disallowed.state.unicast.empty(),
                "FCD-07 allowed_to_send false must remain unselected");
        require(mixed.state.unicast.size() == 3 && mixed.state.unicast[0] == 0 &&
                mixed.state.unicast[1] == 1 && mixed.state.unicast[2] == 5,
                "FCD-07 preserve prior index and append first owned indexes only");
        require(repeated.state.unicast.empty(),
                "FCD-07 same owned remotes in later entries must not duplicate transmission");
        require(multicast_only.state.multicast.empty() && multicast_only.state.unicast.empty(),
                "FCD-07 multicast-only project locator must not be selected");
        require(selector.selected_size() == 3 && selected_values(selector) == expected,
                "FCD-07 exact mixed-transport selection and order");
        require(!selector.is_selected(malformed) && !selector.is_selected(unowned),
                "FCD-07 malformed and unowned locators must be ignored");
        require(std::vector<Locator>(mixed.unicast.begin(), mixed.unicast.end()) == advertised &&
                multicast_only.multicast.size() == 1 && multicast_only.multicast[0] == remote10,
                "FCD-07 advertised locators must remain unchanged");

        facade.select_locators(selector);
        require(selector.selected_size() == 3 && selected_values(selector) == expected &&
                mixed.state.unicast.size() == 3 && repeated.state.unicast.empty(),
                "FCD-07 repeated transport selection must not reset or duplicate indexes");
    }

    {
        LocatorSelectorEntry first(2, 0);
        LocatorSelectorEntry second(3, 0);
        add_unicast(first, {remote11, remote11});
        add_unicast(second, {remote11, remote10, remote10});
        first.enabled = true;
        second.enabled = true;
        LocatorSelector selector(ResourceLimitedContainerConfig::fixed_size_configuration(2));
        require(selector.add_entry(&first) && selector.add_entry(&second),
                "FCD-07 add first-appearance entries");
        selector.selection_start();
        facade.select_locators(selector);
        require(first.state.unicast.size() == 1 && first.state.unicast[0] == 0 &&
                second.state.unicast.size() == 1 && second.state.unicast[0] == 1,
                "FCD-07 deduplicate before and after entry publication");
        require(selector.selected_size() == 2 &&
                selected_values(selector) == std::vector<Locator>{remote11, remote10},
                "FCD-07 first appearance determines transmission order, not route order");
    }

    {
        LocatorSelectorEntry non_targets(3, 1);
        add_unicast(non_targets, {other_kind, malformed, unowned});
        require(non_targets.multicast.push_back(remote11) != nullptr,
                "FCD-07 add project multicast-only target");
        non_targets.enabled = true;
        LocatorSelector selector(ResourceLimitedContainerConfig::fixed_size_configuration(1));
        require(selector.add_entry(&non_targets), "FCD-07 add no-owned entry");
        selector.selection_start();
        facade.select_locators(selector);
        require(non_targets.state.unicast.empty() && non_targets.state.multicast.empty() &&
                selector.selected_size() == 0 && selected_values(selector).empty(),
                "FCD-07 no owned unicast means no entry publication");
    }

    {
        LocatorSelectorEntry saturated(2, 0);
        LocatorSelectorEntry eligible(1, 0);
        add_unicast(saturated, {other_kind, remote10});
        add_unicast(eligible, {remote10});
        saturated.enabled = true;
        eligible.enabled = true;
        LocatorSelector selector(ResourceLimitedContainerConfig::fixed_size_configuration(2));
        require(selector.add_entry(&saturated) && selector.add_entry(&eligible),
                "FCD-07 add fixed-capacity entries");
        selector.selection_start();

        // Deliberately saturated public state: repeated indexes are all valid,
        // but no entry has been published in the selector. This supplies a real
        // fixed-capacity append failure without changing Fast DDS containers,
        // replacing them with mocks, or injecting an allocation failure.
        const auto capacity = saturated.state.unicast.capacity();
        require(capacity != 0, "FCD-07 nonzero fixed state capacity");
        for (std::size_t i = 0; i < capacity; ++i)
        {
            require(saturated.state.unicast.push_back(0) != nullptr,
                    "FCD-07 saturate state with valid other-kind index");
        }
        require(saturated.state.unicast.push_back(1) == nullptr,
                "FCD-07 actual fixed-capacity append must fail");

        facade.select_locators(selector);
        require(saturated.state.unicast.size() == capacity,
                "FCD-07 failed append must preserve full state");
        for (const auto index : saturated.state.unicast)
        {
            require(index == 0, "FCD-07 failed append must not rewrite earlier indexes");
        }
        require(!selector.is_selected(other_kind) && selector.is_selected(remote10) &&
                selector.selected_size() == 1 && eligible.state.unicast.size() == 1 &&
                eligible.state.unicast[0] == 0,
                "FCD-07 failed append must not publish entry or suppress later eligible target");
        require(selected_values(selector) == std::vector<Locator>{remote10},
                "FCD-07 only successfully appended target may be transmitted");
    }
}

#endif // FASTDDS_DSOFTBUS_TESTS_TRANSPORT_FACADE_SELECTOR_CASES_HPP
