#include <mdds/dsoftbus/V1Address.hpp>
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace mdds::dsoftbus;
namespace v1 = mdds::dsoftbus::v1;
#define CHECK(c) do { if (!(c)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #c); } while(false)

template<std::size_t N> std::string Hex(const std::array<std::uint8_t, N>& value)
{
    const char* digits = "0123456789abcdef";
    std::string result;
    for (const auto byte : value) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}

int main()
{
    std::vector<std::pair<std::string, std::function<void()>>> cases;
    const v1::DeviceIdentity identity{"alpha", 0x01020304, "udid-1"};
    cases.emplace_back("V1-01-independent-sha256-vectors", [] {
        // Independent .NET SHA256 results, not values computed by the codec.
        CHECK(Hex(v1::detail::Sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        CHECK(Hex(v1::detail::Sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        CHECK(Hex(v1::detail::Sha256(std::string(1000, 'a'))) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
        CHECK(Hex(v1::detail::Sha256(std::string(55, 'a'))) == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
        CHECK(Hex(v1::detail::Sha256(std::string(56, 'a'))) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
        CHECK(Hex(v1::detail::Sha256(std::string(63, 'a'))) == "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
        CHECK(Hex(v1::detail::Sha256(std::string(64, 'a'))) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    });
    cases.emplace_back("V1-02-canonical-identity-vectors", [&] {
        EndpointId key{};
        CHECK(v1::AddressCodec::DeriveKey(identity, key));
        CHECK(Hex(key) == "c3797784976ed68cef55b2f0f22e8cc4");
        CHECK(v1::AddressCodec::DeriveKey({std::string(116, 'n'), 0xffffffff, std::string(64, 'u')}, key));
        CHECK(Hex(key) == "663732be9a65ebd1d02726201cc132b6");
    });
    cases.emplace_back("V1-03-identity-validation-and-field-separation", [&] {
        EndpointId key{}; key.fill(0x5a);
        for (const auto& invalid : std::vector<v1::DeviceIdentity>{
            {"", 0, "u"}, {std::string(117, 'n'), 0, "u"}, {std::string("a\0b", 3), 0, "u"},
            {"n", 0, ""}, {"n", 0, std::string(65, 'u')}, {"n", 0, std::string("u\0d", 3)}}) {
            CHECK(!v1::AddressCodec::ValidIdentity(invalid));
            CHECK(!v1::AddressCodec::DeriveKey(invalid, key));
            CHECK(std::all_of(key.begin(), key.end(), [](auto b) { return b == 0x5a; }));
        }
        std::vector<EndpointId> keys;
        for (const auto& value : std::vector<v1::DeviceIdentity>{identity, {"Alpha", identity.domain, identity.udid},
            {identity.name_space, 0x04030201, identity.udid}, {identity.name_space, identity.domain, "udid-2"},
            {"a", 0, "bc"}, {"ab", 0, "c"}, {std::string("\x80\xff",2), 0, std::string("\xfe\x01",2)}}) {
            CHECK(v1::AddressCodec::DeriveKey(value, key));
            CHECK(std::find(keys.begin(), keys.end(), key) == keys.end()); keys.push_back(key);
        }
    });
    cases.emplace_back("V1-04-locator-wire-and-all-digest-bytes", [] {
        for (const auto profile : {v1::Profile::BytesNative, v1::Profile::RawPlain, v1::Profile::RawEncrypted}) {
            for (const auto slot : {0u, 1u, 0x00fffffeu}) {
                for (const unsigned pattern : {0u, 1u, 255u}) {
                    v1::EndpointAddress address{}; address.slot = slot; address.profile = profile;
                    address.device_key[0] = static_cast<std::uint8_t>(pattern == 255 ? 255 : 0);
                    address.device_key[15] = static_cast<std::uint8_t>(pattern == 1 ? 1 : 0);
                    Locator encoded;
                    CHECK(v1::AddressCodec::EncodeLocator(address, encoded));
                    CHECK(encoded.kind == 0x40000000);
                    CHECK(encoded.port == ((static_cast<std::uint32_t>(profile) << 24) | (slot + 1)));
                    CHECK(std::equal(address.device_key.begin(), address.device_key.end(), encoded.address));
                    v1::EndpointAddress decoded;
                    CHECK(v1::AddressCodec::DecodeLocator(encoded, decoded));
                    CHECK(decoded.slot == slot && decoded.profile == profile && decoded.device_key == address.device_key);
                    CHECK(v1::AddressCodec::Supports(encoded, profile));
                    CHECK(!v1::AddressCodec::Supports(encoded, profile == v1::Profile::RawPlain ? v1::Profile::BytesNative : v1::Profile::RawPlain));
                }
            }
        }
    });
    cases.emplace_back("V1-05-forged-locators-rejected-transactionally", [] {
        v1::EndpointAddress result{}; result.slot = 17;
        for (const auto port : {0u, 1u, 0x01000000u, 0x02000000u, 0x03000000u, 0x04000001u, 0xff000001u}) {
            const Locator forged(0x40000000, port);
            CHECK(!v1::AddressCodec::DecodeLocator(forged, result)); CHECK(result.slot == 17);
        }
        for (const auto kind : {-1, 0, 1, 2, 4, 8, 16, 0x40000001}) {
            CHECK(!v1::AddressCodec::DecodeLocator(Locator(kind, 0x01000001), result)); CHECK(result.slot == 17);
        }
        for (const auto slot : {0x00ffffffu, 0x01000000u, 0xffffffffu}) {
            Locator output(7, 29);
            CHECK(!v1::AddressCodec::EncodeLocator({{}, slot, v1::Profile::BytesNative}, output));
            CHECK(output.kind == 7 && output.port == 29);
        }
        for (const auto invalid : {0u, 4u, 255u}) {
            Locator output(7,29);
            CHECK(!v1::AddressCodec::EncodeLocator({{}, 0, static_cast<v1::Profile>(invalid)}, output));
            CHECK(output.kind == 7 && output.port == 29);
            CHECK(!v1::AddressCodec::Supports(Locator(0x40000000, 0x01000001), static_cast<v1::Profile>(invalid)));
        }
    });
    cases.emplace_back("V1-06-canonical-service-name-boundaries", [] {
        std::string name;
        CHECK(v1::AddressCodec::EncodeServiceName({"alpha", 0x01020304, 0, v1::Profile::BytesNative}, name));
        CHECK(name == "md1.616c706861.01020304.000000.01");
        for (const auto profile : {v1::Profile::BytesNative, v1::Profile::RawPlain, v1::Profile::RawEncrypted}) {
            const v1::ServiceAddress expected{std::string(116, 'n'), 0xffffffff, 0xfffffe, profile};
            CHECK(v1::AddressCodec::EncodeServiceName(expected, name)); CHECK(name.size() == 255);
            v1::ServiceAddress actual;
            CHECK(v1::AddressCodec::DecodeServiceName(name, actual));
            CHECK(actual.name_space == expected.name_space && actual.domain == expected.domain);
            CHECK(actual.slot == expected.slot && actual.profile == expected.profile);
        }
        CHECK(v1::AddressCodec::EncodeServiceName({std::string("\x80\xff",2), 0, 0, v1::Profile::RawPlain}, name));
        CHECK(name == "md1.80ff.00000000.000000.02");
    });
    cases.emplace_back("V1-07-service-parser-noncanonical-and-oversize", [] {
        for (const auto& invalid : std::vector<std::string>{"", "md2.61.00000000.000000.01", "md1..00000000.000000.01",
            "md1.6.00000000.000000.01", "md1.6A.00000000.000000.01", "md1.00.00000000.000000.01",
            "md1.61.0000000.000000.01", "md1.61.000000000.000000.01", "md1.61.ffffffff.ffffff.01",
            "md1.61.00000000.000000.00", "md1.61.00000000.000000.04", "md1.61.00000000.000000.01.",
            "md1.61.00000000.-00001.01", "md1.61.00000000.000000.1", std::string(256, 'a')}) {
            v1::ServiceAddress output{"sentinel", 7, 17, v1::Profile::RawEncrypted};
            CHECK(!v1::AddressCodec::DecodeServiceName(invalid, output));
            CHECK(output.name_space == "sentinel" && output.domain == 7 && output.slot == 17 && output.profile == v1::Profile::RawEncrypted);
        }
        for (const auto& invalid : std::vector<v1::ServiceAddress>{{"",0,0,v1::Profile::BytesNative},
            {std::string(117, 'n'),0,0,v1::Profile::BytesNative}, {std::string("a\0b",3),0,0,v1::Profile::BytesNative},
            {"n",0,0xffffff,v1::Profile::BytesNative}, {"n",0,0,static_cast<v1::Profile>(0)}}) {
            std::string output = "sentinel";
            CHECK(!v1::AddressCodec::EncodeServiceName(invalid, output)); CHECK(output == "sentinel");
        }
    });
    cases.emplace_back("V1-08-route-stability-offline-and-stale-event", [&] {
        v1::DeviceDirectory directory(2); EndpointId key{};
        CHECK(v1::AddressCodec::DeriveKey(identity, key));
        CHECK(directory.Online(identity, "network-a") == v1::DirectoryStatus::Ok);
        const auto old_copy = directory.Resolve(key);
        CHECK(old_copy.route && old_copy.route->network_id == "network-a");
        CHECK(directory.Online(identity, "network-b") == v1::DirectoryStatus::Ok);
        CHECK(directory.size() == 1 && old_copy.route->network_id == "network-a");
        CHECK(directory.Offline(identity, "network-a") == v1::DirectoryStatus::StaleRoute);
        CHECK(directory.Resolve(key).route->network_id == "network-b");
        CHECK(directory.Offline(identity, "network-b") == v1::DirectoryStatus::Ok);
        const auto offline = directory.Resolve(key);
        CHECK(offline.status == v1::DirectoryStatus::Offline && !offline.route);
        CHECK(directory.Online(identity, "network-c") == v1::DirectoryStatus::Ok);
        CHECK(directory.Resolve(key).route->network_id == "network-c");
    });
    cases.emplace_back("V1-09-collision-poisons-both-identities", [&] {
        const auto collide = [](const v1::DeviceIdentity&, EndpointId& output) { output.fill(7); return true; };
        v1::DeviceDirectory directory(1, collide); EndpointId key{}; key.fill(7);
        CHECK(directory.Online(identity, "network-a") == v1::DirectoryStatus::Ok);
        auto different = identity; different.udid = "other-udid";
        CHECK(directory.Online(different, "network-b") == v1::DirectoryStatus::Collision);
        CHECK(directory.size() == 1);
        CHECK(directory.Resolve(key).status == v1::DirectoryStatus::Collision && !directory.Resolve(key).route);
        CHECK(directory.Online(identity, "network-c") == v1::DirectoryStatus::Collision);
        CHECK(directory.Offline(identity, "network-a") == v1::DirectoryStatus::Collision);
    });
    cases.emplace_back("V1-10-device-budget-domain-separation-and-invalid-routes", [&] {
        v1::DeviceDirectory zero(0); CHECK(zero.Online(identity, "net") == v1::DirectoryStatus::Capacity);
        v1::DeviceDirectory directory(2);
        for (const auto& invalid : {std::string(), std::string(65, 'a'), std::string("n\0x",3)}) {
            CHECK(directory.Online(identity, invalid) == v1::DirectoryStatus::InvalidNetworkId);
            CHECK(directory.Offline(identity, invalid) == v1::DirectoryStatus::InvalidNetworkId);
        }
        CHECK(directory.Online({"", 0, "u"}, "net") == v1::DirectoryStatus::InvalidIdentity);
        CHECK(directory.size() == 0);
        auto other_domain = identity; ++other_domain.domain;
        CHECK(directory.Online(identity, std::string(64, 'a')) == v1::DirectoryStatus::Ok);
        CHECK(directory.Online(other_domain, "net-b") == v1::DirectoryStatus::Ok);
        auto third = identity; third.udid = "another";
        CHECK(directory.Online(third, "net-c") == v1::DirectoryStatus::Capacity);
        EndpointId first{}, second{}, missing{};
        CHECK(v1::AddressCodec::DeriveKey(identity, first) && v1::AddressCodec::DeriveKey(other_domain, second));
        CHECK(first != second && directory.Resolve(first).route->identity.domain == identity.domain);
        CHECK(directory.Resolve(second).route->identity.domain == other_domain.domain);
        CHECK(directory.Resolve(missing).status == v1::DirectoryStatus::NotFound);
    });
    cases.emplace_back("V1-11-bounded-deterministic-parser-fuzz", [] {
        // Generated arbitrary bytes and single-byte mutations of canonical input.
        // Independent length/character rejection tests above pin the grammar;
        // this loop checks canonical acceptance and transactional rejection.
        std::uint32_t state = 0x734a12u;
        const auto random = [&] { state = state * 1664525u + 1013904223u; return state; };
        const std::string seed = "md1.616c706861.01020304.000000.01";
        for (unsigned iteration = 0; iteration != 10000; ++iteration) {
            std::string candidate;
            if (iteration % 2 == 0) {
                candidate = seed;
                candidate[random() % candidate.size()] = static_cast<char>(random() >> 24);
            } else {
                candidate.resize(random() % 513);
                for (auto& c : candidate) c = static_cast<char>(random() >> 24);
            }
            v1::ServiceAddress decoded{"sentinel", 7, 17, v1::Profile::RawEncrypted};
            if (v1::AddressCodec::DecodeServiceName(candidate, decoded)) {
                std::string canonical;
                CHECK(v1::AddressCodec::EncodeServiceName(decoded, canonical));
                CHECK(canonical == candidate && canonical.size() <= 255);
            } else {
                CHECK(decoded.name_space == "sentinel" && decoded.domain == 7 && decoded.slot == 17);
                CHECK(decoded.profile == v1::Profile::RawEncrypted);
            }
        }
    });
    std::size_t failures = 0;
    for (const auto& test : cases) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << test.first << ": " << e.what() << '\n'; }
    }
    std::cout << "cases=" << cases.size() << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
