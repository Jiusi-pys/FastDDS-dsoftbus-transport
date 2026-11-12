"""B2 IDL frontend: explicit language profiles, semantic AST, no code backend."""
from decimal import DecimalException
import re
from idl_frontend import Frontend, Diagnostic, Token, KEYWORDS, LEXEME
from idl_profiles import PROFILES
from idl_numeric import Deferred, Integer, Binary, export
from idl_extended_numeric import NumericMixin, INTEGER_TYPES
from idl_annotations import AnnotationMixin
from idl_declarations import DeclarationMixin

LETTER = r'A-Za-z\u00c0-\u00d6\u00d8-\u00f6\u00f8-\u00ff'
IDENTIFIER = re.compile(rf'_?[{LETTER}][{LETTER}_0-9]*')
EXT_LEXEME = re.compile(LEXEME.pattern.replace('[A-Za-z]', f'[{LETTER}]').replace('[A-Za-z_0-9]', f'[{LETTER}_0-9]'))
SCOPES = {'module', 'interface', 'struct', 'union', 'valuetype', 'eventtype',
          'component', 'home', 'porttype', 'connector', 'annotation', 'template_module'}
TYPE_KINDS = {'typedef', 'struct', 'union', 'enum', 'native', 'bitmask', 'bitset',
              'interface', 'valuetype', 'eventtype', 'value_box', 'template_type', 'component'}


