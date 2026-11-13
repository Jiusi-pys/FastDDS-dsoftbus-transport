"""Public Fast DDS 3 TypeObject registration for the B3 data subset."""
import hashlib


REGISTRY_PRIMITIVES = {
    'boolean': '_bool', 'octet': '_byte', 'char': '_char',
    'short': '_int16_t', 'unsigned short': '_uint16_t',
    'long': '_int32_t', 'unsigned long': '_uint32_t',
    'long long': '_int64_t', 'unsigned long long': '_uint64_t',
    'float': '_float', 'double': '_double',
    **{name: '_' + name + '_t' for name in ('int8', 'uint8', 'int16', 'uint16', 'int32', 'uint32', 'int64', 'uint64')},
}


class TypeObjectEmitter:
    def __init__(self, backend):
        self.backend = backend
        self.lines = []
        self.counter = 0

    def identifier(self, type_):
        self.counter += 1
        var = f'ids_{self.counter}'
        self.lines.append(f'TypeIdentifierPair {var};')
        kind = type_['kind']
        if kind == 'named':
            self.lines.append(f'{self.function(type_["name"])}({var});')
        elif kind == 'primitive':
            self.lines.append(f'check(registry.get_type_identifiers("{REGISTRY_PRIMITIVES[type_["name"]]}", {var}));')
        elif kind == 'string':
            form = 's' if type_['bound'] <= 255 else 'l'
            self.lines.append(f'check(U::build_and_register_{form}_string_type_identifier(U::build_string_{form}_type_defn({type_["bound"]}), "_standards_b3_string_{type_["bound"]}", {var}));')
        else:
            element = self.identifier(type_['element'])
            header = f'header_{var}'
            leaf = type_['element']
            while leaf['kind'] in ('array', 'sequence'):
                leaf = leaf['element']
            # A plain collection can carry a complete named element identifier
            # even when its returned pair has no second identifier. Propagate
            # that element equivalence through every anonymous collection level.
            equivalence = 'EK_COMPLETE' if leaf['kind'] == 'named' else 'EK_BOTH'
            self.lines.append(f'auto {header} = U::build_plain_collection_header({equivalence}, 0);')
            pointer = f'::eprosima::fastcdr::external<TypeIdentifier>(new TypeIdentifier(complete({element})))'
            if kind == 'sequence':
                form = 's' if type_['bound'] <= 255 else 'l'
                definition = f'U::build_plain_sequence_{form}_elem_defn({header}, {type_["bound"]}, {pointer})'
            else:
                form = 's' if max(type_['dimensions']) <= 255 else 'l'
                dimensions = ', '.join(map(str, type_['dimensions']))
                definition = f'U::build_plain_array_{form}_elem_defn({header}, {form.upper()}BoundSeq{{{dimensions}}}, {pointer})'
            identity = hashlib.sha256(self.current.encode()).hexdigest()
            label = '_standards_b3_' + kind + '_' + var + '_' + identity
            self.lines.append(f'check(U::build_and_register_{form}_{kind}_type_identifier({definition}, "{label}", {var}));')
        return var

    @staticmethod
    def function(name):
        return name.rsplit('::', 1)[0] + '::register_' + name.rsplit('::', 1)[1] + '_type_identifier'

    def emit(self):
        b = self.backend
        nodes = [n for n in b.ordered if n['kind'] != 'const']
        header = '#pragma once\n#include <fastdds/dds/xtypes/type_representation/TypeObject.hpp>\n'
        for n in nodes:
            header += b.namespace(n, f'inline void register_{n["name"]}_type_identifier(::eprosima::fastdds::dds::xtypes::TypeIdentifierPair&);')
        header += '''#include <stdexcept>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/xtypes/type_representation/ITypeObjectRegistry.hpp>
#include <fastdds/dds/xtypes/type_representation/TypeObjectUtils.hpp>
#ifndef STANDARDS_B3_TYPEOBJECT_UTILS_V1_HPP
#define STANDARDS_B3_TYPEOBJECT_UTILS_V1_HPP
namespace b3 { namespace xtypes_detail {
using eprosima::fastdds::dds::ReturnCode_t;
using eprosima::fastdds::dds::RETCODE_OK;
using eprosima::fastdds::dds::DomainParticipantFactory;
using namespace eprosima::fastdds::dds::xtypes;
using U = TypeObjectUtils;
inline void check(ReturnCode_t code) { if (code != RETCODE_OK) throw std::runtime_error("B3 TypeObject registration failed"); }
inline TypeIdentifier complete(const TypeIdentifierPair& ids) {
    bool ok = false;
    auto result = U::retrieve_complete_type_identifier(ids, ok);
    if (!ok) throw std::runtime_error("B3 incomplete TypeIdentifier pair");
    return result;
}
}}
#endif
'''
        for n in nodes:
            name, kind = n['qualified_name'][2:], n['kind']
            self.current = name
            self.counter = 0
            self.lines = [f'inline void register_{n["name"]}_type_identifier(::eprosima::fastdds::dds::xtypes::TypeIdentifierPair& result) {{',
                'using namespace ::eprosima::fastdds::dds::xtypes;',
                'using ::eprosima::fastdds::dds::RETCODE_OK; using ::eprosima::fastdds::dds::DomainParticipantFactory;',
                'using U = TypeObjectUtils; using ::b3::xtypes_detail::check; using ::b3::xtypes_detail::complete;',
                'auto& registry = DomainParticipantFactory::get_instance()->type_object_registry();',
                f'if (registry.get_type_identifiers("{name}", result) == RETCODE_OK) return;',
                f'auto detail = U::build_complete_type_detail({{}}, {{}}, "{name}");']
            if kind == 'struct':
                self.lines.append('CompleteStructMemberSeq members;')
                for index, member in enumerate(n['members']):
                    ids = self.identifier(member['type'])
                    key = str(b.is_key(member)).lower()
                    self.lines += [f'auto flags_{index} = U::build_struct_member_flag(TryConstructFailAction::DISCARD, false, false, {key}, false);',
                        f'auto common_{index} = U::build_common_struct_member({index}, flags_{index}, complete({ids}));',
                        f'auto detail_{index} = U::build_complete_member_detail("{member["name"]}", {{}}, {{}});',
                        f'U::add_complete_struct_member(members, U::build_complete_struct_member(common_{index}, detail_{index}));']
                self.lines += [f'auto flags = U::build_struct_type_flag(ExtensibilityKind::{b.extent(n)}, false, false);',
                    'auto header = U::build_complete_struct_header(TypeIdentifier(), detail);',
                    f'check(U::build_and_register_struct_type_object(U::build_complete_struct_type(flags, header, members), "{name}", result));']
            elif kind == 'typedef':
                ids = self.identifier(n['type'])
                self.lines += [f'auto common = U::build_common_alias_body(0, complete({ids}));',
                    'auto body = U::build_complete_alias_body(common, {}, {});',
                    'auto header = U::build_complete_alias_header(detail);',
                    f'check(U::build_and_register_alias_type_object(U::build_complete_alias_type(0, header, body), "{name}", result));']
            elif kind == 'enum':
                self.lines.append('CompleteEnumeratedLiteralSeq literals;')
                for index, item in enumerate(n['enumerators']):
                    self.lines += [f'auto common_{index} = U::build_common_enumerated_literal({item["value"]}, 0);',
                        f'auto detail_{index} = U::build_complete_member_detail("{item["name"]}", {{}}, {{}});',
                        f'U::add_complete_enumerated_literal(literals, U::build_complete_enumerated_literal(common_{index}, detail_{index}));']
                self.lines += ['auto header = U::build_complete_enumerated_header(U::build_common_enumerated_header(32), detail);',
                    f'check(U::build_and_register_enumerated_type_object(U::build_complete_enumerated_type(0, header, literals), "{name}", result));']
            self.lines.append('}')
            header += b.guard('registry', n, b.namespace(n, '\n'.join(self.lines)))
        return {f'{b.stem}TypeObjectSupport.hpp': header,
                f'{b.stem}TypeObjectSupport.cxx': f'#include "{b.stem}TypeObjectSupport.hpp"\n'}
