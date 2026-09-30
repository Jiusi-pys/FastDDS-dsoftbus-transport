#include "softbus_probe_profile.hpp"

namespace fastdds_dsoftbus_probe {
bool ParseProbeProfile(std::string_view text, ProbeProfile& result) noexcept
{
    if (text == "bytes") result = ProbeProfile::Bytes;
    else if (text == "raw") result = ProbeProfile::Raw;
    else if (text == "raw-encrypted") result = ProbeProfile::RawEncrypted;
    else return false;
    return true;
}

const ProbeProfileDescription* DescribeProbeProfile(ProbeProfile profile) noexcept
{
    static constexpr ProbeProfileDescription bytes{
        "bytes", probe_bytes_data_type, probe_bytes_data_type, ProbeSendMethod::Bytes};
    static constexpr ProbeProfileDescription raw{
        "raw", probe_raw_data_type, probe_raw_data_type, ProbeSendMethod::Stream};
    // CreateSessionAttributeBySocketInfoTrans maps both raw request types to
    // TYPE_STREAM (4). ClientGetPeerSocketInfoById reports that normalized flag,
    // not the encrypted request enum. This is not encryption negotiation proof.
    static constexpr ProbeProfileDescription encrypted{
        "raw-encrypted", probe_encrypted_raw_data_type, probe_raw_data_type, ProbeSendMethod::Stream};
    switch (profile)
    {
        case ProbeProfile::Bytes: return &bytes;
        case ProbeProfile::Raw: return &raw;
        case ProbeProfile::RawEncrypted: return &encrypted;
    }
    return nullptr;
}

bool ValidateProbeStreamFrame(bool has_data, const void* bytes, int length,
        std::size_t maximum) noexcept
{
    return has_data && bytes != nullptr && length > 0 &&
           static_cast<std::size_t>(length) <= maximum;
}

ProbeIdentityObservation ObserveProbeIdentity(int native_result, const void* bytes,
        std::size_t capacity) noexcept
{
    // The API only defines output bytes when the native result is zero.
    if (native_result != 0 || bytes == nullptr || capacity == 0) return {};
    const auto* text = static_cast<const unsigned char*>(bytes);
    bool has_nonzero_character = false;
    for (std::size_t i = 0; i < capacity; ++i)
    {
        if (text[i] == 0) return {true, i > 0 && has_nonzero_character, i};
        if (text[i] != '0') has_nonzero_character = true;
    }
    return {};
}
} // namespace fastdds_dsoftbus_probe
