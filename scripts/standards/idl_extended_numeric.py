"""Typed constant expressions shared by all B2 language profiles."""
from decimal import Decimal
from fractions import Fraction
from contextlib import contextmanager
from idl_numeric import Binary, Deferred, Integer, NumericError, binary, unary
from idl_frontend import INTEGERS
from idl_strings import narrow_string_sequence

INTEGER_TYPES = dict(INTEGERS)
for bits in (8, 16, 32, 64):
    INTEGER_TYPES[f'int{bits}'] = (-2**(bits-1), 2**(bits-1)-1)
    INTEGER_TYPES[f'uint{bits}'] = (0, 2**bits-1)


class NumericMixin:
    @contextmanager
    def numeric_context(self, type_=None, wide=False):
        previous = self.eval_bits, self.eval_float
        base = self.underlying(type_) if type_ is not None else {}
        name = base.get('name')
        wide = wide or base.get('kind') == 'template_type'
        self.eval_bits = 64 if wide or name in ('long long', 'unsigned long long', 'int64', 'uint64') else 32
        self.eval_float = 'long double' if wide else name
        try:
            yield
        finally:
            self.eval_bits, self.eval_float = previous

    def string_literal_sequence(self):
        value, self.index = narrow_string_sequence(self.tokens, self.index, self.literal)
        return value

    def literal(self, token):
        raw = token.text
        if raw.startswith(('"', "'", 'L"', "L'")) and any(ord(c) > 255 for c in raw):
            self.fail('E_LITERAL', 'wide non-Latin-1 characters require Unicode escapes', token)
        floating = raw[0] in '.0123456789' and not raw.lower().startswith('0x') and (
            '.' in raw or 'e' in raw.lower()) and raw[-1:] not in ('d', 'D')
        if floating:
            if len(raw) > 8192:
                self.fail('E_LIMIT', 'numeric literal exceeds frontend resource limit', token)
            value = Decimal(raw)
            if value.adjusted() > 5000:
                self.fail('E_RANGE', 'floating literal exceeds extended range', token)
            # Far below the smallest extended subnormal: exact rounding is zero.
            fraction = Fraction(0) if value.adjusted() < -6000 else Fraction(value)
            try:
                return Binary.rounded(fraction, 'long double' if self.eval_float == 'long double' else 'double'), 'floating'
            except NumericError as error:
                self.fail('E_RANGE', str(error), token)
        value, kind = super().literal(token)
        if kind == 'integer':
            bits = self.eval_bits
            if value > 2**bits-1:
                self.fail('E_RANGE', f'integer literal exceeds uint{bits}', token)
            value = Integer(value, bits, True)
        return value, kind

    def expression(self, level=0):
        operators = [('|',), ('^',), ('&',), ('<<', '>>'), ('+', '-'), ('*', '/', '%')]
        if level == len(operators):
            if self.token.text in ('+', '-', '~'):
                token = self.take()
                value, kind = self.primary()
                if kind not in ('integer', 'floating', 'fixed'):
                    self.fail('E_CONST_TYPE', 'unary operator requires numeric value', token)
                try:
                    return unary(token.text, value), kind
                except NumericError as error:
                    self.fail('E_CONST_TYPE' if token.text == '~' and kind != 'integer' else 'E_RANGE', str(error), token)
            return self.primary()
        left, kind = self.expression(level+1)
        while self.token.text in operators[level]:
            operator = self.take()
            right, right_kind = self.expression(level+1)
            if self.ignoring_annotation:
                left, kind = Deferred([operator.text, str(left), str(right)]), 'integer'
                continue
            if kind == 'generic' and isinstance(left, Deferred):
                kind = right_kind
            if right_kind == 'generic' and isinstance(right, Deferred):
                right_kind = kind
            if kind != right_kind or kind not in ('integer', 'floating', 'fixed'):
                self.fail('E_CONST_TYPE', 'numeric operand categories must match', operator)
            try:
                left = binary(operator.text, left, right)
            except ZeroDivisionError:
                self.fail('E_DIV_ZERO', 'division by zero', operator)
            except NumericError as error:
                code = 'E_SHIFT' if 'shift count' in str(error) else (
                    'E_CONST_TYPE' if 'requires integer' in str(error) else 'E_RANGE')
                self.fail(code, str(error), operator)
        return left, kind

    def primary(self):
        if self.accept('('):
            value = self.expression()
            self.expect(')')
            return value
        token = self.token
        if self.standard_annotation and isinstance(self.annotation_choices, tuple) and (token.text == self.standard_annotation or token.text == '::'):
            previous_index = self.index
            spelling = self.name_parts()
            prefix, _, choice = spelling.removeprefix('::').rpartition('::')
            if prefix == self.standard_annotation and choice in self.annotation_choices:
                return choice, 'annotation-enum'
            self.index = previous_index
        if token.text in self.annotation_choices:
            self.take()
            if isinstance(self.annotation_choices, dict):
                return self.annotation_choices[token.text]
            return token.text, 'annotation-enum'
        if token.text in ('TRUE', 'FALSE') or token.text[0] in '.0123456789\"\'' or token.text.startswith(('L"', "L'")):
            if token.text.startswith('"'):
                return self.string_literal_sequence(), 'string'
            self.take()
            value, kind = self.literal(token)
            if kind in ('string', 'wstring'):
                prefix = 'L"' if kind == 'wstring' else '"'
                while self.token.text.startswith(prefix):
                    value += self.literal(self.take())[0]
            return value, kind
        if self.ignoring_annotation:
            parts = self.name_parts(allow_keyword=False)
            return Deferred(['unknown-annotation-name', parts]), 'integer'
        node = self.resolve()
        if node['kind'] not in ('const', 'enumerator', 'bit_value', 'template_const'):
            self.fail('E_CONST_TYPE', 'name does not denote a constant', token)
        value, kind = node['value'], node['value_kind']
        if kind == 'floating' and self.eval_float == 'long double':
            value = Binary.rounded(value.value, 'long double', value.negative_zero)
        return value, kind

    def bound(self):
        token = self.token
        value, kind = self.expression()
        if isinstance(value, Deferred) and kind in ('integer', 'generic'):
            return value
        if kind != 'integer':
            self.fail('E_BOUND', 'bound requires integer expression', token)
        if not 0 < value <= 2**32-1:
            self.fail('E_BOUND', 'bound must be a positive uint32', token)
        return int(value)

    def check_value(self, type_, value, kind, token):
        base = self.underlying(type_)
        name = base.get('name')
        if base['kind'] == 'template_type':
            return value  # Rechecked with concrete bindings at instantiation.
        expected = ('integer' if base['kind'] == 'primitive' and name in INTEGER_TYPES else
                    'floating' if base['kind'] == 'primitive' and name in ('float', 'double', 'long double') else
                    name if base['kind'] == 'primitive' else
                    'integer' if base['kind'] == 'bitmask' else base['qualified_name'] if base['kind'] == 'enum' else base['kind'])
        if expected == 'any':
            return value
        if expected != kind:
            self.fail('E_CONST_TYPE', f'constant category {kind} does not match {expected}', token)
        if isinstance(value, Deferred):
            return value
        if expected == 'integer':
            minimum, maximum = (0, 2**self.annotation_value(base, 'bit_bound', 64)-1) if base['kind'] == 'bitmask' else INTEGER_TYPES[name]
            if not minimum <= value <= maximum:
                self.fail('E_RANGE', f'constant exceeds {name} range', token)
            value = Integer(value, maximum.bit_length()+(minimum < 0), minimum == 0)
        elif expected == 'floating':
            try:
                value = Binary.rounded(value.value, name, value.negative_zero)
            except NumericError as error:
                self.fail('E_RANGE', str(error), token)
        elif expected == 'fixed':
            digits = ''.join(map(str, value.as_tuple().digits)).strip('0')
            if len(digits) > 31:
                self.fail('E_RANGE', 'fixed result exceeds 31 significant digits', token)
            if base['digits'] is not None and not any(isinstance(base[k], Deferred) for k in ('digits', 'scale')):
                # Ignore insignificant trailing zeroes without context rounding.
                with_digits = value.as_tuple()
                trailing = len(with_digits.digits) - len(''.join(map(str, with_digits.digits)).rstrip('0'))
                fraction = max(0, -with_digits.exponent-trailing)
                if (max(0, value.adjusted()+1) if value else 0) > base['digits']-base['scale'] or fraction > base['scale']:
                    self.fail('E_RANGE', 'fixed value exceeds declared digits or scale', token)
        elif expected in ('string', 'wstring'):
            bound = base['bound']
            if bound is not None and not isinstance(bound, Deferred) and len(value) > bound:
                self.fail('E_RANGE', 'string constant exceeds bound', token)
        self.check_type_constraints(type_, value, kind, token)
        return value
