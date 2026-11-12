"""One IDL narrow-string sequence rule for expressions, directives and imports."""
from idl_frontend import Diagnostic


def narrow_string_sequence(tokens, index, literal):
    if not tokens[index].text.startswith('"'):
        raise Diagnostic('E_CONST_TYPE', 'expected narrow string literal', tokens[index])
    parts = []
    while tokens[index].text.startswith('"'):
        value, category = literal(tokens[index])
        if category != 'string':
            raise Diagnostic('E_CONST_TYPE', 'expected narrow string literal', tokens[index])
        parts.append(value)
        index += 1
    return ''.join(parts), index
