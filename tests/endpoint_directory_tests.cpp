#include <mdds/dsoftbus/EndpointDirectory.hpp>

#include <algorithm>
#include <atomic>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace mdds::dsoftbus;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

int RunEndpointDirectoryTests(int argc, char** argv)
{
    // Fixtures deliberately construct valid Locator bytes independently of the
    // codec: directory tests must not depend on Encode to prepare their input.
    const auto locator = [](std::uint8_t id) {
        Locator result(LocatorCodec::kind, LocatorCodec::port);
        std::fill(std::begin(result.address), std::end(result.address), 0);
        result.address[15] = id;
        return result;
    };
    const auto configuration = [&] {
        DirectoryConfiguration c;
        c.locals.push_back({locator(2), "namespace-a", "a.listen"});
        Route r;
        r.key = {locator(2), locator(3)};
        r.fields = {"network-b", "b.listen", "a.dial.b", {"b.dial.a"}, RecordDataType::Bytes};
        c.routes.push_back(r);
        return c;
    };
    const auto observed = [] {
        return ObservedPeerKey{"namespace-a", "network-b", "b.dial.a", RecordDataType::Bytes};
    };
    const auto build = [](const DirectoryConfiguration& c) {
        const auto result = EndpointDirectory::Build(c);
        CHECK(result.value.has_value());
        CHECK(result.issue.code == DirectoryError::None);
        return *result.value;
    };
    const auto rejects = [](const DirectoryConfiguration& c, DirectoryError error) {
        const auto result = EndpointDirectory::Build(c);
        CHECK(!result.value.has_value());
        CHECK(result.issue.code == error);
    };
    std::vector<std::pair<std::string, std::function<void()>>> cases;

    cases.emplace_back("LC-01", [&] {
        EndpointId endpoint{};
        endpoint[15] = 2;
        for (const auto first : {0u, 0xfeu}) {
            endpoint[0] = static_cast<std::uint8_t>(first);
            endpoint[7] = 0xff;
            Locator encoded;
            CHECK(LocatorCodec::Encode(endpoint, encoded));
            CHECK(encoded.kind == 0x40000000 && encoded.port == 1);
            CHECK(std::equal(endpoint.begin(), endpoint.end(), encoded.address));
            EndpointId decoded{};
            CHECK(LocatorCodec::Decode(encoded, decoded));
            CHECK(decoded == endpoint);
            CHECK(LocatorCodec::IsValid(encoded));
        }
        for (std::size_t i = 0; i < endpoint.size(); ++i) {
            auto different = endpoint;
            different[i] ^= i == 0 ? 2 : 1;
            Locator encoded;
            CHECK(LocatorCodec::Encode(different, encoded));
            EndpointId decoded{};
            CHECK(LocatorCodec::Decode(encoded, decoded) && decoded == different);
            CHECK(decoded != endpoint);
        }
    });

    cases.emplace_back("LC-02", [&] {
        for (int invalid = 0; invalid < 3; ++invalid) {
            EndpointId endpoint{};
            if (invalid == 1) endpoint[15] = 1;
            if (invalid == 2) { endpoint[0] = 0xff; endpoint[15] = 2; }
            auto sentinel = locator(77);
            CHECK(!LocatorCodec::Encode(endpoint, sentinel));
            CHECK(sentinel == locator(77));
            auto malformed = locator(2);
            std::copy(endpoint.begin(), endpoint.end(), malformed.address);
            EndpointId output{};
            output.fill(17);
            const auto before = output;
            CHECK(!LocatorCodec::Decode(malformed, output));
            CHECK(output == before);
            CHECK(!LocatorCodec::IsValid(malformed));
        }
        for (int invalid = 0; invalid < 5; ++invalid) {
            auto malformed = locator(2);
            if (invalid == 0) malformed.kind = 1;
            if (invalid == 1) malformed.kind = 0x40000001;
            if (invalid == 2) malformed.kind = -1;
            if (invalid == 3) malformed.port = 0;
            if (invalid == 4) malformed.port = 2;
            EndpointId output{};
            output.fill(19);
            const auto before = output;
            CHECK(!LocatorCodec::Decode(malformed, output));
            CHECK(output == before);
        }
        CHECK(LocatorCodec::IsValid(locator(2)));
    });

    cases.emplace_back("ED-01", [&] {
        auto c = configuration();
        auto d = build(c);
        auto local = d->FindLocal(locator(2));
        CHECK(local.value && local.value->local_listen_name == "a.listen");
        auto forward = d->ResolveOutbound(c.routes[0].key);
        auto reverse = d->ResolveInbound(observed());
        CHECK(forward.value && reverse.value);
        CHECK(forward.value->key.local_locator == reverse.value->key.local_locator);
        CHECK(forward.value->key.remote_locator == reverse.value->key.remote_locator);
        CHECK(forward.value->fields.remote_listen_name == "b.listen");
        CHECK(forward.value->route_version == 1);
        auto wrong = observed();
        wrong.actual_remote_socket_name = "a.listen";
        CHECK(!d->ResolveInbound(wrong).value);
        wrong.actual_remote_socket_name = "b.listen";
        CHECK(!d->ResolveInbound(wrong).value);
    });

    cases.emplace_back("ED-02", [&] {
        const auto d = build(configuration());
        CHECK(d->FindLocal(locator(90)).issue.code == DirectoryError::NotFound);
        CHECK(d->FindLocal(locator(0)).issue.code == DirectoryError::InvalidLocator);
        CHECK(d->ResolveOutbound({locator(2), locator(90)}).issue.code == DirectoryError::NotFound);
        CHECK(d->ResolveOutbound({locator(0), locator(3)}).issue.code == DirectoryError::InvalidLocator);
        CHECK(d->ResolveOutbound({locator(2), locator(1)}).issue.code == DirectoryError::InvalidLocator);
        auto key = observed();
        key.callback_namespace = "absent";
        CHECK(d->ResolveInbound(key).issue.code == DirectoryError::NotFound);
        key.callback_namespace = "";
        CHECK(d->ResolveInbound(key).issue.code == DirectoryError::InvalidString);
        key = observed();
        key.actual_data_type = RecordDataType::Unsupported;
        CHECK(d->ResolveInbound(key).issue.code == DirectoryError::UnsupportedDataType);
        CHECK(d->RoutesTo(locator(90)).issue.code == DirectoryError::NotFound);
        CHECK(d->RoutesTo(locator(0)).issue.code == DirectoryError::InvalidLocator);
        CHECK(build({})->FindLocal(locator(2)).issue.code == DirectoryError::NotFound);
    });

    cases.emplace_back("ED-03", [&] {
        auto c = configuration();
        c.routes.push_back(c.routes[0]);
        rejects(c, DirectoryError::DuplicateRoute);
        c.routes[1].key.remote_locator = locator(4);
        rejects(c, DirectoryError::ReverseConflict);
        auto result = EndpointDirectory::Build(c);
        CHECK(result.issue.entry == 1 && result.issue.conflicting_entry == 0);
        std::reverse(c.routes.begin(), c.routes.end());
        rejects(c, DirectoryError::ReverseConflict);
        c = configuration();
        c.routes[0].fields.expected_remote_initiator_names.push_back("b.alias");
        auto d = build(c);
        auto key = observed();
        key.actual_remote_socket_name = "b.alias";
        CHECK(d->ResolveInbound(key).value->key.remote_locator == locator(3));
        c.routes[0].fields.expected_remote_initiator_names.push_back("b.alias");
        rejects(c, DirectoryError::DuplicateAlias);
    });

    cases.emplace_back("ED-04", [&] {
        auto c = configuration();
        c.locals.push_back(c.locals[0]);
        rejects(c, DirectoryError::DuplicateLocal);
        c = configuration(); c.locals[0].local_locator = locator(0);
        rejects(c, DirectoryError::InvalidLocator);
        c = configuration(); c.routes[0].key.remote_locator = locator(1);
        rejects(c, DirectoryError::InvalidLocator);
        c = configuration(); c.routes[0].key.local_locator = locator(8);
        rejects(c, DirectoryError::MissingLocal);
        c = configuration(); c.routes[0].route_version = 0;
        rejects(c, DirectoryError::InvalidVersion);
        c = configuration(); c.routes[0].fields.data_type = RecordDataType::Unsupported;
        rejects(c, DirectoryError::UnsupportedDataType);
    });

    cases.emplace_back("ED-05", [&] {
        auto c = configuration();
        c.locals.push_back({locator(4), "namespace-c", "a.listen"});
        rejects(c, DirectoryError::NamespaceConflict);
        c.locals[1].local_listen_name = "c.listen";
        auto r = c.routes[0];
        r.key.local_locator = locator(4);
        c.routes.push_back(r);
        rejects(c, DirectoryError::NamespaceConflict); // same initiator slot
        c.routes[1].fields.local_initiator_name = "c.dial.b";
        auto d = build(c); // same observed peer, genuinely distinct namespaces
        auto key = observed(); key.callback_namespace = "namespace-c";
        CHECK(d->ResolveInbound(key).value->key.local_locator == locator(4));
        c.locals[1].callback_namespace = "namespace-a";
        rejects(c, DirectoryError::ReverseConflict); // shared dispatcher scope
        c.routes[1].fields.expected_remote_initiator_names = {"b.dial.c"};
        CHECK(build(c)->ResolveInbound(observed()).value.has_value());
        c.locals[1].callback_namespace = "namespace-c";
        c.locals[1].local_listen_name = "a.dial.b"; // different role, allowed
        CHECK(build(c)->FindLocal(locator(4)).value.has_value());
    });

    cases.emplace_back("ED-06", [&] {
        for (int field = 0; field < 6; ++field) {
            for (int bad = 0; bad < 3; ++bad) {
                auto c = configuration();
                std::string* p = nullptr;
                if (field == 0) p = &c.locals[0].callback_namespace;
                if (field == 1) p = &c.locals[0].local_listen_name;
                if (field == 2) p = &c.routes[0].fields.remote_network_id;
                if (field == 3) p = &c.routes[0].fields.remote_listen_name;
                if (field == 4) p = &c.routes[0].fields.local_initiator_name;
                if (field == 5) p = &c.routes[0].fields.expected_remote_initiator_names[0];
                const std::size_t max = field == 2 ? 64 : 255;
                *p = bad == 0 ? "" : bad == 1 ? std::string(max + 1, 'x') : std::string("x\0y", 3);
                rejects(c, DirectoryError::InvalidString);
                *p = std::string(max, 'x');
                CHECK(EndpointDirectory::Build(c).value.has_value());
            }
        }
        auto c = configuration(); c.routes[0].fields.expected_remote_initiator_names.clear();
        rejects(c, DirectoryError::InvalidString);
        c = configuration(); c.routes[0].fields.remote_network_id = " x ";
        auto d = build(c);
        auto key = observed(); key.remote_network_id = "x";
        CHECK(!d->ResolveInbound(key).value);
        key.remote_network_id = " x "; CHECK(d->ResolveInbound(key).value.has_value());
        key.actual_remote_socket_name = "B.DIAL.A"; CHECK(!d->ResolveInbound(key).value);
        key = observed(); key.remote_network_id = std::string(65, 'x');
        CHECK(d->ResolveInbound(key).issue.code == DirectoryError::InvalidString);
        key = observed(); key.actual_remote_socket_name = std::string("b\0x", 3);
        CHECK(d->ResolveInbound(key).issue.code == DirectoryError::InvalidString);
    });

    cases.emplace_back("ED-07", [&] {
        auto c = configuration();
        auto d = build(c);
        c.routes[0].fields.remote_network_id = "modified";
        c.routes[0].key.remote_locator.address[4] = 9;
        c.locals[0].callback_namespace = "modified";
        auto result = d->ResolveInbound(observed());
        CHECK(result.value && result.value->fields.remote_network_id == "network-b");
        result.value->fields.expected_remote_initiator_names[0] = "modified";
        result.value->key.local_locator = locator(9);
        CHECK(d->ResolveInbound(observed()).value->key.local_locator == locator(2));
        auto local = d->FindLocal(locator(2));
        local.value->callback_namespace = "modified";
        CHECK(d->FindLocal(locator(2)).value->callback_namespace == "namespace-a");
    });

    cases.emplace_back("ED-08", [&] {
        auto c = configuration();
        auto old = build(c);
        auto fields = c.routes[0].fields;
        fields.remote_network_id = "network-b-new";
        fields.remote_listen_name = "b.listen.new";
        fields.local_initiator_name = "a.dial.b.new";
        fields.expected_remote_initiator_names = {"b.dial.a.new"};
        auto update = old->WithUpdatedRoute(c.routes[0].key, 1, fields);
        CHECK(update.value.has_value());
        auto fresh = *update.value;
        auto key = observed(); key.remote_network_id = "network-b-new";
        key.actual_remote_socket_name = "b.dial.a.new";
        CHECK(fresh->ResolveInbound(key).value->route_version == 2);
        CHECK(fresh->ResolveInbound(key).value->key.remote_locator == locator(3));
        CHECK(!fresh->ResolveInbound(observed()).value);
        CHECK(old->ResolveInbound(observed()).value->route_version == 1);
        CHECK(!old->ResolveInbound(key).value);
        fields.remote_network_id = "modified";
        CHECK(fresh->ResolveInbound(key).value.has_value());
        CHECK(fresh->WithUpdatedRoute(c.routes[0].key, 1, fields).issue.code == DirectoryError::StaleVersion);
    });

    cases.emplace_back("ED-09", [&] {
        auto c = configuration();
        auto other = c.routes[0]; other.key.remote_locator = locator(4);
        other.fields.expected_remote_initiator_names = {"other.dial.a"};
        c.routes.push_back(other);
        auto d = build(c);
        CHECK(d->WithUpdatedRoute(c.routes[0].key, 1, other.fields).issue.code == DirectoryError::ReverseConflict);
        CHECK(d->ResolveInbound(observed()).value->route_version == 1);
        CHECK(d->WithUpdatedRoute({locator(2), locator(80)}, 1, other.fields).issue.code == DirectoryError::NotFound);
        CHECK(d->WithUpdatedRoute({locator(0), locator(3)}, 1, other.fields).issue.code == DirectoryError::InvalidLocator);
        CHECK(d->WithUpdatedRoute(c.routes[0].key, 0, other.fields).issue.code == DirectoryError::StaleVersion);
        auto bad = other.fields; bad.remote_network_id.clear();
        CHECK(d->WithUpdatedRoute(c.routes[0].key, 1, bad).issue.code == DirectoryError::InvalidString);
        c = configuration(); c.routes[0].route_version = (std::numeric_limits<std::uint64_t>::max)();
        d = build(c);
        CHECK(d->WithUpdatedRoute(c.routes[0].key, c.routes[0].route_version, c.routes[0].fields).issue.code == DirectoryError::VersionExhausted);
        CHECK(d->ResolveInbound(observed()).value->route_version == c.routes[0].route_version);
    });

    cases.emplace_back("ED-10", [&] {
        auto c = configuration();
        c.locals.push_back({locator(4), "namespace-c", "c.listen"});
        auto second = c.routes[0]; second.key.local_locator = locator(4);
        second.fields.local_initiator_name = "c.dial.b";
        c.routes.push_back(second);
        const auto d = build(c);
        auto matches = d->RoutesTo(locator(3));
        CHECK(matches.value && matches.value->size() == 2);
        CHECK(matches.value->at(0).key.local_locator == locator(2));
        CHECK(matches.value->at(1).key.local_locator == locator(4));
        matches.value->clear();
        CHECK(d->RoutesTo(locator(3)).value->size() == 2);
    });

    cases.emplace_back("ED-11", [&] {
        auto d = build(configuration());
        std::atomic<int> errors{0};
        std::vector<std::thread> readers;
        for (int n = 0; n < 8; ++n) {
            readers.emplace_back([&] {
                for (int i = 0; i < 500; ++i) {
                    const auto a = d->ResolveInbound(observed());
                    const auto b = d->ResolveOutbound({locator(2), locator(3)});
                    if (!a.value || !b.value || a.value->route_version != 1 ||
                            b.value->fields.remote_network_id != "network-b") ++errors;
                }
            });
        }
        for (auto& thread : readers) thread.join();
        CHECK(errors == 0);
    });

    if (argc > 2) return 64;
    int failures = 0;
    int executed = 0;
    for (const auto& test : cases) {
        if (argc == 2 && test.first != argv[1]) continue;
        ++executed;
        try { test.second(); std::cout << test.first << " PASS\n"; }
        catch (const std::exception& error) {
            ++failures;
            std::cout << test.first << " FAIL " << error.what() << '\n';
        }
    }
    if (executed == 0) return 64;
    std::cout << "S3-A directory cases=" << executed << " failures=" << failures
              << " (host model only; no Socket/discovery evidence)\n";
    return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
    return RunEndpointDirectoryTests(argc, argv);
}
