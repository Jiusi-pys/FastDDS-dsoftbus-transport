"""Interface, value, component, CORBA and template-module declaration rules."""
from dataclasses import asdict
import re
from idl_frontend import Token
from idl_numeric import Deferred, Integer, NumericError, binary, unary


class DeclarationMixin:
    def base_reference(self, expected, complete=True):
        token = self.token
        node = self.resolve()
        if node['kind'] == 'typedef':
            node = self.underlying(node['type'])
        if node['kind'] == 'template_type' and node['constraint'] in (expected, 'typename'):
            return node['qualified_name']
        if node['kind'] != expected:
            self.fail('E_TYPE', f'expected {expected} reference', token)
        if complete and node.get('complete') is False:
            self.fail('E_INCOMPLETE', 'base must be fully defined', token)
        return node['qualified_name']

    def reference_list(self, expected):
        result = [self.base_reference(expected)]
        while self.accept(','):
            result.append(self.base_reference(expected))
        if len(set(result)) != len(result):
            self.fail('E_INHERITANCE', 'duplicate base/support reference')
        return result

    def object_declaration(self):
        modifier = self.take().text if self.token.text in ('abstract', 'local', 'custom') else None
        token = self.take()
        kind = token.text
        required = {'interface': 'interfaces-basic', 'valuetype': 'value-types',
                    'eventtype': 'ccm', 'component': 'components-basic', 'home': 'homes',
                    'porttype': 'ports-connectors', 'connector': 'ports-connectors'}
        if kind not in required:
            self.fail('E_SYNTAX', 'invalid declaration after modifier', token)
        self.require(required[kind], token)
        if modifier:
            if modifier == 'local' and kind == 'interface':
                self.require('corba-interfaces', token)
            elif modifier == 'abstract' and kind in ('interface', 'valuetype'):
                self.require('corba-values', token)
            elif modifier in ('abstract', 'custom') and kind == 'eventtype':
                self.require('ccm', token)
            elif modifier == 'custom' and kind == 'valuetype':
                self.require('corba-values', token)
            else:
                self.fail('E_SYNTAX', 'modifier not applicable to declaration', token)
        name, ntoken = self.identifier()
        outer, path = self.scope, self.scope+(name,)
        old = self.symbols.get(path)
        if old and old['kind'] == kind and (not old['complete'] or self.token.text == ';'):
            if old.get('modifier') != modifier:
                self.fail('E_INHERITANCE', 'forward declaration modifier mismatch', ntoken)
            node = old
        else:
            node = self.declare(name, ntoken, kind, modifier=modifier, complete=False, bases=[], supports=[])
        if self.accept(';'):
            if kind not in ('interface', 'valuetype', 'eventtype', 'component', 'porttype') or modifier == 'custom':
                self.fail('E_SYNTAX', 'invalid forward declaration', ntoken)
            return {'kind': 'forward', 'target': node['qualified_name']}
        if kind == 'valuetype' and self.token.text not in (':', 'supports', '{'):
            self.require('corba-values', token)
            if modifier:
                self.fail('E_SYNTAX', 'boxed value cannot have modifier', token)
            node['kind'], node['type'] = 'value_box', self.type_spec()
            node['complete'] = True
            self.expect(';')
            return node
        if self.accept(':'):
            if kind == 'porttype':
                self.fail('E_SYNTAX', 'porttype cannot inherit', token)
            node['truncatable'] = self.accept('truncatable')
            if node['truncatable']:
                self.require('corba-values', token)
                if kind not in ('valuetype', 'eventtype') or modifier == 'custom':
                    self.fail('E_INHERITANCE', 'invalid truncatable declaration', token)
            node['bases'] = self.reference_list(kind)
            if kind in ('component', 'home', 'connector') and len(node['bases']) != 1:
                self.fail('E_INHERITANCE', 'single inheritance required', token)
            if kind in ('valuetype', 'eventtype') and len(node['bases']) > 1:
                self.require('corba-values', token)
        if self.accept('supports'):
            if kind not in ('valuetype', 'eventtype', 'component', 'home'):
                self.fail('E_SYNTAX', 'supports is unavailable on this declaration', token)
            self.require('ccm' if kind in ('component', 'home') else 'value-types', token)
            node['supports'] = self.reference_list('interface')
        if kind == 'home':
            self.expect('manages')
            node['manages'] = self.base_reference('component')
            if self.accept('primarykey'):
                self.require('ccm', token)
                node['primarykey'] = self.base_reference('valuetype')
                key_type = self.symbols[tuple(node['primarykey'][2:].split('::'))]
                node['primarykey_constraint'] = {'ancestor': '::Components::PrimaryKeyBase',
                                                 'deferred': key_type['kind'] == 'template_type'}
                if not node['primarykey_constraint']['deferred'] and not self.derives_from(node['primarykey'], '::Components::PrimaryKeyBase'):
                    self.fail('E_TYPE', 'primarykey type must derive from Components::PrimaryKeyBase', token)
        self.validate_bases(node, ntoken)
        self.expect('{')
        self.scope = path
        declarations = []
        while self.token.text not in ('}', '<eof>'):
            declarations.extend(self.object_member(kind))
        self.expect('}')
        self.expect(';')
        if kind == 'porttype' and (not declarations or declarations[0]['kind'] not in ('provides', 'uses', 'port', 'mirrorport')):
            self.fail('E_EMPTY', 'porttype must begin with a port reference', ntoken)
        if kind == 'connector' and not declarations:
            self.fail('E_EMPTY', 'connector requires an export', ntoken)
        if modifier == 'abstract' and any(n['kind'] in ('state_member', 'factory') for n in declarations):
            self.fail('E_TYPE', 'abstract values cannot have state or initializers', ntoken)
        if kind == 'valuetype' and modifier == 'custom' and node.get('truncatable'):
            self.fail('E_TYPE', 'custom values cannot be truncatable', ntoken)
        self.check_siblings(self.inherited_members(node)+declarations)
        node.update(declarations=declarations, complete=True)
        self.scope = outer
        return node

    def validate_bases(self, node, token):
        for index, name in enumerate(node['bases']):
            base = self.symbols[tuple(name[2:].split('::'))]
            if base['kind'] == 'template_type':
                continue
            if node.get('modifier') == 'abstract' and base.get('modifier') != 'abstract':
                self.fail('E_INHERITANCE', 'abstract type requires abstract bases', token)
            if node['kind'] == 'interface' and node.get('modifier') != 'local' and base.get('modifier') == 'local':
                self.fail('E_INHERITANCE', 'non-local interface cannot inherit local interface', token)
            if node['kind'] in ('valuetype', 'eventtype') and index > 0 and base.get('modifier') != 'abstract':
                self.fail('E_INHERITANCE', 'only first value base may be concrete', token)
            if node['kind'] in ('valuetype', 'eventtype') and node.get('modifier') != 'custom' and self.has_custom_base(name):
                self.fail('E_INHERITANCE', 'non-custom values cannot inherit custom values', token)
        # Distinct inherited declarations with the same name are ambiguous even
        # when the derived type never uses the conflicting name.
        inherited = {}
        def collect(name, visited):
            if name in visited:
                return
            visited.add(name)
            prefix = tuple(name[2:].split('::'))
            for path, member in self.symbols.items():
                if path[:-1] == prefix:
                    inherited.setdefault(path[-1].lower(), {})[member['qualified_name']] = member['kind']
            for base in self.symbols[prefix].get('bases', []):
                collect(base, visited)
        for base in node['bases']:
            collect(base, set())
        if any(len(targets) > 1 and any(kind in ('operation', 'attribute', 'state_member', 'member', 'provides', 'uses', 'emits', 'consumes', 'publishes', 'port', 'mirrorport') for kind in targets.values()) for targets in inherited.values()):
            self.fail('E_AMBIGUOUS', 'inherited declarations have conflicting names', token)

    def has_custom_base(self, name, visited=None):
        visited = set() if visited is None else set(visited)
        if name in visited:
            return False
        visited.add(name)
        node = self.symbols[tuple(name[2:].split('::'))]
        return node.get('modifier') == 'custom' or any(self.has_custom_base(base, visited) for base in node.get('bases', []))

    def object_member(self, owner):
        annotations = self.annotations()
        word = self.token.text
        general = owner in ('interface', 'valuetype', 'eventtype', 'home')
        if word in ('typedef', 'const', 'struct', 'union', 'enum', 'native', 'exception', 'bitmask', 'bitset', '@'):
            if not general:
                self.fail('E_SYNTAX', f'{owner} cannot contain nested type declarations')
            self.require('interfaces-full')
            return [self.attach(self.definition(), annotations)]
        if word in ('typeid', 'typeprefix', 'import'):
            if not general:
                self.fail('E_SYNTAX', 'invalid CORBA directive context')
            return [self.attach(self.corba_directive(), annotations)]
        if word in ('readonly', 'attribute'):
            nodes = self.attribute()
        elif word in ('public', 'private'):
            if owner not in ('valuetype', 'eventtype'):
                self.fail('E_SYNTAX', 'state member requires value/event type')
            visibility = self.take().text
            type_ = self.type_spec()
            nodes = self.declarators(type_, 'state_member')
            for node in nodes:
                node['visibility'] = visibility
            self.expect(';')
        elif word in ('factory', 'finder'):
            if owner not in ('valuetype', 'eventtype', 'home') or word == 'finder' and owner != 'home':
                self.fail('E_SYNTAX', 'initializer is unavailable in this scope')
            if word == 'finder':
                self.require('ccm')
            nodes = [self.operation(initializer=self.take().text)]
        elif word in ('provides', 'uses', 'emits', 'publishes', 'consumes', 'port', 'mirrorport'):
            if owner not in ('component', 'porttype', 'connector'):
                self.fail('E_SYNTAX', 'port declaration outside component/port scope')
            nodes = [self.port_declaration()]
        else:
            if not general:
                self.fail('E_SYNTAX', f'operations unavailable in {owner}')
            nodes = [self.operation()]
        return [self.attach(node, annotations) for node in nodes]

    def exception_declaration(self):
        self.require('interfaces-basic')
        self.expect('exception')
        name, token = self.identifier()
        node = self.declare(name, token, 'exception', members=[])
        outer, self.scope = self.scope, self.scope+(name,)
        self.expect('{')
        while self.token.text not in ('}', '<eof>'):
            annotations = self.annotations()
            type_ = self.type_spec()
            fields = self.declarators(type_, 'member')
            node['members'].extend(self.attach(n, annotations) for n in fields)
            self.expect(';')
        self.expect('}')
        self.expect(';')
        self.check_siblings(node['members'])
        self.scope = outer
        return node

    def exception_list(self):
        self.expect('(')
        values = [self.base_reference('exception')]
        while self.accept(','):
            values.append(self.base_reference('exception'))
        self.expect(')')
        if len(values) != len(set(values)):
            self.fail('E_EXCEPTION', 'duplicate exception in raises list')
        return values

    def operation(self, initializer=None):
        oneway = self.accept('oneway') if initializer is None else False
        if oneway:
            self.require('corba-interfaces')
        result = {'kind': 'void'} if initializer or self.accept('void') else self.type_spec()
        name, token = self.identifier()
        node = self.declare(name, token, initializer or 'operation', return_type=result,
                            parameters=[], raises=[], oneway=oneway)
        outer, self.scope = self.scope, self.scope+(name,)
        self.expect('(')
        if self.token.text != ')':
            while True:
                annotations = self.annotations()
                direction = self.take()
                if direction.text not in ('in', 'out', 'inout') or initializer and direction.text != 'in':
                    self.fail('E_PARAMETER', 'invalid parameter direction', direction)
                type_ = self.type_spec()
                pname, ptoken = self.identifier()
                parameter = self.declare(pname, ptoken, 'parameter', type=type_, direction=direction.text)
                node['parameters'].append(self.attach(parameter, annotations))
                if not self.accept(','):
                    break
        self.expect(')')
        if self.accept('raises'):
            node['raises'] = self.exception_list()
        if self.accept('context'):
            self.require('corba-interfaces')
            self.expect('(')
            contexts = []
            while True:
                contexts.append(self.string_literal_sequence())
                if not self.accept(','):
                    break
            self.expect(')')
            node['contexts'] = contexts
        self.expect(';')
        self.scope = outer
        if oneway:
            self.check_oneway(node)
        return node

    def check_oneway(self, node):
        if node['return_type']['kind'] != 'void' or any(p['direction'] != 'in' for p in node['parameters']) or node['raises']:
            self.fail('E_ONEWAY', 'oneway operation requires void, in-only parameters and no raises')

    def attribute(self):
        readonly = self.accept('readonly')
        self.expect('attribute')
        type_ = self.type_spec()
        nodes = []
        while True:
            annotations = self.annotations()
            name, token = self.identifier()
            nodes.append(self.attach(self.declare(name, token, 'attribute', type=type_), annotations))
            if not self.accept(','):
                break
        for node in nodes:
            node.update(readonly=readonly, getraises=[], setraises=[])
        if self.accept('raises'):
            if not readonly or len(nodes) != 1:
                self.fail('E_EXCEPTION', 'attribute raises requires one readonly attribute')
            nodes[0]['getraises'] = self.exception_list()
        else:
            for keyword in ('getraises', 'setraises'):
                if self.accept(keyword):
                    if readonly or len(nodes) != 1:
                        self.fail('E_EXCEPTION', 'get/setraises requires one writable attribute')
                    nodes[0][keyword] = self.exception_list()
        self.expect(';')
        return nodes

    def port_declaration(self):
        keyword = self.take()
        kind = keyword.text
        if kind in ('port', 'mirrorport'):
            self.require('ports-connectors', keyword)
            target = self.base_reference('porttype', complete=False)
            multiple = False
        elif kind in ('emits', 'publishes', 'consumes'):
            self.require('ccm', keyword)
            target = self.base_reference('eventtype')
            multiple = False
        else:
            multiple = self.accept('multiple') if kind == 'uses' else False
            if multiple:
                self.require('ccm', keyword)
            if self.accept('Object'):
                self.require('ccm', keyword)
                target = 'Object'
            else:
                target = self.base_reference('interface')
        name, token = self.identifier()
        self.expect(';')
        return self.declare(name, token, kind, target=target, multiple=multiple)

    def derives_from(self, name, ancestor, visited=None):
        visited = set() if visited is None else set(visited)
        if name in visited:
            return False
        if name == ancestor:
            return True
        visited.add(name)
        node = self.symbols[tuple(name[2:].split('::'))]
        return any(self.derives_from(base, ancestor, visited) for base in node.get('bases', []))

    def validate_port_cycles(self):
        visited, active = set(), set()
        def visit(name):
            if name in active:
                node = self.symbols[tuple(name[2:].split('::'))]
                location = node['location']
                self.fail('E_PORT_CYCLE', 'cyclic embedded porttype definitions', Token(node['name'], location['file'], location['line'], location['column']))
            if name in visited:
                return
            active.add(name)
            node = self.symbols[tuple(name[2:].split('::'))]
            for member in node.get('declarations', []):
                if member['kind'] in ('port', 'mirrorport'):
                    visit(member['target'])
            active.remove(name)
            visited.add(name)
        for node in self.symbols.values():
            if node['kind'] == 'porttype':
                visit(node['qualified_name'])

    def corba_directive(self):
        self.require('corba-interfaces')
        keyword = self.take()
        if keyword.text == 'import':
            if self.token.text.startswith('"'):
                value = self.string_literal_sequence()
                found = [n for n in self.symbols.values() if n['kind'] in ('module', 'interface', 'valuetype', 'eventtype', 'value_box') and self.repository_identity(n) == value]
                if len(found) != 1:
                    self.fail('E_IMPORT', 'repository ID must identify exactly one available naming scope; provide --idl-import if external', keyword)
                target = found[0]['qualified_name']
            else:
                node = self.resolve()
                if node['kind'] not in ('module', 'interface', 'valuetype', 'eventtype'):
                    self.fail('E_IMPORT', 'import requires a naming scope', keyword)
                target = node['qualified_name']
            self.expect(';')
            return {'kind': 'import', 'target': target}
        root_prefix = keyword.text == 'typeprefix' and self.token.text == '::' and self.tokens[self.index+1].text.startswith('"')
        if root_prefix:
            self.take()
            node = None
        else:
            node = self.resolve()
        value = self.string_literal_sequence()
        if keyword.text == 'typeid':
            if node['kind'] not in ('module', 'interface', 'struct', 'union', 'valuetype', 'eventtype', 'exception', 'component', 'home', 'porttype', 'connector', 'value_box'):
                self.fail('E_TYPE', 'repository ID requires a scope-defining construct', keyword)
            if 'repository_id' in node:
                self.fail('E_TYPE', 'repository ID cannot be redefined', keyword)
            if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_-]*:\S+', value) or value.startswith('IDL:') and not re.fullmatch(r'IDL:[^:\s]+:\d+\.\d+', value):
                self.fail('E_TYPE', 'invalid repository ID', keyword)
            node['repository_id'] = value
        else:
            if node is not None and node['kind'] not in ('module', 'interface', 'valuetype', 'eventtype', 'value_box'):
                self.fail('E_TYPE', 'repository prefix requires module/interface/value/event scope', keyword)
            if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*(?:/[A-Za-z0-9_.-]+)*', value):
                self.fail('E_TYPE', 'invalid repository prefix', keyword)
            if root_prefix:
                self.root_type_prefix = value
            else:
                node['type_prefix'] = value
        self.expect(';')
        return {'kind': keyword.text, 'target': '::' if root_prefix else node['qualified_name'], 'value': value}

    def repository_identity(self, node):
        if 'repository_id' in node:
            return node['repository_id']
        path = tuple(node['qualified_name'][2:].split('::'))
        prefix = getattr(self, 'root_type_prefix', None)
        for length in range(len(path), 0, -1):
            candidate = self.symbols.get(path[:length], {}).get('type_prefix')
            if candidate is not None:
                prefix = candidate
                break
        return 'IDL:' + ((prefix + '/') if prefix else '') + '/'.join(path) + ':1.0'

    def module_declaration(self):
        self.expect('module')
        start = self.token
        spelling = self.name_parts()
        if self.token.text == '<':
            self.require('templates', start)
            existing = self.find_spelling(spelling)
            if existing is not None:
                return self.template_instantiation(existing)
            if '::' in spelling:
                self.fail('E_UNRESOLVED', 'unknown template module', start)
            return self.template_declaration(spelling, start)
        if '::' in spelling:
            self.fail('E_SYNTAX', 'module declaration requires an identifier', start)
        path = self.scope+(spelling,)
        node = self.symbols.get(path)
        if not node or node['kind'] != 'module':
            node = self.declare(spelling, start, 'module', declarations=[])
        outer, self.scope = self.scope, path
        self.expect('{')
        body = self.definitions('}')
        if not body:
            self.fail('E_EMPTY', 'module requires a declaration', start)
        self.expect('}')
        self.expect(';')
        node['declarations'].extend(n for n in body if not any(n is old for old in node['declarations']))
        self.scope = outer
        return node

    def template_declaration(self, name, token):
        if self.template_stack or any(self.symbols.get(self.scope[:i], {}).get('kind') == 'template_module' for i in range(1, len(self.scope)+1)):
            self.fail('E_TEMPLATE', 'template modules cannot be nested', token)
        node = self.declare(name, token, 'template_module', parameters=[])
        outer, self.scope = self.scope, self.scope+(name,)
        self.expect('<')
        while True:
            word = self.take()
            if word.text == 'const':
                type_ = self.type_spec(constant=True)
                pname, ptoken = self.identifier()
                base = self.underlying(type_)
                if not self.constant_type(base):
                    self.fail('E_TEMPLATE', 'invalid constant parameter type', word)
                category = self.constant_category(base)
                formal = self.declare(pname, ptoken, 'template_const', type=type_, value=Deferred(pname), value_kind=category)
            else:
                if word.text not in ('typename', 'interface', 'valuetype', 'eventtype', 'struct', 'union', 'exception', 'enum', 'sequence'):
                    self.fail('E_TEMPLATE', 'invalid template formal kind', word)
                sequence_element = None
                sequence_type = None
                if word.text == 'sequence' and self.accept('<'):
                    sequence_element = self.type_spec()
                    bound = self.bound() if self.accept(',') else None
                    self.expect('>')
                    sequence_type = {'kind': 'sequence', 'element': sequence_element, 'bound': bound}
                pname, ptoken = self.identifier()
                formal = self.declare(pname, ptoken, 'template_type', constraint=word.text, sequence_type=sequence_type)
            node['parameters'].append(formal)
            if not self.accept(','):
                break
        self.expect('>')
        self.expect('{')
        first, depth = self.index, 1
        while depth:
            item = self.take()
            if item.text == '<eof>':
                self.fail('E_SYNTAX', 'unterminated template module', item)
            depth += (item.text == '{') - (item.text == '}')
        body = self.tokens[first:self.index-1]
        self.expect(';')
        self.templates[node['qualified_name']] = body
        node['body_tokens'] = [asdict(t) for t in body]
        node['declarations'] = self.parse_template_body(body)
        self.scope = outer
        return node

    def parse_template_body(self, body):
        old_tokens, old_index = self.tokens, self.index
        self.tokens, self.index = body+[Token('<eof>', self.token.file, self.token.line, self.token.column)], 0
        try:
            nodes = self.definitions('<eof>')
            if not nodes:
                self.fail('E_EMPTY', 'template module requires declarations')
            return nodes
        finally:
            self.tokens, self.index = old_tokens, old_index

    def template_instantiation(self, template, alias=False):
        if template['kind'] != 'template_module':
            self.fail('E_TEMPLATE', 'name does not denote a template module')
        token = self.token
        self.expect('<')
        arguments = []
        bindings = {}
        for index, formal in enumerate(template['parameters']):
            if index:
                self.expect(',')
            if alias:
                actual_token = self.token
                if self.tokens[self.index+1].text not in (',', '>') or not actual_token.text.lstrip('_').isidentifier():
                    self.fail('E_TEMPLATE', 'alias arguments must be formal parameter names', actual_token)
                from idl_frontend import KEYWORDS
                if actual_token.text in KEYWORDS:
                    self.fail('E_TEMPLATE', 'alias arguments must be formal parameter names', actual_token)
                parameter = self.resolve()
                if not (parameter['kind'] in ('template_type', 'template_const') or parameter.get('template_parameter')):
                    self.fail('E_TEMPLATE', 'alias arguments must come from enclosing template formals', actual_token)
                if formal['kind'] == 'template_const':
                    if 'value' not in parameter:
                        self.fail('E_TEMPLATE', 'alias requires constant formal', actual_token)
                    type_ = self.bind_template_type(formal['type'], bindings)
                    value = self.check_value(type_, parameter['value'], parameter['value_kind'], actual_token)
                    arguments.append({'value': value, 'value_kind': parameter['value_kind'], 'type': type_})
                else:
                    if parameter['kind'] in ('const', 'template_const'):
                        self.fail('E_TEMPLATE', 'alias requires a type formal', actual_token)
                    arguments.append({'type': {'kind': 'named', 'name': parameter['qualified_name']}})
            elif formal['kind'] == 'template_const':
                type_ = self.bind_template_type(formal['type'], bindings)
                with self.numeric_context(type_):
                    value, category = self.expression()
                value = self.check_value(type_, value, category, self.token)
                arguments.append({'value': value, 'value_kind': category, 'type': type_})
            else:
                if formal['constraint'] == 'exception':
                    type_ = {'kind': 'named', 'name': self.base_reference('exception')}
                else:
                    type_ = self.type_spec()
                arguments.append({'type': type_})
            if formal['kind'] == 'template_type':
                type_ = arguments[-1]['type']
                base = self.underlying(type_)
                constraint = formal['constraint']
                if constraint != 'typename' and base['kind'] != constraint and not (base['kind'] == 'template_type' and base['constraint'] == constraint):
                    self.fail('E_TEMPLATE', f'actual parameter must satisfy {constraint}')
                if formal.get('sequence_type') is not None and base['kind'] == 'sequence':
                    expected = self.bind_template_type(formal['sequence_type'], bindings)
                    if self.canonical_type(expected['element']) != self.canonical_type(base['element']) or expected['bound'] is not None and expected['bound'] != base['bound']:
                        self.fail('E_TEMPLATE', 'actual sequence does not match formal sequence declaration')
            bindings[formal['qualified_name']] = arguments[-1]
            bindings[formal['name']] = arguments[-1]
        self.expect('>')
        name, ntoken = self.identifier()
        self.expect(';')
        if template['qualified_name'] in self.template_stack or len(self.template_stack) >= 32:
            self.fail('E_TEMPLATE', 'recursive template instantiation', token)
        node = self.declare(name, ntoken, 'module', template=template['qualified_name'], arguments=arguments, declarations=[])
        outer, self.scope = self.scope, self.scope+(name,)
        self.template_stack.append(template['qualified_name'])
        try:
            for formal, actual in zip(template['parameters'], arguments):
                if formal['kind'] == 'template_const':
                    self.declare(formal['name'], ntoken, 'const', template_parameter=True, **actual)
                else:
                    self.declare(formal['name'], ntoken, 'typedef', template_parameter=True, type=actual['type'])
            node['declarations'] = self.parse_template_body(self.templates[template['qualified_name']])
        finally:
            self.template_stack.pop()
            self.scope = outer
        return node

    def bind_template_type(self, type_, bindings):
        if isinstance(type_, Deferred):
            def evaluate(expression):
                if isinstance(expression, str) and expression in bindings:
                    return bindings[expression]['value']
                if isinstance(expression, list):
                    operands = [evaluate(v) for v in expression[1:]]
                    return unary(expression[0], operands[0]) if len(operands) == 1 else binary(expression[0], *operands)
                return Integer(expression) if isinstance(expression, int) else Deferred(expression)
            try:
                return evaluate(type_.expression)
            except (NumericError, ZeroDivisionError):
                self.fail('E_TEMPLATE', 'invalid dependent template expression')
        if not isinstance(type_, dict):
            return type_
        if type_.get('kind') == 'named' and type_['name'] in bindings:
            return bindings[type_['name']]['type']
        return {k: self.bind_template_type(v, bindings) for k, v in type_.items()}

    def canonical_type(self, type_):
        type_ = self.underlying(type_)
        if type_['kind'] == 'primitive':
            aliases = {'int16': 'short', 'int32': 'long', 'int64': 'long long', 'uint16': 'unsigned short', 'uint32': 'unsigned long', 'uint64': 'unsigned long long'}
            return ('primitive', aliases.get(type_['name'], type_['name']))
        return type_.get('qualified_name') or type_

    def template_alias(self):
        self.require('templates')
        token = self.take()
        if not self.template_stack and not any(self.symbols.get(self.scope[:i], {}).get('kind') == 'template_module' for i in range(1, len(self.scope)+1)):
            self.fail('E_TEMPLATE', 'alias only allowed in template module', token)
        template = self.resolve()
        return self.template_instantiation(template, alias=True)
