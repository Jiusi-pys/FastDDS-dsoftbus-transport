"""IDL integer promotions and target-independent IEEE binary constant folding.

Binary32, binary64 and the minimum IDL extended format (64-bit significand,
15-bit exponent) use exact rationals, round-to-nearest/ties-to-even, gradual
underflow. No host C long-double ABI or ambient Decimal context is used.
"""
from dataclasses import dataclass
from decimal import Decimal, localcontext, ROUND_DOWN
from fractions import Fraction


class NumericError(ValueError):
    pass


class Integer(int):
    def __new__(cls, value, bits=32, unsigned=False):
        result = int.__new__(cls, value)
        result.bits, result.unsigned = bits, unsigned
        return result


FORMATS = {'float': (24, -126, 127), 'double': (53, -1022, 1023),
           'long double': (64, -16382, 16383)}


def power2(exponent):
    return Fraction(2**exponent) if exponent >= 0 else Fraction(1, 2**-exponent)


@dataclass(frozen=True)
class Binary:
    value: Fraction
    format: str = 'double'
    negative_zero: bool = False

    @classmethod
    def rounded(cls, value, format='double', negative_zero=False):
        value = Fraction(value)
        precision, minimum, maximum = FORMATS[format]
        magnitude = abs(value)
        largest = (2 - power2(1-precision)) * power2(maximum)
        if magnitude > largest:
            raise NumericError(f'floating constant exceeds {format} range')
        if not magnitude:
            return cls(Fraction(0), format, negative_zero)
        exponent = magnitude.numerator.bit_length() - magnitude.denominator.bit_length()
        if magnitude < power2(exponent):
            exponent -= 1
        unit = power2(max(exponent, minimum) - precision + 1)
        scaled = magnitude / unit
        quotient, remainder = divmod(scaled.numerator, scaled.denominator)
        doubled = 2*remainder
        if doubled > scaled.denominator or doubled == scaled.denominator and quotient % 2:
            quotient += 1
        rounded = quotient * unit * (-1 if value < 0 else 1)
        if abs(rounded) > largest:
            raise NumericError(f'floating rounding exceeds {format} range')
        return cls(rounded, format, bool(not rounded and value < 0))

    def export(self):
        # Denominator is a power of two after rounding. The pair is exact;
        # decimal is presentation only and is sufficient for round-tripping.
        exponent = -(self.value.denominator.bit_length()-1)
        coefficient = self.value.numerator
        if coefficient:
            trailing = (abs(coefficient) & -abs(coefficient)).bit_length()-1
            coefficient >>= trailing
            exponent += trailing
        with localcontext() as context:
            context.prec = 25 if self.format == 'long double' else 20
            decimal = str(Decimal(self.value.numerator) / Decimal(self.value.denominator))
        return {'coefficient': str(coefficient), 'exponent2': exponent,
                'format': self.format, 'decimal': decimal, 'negative_zero': self.negative_zero}


@dataclass(frozen=True)
class Deferred:
    expression: object


def integer_check(value, bits, unsigned):
    minimum = 0 if unsigned else -2**(bits-1)
    maximum = 2**bits-1 if unsigned else 2**(bits-1)-1
    if not minimum <= value <= maximum:
        raise NumericError(f'integer expression exceeds {"unsigned " if unsigned else ""}{bits}-bit range')
    return Integer(value, bits, unsigned)


def unary(operator, value):
    if isinstance(value, Deferred):
        return Deferred([operator, value.expression])
    if isinstance(value, Binary):
        if operator == '~':
            raise NumericError('bitwise complement requires integer')
        return Binary(-value.value if operator == '-' else value.value, value.format,
                      not value.negative_zero if operator == '-' and not value.value else value.negative_zero)
    if isinstance(value, Decimal):
        if operator == '~':
            raise NumericError('bitwise complement requires integer')
        return value.copy_negate() if operator == '-' else value
    bits = max(32, value.bits)
    if operator == '+':
        return value
    if operator == '-':
        return integer_check(-int(value), bits, False)
    return integer_check((2**bits-1)-int(value) if value.unsigned else -(int(value)+1), bits, value.unsigned)


def binary(operator, left, right):
    if isinstance(left, Deferred) or isinstance(right, Deferred):
        return Deferred([operator, left.expression if isinstance(left, Deferred) else export(left),
                         right.expression if isinstance(right, Deferred) else export(right)])
    if isinstance(left, Binary):
        if operator not in ('+', '-', '*', '/'):
            raise NumericError('operator requires integer operands')
        if operator == '/' and not right.value:
            raise ZeroDivisionError()
        value = {'+': lambda: left.value+right.value, '-': lambda: left.value-right.value,
                 '*': lambda: left.value*right.value, '/': lambda: left.value/right.value}[operator]()
        format = 'long double' if 'long double' in (left.format, right.format) else 'double'
        negative_zero = False
        if not value:
            ls = left.value < 0 or left.negative_zero
            rs = right.value < 0 or right.negative_zero
            negative_zero = (ls != rs) if operator in ('*', '/') else (
                not left.value and not right.value and ls and (rs if operator == '+' else not rs))
        return Binary.rounded(value, format, negative_zero)
    if isinstance(left, Decimal):
        if operator not in ('+', '-', '*', '/'):
            raise NumericError('operator requires integer operands')
        if operator == '/' and not right:
            raise ZeroDivisionError()
        with localcontext() as context:
            context.prec, context.rounding = 62, ROUND_DOWN
            value = {'+': lambda: left+right, '-': lambda: left-right,
                     '*': lambda: left*right, '/': lambda: left/right}[operator]()
        with localcontext() as context:
            context.prec, context.rounding = 31, ROUND_DOWN
            return +value
    bits = max(32, left.bits, right.bits)
    unsigned = any(n.bits == bits and n.unsigned for n in (left, right))
    a, b = int(left), int(right)
    if operator in ('/', '%') and b == 0:
        raise ZeroDivisionError()
    if operator in ('<<', '>>') and not 0 <= b < 64:
        raise NumericError('shift count must be between 0 and 63')
    if unsigned and operator not in ('<<', '>>'):
        a, b = a % 2**bits, b % 2**bits
    quotient = (abs(a)//abs(b)) * (-1 if (a < 0) != (b < 0) else 1) if operator in ('/', '%') else 0
    value = {'+': lambda: a+b, '-': lambda: a-b, '*': lambda: a*b,
             '/': lambda: quotient, '%': lambda: a-quotient*b, '|': lambda: a|b,
             '&': lambda: a&b, '^': lambda: a^b, '<<': lambda: a<<b,
             '>>': lambda: (a & (2**bits-1)) >> b}[operator]()
    return integer_check(value, bits, unsigned)


def export(value):
    if isinstance(value, Binary):
        return value.export()
    if isinstance(value, Deferred):
        return {'deferred': export(value.expression)}
    if isinstance(value, Decimal):
        return str(value)
    if isinstance(value, dict):
        return {k: export(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [export(v) for v in value]
    return value
