// Contract tests use the exact private header applied to the candidate library.
#include <rtps/builtin/discovery/endpoint/TypeAssignability.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace eprosima::fastdds::dds;
using namespace eprosima::fastdds::dds::xtypes;
using eprosima::fastdds::rtps::type_assignability::struct_assignable;
using eprosima::fastdds::rtps::type_assignability::member_type_assignable;

static unsigned checks = 0;
static void check(bool value, const char* label)
{
    if (!value) throw std::runtime_error(label);
    ++checks;
}

static CompleteStructMember member(unsigned id, const std::string& name, TypeKind kind = TK_UINT32)
{
    CompleteStructMember result;
    result.common().member_id(id);
    result.common().member_type_id()._d(kind);
    result.detail().name(name);
    return result;
}

int main()
{
    try {
        TypeConsistencyEnforcementQosPolicy policy;
        CompleteStructType reader, writer;
        reader.struct_flags(IS_APPENDABLE);
        reader.member_seq({member(0, "side"), member(1, "n")});
        writer = reader;
        writer.member_seq().push_back(member(2, "extra"));
        check(struct_assignable(reader, writer, policy), "appendable extension");
        policy.m_prevent_type_widening = true;
        check(!struct_assignable(reader, writer, policy), "prevent widening");
        policy.m_prevent_type_widening = false;
        reader.struct_flags(IS_FINAL); writer.struct_flags(IS_FINAL);
        check(!struct_assignable(reader, writer, policy), "final wider");
        writer.struct_flags(IS_APPENDABLE);
        check(!struct_assignable(reader, writer, policy), "final versus appendable");
        reader.struct_flags(IS_MUTABLE);
        check(!struct_assignable(reader, writer, policy), "mutable versus appendable");
        writer.struct_flags(IS_MUTABLE);
        check(struct_assignable(reader, writer, policy), "mutable extension");
        std::swap(writer.member_seq()[0], writer.member_seq()[1]);
        check(struct_assignable(reader, writer, policy), "mutable ordering by ID");
        reader.struct_flags(IS_APPENDABLE); writer.struct_flags(IS_APPENDABLE);
        check(!struct_assignable(reader, writer, policy), "appendable ordering");
        writer = reader;
        writer.member_seq()[1].common().member_type_id()._d(TK_FLOAT32);
        check(!struct_assignable(reader, writer, policy), "incompatible primitive");
        writer = reader;
        writer.member_seq()[1].detail().name("renamed");
        check(!struct_assignable(reader, writer, policy), "same ID different name");
        policy.m_ignore_member_names = true;
        check(struct_assignable(reader, writer, policy), "ignore member names");
        policy.m_ignore_member_names = false;
        writer = reader;
        writer.member_seq().push_back(member(2, "extra"));
        writer.member_seq().back().common().member_flags(IS_MUST_UNDERSTAND);
        check(!struct_assignable(reader, writer, policy), "missing must-understand member");
        writer.member_seq().back().common().member_flags(IS_KEY);
        check(!struct_assignable(reader, writer, policy), "missing key");
        writer = reader;
        writer.member_seq()[1].common().member_flags(IS_OPTIONAL);
        check(!struct_assignable(reader, writer, policy), "optional mismatch");
        writer = reader;
        writer.header().base_type().equivalence_hash(EquivalenceHash{});
        check(!struct_assignable(reader, writer, policy), "unsupported inheritance fails closed");
        writer = reader;
        reader.member_seq().clear();
        check(!struct_assignable(reader, writer, policy), "empty shared member set");
        reader = writer;
        while (writer.member_seq().size() < 256)
            writer.member_seq().push_back(member(static_cast<unsigned>(writer.member_seq().size()),
                    "m" + std::to_string(writer.member_seq().size())));
        check(struct_assignable(reader, writer, policy), "member limit inclusive");
        writer.member_seq().push_back(member(256, "overflow"));
        check(!struct_assignable(reader, writer, policy), "member limit overflow");
        TypeIdentifier small, large;
        StringSTypeDefn string_type;
        string_type.bound(8); small.string_sdefn(string_type);
        string_type.bound(16); large.string_sdefn(string_type);
        check(member_type_assignable(small, large, policy, false), "ignore string bounds");
        check(!member_type_assignable(small, large, policy, true), "key string bounds enforced");
        policy.m_ignore_string_bounds = false;
        check(!member_type_assignable(small, large, policy, false), "string bounds enforced");
        check(member_type_assignable(large, small, policy, false), "wider reader string");
        large.string_sdefn().bound(0);
        check(!member_type_assignable(small, large, policy, false), "unbounded writer string");
        check(member_type_assignable(large, small, policy, false), "unbounded reader string");
        MinimalStructType mr, mw;
        mr.struct_flags(IS_MUTABLE); mw.struct_flags(IS_MUTABLE);
        MinimalStructMember mm;
        mm.common().member_id(0); mm.common().member_type_id()._d(TK_UINT32);
        mm.detail().name_hash({{1, 2, 3, 4}});
        mr.member_seq({mm}); mw.member_seq({mm});
        check(struct_assignable(mr, mw, policy), "minimal object member name hashes");
        mw.member_seq()[0].detail().name_hash({{4, 3, 2, 1}});
        check(!struct_assignable(mr, mw, policy), "minimal hash conflict");
        // Optional extensions do not constitute widening (XTypes 7.6.3.4.1).
        // Keep non-optional extension rejection as a control for both extents.
        for (const auto extent : {IS_MUTABLE, IS_APPENDABLE}) {
            for (const StructMemberFlag flags : {StructMemberFlag(0), StructMemberFlag(IS_OPTIONAL),
                    StructMemberFlag(IS_OPTIONAL | IS_MUST_UNDERSTAND)}) {
                for (const bool prevent : {false, true}) {
                    CompleteStructType narrow, wide;
                    narrow.struct_flags(extent);
                    narrow.member_seq({member(0, "shared")});
                    wide = narrow;
                    wide.member_seq().push_back(member(1, "extra"));
                    wide.member_seq().back().common().member_flags(flags);
                    TypeConsistencyEnforcementQosPolicy optional_policy;
                    optional_policy.m_prevent_type_widening = prevent;
                    const bool expected = !prevent || (flags & IS_OPTIONAL) != 0;
                    const auto label = "optional/widening extent=" + std::to_string(extent) +
                            " flags=" + std::to_string(flags) + " prevent=" + std::to_string(prevent);
                    check(struct_assignable(narrow, wide, optional_policy) == expected, label.c_str());
                }
            }
        }
        std::cout << "assignability contract checks: " << checks << " PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
