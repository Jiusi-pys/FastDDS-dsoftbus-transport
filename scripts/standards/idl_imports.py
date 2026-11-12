"""Local CORBA IDL string imports with retained preprocessing provenance."""
from pathlib import Path
import hashlib
import json
import subprocess
from idl_frontend import Diagnostic, Frontend, LINE_MARKER, Token, marker_file, tokenize
from idl_strings import narrow_string_sequence


def expand_imports(text, source, command, bindings, evidence, lexeme):
    imports, visiting, complete, units = {}, set(), set(), []
    cache = {}
    evidence['import_stages'] = []
    evidence['import_bindings'] = {identity: str(Path(path).resolve()) for identity, path in bindings.items()}
    literal_parser = Frontend('')

    def paths_in(unit):
        paths = set()
        for line in unit.splitlines():
            marker = LINE_MARKER.fullmatch(line)
            if marker and not marker_file(marker[2]).startswith('<'):
                path = Path(marker_file(marker[2])).resolve()
                if path.is_file():
                    paths.add(path)
        return paths

    def visit(path, unit):
        if path in visiting:
            raise Diagnostic('E_IMPORT_CYCLE', 'cyclic IDL string imports', Token('', str(path), 1, 1))
        if path in complete:
            return
        if len(complete) + len(visiting) >= 128:
            raise Diagnostic('E_LIMIT', 'at most 128 imported compilation units', Token('', str(path), 1, 1))
        visiting.add(path)
        included = paths_in(unit)
        tokens = tokenize(unit, str(path), lexeme)
        for index, token in enumerate(tokens[:-2]):
            if token.text != 'import' or not tokens[index+1].text.startswith('"'):
                continue
            spelling, end = narrow_string_sequence(tokens, index+1, literal_parser.literal)
            if tokens[end].text != ';':
                continue
            # IDL string imports identify repository scopes, NOT filenames.
            # The driver can supply an explicit offline repository binding.
            if spelling not in bindings:
                continue  # A scope already present in this TU may satisfy it.
            imported = Path(bindings[spelling]).resolve()
            if not imported.is_file():
                raise Diagnostic('E_IMPORT', f'cannot open repository binding for {spelling}', token)
            if spelling in imports and imports[spelling] != str(imported):
                raise Diagnostic('E_IMPORT', 'import spelling resolves to different files', token)
            imports[spelling] = str(imported)
            if imported in included:
                continue
            if imported not in cache:
                argv = command[:-1] + [str(imported)]
                before = hashlib.sha256(imported.read_bytes()).hexdigest()
                result = subprocess.run(argv, capture_output=True, timeout=30)
                stage = {'source': str(imported), 'source_sha256': before, 'command': argv,
                         'exit_code': result.returncode, 'stderr': result.stderr.decode('utf-8', 'replace'),
                         'preprocessed_sha256': hashlib.sha256(result.stdout).hexdigest()}
                evidence['import_stages'].append(stage)
                if result.returncode:
                    raise Diagnostic('E_PREPROCESS', 'import preprocessing failed; see import_stages', token)
                if hashlib.sha256(imported.read_bytes()).hexdigest() != before:
                    raise Diagnostic('E_IMPORT', 'import source changed while preprocessing', token)
                cache[imported] = result.stdout.decode('utf-8')
            visit(imported, cache[imported])
        visiting.remove(path)
        complete.add(path)
        units.append(unit)

    visit(Path(source).resolve(), text)
    # Imported files are separate preprocessing units. Retain source line
    # identity and only coalesce identical physical lines across units; a
    # macro-dependent disagreement is diagnosed instead of choosing silently.
    seen, combined = {}, []
    for unit in units:
        current_file, current_line, local = str(source), 1, {}
        for line in unit.splitlines():
            marker = LINE_MARKER.fullmatch(line)
            if marker:
                current_line, current_file = int(marker[1]), marker_file(marker[2])
                continue
            key = (current_file, current_line)
            if line.strip() and not current_file.startswith('<'):
                previous = seen.get(key)
                if previous is not None and previous != line:
                    raise Diagnostic('E_IMPORT_CONTEXT', 'preprocessed import definitions disagree', Token('', current_file, current_line, 1))
                if previous is None:
                    combined += [f'# {current_line} {json.dumps(current_file, ensure_ascii=False)}', line]
                local[key] = line
            current_line += 1
        seen.update(local)
        for dependency in paths_in(unit):
            evidence['dependencies'][str(dependency)] = hashlib.sha256(dependency.read_bytes()).hexdigest()
    combined_text = '\n'.join(combined) + '\n'
    evidence['linked_preprocessed_sha256'] = hashlib.sha256(combined_text.encode('utf-8')).hexdigest()
    return combined_text, imports
