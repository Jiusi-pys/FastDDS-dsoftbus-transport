"""B1 IDL core frontend. No code generation or middleware dependency.

The supported profile is deliberately smaller than complete IDL 4.2; see
docs/standards/B1_IDL_FRONTEND.md. Diagnostics stop at the first error.
"""
from dataclasses import dataclass, asdict
from decimal import Decimal, localcontext, ROUND_DOWN, DecimalException
import re


@dataclass(frozen=True)
class Token:
    text: str
    file: str
    line: int
    column: int


class Diagnostic(Exception):
    def __init__(self, code, message, token):
        super().__init__(message)
        self.code, self.message, self.token = code, message, token

    def to_dict(self):
        return {"code": self.code, "message": self.message,
                "location": {k: v for k, v in asdict(self.token).items() if k != "text"}}


# All IDL keywords are reserved even when their building block is deferred.
KEYWORDS = set("""abstract any alias attribute bitfield bitmask bitset boolean case
char component connector const consumes context custom default double emits enum
eventtype exception factory FALSE finder fixed float getraises home import in inout
interface local long manages map mirrorport module multiple native Object octet
oneway out port porttype primarykey private provides public publishes raises readonly
setraises sequence short string struct supports switch TRUE truncatable
typedef typeid typename typeprefix uint8 uint16 uint32 uint64 int8 int16 int32 int64 unsigned
union uses ValueBase valuetype void wchar wstring""".split())
INTEGERS = {"short": (-2**15, 2**15-1), "long": (-2**31, 2**31-1),
            "long long": (-2**63, 2**63-1), "unsigned short": (0, 2**16-1),
            "unsigned long": (0, 2**32-1), "unsigned long long": (0, 2**64-1),
            "octet": (0, 255)}
PRIMITIVES = set(INTEGERS) | {"float", "double", "long double", "boolean", "char"}
LINE_MARKER = re.compile(r'^\s*#\s*(?:line\s+)?(\d+)\s+"((?:\\.|[^"\\])*)"(?:\s+\d+)*\s*$')
LEXEME = re.compile(r'''L?"(?:[^"\\\n]|\\[^\n])*"|L?'(?:[^'\\\n]|\\[^\n])*'|(?:[0-9]+\.[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?[dD]?|[0-9]+[eE][+-]?[0-9]+|[0-9]+[dD]|[A-Za-z][A-Za-z_0-9]*|_[A-Za-z][A-Za-z_0-9]*|0[xX][0-9a-fA-F]+|[0-9]+|::|<<|>>|[{}();,:<>\[\]=+*/%~|^&@-]''')


def marker_file(raw):
    return raw.replace('\\\\', '\\').replace('\\"', '"')


def tokenize(text, filename, lexeme=LEXEME):
    """Retain preprocessor line markers; do not remove comments inside literals."""
    tokens, block = [], False
    line, file = 1, filename
    for physical in text.splitlines():
        marker = LINE_MARKER.fullmatch(physical) if not block else None
        if marker:
            line, file = int(marker[1]), marker_file(marker[2])
            continue
        pos = 0
        while pos < len(physical):
            if block:
                end = physical.find('*/', pos)
                if end < 0:
                    break
                block, pos = False, end + 2
                continue
            if physical[pos].isspace():
                pos += 1
                continue
            if physical.startswith('//', pos):
                break
            if physical.startswith('/*', pos):
                block, pos = True, pos + 2
                continue
            token = Token(physical[pos:], file, line, pos + 1)
            match = lexeme.match(physical, pos)
            if not match:
                raise Diagnostic('E_LEX', 'invalid character or unterminated literal', token)
            tokens.append(Token(match[0], file, line, pos + 1))
            pos = match.end()
        line += 1
    eof = Token('<eof>', file, line, 1)
    if block:
        raise Diagnostic('E_LEX', 'unterminated block comment', eof)
    return tokens + [eof]


