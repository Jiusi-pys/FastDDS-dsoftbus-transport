"""Bounded byte checkpoint. No Git mutations, archive extraction, or deletion."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys

DENIED_COMPONENTS = {'.git', '.codex', '.vscode', '.claude', '__pycache__', 'node_modules'}
RESERVED = re.compile(r'^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)', re.I)
KEY_MARKER = re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----')


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + '\n').encode('utf-8')


def is_within(path, parent):
    return path == parent or parent in path.parents


def no_links(path):
    """Reject symlinks and Windows junction/reparse ancestors, including root."""
    for item in [path, *path.parents]:
        if not item.exists() and not item.is_symlink():
            continue
        info = item.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('symlink/reparse point rejected: ' + str(item))


def canonical_path(path):
    path = Path(path).absolute()
    no_links(path)
    return path.resolve()


def safe_relative(value):
    if not isinstance(value, str) or not value or '\\' in value or ':' in value or '\x00' in value:
        raise ValueError('unsafe relative path: ' + repr(value))
    parts = value.split('/')
    if any(p in ('', '.', '..') or p.endswith(('.', ' ')) or RESERVED.match(p) for p in parts):
        raise ValueError('unsafe relative path: ' + repr(value))
    if PurePosixPath(value).is_absolute():
        raise ValueError('absolute path rejected: ' + value)
    if any(p.casefold() in DENIED_COMPONENTS for p in parts):
        raise ValueError('excluded path: ' + value)
    lower = value.casefold()
    if lower.endswith(('.pem', '.p12', '.pfx', '.key')) or parts[-1].casefold() in ('window.json', '.env', 'id_rsa', 'id_ed25519'):
        raise ValueError('excluded credential/runtime path: ' + value)
    if '/permissions/' in '/' + lower or lower.startswith('permissions/'):
        raise ValueError('excluded permission path: ' + value)
    return value


def path_set(values):
    result = {}
    for value in values:
        safe_relative(value)
        folded = value.casefold()
        if folded in result:
            raise ValueError('duplicate/case collision: ' + value)
        result[folded] = value
    return result


def require_subset(required, selected):
    missing = sorted(set(path_set(required)) - set(selected))
    if missing:
        raise ValueError('missing required files: ' + ', '.join(missing))


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def create_checkpoint(recipe, output):
    if recipe.get('schema') != 1:
        raise ValueError('unsupported recipe schema')
    source = canonical_path(recipe['source_root'])
    output = canonical_path(output)
    if output.exists():
        raise ValueError('checkpoint output already exists')
    if not source.is_dir():
        raise ValueError('source root does not exist')
    selected = path_set(recipe['files'])
    require_subset(recipe['required_files'], selected)
    require_subset(recipe['required_untracked'], selected)
    payload = []
    for rel in sorted(selected.values(), key=str.casefold):
        path = source.joinpath(*rel.split('/'))
        if is_within(path, output) or is_within(output, path):
            raise ValueError('checkpoint self inclusion rejected: ' + rel)
        no_links(path)
        if not path.is_file():
            raise ValueError('source file missing: ' + rel)
        data = path.read_bytes()
        if KEY_MARKER.search(data):
            raise ValueError('private key material rejected: ' + rel)
        expected = recipe.get('file_hashes', {}).get(rel)
        if expected and sha256(data) != expected:
            raise ValueError('source changed after inventory collection: ' + rel)
        payload.append((rel, data))
    # All selection and payload validation precedes any write.
    inventory = {
        'schema': 1,
        'created_utc': datetime.now(timezone.utc).isoformat(),
        'source_root': str(source),
        'status': 'BYTE_SNAPSHOT_CREATED_NOT_BUILD_VERIFIED',
        'required_files': sorted(recipe['required_files']),
        'required_untracked': sorted(recipe['required_untracked']),
        'metadata': recipe.get('metadata', {}),
        'references': recipe.get('references', []),
        'files': [{'path': rel, 'bytes': len(data), 'sha256': sha256(data)} for rel, data in payload],
        'limitations': [
            'File hash integrity is not signer authenticity.',
            'Only selected source bytes are included; external requirements must be provisioned.',
            'No build, device test or binary reproducibility is established by restoration.'],
    }
    output.mkdir(parents=True, exist_ok=False)
    (output / 'snapshot').mkdir()
    for rel, data in payload:
        path = output / 'snapshot' / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('xb') as handle:
            handle.write(data)
    blob = json_bytes(inventory)
    with (output / 'inventory.json').open('xb') as handle:
        handle.write(blob)
    with (output / 'inventory.sha256').open('x', encoding='ascii') as handle:
        handle.write(sha256(blob) + '  inventory.json\n')
    return verify_checkpoint(output)


def payload_paths(root):
    no_links(root)
    actual = []
    for directory, dirs, names in os.walk(root, followlinks=False):
        for name in [*dirs, *names]:
            no_links(Path(directory) / name)
        for name in names:
            actual.append((Path(directory) / name).relative_to(root).as_posix())
    path_set(actual)
    return actual


def verify_checkpoint(bundle):
    bundle = canonical_path(bundle)
    if not bundle.is_dir():
        raise ValueError('checkpoint missing')
    no_links(bundle / 'inventory.json')
    no_links(bundle / 'inventory.sha256')
    blob = (bundle / 'inventory.json').read_bytes()
    expected = (bundle / 'inventory.sha256').read_text(encoding='ascii').strip()
    if expected != sha256(blob) + '  inventory.json':
        raise ValueError('inventory hash mismatch')
    inventory = json.loads(blob)
    if inventory.get('schema') != 1:
        raise ValueError('unsupported inventory schema')
    selected = path_set([item['path'] for item in inventory['files']])
    require_subset(inventory['required_files'], selected)
    require_subset(inventory['required_untracked'], selected)
    actual = payload_paths(bundle / 'snapshot')
    if set(actual) != set(selected.values()):
        raise ValueError('snapshot file set mismatch')
    for item in inventory['files']:
        data = (bundle / 'snapshot' / item['path']).read_bytes()
        if len(data) != item['bytes'] or sha256(data) != item['sha256']:
            raise ValueError('snapshot size/hash mismatch: ' + item['path'])
    return {'status': 'BYTE_SNAPSHOT_VERIFIED', 'files': len(selected),
            'bytes': sum(item['bytes'] for item in inventory['files']),
            'inventory_sha256': sha256(blob)}


def verify_restored(bundle, destination):
    result = verify_checkpoint(bundle)
    inventory = read_json(Path(bundle) / 'inventory.json')
    destination = canonical_path(destination)
    if not destination.is_dir():
        raise ValueError('restore destination missing')
    expected = {item['path'] for item in inventory['files']}
    if set(payload_paths(destination)) != expected:
        raise ValueError('restored file set mismatch')
    for item in inventory['files']:
        data = (destination / item['path']).read_bytes()
        if len(data) != item['bytes'] or sha256(data) != item['sha256']:
            raise ValueError('restored size/hash mismatch: ' + item['path'])
    return {**result, 'status': 'FILE_RESTORE_VERIFIED', 'destination': str(destination),
            'build_reproduced': False, 'binary_reproducibility_verified': False}


def restore_checkpoint(bundle, destination):
    bundle = canonical_path(bundle)
    destination = canonical_path(destination)
    verify_checkpoint(bundle)
    inventory = read_json(bundle / 'inventory.json')
    source = canonical_path(inventory['source_root'])
    if is_within(destination, source) or is_within(source, destination):
        raise ValueError('restore inside source workspace rejected')
    if is_within(destination, bundle) or is_within(bundle, destination):
        raise ValueError('restore overlaps checkpoint')
    if destination.exists():
        raise ValueError('restore destination already exists')
    destination.mkdir(parents=True, exist_ok=False)
    for item in inventory['files']:
        target = destination / item['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        no_links(target)
        with target.open('xb') as handle:
            handle.write((bundle / 'snapshot' / item['path']).read_bytes())
    return verify_restored(bundle, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    create = commands.add_parser('create')
    create.add_argument('--recipe', type=Path, required=True)
    create.add_argument('--output', type=Path, required=True)
    verify = commands.add_parser('verify')
    verify.add_argument('--bundle', type=Path, required=True)
    verify.add_argument('--restored', type=Path)
    restore = commands.add_parser('restore')
    restore.add_argument('--bundle', type=Path, required=True)
    restore.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == 'create':
            result = create_checkpoint(read_json(args.recipe), args.output)
        elif args.command == 'restore':
            result = restore_checkpoint(args.bundle, args.destination)
        elif args.restored:
            result = verify_restored(args.bundle, args.restored)
        else:
            result = verify_checkpoint(args.bundle)
        print(json.dumps(result, indent=2))
        return 0
    except (ValueError, OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(json.dumps({'status': 'FAIL', 'error': str(error)}), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
