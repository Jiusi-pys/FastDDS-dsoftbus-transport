"""Independent B3 DDS C++ emitter over the validated schema-2 semantic model.

No Fast DDS-Gen process or generated fixture is used by this emitter.
"""
import json
import hashlib
import math
import re


PRIMITIVES = {
    'boolean': ('bool', 1), 'octet': ('uint8_t', 1), 'char': ('char', 1),
    'short': ('int16_t', 2), 'unsigned short': ('uint16_t', 2),
    'long': ('int32_t', 4), 'unsigned long': ('uint32_t', 4),
    'long long': ('int64_t', 8), 'unsigned long long': ('uint64_t', 8),
    'float': ('float', 4), 'double': ('double', 8),
    'int8': ('int8_t', 1), 'uint8': ('uint8_t', 1),
    'int16': ('int16_t', 2), 'uint16': ('uint16_t', 2),
    'int32': ('int32_t', 4), 'uint32': ('uint32_t', 4),
    'int64': ('int64_t', 8), 'uint64': ('uint64_t', 8),
}
CPP_RESERVED = set('alignas alignof and asm auto bitand bitor bool break case catch char class compl concept const constexpr continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor'.split())
CPP_RESERVED.update('char8_t char16_t char32_t consteval constinit and_eq not_eq or_eq xor_eq'.split())


class BackendError(ValueError):
    pass