class Frontend:
    def __init__(self, text, filename='<input>', lexeme=LEXEME):
        self.tokens = tokenize(text, filename, lexeme)
        self.index = 0
        self.scope = ()
        self.symbols = {}
        self.declarations = []
        self.references = []
        self.introduced = {}
        self.eval_bits = 64
        self.eval_unsigned = False
        self.eval_float = None

    @property
    def token(self):
        return self.tokens[self.index]

    def fail(self, code, message, token=None):
        raise Diagnostic(code, message, token or self.token)

    def take(self):
        result = self.token
        if result.text != '<eof>':
            self.index += 1
        return result

    def accept(self, value):
        if self.token.text == value:
            self.take()
            return True
        return False

    def expect(self, value):
        if not self.accept(value):
            self.fail('E_SYNTAX', f'expected {value}, found {self.token.text}')

    def identifier(self):
        token = self.take()
        name = token.text
        if not re.fullmatch(r'_?[A-Za-z][A-Za-z_0-9]*', name):
            self.fail('E_IDENTIFIER', 'expected identifier', token)
        # A single leading underscore escapes an IDL keyword (not part of name).
        if name.startswith('_'):
            return name[1:], token
        if name.lower() in {k.lower() for k in KEYWORDS}:
            self.fail('E_IDENTIFIER', f'reserved keyword {name}', token)
        return name, token

    @staticmethod
    def qualified(path):
        return '::' + '::'.join(path)

    def declare(self, name, token, kind, **attrs):
        path = self.scope + (name,)
        if name.lower() in self.introduced.get(self.scope, {}):
            self.fail('E_DUPLICATE', f'name {name} already introduced by a reference', token)
        for existing in self.symbols:
            if tuple(p.lower() for p in existing) == tuple(p.lower() for p in path):
                self.fail('E_DUPLICATE', f'duplicate or case-colliding name {name}', token)
        if any(name.lower() == part.lower() for part in self.scope[-1:]):
            self.fail('E_DUPLICATE', 'name repeats its enclosing scope', token)
        node = {"kind": kind, "name": name, "qualified_name": self.qualified(path),
                "location": {k: v for k, v in asdict(token).items() if k != 'text'}, **attrs}
        self.symbols[path] = node
        return node

    def resolve(self):
        start = self.token
        absolute = self.accept('::')
        first, _ = self.identifier()
        parts = [first]
        while self.accept('::'):
            parts.append(self.identifier()[0])
        # Resolve the FIRST component before traversing the rest. A nearer
        # non-module hides outer names, even if the whole path exists outside.
        roots = [()] if absolute else [self.scope[:i] for i in range(len(self.scope), -1, -1)]
        for root in roots:
            path = root + (parts[0],)
            if path not in self.symbols:
                continue
            if not absolute and root != self.scope:
                self.introduced.setdefault(self.scope, {})[parts[0].lower()] = path
            for part in parts[1:]:
                if self.symbols[path]['kind'] != 'module':
                    self.fail('E_SCOPE', 'name is not a module scope in B1', start)
                path += (part,)
                if path not in self.symbols:
                    self.fail('E_UNRESOLVED', f'unknown name {self.qualified(path)}', start)
            node = self.symbols[path]
            self.references.append({"spelling": ('::' if absolute else '') + '::'.join(parts),
                                    "target": node['qualified_name'],
                                    "location": {k: v for k, v in asdict(start).items() if k != 'text'}})
            return node
        self.fail('E_UNRESOLVED', f'unknown name {first}', start)

    def underlying(self, type_):
        while type_['kind'] == 'named':
            node = next(n for n in self.symbols.values() if n['qualified_name'] == type_['name'])
            if node['kind'] == 'typedef':
                type_ = node['type']
            else:
                return node
        return type_

    def is_incomplete(self, type_, seen=None):
        seen = set() if seen is None else set(seen)
        type_ = self.underlying(type_)
        name = type_.get('qualified_name')
        if name in seen:
            return False
        if name:
            seen.add(name)
        if type_.get('complete') is False:
            return True
        if type_['kind'] in ('sequence', 'array'):
            return self.is_incomplete(type_['element'], seen)
        return any(self.is_incomplete(n['type'], seen) for n in type_.get('members', []))

    def type_spec(self, template=False, incomplete=False, constant=False):
        token = self.token
        word = token.text
        if word in ('struct', 'union', 'enum') and template:
            if word == 'enum':
                self.take()
                node = self.enumeration()
            else:
                self.take()
                node = self.aggregate(word)
            if node['kind'] == 'forward':
                self.fail('E_SYNTAX', 'inline type must be defined', token)
            return {"kind": "named", "name": node['qualified_name']}
        if word == 'fixed':
            self.take()
            result = {"kind": "fixed", "digits": None, "scale": None}
            if self.accept('<'):
                if constant:
                    self.fail('E_SYNTAX', 'const fixed uses no explicit digits or scale', token)
                if not template:
                    self.fail('E_PROFILE', 'anonymous fixed requires typedef', token)
                result['digits'] = self.bound()
                self.expect(',')
                scale, kind = self.expression()
                if kind != 'integer' or not 0 <= scale <= result['digits'] <= 31:
                    self.fail('E_BOUND', 'fixed requires 1 <= digits <= 31 and 0 <= scale <= digits', token)
                result['scale'] = scale
                self.expect('>')
            elif not constant:
                self.fail('E_SYNTAX', 'fixed type requires digits and scale', token)
            return result
        if word in ('string', 'wstring', 'sequence'):
            if not template:
                self.fail('E_PROFILE', 'anonymous template types require B2; use typedef', token)
            self.take()
            result = {"kind": word, "bound": None}
            if word == 'sequence':
                self.expect('<')
                result['element'] = self.type_spec(incomplete=True)
                if self.accept(','):
                    result['bound'] = self.bound()
                self.expect('>')
            elif self.accept('<'):
                result['bound'] = self.bound()
                self.expect('>')
            return result
        if word in ('short', 'long', 'unsigned', 'float', 'double', 'char', 'wchar', 'boolean', 'octet'):
            self.take()
            if word == 'unsigned':
                if self.token.text not in ('short', 'long'):
                    self.fail('E_SYNTAX', 'unsigned requires short or long')
                word += ' ' + self.take().text
            if word.endswith('long') and self.accept('long'):
                word += ' long'
            elif word == 'long' and self.accept('double'):
                word = 'long double'
            return {"kind": "primitive", "name": word}
        node = self.resolve()
        if node['kind'] not in ('typedef', 'struct', 'union', 'enum', 'native'):
            self.fail('E_TYPE', 'name does not denote a type', token)
        base = self.underlying({"kind": "named", "name": node['qualified_name']})
        if not incomplete and base['kind'] != 'sequence' and self.is_incomplete(base):
            self.fail('E_INCOMPLETE', 'incomplete type only allowed as sequence element', token)
        return {"kind": "named", "name": node['qualified_name']}

    def bound(self):
        token = self.token
        value, kind = self.expression()
        if kind != 'integer' or not 0 < value <= 2**32-1:
            self.fail('E_BOUND', 'bound must be a positive uint32 constant', token)
        return value

    def literal(self, token):
        raw = token.text
        wide = raw.startswith('L') and raw[1:2] in ('\"', "'")
        if wide:
            raw = raw[1:]
        if raw in ('TRUE', 'FALSE'):
            return raw == 'TRUE', 'boolean'
        if raw[0] not in ('\"', "'") and not raw.lower().startswith('0x') and ('.' in raw or 'e' in raw.lower() or raw[-1:] in ('d', 'D')):
            fixed = raw[-1:] in ('d', 'D')
            if fixed and ('e' in raw.lower()):
                self.fail('E_LITERAL', 'fixed literals cannot contain an exponent', token)
            value = Decimal(raw[:-1] if fixed else raw)
            if fixed and len(''.join(map(str, value.as_tuple().digits)).strip('0')) > 31:
                self.fail('E_RANGE', 'fixed literal exceeds 31 significant digits', token)
            return value, 'fixed' if fixed else 'floating'
        if raw[0] in ('"', "'"):
            # IDL escape set, not Python's broader escape grammar.
            value, pos = '', 1
            escapes = {'n': '\n', 't': '\t', 'v': '\v', 'b': '\b', 'r': '\r',
                       'f': '\f', 'a': '\a', '\\': '\\', '?': '?', "'": "'", '"': '"'}
            while pos < len(raw)-1:
                char = raw[pos]
                pos += 1
                if char == '\\':
                    char = raw[pos]
                    pos += 1
                    if char in escapes:
                        char = escapes[char]
                    elif char in '01234567' or char in ('x', 'u'):
                        if char == 'u' and not wide:
                            self.fail('E_LITERAL', 'Unicode escapes require wide literal', token)
                        match = re.match(r'[0-7]{0,2}' if char not in ('x', 'u') else (r'[0-9a-fA-F]{1,4}' if char == 'u' else r'[0-9a-fA-F]{1,2}'), raw[pos:])
                        if match is None:
                            self.fail('E_LITERAL', 'invalid hexadecimal escape', token)
                        digits = (char if char not in ('x', 'u') else '') + match[0]
                        pos += len(match[0])
                        number = int(digits, 8 if char not in ('x', 'u') else 16)
                        if number > (65535 if wide else 255):
                            self.fail('E_LITERAL', 'escape exceeds char range', token)
                        char = chr(number)
                    else:
                        self.fail('E_LITERAL', 'unknown escape sequence', token)
                if ord(char) > (65535 if wide else 255):
                    self.fail('E_LITERAL', 'non 8-bit character', token)
                value += char
            kind = 'string' if raw[0] == '"' else 'char'
            if kind == 'char' and len(value) != 1:
                self.fail('E_LITERAL', 'char literal must contain one character', token)
            if '\0' in value and kind == 'string':
                self.fail('E_LITERAL', 'strings cannot contain null characters', token)
            return value, ('w' + kind) if wide else kind
        try:
            base = 16 if raw.lower().startswith('0x') else (8 if raw.startswith('0') else 10)
            value = int(raw, base)
            if value > 2**64-1:
                self.fail('E_RANGE', 'integer literal exceeds uint64', token)
            return value, 'integer'
        except ValueError:
            self.fail('E_LITERAL', 'invalid integer literal', token)

    def expression(self, level=0):
        ops = [('|',), ('^',), ('&',), ('<<', '>>'), ('+', '-'), ('*', '/', '%')]
        if level < len(ops):
            left, kind = self.expression(level+1)
            while self.token.text in ops[level]:
                operator = self.take()
                right, rkind = self.expression(level+1)
                if kind != rkind or kind not in ('integer', 'floating', 'fixed'):
                    self.fail('E_CONST_TYPE', 'arithmetic requires matching numeric operand categories', operator)
                if kind != 'integer':
                    if operator.text not in ('+', '-', '*', '/'):
                        self.fail('E_CONST_TYPE', 'operator requires integer operands', operator)
                    if operator.text == '/' and right == 0:
                        self.fail('E_DIV_ZERO', 'division by zero', operator)
                    with localcontext() as context:
                        context.prec, context.rounding = (62 if kind == 'fixed' else 40), ROUND_DOWN
                        left = {'+': lambda: left+right, '-': lambda: left-right,
                                '*': lambda: left*right, '/': lambda: left/right}[operator.text]()
                    if kind == 'fixed':
                        with localcontext() as context:
                            context.prec, context.rounding = 31, ROUND_DOWN
                            left = +left
                    elif self.eval_float:
                        self.check_float(left, self.eval_float, operator)
                    continue
                if operator.text in ('/', '%') and right == 0:
                    self.fail('E_DIV_ZERO', 'division by zero', operator)
                if operator.text in ('<<', '>>') and not 0 <= right < 64:
                    self.fail('E_SHIFT', 'shift count must be between 0 and 63', operator)
                if operator.text == '+': left += right
                elif operator.text == '-': left -= right
                elif operator.text == '*': left *= right
                elif operator.text in ('/', '%'):
                    quotient = (abs(left)//abs(right)) * (-1 if (left < 0) != (right < 0) else 1)
                    left = quotient if operator.text == '/' else left - quotient*right
                elif operator.text == '|': left |= right
                elif operator.text == '&': left &= right
                elif operator.text == '^': left ^= right
                elif operator.text == '<<': left <<= right
                else: left = (left & (2**self.eval_bits-1)) >> right
                self.check_intermediate(left, operator)
            return left, kind
        if self.token.text in ('+', '-', '~'):
            operator = self.take()
            value, kind = self.primary()
            if kind not in ('integer', 'floating', 'fixed') or operator.text == '~' and kind != 'integer':
                self.fail('E_CONST_TYPE', 'invalid unary operand category', operator)
            # Decimal unary +/- otherwise inherit Python's process-wide
            # default precision (28), silently rounding valid IDL literals.
            value = {'+': lambda: value,
                     '-': lambda: value.copy_negate() if isinstance(value, Decimal) else -value,
                     '~': lambda: ~value}[operator.text]()
            if operator.text == '~' and self.eval_unsigned:
                value &= 2**self.eval_bits-1
            if kind == 'integer':
                self.check_intermediate(value, operator)
            return value, kind
        return self.primary()

    def check_intermediate(self, value, token):
        if not -(2**(self.eval_bits-1)) <= value <= 2**self.eval_bits-1:
            self.fail('E_RANGE', f'expression exceeds {self.eval_bits}-bit evaluation range', token)

    def primary(self):
        if self.accept('('):
            value = self.expression()
            self.expect(')')
            return value
        token = self.token
        if token.text in ('TRUE', 'FALSE') or token.text[0] in '.0123456789\"\'' or token.text.startswith(('L\"', "L'")):
            self.take()
            value, kind = self.literal(token)
            if kind == 'integer':
                self.check_intermediate(value, token)
            elif kind == 'floating' and self.eval_float:
                self.check_float(value, self.eval_float, token)
            if kind in ('string', 'wstring'):
                prefix = 'L"' if kind == 'wstring' else '"'
                while self.token.text.startswith(prefix):
                    value += self.literal(self.take())[0]
            return value, kind
        node = self.resolve()
        if node['kind'] not in ('const', 'enumerator'):
            self.fail('E_CONST_TYPE', 'name is not a constant', token)
        if node['value_kind'] == 'integer':
            self.check_intermediate(node['value'], token)
        value = Decimal(node['value']) if node['value_kind'] in ('floating', 'fixed') else node['value']
        if node['value_kind'] == 'floating' and self.eval_float:
            self.check_float(value, self.eval_float, token)
        return value, node['value_kind']

    def check_float(self, value, name, token):
        limit = Decimal({'float': '3.4028234663852885981e38', 'double': '1.7976931348623157081e308', 'long double': '1.189731495357231765e4932'}[name])
        if not value.is_finite() or value.copy_abs() > limit:
            self.fail('E_RANGE', 'floating constant exceeds type range', token)

    def check_value(self, type_, value, kind, token):
        type_ = self.underlying(type_)
        name = type_.get('name')
        if type_['kind'] == 'primitive' and name in INTEGERS:
            if kind != 'integer':
                self.fail('E_CONST_TYPE', 'expected integer constant', token)
            lo, hi = INTEGERS[name]
            if not lo <= value <= hi:
                self.fail('E_RANGE', f'constant outside {name} range', token)
        elif type_['kind'] == 'primitive' and name in ('float', 'double', 'long double'):
            if kind != 'floating':
                self.fail('E_CONST_TYPE', 'expected floating constant', token)
            self.check_float(value, name, token)
        elif type_['kind'] == 'fixed':
            if kind != 'fixed':
                self.fail('E_CONST_TYPE', 'expected fixed constant', token)
            digits = ''.join(map(str, value.as_tuple().digits)).strip('0')
            if len(digits) > 31:
                self.fail('E_RANGE', 'fixed constant exceeds 31 significant digits', token)
            if type_['digits'] is not None:
                integer_digits = max(0, value.adjusted()+1) if value else 0
                fraction_digits = max(0, -value.as_tuple().exponent)
                if integer_digits > type_['digits'] - type_['scale'] or fraction_digits > type_['scale']:
                    self.fail('E_RANGE', 'fixed constant exceeds declared digits or scale', token)
        elif type_['kind'] == 'primitive' and name in ('boolean', 'char', 'wchar'):
            if kind != name:
                self.fail('E_CONST_TYPE', f'expected {name} constant', token)
        elif type_['kind'] in ('string', 'wstring'):
            if kind != type_['kind']:
                self.fail('E_CONST_TYPE', 'expected string constant', token)
            if type_['bound'] is not None and len(value) > type_['bound']:
                self.fail('E_RANGE', 'string constant exceeds bound', token)
        elif type_['kind'] == 'enum':
            if kind != type_['qualified_name']:
                self.fail('E_CONST_TYPE', 'expected enumerator of discriminator type', token)
        else:
            self.fail('E_PROFILE', 'constant type not supported by B1', token)

    def declarators(self, type_, kind, arrays=False):
        nodes = []
        while True:
            name, token = self.identifier()
            dimensions = []
            while self.accept('['):
                if not arrays:
                    self.fail('E_PROFILE', 'anonymous arrays require B2; use typedef', token)
                dimensions.append(self.bound())
                self.expect(']')
            actual = {"kind": "array", "element": type_, "dimensions": dimensions} if dimensions else type_
            if dimensions and self.is_incomplete(type_):
                self.fail('E_INCOMPLETE', 'array element must be complete', token)
            nodes.append(self.declare(name, token, kind, type=actual))
            if not self.accept(','):
                return nodes

    def aggregate(self, kind):
        name, token = self.identifier()
        path = self.scope + (name,)
        old = self.symbols.get(path)
        if old and old['kind'] == kind and (not old['complete'] or self.token.text == ';'):
            node = old
        else:
            node = self.declare(name, token, kind, complete=False)
        if self.token.text == ';':
            return {"kind": "forward", "target": node['qualified_name']}
        discriminator = None
        outer = self.scope
        if kind == 'union':
            self.expect('switch')
            self.expect('(')
            self.scope = path
            discriminator = self.type_spec()
            base = self.underlying(discriminator)
            if not (base['kind'] == 'enum' or base['kind'] == 'primitive' and base['name'] in (set(INTEGERS)-{'octet'}) | {'char', 'boolean'}):
                self.fail('E_DISCRIMINATOR', 'invalid union discriminator', token)
            self.expect(')')
            node['discriminator'] = discriminator
        self.expect('{')
        self.scope = path
        members, labels, default = [], set(), False
        while self.token.text not in ('}', '<eof>'):
            branch = []
            if kind == 'union':
                while self.token.text in ('case', 'default'):
                    label = self.take()
                    if label.text == 'default':
                        if default:
                            self.fail('E_CASE', 'duplicate default label', label)
                        default = True
                        branch.append({"default": True})
                    else:
                        value, vkind = self.expression()
                        self.check_value(discriminator, value, vkind, label)
                        if value in labels:
                            self.fail('E_CASE', 'duplicate case label', label)
                        labels.add(value)
                        branch.append({"value": value})
                    self.expect(':')
                if not branch:
                    self.fail('E_SYNTAX', 'union member requires case or default label')
            type_ = self.type_spec()
            fields = self.declarators(type_, 'member')
            if kind == 'union' and len(fields) != 1:
                self.fail('E_SYNTAX', 'union branch requires exactly one declarator')
            if branch:
                fields[0]['labels'] = branch
            members.extend(fields)
            self.expect(';')
        self.expect('}')
        if not members:
            self.fail('E_EMPTY', 'core struct/union must have a member', token)
        if default and discriminator:
            base = self.underlying(discriminator)
            count = len(base['enumerators']) if base['kind'] == 'enum' else (2 if base.get('name') == 'boolean' else None)
            if count is not None and len(labels) == count:
                self.fail('E_CASE', 'default label has no remaining discriminator value', token)
        self.scope = outer
        node.update(members=members, complete=True)
        return node

    def definition(self):
        keyword = self.take()
        if keyword.text == 'module':
            name, token = self.identifier()
            path = self.scope + (name,)
            node = self.symbols.get(path)
            if not node or node['kind'] != 'module':
                node = self.declare(name, token, 'module', declarations=[])
            outer, self.scope = self.scope, path
            self.expect('{')
            nodes = self.definitions('}')
            node['declarations'].extend(nodes)
            self.expect('}')
            self.scope = outer
            result = node
        elif keyword.text == 'const':
            type_ = self.type_spec(template=True, constant=True)
            name, token = self.identifier()
            self.expect('=')
            base = self.underlying(type_)
            self.eval_bits = 32 if base.get('name') in ('long', 'unsigned long') else 64
            self.eval_unsigned = base.get('name', '').startswith('unsigned')
            self.eval_float = base.get('name') if base.get('name') in ('float', 'double', 'long double') else None
            value, kind = self.expression()
            self.eval_bits = 64
            self.eval_unsigned, self.eval_float = False, None
            self.check_value(type_, value, kind, token)
            if kind in ('floating', 'fixed'):
                value = str(value)
            result = self.declare(name, token, 'const', type=type_, value=value, value_kind=kind)
        elif keyword.text == 'typedef':
            type_ = self.type_spec(template=True)
            result = {"kind": "typedef_group", "declarations": self.declarators(type_, 'typedef', arrays=True)}
        elif keyword.text in ('struct', 'union'):
            result = self.aggregate(keyword.text)
        elif keyword.text == 'enum':
            result = self.enumeration()
        elif keyword.text == 'native':
            name, token = self.identifier()
            result = self.declare(name, token, 'native')
        else:
            self.fail('E_PROFILE' if keyword.text in KEYWORDS or keyword.text == '@' else 'E_SYNTAX',
                      f'unsupported declaration {keyword.text}', keyword)
        self.expect(';')
        return result

    def enumeration(self):
        name, token = self.identifier()
        result = self.declare(name, token, 'enum', enumerators=[])
        self.expect('{')
        while True:
            ename, etoken = self.identifier()
            enumerator = self.declare(ename, etoken, 'enumerator', value=len(result['enumerators']),
                                      value_kind=result['qualified_name'])
            result['enumerators'].append(enumerator)
            if not self.accept(','):
                break
        self.expect('}')
        return result

    def definitions(self, end):
        nodes = []
        while self.token.text not in (end, '<eof>'):
            node = self.definition()
            # A reopened module shares one AST node.
            if not any(n is node for n in nodes):
                nodes.append(node)
        if not nodes:
            self.fail('E_EMPTY', 'at least one definition is required')
        return nodes

    def parse(self):
        self.declarations = self.definitions('<eof>')
        self.expect('<eof>')
        for node in self.symbols.values():
            if node.get('complete') is False:
                loc = node['location']
                self.fail('E_INCOMPLETE', f'unresolved forward declaration {node["name"]}',
                          Token(node['name'], loc['file'], loc['line'], loc['column']))
        return {"schema": 1, "profile": "b1-core", "declarations": self.declarations,
                "symbols": list(self.symbols.values()), "references": self.references}


def parse(text, filename='<input>'):
    try:
        return Frontend(text, filename).parse()
    except RecursionError:
        raise Diagnostic('E_LIMIT', 'frontend nesting limit exceeded', Token('', filename, 1, 1)) from None
    except DecimalException:
        raise Diagnostic('E_RANGE', 'decimal evaluation range exceeded', Token('', filename, 1, 1)) from None