class ExtendedFrontend(NumericMixin, AnnotationMixin, DeclarationMixin, Frontend):
    def __init__(self, text, filename='<input>', profile='b2', imports=None):
        super().__init__(text, filename, EXT_LEXEME)
        self.profile = profile
        self.blocks, self.groups = PROFILES[profile]
        self.annotation_choices = ()
        self.ignoring_annotation = False
        self.annotation_scope = None
        self.standard_annotation = None
        self.eval_bits = 32
        self.templates = {}
        self.template_stack = []
        self.imports = imports or {}

    def require(self, block, token=None):
        if block not in self.blocks:
            self.fail('E_PROFILE', f'{block} is unavailable in profile {self.profile}', token)

    def identifier(self):
        token = self.take()
        if not IDENTIFIER.fullmatch(token.text):
            self.fail('E_IDENTIFIER', 'expected IDL Latin-1 identifier', token)
        if token.text.startswith('_'):
            return token.text[1:], token
        if token.text.lower() in {k.lower() for k in KEYWORDS}:
            self.fail('E_IDENTIFIER', f'reserved keyword {token.text}', token)
        return token.text, token

    def name_parts(self, allow_keyword=False):
        absolute = self.accept('::')
        parts = []
        while True:
            if allow_keyword and self.token.text in KEYWORDS:
                parts.append(self.take().text)
            else:
                parts.append(self.identifier()[0])
            if not self.accept('::'):
                break
        return ('::' if absolute else '') + '::'.join(parts)

    def lookup(self, scope, name, seen=None):
        seen = set() if seen is None else set(seen)
        if scope in seen:
            return []
        seen.add(scope)
        direct = self.symbols.get(scope+(name,))
        if direct:
            return [direct]
        node = self.symbols.get(scope, {})
        matches = {}
        for base in node.get('bases', []):
            for candidate in self.lookup(tuple(base[2:].split('::')), name, seen):
                matches[candidate['qualified_name']] = candidate
        return list(matches.values())

    def find_spelling(self, spelling, introduce=False):
        absolute = spelling.startswith('::')
        parts = tuple(spelling.removeprefix('::').split('::'))
        roots = [()] if absolute else [self.scope[:i] for i in range(len(self.scope), -1, -1)]
        if not absolute and self.annotation_scope is not None:
            roots = [self.annotation_scope] + [r for r in roots if r != self.annotation_scope]
        for root in roots:
            found = self.lookup(root, parts[0])
            if not found:
                continue
            if len(found) != 1:
                self.fail('E_AMBIGUOUS', f'ambiguous inherited name {parts[0]}')
            node = found[0]
            if introduce and not absolute and root != self.scope and root != self.annotation_scope:
                self.introduced.setdefault(self.scope, {})[parts[0].lower()] = node['qualified_name']
                # Potential scope extends out through nested non-module scopes.
                for length in range(len(self.scope)-1, 0, -1):
                    scope = self.scope[:length]
                    if self.symbols.get(scope, {}).get('kind') == 'module':
                        break
                    self.introduced.setdefault(scope, {})[parts[0].lower()] = node['qualified_name']
            for part in parts[1:]:
                if node['kind'] == 'annotation':
                    path = tuple(node['qualified_name'][2:].split('::'))
                    if self.annotation_scope != path and self.scope[:len(path)] != path:
                        self.fail('E_SCOPE', 'annotation-local names are only visible inside annotation definitions/applications')
                if node['kind'] not in SCOPES:
                    self.fail('E_SCOPE', f'{node["name"]} is not a scope')
                found = self.lookup(tuple(node['qualified_name'][2:].split('::')), part)
                if not found:
                    self.fail('E_UNRESOLVED', f'unknown qualified name {spelling}')
                if len(found) != 1:
                    self.fail('E_AMBIGUOUS', f'ambiguous inherited name {part}')
                node = found[0]
            return node
        return None

    def resolve(self):
        token = self.token
        spelling = self.name_parts()
        try:
            node = self.find_spelling(spelling, introduce=True)
        except Diagnostic as error:
            raise Diagnostic(error.code, error.message, token) from None
        if node is None:
            self.fail('E_UNRESOLVED', f'unknown name {spelling}', token)
        self.references.append({'spelling': spelling, 'target': node['qualified_name'],
                                'location': {'file': token.file, 'line': token.line, 'column': token.column}})
        return node

    def declare(self, name, token, kind, **attrs):
        owner = self.symbols.get(self.scope, {})
        inherited = [n for base in owner.get('bases', []) for n in self.lookup(tuple(base[2:].split('::')), name)]
        if inherited and not (kind in TYPE_KINDS and all(n['kind'] in TYPE_KINDS for n in inherited)):
            self.fail('E_INHERITANCE', f'cannot redefine inherited non-type name {name}', token)
        return super().declare(name, token, kind, **attrs)

    def inherited_members(self, node, seen=None):
        seen = set() if seen is None else set(seen)
        nodes = []
        for name in node.get('bases', []):
            if name in seen:
                continue
            seen.add(name)
            base = self.symbols[tuple(name[2:].split('::'))]
            nodes.extend(self.inherited_members(base, seen))
            nodes.extend(base.get('members', base.get('declarations', [])))
        return list({n.get('qualified_name', id(n)): n for n in nodes}.values())

    def type_spec(self, template=False, incomplete=False, constant=False):
        token, word = self.token, self.token.text
        if word in INTEGER_TYPES and word not in ('short', 'long', 'octet') and ' ' not in word:
            self.require('extended', token)
            self.take()
            return {'kind': 'primitive', 'name': word}
        if word in ('any', 'Object', 'ValueBase'):
            self.require({'any': 'any', 'Object': 'corba-interfaces', 'ValueBase': 'corba-values'}[word], token)
            self.take()
            return {'kind': 'primitive', 'name': word}
        if word in ('map', 'string', 'wstring', 'sequence', 'fixed'):
            if not template and not constant:
                self.require('anonymous', token)
            if word == 'map':
                self.require('extended', token)
                self.take()
                self.expect('<')
                key = self.type_spec()
                self.expect(',')
                value = self.type_spec()
                bound = self.bound() if self.accept(',') else None
                self.expect('>')
                return {'kind': 'map', 'key': key, 'element': value, 'bound': bound}
            if word == 'fixed':
                self.take()
                result = {'kind': 'fixed', 'digits': None, 'scale': None}
                if self.accept('<'):
                    if constant:
                        self.fail('E_SYNTAX', 'const fixed uses no explicit digits or scale', token)
                    digits = self.bound()
                    self.expect(',')
                    scale, category = self.expression()
                    if category != 'integer' or not isinstance(digits, Deferred) and not isinstance(scale, Deferred) and not 0 <= scale <= digits <= 31:
                        self.fail('E_BOUND', 'fixed requires 1<=digits<=31 and 0<=scale<=digits', token)
                    self.expect('>')
                    result.update(digits=digits, scale=scale)
                elif not constant:
                    self.fail('E_SYNTAX', 'fixed type requires digits and scale', token)
                return result
            return super().type_spec(template=True, incomplete=incomplete, constant=constant)
        if word in ('struct', 'union', 'enum', 'bitset', 'bitmask'):
            if word in ('bitset', 'bitmask'):
                self.require('extended', token)
            self.take()
            node = self.enumeration(bitmask=word == 'bitmask') if word in ('enum', 'bitmask') else self.aggregate(word)
            if node['kind'] == 'forward':
                self.fail('E_SYNTAX', 'inline type requires definition', token)
            return {'kind': 'named', 'name': node['qualified_name']}
        if word in ('short', 'long', 'unsigned', 'float', 'double', 'char', 'wchar', 'boolean', 'octet'):
            return super().type_spec(template=template, incomplete=incomplete, constant=constant)
        node = self.resolve()
        if node['kind'] not in TYPE_KINDS:
            self.fail('E_TYPE', 'name does not denote a type', token)
        type_ = {'kind': 'named', 'name': node['qualified_name']}
        base = self.underlying(type_)
        if not incomplete and base['kind'] not in ('sequence', 'interface', 'valuetype', 'eventtype', 'component') and self.is_incomplete(base):
            self.fail('E_INCOMPLETE', 'incomplete type requires sequence element context', token)
        return type_

    def declarators(self, type_, kind, arrays=False):
        nodes = []
        while True:
            annotations = self.annotations()
            name, token = self.identifier()
            dimensions = []
            while self.accept('['):
                if not arrays:
                    self.require('anonymous', token)
                dimensions.append(self.bound())
                self.expect(']')
            if dimensions and self.is_incomplete(type_):
                self.fail('E_INCOMPLETE', 'array element must be complete', token)
            actual = {'kind': 'array', 'element': type_, 'dimensions': dimensions} if dimensions else type_
            nodes.append(self.attach(self.declare(name, token, kind, type=actual), annotations))
            if not self.accept(','):
                return nodes

    def aggregate(self, kind):
        name, token = self.identifier()
        path, outer = self.scope+(name,), self.scope
        old = self.symbols.get(path)
        if old and old['kind'] == kind and (not old['complete'] or self.token.text == ';'):
            node = old
        else:
            node = self.declare(name, token, kind, complete=False, bases=[])
        if self.token.text == ';':
            if kind not in ('struct', 'union'):
                self.fail('E_SYNTAX', f'{kind} has no forward declaration', token)
            return {'kind': 'forward', 'target': node['qualified_name']}
        if self.accept(':'):
            self.require('extended', token)
            if kind not in ('struct', 'bitset'):
                self.fail('E_INHERITANCE', 'this aggregate cannot inherit', token)
            node['bases'] = [self.base_reference(kind)]
        self.scope = path
        discriminator = None
        if kind == 'union':
            self.expect('switch')
            self.expect('(')
            discriminator = self.type_spec()
            base = self.underlying(discriminator)
            names = set(INTEGER_TYPES)-{'octet', 'int8', 'uint8', 'int16', 'uint16', 'int32', 'uint32', 'int64', 'uint64'}
            names |= {'char', 'boolean'}
            if 'extended' in self.blocks:
                names |= set(INTEGER_TYPES) | {'wchar'}
            if not (base['kind'] == 'enum' or base['kind'] == 'primitive' and base['name'] in names or base['kind'] == 'template_type'):
                self.fail('E_DISCRIMINATOR', 'invalid union discriminator', token)
            self.expect(')')
            node['discriminator'] = discriminator
        self.expect('{')
        members, labels, has_default = [], set(), False
        while self.token.text not in ('}', '<eof>'):
            annotations, branch = self.annotations(), []
            if kind == 'bitset':
                members.extend(self.bitfield(annotations))
                continue
            if kind == 'union':
                while self.token.text in ('case', 'default'):
                    label = self.take()
                    if label.text == 'default':
                        if has_default:
                            self.fail('E_CASE', 'duplicate default case', label)
                        has_default = True
                        branch.append({'default': True})
                    else:
                        value, category = self.expression()
                        self.check_value(discriminator, value, category, label)
                        key = repr(value) if isinstance(value, Deferred) else value
                        if key in labels:
                            self.fail('E_CASE', 'duplicate union case', label)
                        labels.add(key)
                        branch.append({'value': value})
                    self.expect(':')
                if not branch:
                    self.fail('E_SYNTAX', 'union member requires case label')
                annotations += self.annotations()
            type_ = self.type_spec()
            fields = self.declarators(type_, 'member')
            if kind == 'union' and len(fields) != 1:
                self.fail('E_SYNTAX', 'union branch requires one declarator')
            for field in fields:
                self.attach(field, annotations)
                if branch:
                    field['labels'] = branch
            self.expect(';')
            members.extend(fields)
        self.expect('}')
        if not members and kind != 'bitset':
            if kind == 'union':
                self.fail('E_EMPTY', 'union requires branch', token)
            self.require('extended', token)
        if has_default and discriminator:
            base = self.underlying(discriminator)
            count = len(base['enumerators']) if base['kind'] == 'enum' else 2 if base.get('name') == 'boolean' else None
            if count is not None and len(labels) == count:
                self.fail('E_CASE', 'default cannot follow exhaustive cases', token)
        self.check_siblings(self.inherited_members(node)+members)
        self.scope = outer
        node.update(members=members, complete=True)
        return node

    def enumeration(self, bitmask=False):
        name, token = self.identifier()
        node = self.declare(name, token, 'bitmask' if bitmask else 'enum', enumerators=[])
        self.expect('{')
        next_value, used = 0, set()
        while True:
            annotations = self.annotations()
            ename, etoken = self.identifier()
            value = next_value
            for annotation in annotations:
                if annotation['name'] == 'value':
                    self.retype_argument(annotation, 'value', {'kind': 'primitive', 'name': 'unsigned long long' if bitmask else 'unsigned long'})
                if annotation['name'] == ('position' if bitmask else 'value'):
                    value = annotation['arguments']['value']
                    if annotation['argument_kinds']['value'] != 'integer':
                        self.fail('E_ANNOTATION', 'enumerator value/position requires integer', etoken)
                elif bitmask and annotation['name'] == 'value':
                    explicit = annotation['arguments']['value']
                    if annotation['argument_kinds']['value'] != 'integer' or isinstance(explicit, Deferred) or explicit <= 0 or explicit & (explicit-1):
                        self.fail('E_ANNOTATION', 'bitmask value requires one positive bit', etoken)
                    position = explicit.bit_length()-1
                    chosen = next((a['arguments']['value'] for a in annotations if a['name'] == 'position'), position)
                    if position != chosen:
                        self.fail('E_ANNOTATION', 'bitmask value and position disagree', etoken)
                    value = position
            if not isinstance(value, Deferred) and not 0 <= value <= (63 if bitmask else 2**32-1):
                self.fail('E_RANGE', 'enumerator value/bit position outside range', etoken)
            key = repr(value) if isinstance(value, Deferred) else value
            if key in used:
                self.fail('E_ANNOTATION', 'duplicate enumerator value/bit position', etoken)
            used.add(key)
            actual = Integer(1 << value, 64, True) if bitmask and not isinstance(value, Deferred) else value
            entry = self.declare(ename, etoken, 'bit_value' if bitmask else 'enumerator', value=actual,
                                 value_kind='integer' if bitmask else node['qualified_name'])
            if bitmask:
                entry['position'] = value
            self.attach(entry, annotations)
            node['enumerators'].append(entry)
            next_value = value+1 if not isinstance(value, Deferred) else value
            if not self.accept(','):
                break
        self.expect('}')
        self.check_siblings(node['enumerators'])
        return node

    def bitfield(self, annotations):
        self.expect('bitfield')
        self.expect('<')
        width = self.bound()
        destination = self.type_spec() if self.accept(',') else None
        self.expect('>')
        if not isinstance(width, Deferred) and width > 64:
            self.fail('E_RANGE', 'bitfield width cannot exceed 64')
        if destination:
            base = self.underlying(destination)
            name = base.get('name')
            if base['kind'] != 'primitive' or name not in set(INTEGER_TYPES) | {'boolean'}:
                self.fail('E_TYPE', 'invalid bitfield destination')
            capacity = 1 if name == 'boolean' else INTEGER_TYPES[name][1].bit_length() + (0 if INTEGER_TYPES[name][0] == 0 else 1)
            if not isinstance(width, Deferred) and width > capacity:
                self.fail('E_RANGE', 'bitfield width exceeds destination capacity')
        fields = []
        while self.token.text != ';':
            name, token = self.identifier()
            fields.append(self.attach(self.declare(name, token, 'bitfield', width=width, destination=destination), annotations))
        if not fields:
            fields.append({'kind': 'padding', 'width': width})
        self.expect(';')
        return fields

    def constant_type(self, base, allow_any=False):
        return base['kind'] in ('enum', 'bitmask', 'fixed', 'string', 'wstring') or base['kind'] == 'template_type' and base['constraint'] in ('typename', 'enum') or base['kind'] == 'primitive' and base['name'] in set(INTEGER_TYPES) | {'boolean', 'char', 'wchar', 'float', 'double', 'long double'} | ({'any'} if allow_any else set())

    def constant_category(self, base):
        if base['kind'] == 'template_type':
            return base['qualified_name'] if base['constraint'] == 'enum' else 'generic'
        if base['kind'] == 'primitive':
            name = base['name']
            return 'integer' if name in INTEGER_TYPES else 'floating' if name in ('float', 'double', 'long double') else name
        return 'integer' if base['kind'] == 'bitmask' else base['qualified_name'] if base['kind'] == 'enum' else base['kind']

    @staticmethod
    def scalar(value):
        return value.value if isinstance(value, Binary) else value

    def definition(self):
        annotations = self.annotations()
        word = self.token.text
        if word == '@':
            node = self.annotation_declaration()
        elif word == 'module':
            node = self.module_declaration()
        elif word in ('interface', 'valuetype', 'eventtype', 'component', 'home', 'porttype', 'connector', 'abstract', 'local', 'custom'):
            node = self.object_declaration()
        elif word == 'exception':
            node = self.exception_declaration()
        elif word in ('typeid', 'typeprefix', 'import'):
            node = self.corba_directive()
        elif word == 'alias':
            node = self.template_alias()
        elif word == 'const':
            self.take()
            type_ = self.type_spec(constant=True)
            base = self.underlying(type_)
            if not self.constant_type(base):
                self.fail('E_CONST_TYPE', 'invalid constant declaration type')
            name, token = self.identifier()
            self.expect('=')
            with self.numeric_context(type_):
                value, category = self.expression()
            value = self.check_value(type_, value, category, token)
            if base.get('name') in INTEGER_TYPES and not isinstance(value, Deferred):
                lo, hi = INTEGER_TYPES[base['name']]
                value = Integer(value, hi.bit_length() + (lo < 0), lo == 0)
            node = self.declare(name, token, 'const', type=type_, value=value, value_kind=category)
            self.expect(';')
        elif word in ('bitmask', 'bitset'):
            self.require('extended')
            self.take()
            node = self.enumeration(True) if word == 'bitmask' else self.aggregate(word)
            self.expect(';')
        else:
            node = super().definition()
        return self.attach(node, annotations)

    def definitions(self, end):
        nodes = []
        while self.token.text not in (end, '<eof>'):
            node = self.definition()
            if not any(n is node for n in nodes):
                nodes.append(node)
        return nodes

    def parse(self):
        declarations = self.definitions('<eof>')
        if not declarations:
            self.fail('E_EMPTY', 'at least one definition is required')
        self.expect('<eof>')
        self.validate_port_cycles()
        for node in self.symbols.values():
            if node.get('complete') is False and node['kind'] in ('struct', 'union', 'component'):
                loc = node['location']
                self.fail('E_INCOMPLETE', 'unresolved aggregate forward declaration',
                          Token(node['name'], loc['file'], loc['line'], loc['column']))
        return export({'schema': 2, 'profile': self.profile, 'building_blocks': sorted(self.blocks),
                       'annotation_groups': sorted(self.groups), 'declarations': declarations,
                       'symbols': list(self.symbols.values()), 'references': self.references})


def parse_extended(text, filename='<input>', profile='b2', imports=None):
    if profile not in PROFILES:
        raise Diagnostic('E_PROFILE', f'unknown profile {profile}', Token('', filename, 1, 1))
    try:
        return ExtendedFrontend(text, filename, profile, imports).parse()
    except RecursionError:
        raise Diagnostic('E_LIMIT', 'frontend nesting limit exceeded', Token('', filename, 1, 1)) from None
    except DecimalException:
        raise Diagnostic('E_RANGE', 'decimal evaluation range exceeded', Token('', filename, 1, 1)) from None