class CppBackend:
    def __init__(self, ast, stem, fastdds):
        if ast.get('schema') != 2 or fastdds not in ('2.14.5', '3.6.2'):
            raise BackendError('B3 requires schema 2 and an exact supported Fast DDS version')
        self.ast, self.stem, self.fastdds = ast, stem, fastdds
        self.nodes = {n['qualified_name']: n for n in ast['symbols']
                      if n['kind'] not in ('member', 'enumerator')}
        for name, node in self.nodes.items():
            if node['kind'] == 'struct' and name + 'PubSubType' in self.nodes:
                raise BackendError('generated PubSubType name collides with ' + name + 'PubSubType')
            if fastdds == '3.6.2' and node['kind'] in ('struct', 'enum', 'typedef'):
                scope = name.rsplit('::', 1)[0]
                registration = scope + '::register_' + node['name'] + '_type_identifier'
                if registration in self.nodes:
                    raise BackendError('generated TypeObject registration name collides with ' + registration)
        self.ordered, self.visiting = [], set()
        for node in self.nodes.values():
            if node['kind'] not in ('module', 'struct', 'enum', 'typedef', 'const'):
                raise BackendError('unsupported DDS declaration: ' + node['qualified_name'])
            for part in node['qualified_name'].split('::')[1:]:
                if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', part) or part in CPP_RESERVED:
                    raise BackendError('unsupported C++ identifier: ' + part)
            if node['qualified_name'].split('::')[1] in ('b3', 'std'):
                raise BackendError('root namespace/name is reserved by the C++ backend')
            if node['qualified_name'].count('::') == 1 and node['name'] in {p[0] for p in PRIMITIVES.values()}:
                raise BackendError('global name collides with a C++ primitive ABI type')
            scopes = node['qualified_name'].split('::')[1:-1]
            if scopes and scopes[0] == 'b3':
                raise BackendError('namespace b3 is reserved by the backend')
            for index in range(len(scopes)):
                if self.nodes['::' + '::'.join(scopes[:index + 1])]['kind'] != 'module':
                    raise BackendError('nested type declarations require a later C++ backend extension')
            if node['kind'] != 'module':
                self.visit(node)

    def annotations(self, node, allowed):
        result = {}
        for annotation in node.get('annotations', []):
            if annotation.get('ignored'):
                continue
            name = annotation['name']
            if name not in allowed:
                raise BackendError('unsupported DDS annotation @' + name)
            result[name] = annotation.get('arguments', {})
        return result

    def resolve(self, type_):
        if type_['kind'] == 'named':
            return self.nodes[type_['name']]
        return type_

    def visit(self, node):
        if node in self.ordered:
            return
        name = node['qualified_name']
        if name in self.visiting:
            raise BackendError('recursive data types are outside the B3 backend subset')
        self.visiting.add(name)
        kind = node['kind']
        if kind == 'struct':
            if not node.get('complete') or node.get('bases') or not node.get('members'):
                raise BackendError('B3 requires complete, nonempty structs without inheritance')
            self.annotations(node, {'final', 'appendable', 'extensibility'})
            if self.extent(node) not in ('FINAL', 'APPENDABLE'):
                raise BackendError('mutable data types are outside the B3 backend subset')
            for member in node['members']:
                self.annotations(member, {'key'})
                self.ctype(member['type'])
                if self.is_key(member) and self.base(member['type'])['kind'] not in ('primitive', 'enum', 'string'):
                    raise BackendError('B3 keys must be scalar, enum or bounded narrow strings')
        elif kind == 'typedef':
            self.annotations(node, set())
            self.ctype(node['type'])
        elif kind == 'enum':
            self.annotations(node, set())
            for enumerator in node['enumerators']:
                self.annotations(enumerator, set())
                if enumerator['name'] in CPP_RESERVED or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', enumerator['name']):
                    raise BackendError('unsupported C++ enumerator ' + enumerator['name'])
                if not -(2**31) <= enumerator['value'] < 2**31:
                    raise BackendError('enum value is outside int32')
        elif kind == 'const':
            self.annotations(node, set())
            if self.base(node['type'])['kind'] != 'primitive' or node.get('value_kind') not in ('integer', 'boolean'):
                raise BackendError('B3 currently emits integer and boolean constants')
        self.visiting.remove(name)
        self.ordered.append(node)
        if kind == 'struct' and self.upper({'kind': 'named', 'name': name}) > 1024 * 1024:
            raise BackendError('B3 bounded sample capacity exceeds 1 MiB')

    def base(self, type_):
        node = self.resolve(type_)
        return self.base(node['type']) if node['kind'] == 'typedef' else node

    def ctype(self, type_):
        kind = type_['kind']
        if self.fastdds == '3.6.2' and kind in ('sequence', 'array') and type_['element']['kind'] in ('sequence', 'array'):
            leaf = type_['element']
            while leaf['kind'] in ('sequence', 'array'):
                leaf = leaf['element']
            if leaf['kind'] == 'named':
                raise BackendError('Fast DDS 3.6.2 TypeObject API requires a named inner collection (typedef) for nested collections of named elements')
        if kind == 'named':
            node = self.resolve(type_)
            self.visit(node)
            return node['qualified_name']
        if kind == 'primitive' and type_['name'] in PRIMITIVES:
            cpp = PRIMITIVES[type_['name']][0]
            return cpp if cpp in ('bool', 'char', 'float', 'double') else '::std::' + cpp
        if kind in ('string', 'sequence'):
            if not isinstance(type_.get('bound'), int) or not 0 < type_['bound'] <= 65535:
                raise BackendError('B3 strings/sequences require bounds from 1 through 65535')
            return '::std::string' if kind == 'string' else '::std::vector<' + self.ctype(type_['element']) + '>'
        if kind == 'array':
            result = self.ctype(type_['element'])
            for size in reversed(type_['dimensions']):
                if not isinstance(size, int) or not 0 < size <= 65535:
                    raise BackendError('B3 array extent exceeds backend resource bounds')
                result = f'::std::array<{result}, {size}>'
            return result
        raise BackendError('unsupported DDS member type: ' + str(type_))

    def extent(self, node):
        ann = self.annotations(node, {'final', 'appendable', 'extensibility'})
        if 'final' in ann:
            return 'FINAL'
        if 'appendable' in ann:
            return 'APPENDABLE'
        return ann.get('extensibility', {}).get('value', 'APPENDABLE').split('::')[-1]

    @staticmethod
    def is_key(member):
        return any(a['name'] == 'key' and a.get('arguments', {}).get('value', True)
                   for a in member.get('annotations', []))

    def upper(self, type_):
        node = self.base(type_)
        kind = node['kind']
        if kind == 'primitive':
            return PRIMITIVES[node['name']][1] + 8
        if kind == 'enum':
            return 12
        if kind == 'string':
            return node['bound'] + 9
        if kind == 'sequence':
            return 12 + node['bound'] * self.upper(node['element'])
        if kind == 'array':
            return 8 + math.prod(node['dimensions']) * self.upper(node['element'])
        return 16 + sum(self.upper(m['type']) for m in node['members'])

    def valid(self, type_, expression, depth=0):
        node = self.base(type_)
        kind = node['kind']
        if kind == 'string':
            return f'({expression}.size() <= {node["bound"]} && {expression}.find(char(0)) == std::string::npos)'
        if kind in ('sequence', 'array'):
            item = f'b3_item_{depth}'
            element = node['element'] if kind == 'sequence' or len(node['dimensions']) == 1 else dict(node, dimensions=node['dimensions'][1:])
            check = self.valid(element, item, depth + 1)
            prefix = f'{expression}.size() <= {node["bound"]} && ' if kind == 'sequence' else ''
            return f'({prefix}std::all_of({expression}.begin(), {expression}.end(), [](const auto& {item}) {{ return {check}; }}))'
        if kind == 'enum':
            return '(' + ' || '.join(f'{expression} == {node["qualified_name"]}::{v["name"]}' for v in node['enumerators']) + ')'
        if kind == 'struct':
            return f'::b3::Traits<{node["qualified_name"]}>::valid({expression})'
        return 'true'

    @staticmethod
    def namespace(node, body):
        parts = node['qualified_name'].split('::')[1:-1]
        return ''.join(f'namespace {p} {{\n' for p in parts) + body + '\n' + '}\n' * len(parts)

    def data_class(self, node):
        name, kind = node['name'], node['kind']
        if kind == 'typedef':
            return self.namespace(node, f'using {name} = {self.ctype(node["type"])};')
        if kind == 'const':
            value = node['value']
            if node['value_kind'] == 'boolean':
                literal = str(value).lower()
            elif value == -(2**63):
                literal = '(-9223372036854775807LL - 1)'
            else:
                literal = str(value) + ('ULL' if value >= 0 else 'LL')
            return self.namespace(node, f'constexpr {self.ctype(node["type"])} {name} = {literal};')
        if kind == 'enum':
            items = ', '.join(f'{v["name"]} = {v["value"]}' for v in node['enumerators'])
            return self.namespace(node, f'enum class {name} : ::std::int32_t {{ {items} }};')
        lines = [f'class {name} {{', 'public:']
        for member in node['members']:
            field, type_ = member['name'], self.ctype(member['type'])
            if field in CPP_RESERVED or field == name or field.startswith('b3_') or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', field):
                raise BackendError('unsupported C++ member name ' + field)
            lines += [f'    void {field}(const {type_}& value) {{ b3_{field}_ = value; }}',
                      f'    const {type_}& {field}() const {{ return b3_{field}_; }}',
                      f'    {type_}& {field}() {{ return b3_{field}_; }}']
        comparisons = ' && '.join(f'b3_{m["name"]}_ == other.b3_{m["name"]}_' for m in node['members'])
        lines += [f'    bool operator==(const {name}& other) const {{ return {comparisons}; }}', 'private:']
        for member in node['members']:
            type_ = self.base(member['type'])
            initializer = f'{type_["qualified_name"]}::{type_["enumerators"][0]["name"]}' if type_['kind'] == 'enum' else ''
            lines.append(f'    {self.ctype(member["type"])} b3_{member["name"]}_{{{initializer}}};')
        return self.namespace(node, '\n'.join(lines) + '\n};')

    def traits(self, node):
        name = node['qualified_name']
        members = node['members']
        checks = ' && '.join(self.valid(m['type'], 'data.' + m['name'] + '()') for m in members)
        key_max = 0
        for member in members:
            if self.is_key(member):
                type_ = self.base(member['type'])
                size = 4 if type_['kind'] in ('enum', 'string') else PRIMITIVES[type_['name']][1]
                # Both pinned generators select MD5 using the maximum key
                # metadata with eight-byte alignment, including the 3.x ABI
                # whose actual key serialization uses XCDRv2.
                alignment = min(size, 8)
                key_max += (-key_max) % alignment + size
                if type_['kind'] == 'string':
                    key_max += type_['bound'] + 1
        extent = self.extent(node) == 'APPENDABLE'
        lines = [f'template<> struct Traits<{name}> {{', f'    static constexpr bool appendable = {str(extent).lower()};',
                 f'    static constexpr bool has_key = {str(any(self.is_key(m) for m in members)).lower()};',
                 f'    static constexpr size_t key_max = {key_max};',
                 f'    static constexpr uint32_t max_size = {self.upper({"kind":"named", "name":name}) + 4};',
                 f'    static const char* name() {{ return "{name[2:]}"; }}',
                 f'    static bool valid(const {name}& data) {{ return {checks}; }}',
                 f'    static void write(cdr::Cdr& stream, const {name}& data) {{',
                 '        require(valid(data)); cdr::Cdr::state state(stream);',
                 '        stream.begin_serialize_type(state, encoding(stream.get_cdr_version(), appendable));']
        lines += [f'        stream << cdr::MemberId({i}) << data.{m["name"]}();' for i, m in enumerate(members)]
        lines += ['        stream.end_serialize_type(state);', '    }',
                  f'    static void read(cdr::Cdr& stream, {name}& data) {{',
                  '        stream.deserialize_type(encoding(stream.get_cdr_version(), appendable),',
                  '            [&data](cdr::Cdr& field, const cdr::MemberId& id) { switch(id.id) {']
        lines += [f'            case {i}: field >> data.{m["name"]}(); return true;' for i, m in enumerate(members)]
        lines += ['            default: return false; }});', '        require(valid(data));', '    }',
                  f'    static size_t size(cdr::CdrSizeCalculator& calc, const {name}& data, size_t& alignment) {{',
                  '        require(valid(data)); auto previous = calc.get_encoding();',
                  '        size_t result = calc.begin_calculate_type_serialized_size(encoding(calc.get_cdr_version(), appendable), alignment);']
        lines += [f'        result += calc.calculate_member_serialized_size(cdr::MemberId({i}), data.{m["name"]}(), alignment);' for i, m in enumerate(members)]
        lines += ['        return result + calc.end_calculate_type_serialized_size(previous, alignment);', '    }',
                  f'    static void key(cdr::Cdr& stream, const {name}& data) {{ (void)stream; (void)data;']
        lines += [f'        stream << data.{m["name"]}();' for m in members if self.is_key(m)]
        return '\n'.join(lines + ['    }', '};'])

    def guard(self, area, node, body):
        model = {'cpp': self.data_class(node), 'type': node.get('type'),
                 'members': [{'type': m['type'], 'key': self.is_key(m)} for m in node.get('members', [])],
                 'extent': self.extent(node) if node['kind'] == 'struct' else None}
        identity = hashlib.sha256(node['qualified_name'].encode()).hexdigest()[:24].upper()
        signature = hashlib.sha256(json.dumps(model, sort_keys=True).encode()).hexdigest()[:16]
        macro = 'STANDARDS_B3_' + area.upper() + '_' + identity
        return f'#ifndef {macro}\n#define {macro} 0x{signature}ULL\n{body}\n#elif {macro} != 0x{signature}ULL\n#error Conflicting B3 declarations for {node["qualified_name"]}\n#endif\n'

    def emit(self):
        stem = self.stem
        ext = 'h' if self.fastdds == '2.14.5' else 'hpp'
        data = '#pragma once\n#include "B3Support.hpp"\n' + '\n'.join(self.guard('data', n, self.data_class(n)) for n in self.ordered)
        structs = [n for n in self.ordered if n['kind'] == 'struct']
        if not structs:
            raise BackendError('B3 requires at least one DDS struct')
        # Declare custom-type overloads before instantiating any nested container.
        aux = f'#pragma once\n#include "{stem}.{ext}"\nnamespace eprosima {{ namespace fastcdr {{\n'
        for n in structs:
            q = n['qualified_name']
            aux += f'template<> void serialize(Cdr&, const {q}&);\ntemplate<> void deserialize(Cdr&, {q}&);\ntemplate<> size_t calculate_serialized_size(CdrSizeCalculator&, const {q}&, size_t&);\n'
        aux += '}}\n'
        traits = '#pragma once\n' + f'#include "{stem}CdrAux.hpp"\n'
        for n in structs:
            q = n['qualified_name']
            body = 'namespace b3 {\n' + self.traits(n) + '\n}\nnamespace eprosima { namespace fastcdr {\n'
            body += f'template<> inline void serialize(Cdr& c, const {q}& d) {{ ::b3::Traits<{q}>::write(c,d); }}\n'
            body += f'template<> inline void deserialize(Cdr& c, {q}& d) {{ ::b3::Traits<{q}>::read(c,d); }}\n'
            body += f'template<> inline size_t calculate_serialized_size(CdrSizeCalculator& c, const {q}& d, size_t& a) {{ return ::b3::Traits<{q}>::size(c,d,a); }}\n' + '}}\n'
            traits += self.guard('cdr', n, body)
        pub = f'#pragma once\n#include "{stem}CdrAux.ipp"\n'
        if self.fastdds == '3.6.2':
            pub += f'#include "{stem}TypeObjectSupport.hpp"\n'
        for n in structs:
            body = '' if self.fastdds == '2.14.5' else f'public: void register_type_object_representation() override {{ register_{n["name"]}_type_identifier(this->type_identifiers_); }}'
            pub += self.guard('support', n, self.namespace(n, f'class {n["name"]}PubSubType : public ::b3::B3Support<{n["qualified_name"]}> {{{body}}};'))
        files = {f'{stem}.{ext}': data, f'{stem}CdrAux.hpp': aux, f'{stem}CdrAux.ipp': traits,
                 f'{stem}PubSubTypes.{ext}': pub, f'{stem}PubSubTypes.cxx': f'#include "{stem}PubSubTypes.{ext}"\n'}
        if ext == 'h':
            files[f'{stem}.cxx'] = f'#include "{stem}.h"\n'
        else:
            from idl_cpp_typeobjects import TypeObjectEmitter
            files.update(TypeObjectEmitter(self).emit())
        return files
