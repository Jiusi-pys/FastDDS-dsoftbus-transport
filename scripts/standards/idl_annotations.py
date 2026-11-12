"""IDL annotation declarations, applications and frontend constraints."""
from idl_profiles import ANNOTATIONS
from idl_numeric import Deferred, Integer
from idl_frontend import Token
from dataclasses import asdict

CONSTRUCTED = {'struct', 'union', 'enum', 'bitset', 'bitmask', 'interface', 'valuetype', 'eventtype', 'component'}
DATA = {'member', 'parameter', 'attribute', 'typedef', 'const', 'state_member'}


class AnnotationMixin:
    def annotations(self):
        result = []
        while self.token.text == '@' and self.tokens[self.index+1].text != 'annotation':
            token = self.take()
            self.require('annotations', token)
            spelling = self.name_parts(allow_keyword=True)
            standard = ANNOTATIONS.get(spelling) if '::' not in spelling else None
            custom = self.find_spelling(spelling)
            if standard:
                group, signature = standard
                if group not in self.groups:
                    self.fail('E_PROFILE', f'annotation group {group} is unavailable', token)
            elif custom:
                if custom['kind'] != 'annotation':
                    self.fail('E_ANNOTATION', 'annotation name denotes a different declaration', token)
                signature = {n['name']: (n['type'], n.get('default'), 'default' not in n)
                             for n in custom['members']}
                group = 'custom'
            else:
                signature, group = {}, 'unknown'
            arguments, kinds, expressions = {}, {}, {}
            if self.accept('('):
                if self.token.text == ')':
                    self.fail('E_ANNOTATION', 'empty annotation parameter list', token)
                named = self.tokens[self.index+1].text == '='
                while True:
                    if named:
                        member, _ = self.identifier()
                        self.expect('=')
                    else:
                        if group != 'unknown' and len(signature) != 1:
                            self.fail('E_ANNOTATION', 'positional syntax requires exactly one annotation member', token)
                        member = next(iter(signature), 'value')
                    if member in arguments:
                        self.fail('E_ANNOTATION', 'duplicate annotation argument', token)
                    if group != 'unknown' and member not in signature:
                        self.fail('E_ANNOTATION', f'unknown annotation member {member}', token)
                    category = signature.get(member, ('any', None, False))[0]
                    old_choices, old_ignored = self.annotation_choices, self.ignoring_annotation
                    old_scope, old_standard = self.annotation_scope, self.standard_annotation
                    self.annotation_choices = category if isinstance(category, tuple) else ()
                    self.ignoring_annotation = group == 'unknown'
                    self.annotation_scope = tuple(custom['qualified_name'][2:].split('::')) if custom and custom['kind'] == 'annotation' else None
                    self.standard_annotation = spelling if standard else None
                    first, reference_count = self.index, len(self.references)
                    try:
                        with self.numeric_context(category if isinstance(category, dict) else None,
                                                  wide=standard is not None and category == 'any'):
                            value, kind = self.expression()
                    finally:
                        self.annotation_choices, self.ignoring_annotation = old_choices, old_ignored
                        self.annotation_scope, self.standard_annotation = old_scope, old_standard
                    if standard is not None and category == 'any':
                        expressions[member] = {'tokens': [asdict(t) for t in self.tokens[first:self.index]], 'scope': list(self.scope)}
                        del self.references[reference_count:]
                    arguments[member], kinds[member] = value, kind
                    if not named or not self.accept(','):
                        break
                self.expect(')')
            for member, (category, default, required) in signature.items():
                if member not in arguments:
                    if required:
                        self.fail('E_ANNOTATION', f'missing annotation member {member}', token)
                    arguments[member] = default
                    kinds[member] = (next(n['default_kind'] for n in custom['members'] if n['name'] == member) if group == 'custom' else 'boolean' if category == 'boolean' else
                                     'annotation-enum' if isinstance(category, tuple) else category)
                else:
                    value, kind = arguments[member], kinds[member]
                    if isinstance(category, dict):
                        arguments[member] = self.check_value(category, value, kind, token)
                    elif isinstance(category, tuple):
                        if kind != 'annotation-enum' or value not in category:
                            self.fail('E_ANNOTATION', 'invalid annotation enumerator', token)
                    elif category in ('uint16', 'uint32'):
                        if kind != 'integer' or not isinstance(value, Deferred) and not 0 <= value < 2**int(category[4:]):
                            self.fail('E_ANNOTATION', f'annotation requires {category}', token)
                    elif category != 'any' and kind != category:
                        self.fail('E_ANNOTATION', f'annotation requires {category}', token)
            result.append({'name': spelling, 'group': group, 'arguments': arguments,
                           'argument_kinds': kinds, 'ignored': group == 'unknown',
                           'argument_expressions': expressions, 'location': {'file': token.file, 'line': token.line, 'column': token.column}})
        return result

    def attach(self, node, annotations):
        if not annotations:
            return node
        if node['kind'] == 'typedef_group':
            for child in node['declarations']:
                self.attach(child, annotations)
            return node
        previous = node.setdefault('annotations', [])
        known = {a['name'] for a in previous if not a['ignored']}
        for annotation in annotations:
            name = annotation['name']
            if not annotation['ignored'] and name in known and name != 'verbatim':
                self.fail('E_ANNOTATION', f'duplicate annotation {name}')
            known.add(name)
            kind = node['kind']
            if annotation['group'] in ('unknown', 'custom'):
                previous.append(annotation)
                continue
            allowed = {
                'id': DATA | {'operation', 'enumerator', 'bit_value'},
                'position': DATA | {'operation', 'enumerator', 'bit_value'},
                'autoid': CONSTRUCTED, 'extensibility': CONSTRUCTED,
                'final': CONSTRUCTED, 'appendable': CONSTRUCTED, 'mutable': CONSTRUCTED,
                'key': {'member'}, 'must_understand': {'member'},
                'default_literal': {'enumerator'}, 'nested': CONSTRUCTED,
                'service': {'interface'}, 'oneway': {'operation'}, 'ami': {'interface', 'operation'},
                'bit_bound': {'enum', 'bitmask', 'typedef', 'member', 'state_member'},
                'external': DATA, 'optional': DATA,
                'default': DATA, 'range': DATA, 'min': DATA, 'max': DATA, 'unit': DATA,
                'value': DATA | {'enumerator', 'bit_value'},
            }.get(name)
            if allowed is not None and kind not in allowed:
                self.fail('E_ANNOTATION', f'{name} cannot annotate {kind}')
            args, kinds = annotation['arguments'], annotation['argument_kinds']
            if name in ('value', 'default', 'min', 'max', 'range') and 'type' in node:
                for member, value in args.items():
                    self.retype_argument(annotation, member, node['type'])
                    args[member] = self.check_value(node['type'], args[member], kinds[member], self.token)
            if name == 'range' and not any(isinstance(v, Deferred) for v in args.values()):
                if kinds['min'] != kinds['max'] or self.scalar(args['min']) > self.scalar(args['max']):
                    self.fail('E_ANNOTATION', 'range minimum exceeds maximum or categories differ')
            if name == 'oneway' and args['value']:
                self.check_oneway(node)
            if name == 'bit_bound' and not isinstance(args['value'], Deferred) and not 1 <= args['value'] <= 64:
                self.fail('E_ANNOTATION', 'bit_bound must be between 1 and 64')
            previous.append(annotation)
        extensibility = {a['name'].upper() if a['name'] in ('final', 'appendable', 'mutable') else a['arguments'].get('value')
                         for a in previous if a['name'] in ('final', 'appendable', 'mutable', 'extensibility')}
        if len(extensibility) > 1:
            self.fail('E_ANNOTATION', 'conflicting extensibility annotations')
        self.validate_annotation_constraints(node)
        return node

    def retype_argument(self, annotation, member, type_):
        expression = annotation.get('argument_expressions', {}).get(member)
        if not expression or self.underlying(type_)['kind'] == 'template_type':
            return
        old_tokens, old_index, old_scope = self.tokens, self.index, self.scope
        tokens = [Token(**t) for t in expression['tokens']]
        last = tokens[-1]
        self.tokens, self.index, self.scope = tokens+[Token('<eof>', last.file, last.line, last.column)], 0, tuple(expression['scope'])
        try:
            with self.numeric_context(type_):
                value, kind = self.expression()
            self.expect('<eof>')
            annotation['arguments'][member] = value
            annotation['argument_kinds'][member] = kind
        finally:
            self.tokens, self.index, self.scope = old_tokens, old_index, old_scope

    def validate_annotation_constraints(self, node):
        lower, upper, defaults = [], [], []
        for annotation in node.get('annotations', []):
            name, args = annotation['name'], annotation['arguments']
            if name in ('min', 'range'):
                lower.append(args['min'] if name == 'range' else args['value'])
            if name in ('max', 'range'):
                upper.append(args['max'] if name == 'range' else args['value'])
            if name in ('default', 'value') and 'type' in node:
                defaults.append(args['value'])
        if node['kind'] == 'const':
            defaults.append(node['value'])
        concrete = lambda values: [self.scalar(v) for v in values if not isinstance(v, Deferred)]
        lows, highs, values = concrete(lower), concrete(upper), concrete(defaults)
        try:
            if lows and highs and max(lows) > min(highs):
                self.fail('E_ANNOTATION', 'annotated minimum exceeds maximum')
            if any(lows and v < max(lows) or highs and v > min(highs) for v in values):
                self.fail('E_RANGE', 'default/value violates annotated range')
        except TypeError:
            self.fail('E_ANNOTATION', 'annotation range values are not comparable')
        bound = self.annotation_value(node, 'bit_bound')
        if bound is None or isinstance(bound, Deferred):
            return
        if node['kind'] in ('enum', 'bitmask'):
            for entry in node['enumerators']:
                value = entry.get('position', entry['value'])
                if not isinstance(value, Deferred) and value >= (bound if node['kind'] == 'bitmask' else 2**bound):
                    self.fail('E_RANGE', 'enumerator exceeds annotated bit bound')
        elif 'type' in node:
            base = self.underlying(node['type'])
            from idl_extended_numeric import INTEGER_TYPES
            if base['kind'] != 'primitive' or base['name'] not in INTEGER_TYPES:
                self.fail('E_ANNOTATION', 'bit_bound requires integer, enum or bitmask type')
            lo, hi = INTEGER_TYPES[base['name']]
            if bound > hi.bit_length()+(lo < 0):
                self.fail('E_RANGE', 'bit_bound exceeds declared integer width')
            minimum, maximum = (0, 2**bound-1) if lo == 0 else (-2**(bound-1), 2**(bound-1)-1)
            if any(not minimum <= v <= maximum for v in values):
                self.fail('E_RANGE', 'default/value exceeds bit_bound')

    @staticmethod
    def annotation_value(node, name, default=None):
        for annotation in node.get('annotations', []):
            if annotation['name'] == name and not annotation['ignored']:
                return annotation['arguments'].get('value', default)
        return default

    def check_type_constraints(self, type_, value, kind, token):
        if isinstance(value, Deferred):
            return
        while type_['kind'] == 'named':
            node = self.symbols[tuple(type_['name'][2:].split('::'))]
            for annotation in node.get('annotations', []):
                name, args = annotation['name'], annotation['arguments']
                if name == 'bit_bound' and not isinstance(args['value'], Deferred):
                    base = self.underlying(node['type']) if node['kind'] == 'typedef' else node
                    from idl_extended_numeric import INTEGER_TYPES
                    if base.get('name') in INTEGER_TYPES:
                        bits = args['value']
                        lo, _ = INTEGER_TYPES[base['name']]
                        minimum, maximum = (0, 2**bits-1) if lo == 0 else (-2**(bits-1), 2**(bits-1)-1)
                        if not minimum <= value <= maximum:
                            self.fail('E_RANGE', 'constant exceeds annotated bit_bound', token)
                if name in ('min', 'range'):
                    bound = args['min'] if name == 'range' else args['value']
                    if not isinstance(bound, Deferred) and self.scalar(value) < self.scalar(bound):
                        self.fail('E_RANGE', 'constant is below annotated minimum', token)
                if name in ('max', 'range'):
                    bound = args['max'] if name == 'range' else args['value']
                    if not isinstance(bound, Deferred) and self.scalar(value) > self.scalar(bound):
                        self.fail('E_RANGE', 'constant is above annotated maximum', token)
            if node['kind'] != 'typedef':
                break
            type_ = node['type']

    def annotation_declaration(self):
        self.require('annotations')
        self.expect('@')
        self.expect('annotation')
        if not self.scope and self.token.text in ANNOTATIONS:
            token = self.take()
            name = token.text
        else:
            name, token = self.identifier()
        path = self.scope+(name,)
        previous = self.symbols.get(path)
        saved_symbols = self.symbols.copy()
        saved_introduced = {p: dict(v) for p, v in self.introduced.items()}
        if previous:
            if previous['kind'] != 'annotation':
                self.fail('E_DUPLICATE', 'annotation name already denotes another declaration', token)
            self.symbols = {p: n for p, n in self.symbols.items() if p[:len(path)] != path}
            self.introduced = {p: v for p, v in self.introduced.items() if p[:len(path)] != path}
        try:
            node = self.annotation_body(name, token)
            if not self.scope and name in ANNOTATIONS and not self.standard_signature_matches(node):
                self.fail('E_DUPLICATE', 'inconsistent standardized annotation definition', token)
            if previous and self.annotation_model(previous) != self.annotation_model(node):
                self.fail('E_ANNOTATION', 'inconsistent repeated annotation definition', token)
            return previous or node
        finally:
            if previous:
                self.symbols, self.introduced = saved_symbols, saved_introduced

    @staticmethod
    def annotation_model(value):
        if isinstance(value, dict):
            return {k: AnnotationMixin.annotation_model(v) for k, v in value.items()
                    if k not in ('location', 'body_tokens', 'argument_expressions')}
        if isinstance(value, list):
            return [AnnotationMixin.annotation_model(v) for v in value]
        return value

    def standard_signature_matches(self, node):
        _, expected = ANNOTATIONS[node['name']]
        members = {m['name']: m for m in node['members']}
        if set(members) != set(expected):
            return False
        from idl_extended_numeric import INTEGER_TYPES
        for name, (category, default, required) in expected.items():
            member = members[name]
            base = self.underlying(member['type'])
            actual = self.constant_category(base)
            value = member.get('default')
            if required != ('default' not in member):
                return False
            if isinstance(category, tuple):
                if base['kind'] != 'enum' or tuple(n['name'] for n in base['enumerators']) != category or [n['value'] for n in base['enumerators']] != list(range(len(category))):
                    return False
                value = next((n['name'] for n in base['enumerators'] if n['value'] == value), None)
            elif category in ('uint16', 'uint32'):
                if base['kind'] != 'primitive' or INTEGER_TYPES.get(base['name']) != (0, 2**int(category[4:])-1):
                    return False
            elif category != actual:
                return False
            if not required and value != default:
                return False
        return True

    def annotation_body(self, name, token):
        node = self.declare(name, token, 'annotation', members=[], declarations=[])
        outer, self.scope = self.scope, self.scope+(name,)
        self.expect('{')
        while self.token.text not in ('}', '<eof>'):
            if self.token.text in ('enum', 'const', 'typedef'):
                node['declarations'].append(self.definition())
                continue
            type_ = self.type_spec(constant=True)
            base = self.underlying(type_)
            if not self.constant_type(base, allow_any=True):
                self.fail('E_ANNOTATION', 'annotation member must have constant-compatible type')
            member, mtok = self.identifier()
            field = self.declare(member, mtok, 'annotation_member', type=type_)
            if self.accept('default'):
                with self.numeric_context(type_):
                    value, kind = self.expression()
                field['default'] = self.check_value(type_, value, kind, mtok)
                field['default_kind'] = kind
            self.expect(';')
            node['members'].append(field)
        self.expect('}')
        self.expect(';')
        self.scope = outer
        return node

    def check_siblings(self, nodes):
        for annotation_name in ('id', 'position'):
            values = []
            for node in nodes:
                value = self.annotation_value(node, annotation_name)
                if value is not None and not isinstance(value, Deferred):
                    if value in values:
                        self.fail('E_ANNOTATION', f'duplicate sibling {annotation_name}')
                    values.append(value)
        if sum(any(a['name'] == 'default_literal' for a in n.get('annotations', [])) for n in nodes) > 1:
            self.fail('E_ANNOTATION', 'multiple default enumerators')
