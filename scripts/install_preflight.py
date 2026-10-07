#!/usr/bin/env python3
"""Offline installation-input audit. Never loads target DSOs or calls a platform API."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

PACKAGES = {
    'FastDDSDSoftBusTransport': ('fastdds', '3.6.2.0'),
    'FastDDSDSoftBusTransportRos2Host': ('fastrtps', '2.14.5'),
    'FastDDSDSoftBusTransportRos2Target': ('fastrtps', '2.14.5'),
}


def load_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('duplicate JSON key: ' + key)
            result[key] = value
        return result
    return json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def parse_binary_info(text):
    arch = re.search(r'^Arch: (\S+)', text, re.M)
    fmt = re.search(r'^Format: (\S+)', text, re.M)
    if not arch or not fmt:
        raise ValueError('readobj did not identify an object format and architecture')
    if len(set(re.findall(r'^Arch: (\S+)', text, re.M))) != 1 or len(set(re.findall(r'^Format: (\S+)', text, re.M))) != 1:
        raise ValueError('mixed binary format/architecture in archive')
    needed = []
    for block in re.findall(r'NeededLibraries \[([^\]]*)\]', text):
        needed.extend(x.strip() for x in block.splitlines() if x.strip())
    for block in re.findall(r'^Import \{(.*?)^\}', text, re.M | re.S):
        needed.extend(re.findall(r'^\s*Name: (\S+)', block, re.M))
    delayed = []
    for block in re.findall(r'^DelayImport \{(.*?)^\}', text, re.M | re.S):
        delayed.extend(re.findall(r'^\s*Name: (\S+)', block, re.M))
    return dict(architecture=arch.group(1), format=fmt.group(1), needed=sorted(set(needed)),
                delay_needed=sorted(set(delayed)))


def inspect_binary(path, readobj):
    result = subprocess.run([str(readobj), '--file-headers', '--needed-libs',
                             '--coff-imports', str(path)], capture_output=True,
                            text=True, encoding='utf-8', errors='replace', timeout=30)
    if result.returncode:
        raise ValueError('readobj rejected ' + path.name + ' (exit ' + str(result.returncode) + ')')
    return parse_binary_info(result.stdout)


def valid_bytes(value, maximum):
    return isinstance(value, str) and 0 < len(value.encode('utf-8')) <= maximum and '\x00' not in value


def valid_native_scope(package, prefix, namespace):
    # V1Address::ValidNativeScope + EncodeScopedServiceName, without normalization.
    def grammar(value, maximum):
        return (isinstance(value, str) and 0 < len(value) <= maximum and
                re.fullmatch(r'[A-Za-z0-9_.-]+', value) and
                not value.startswith('.') and not value.endswith('.') and '..' not in value)
    if not grammar(package, 64) or not grammar(prefix, 254):
        return False
    if len(prefix) <= len(package) + 1 or not prefix.startswith(package + '.'):
        return False
    if package in prefix[len(package) + 1:] or '.md1' in prefix:
        return False
    for split, character in enumerate(prefix):
        if character == '.' and split * 2 + 1 == len(prefix) and prefix[:split] == prefix[split + 1:]:
            return False
    return valid_bytes(namespace, 116) and len(prefix) + 1 + 23 + 2 * len(namespace.encode('utf-8')) <= 255


def runtime_configuration(path, native):
    """Scalar rules from Bridge.cpp/TargetBridge.cpp; no private artifact validator."""
    common = {'bridge_library', 'fastdds_module', 'fastcdr_module', 'backend', 'run',
              'side', 'domain', 'authorized_package', 'service_prefix', 'locator_version',
              'native_name_contract'}
    target = {'softbus_module', 'peer_network_id', 'connect_release', 'namespace',
              'local_udid', 'peer_udid', 'service_slot', 'peer_slot', 'profile',
              'local_initiator_name', 'peer_initiator_name'}
    host = {'port', 'peer_port', 'forward'}
    # This first public entry accepts the ordinary startup schema. Bounded harness
    # options require their own future public contract, never silent pass-through.
    required = common | (target if native else host)
    values = {}
    for line in path.read_text(encoding='utf-8').splitlines():
        key, separator, value = line.partition('=')
        if not separator or not value or key not in required or key in values:
            raise ValueError('Runtime config missing/unknown/duplicate field')
        values[key] = value
    if set(values) != required:
        raise ValueError('Incomplete runtime config; missing ' + ', '.join(sorted(required - set(values))))
    if values['locator_version'] != 'V1' or values['native_name_contract'] != 'PACKAGE_SCOPED_V1':
        raise ValueError('Runtime requires V1/PACKAGE_SCOPED_V1')
    backend = 'OHOS_PUBLIC_SOCKET_EXPERIMENTAL' if native else 'HOST_OPAQUE_BYTES_TEST_ONLY'
    if values['backend'] != backend:
        raise ValueError('Runtime backend unsupported by ordinary public preflight schema')
    if not re.fullmatch('[0-9a-f]{16}', values['run']):
        raise ValueError('Runtime run syntax invalid')
    for key, limit in [('side', 1), ('domain', 232)] + ([('service_slot', 0xfffffe), ('peer_slot', 0xfffffe)] if native else [('port', 65535), ('peer_port', 65535), ('forward', 1)]):
        if not re.fullmatch('[0-9]+', values[key]) or int(values[key]) > limit:
            raise ValueError('Runtime scalar range invalid: ' + key)
    package, prefix = values['authorized_package'], values['service_prefix']
    if not valid_native_scope(package, prefix, values['namespace'] if native else 'host-ros'):
        raise ValueError('Runtime package/service scope invalid')
    if native:
        if values['profile'] != 'BYTES_NATIVE' or not re.fullmatch('[0-9a-f]{64}', values['peer_network_id']):
            raise ValueError('Runtime profile/peer syntax invalid')
        for key, maximum in [('local_udid', 64), ('peer_udid', 64),
                             ('local_initiator_name', 255), ('peer_initiator_name', 255)]:
            if not valid_bytes(values[key], maximum):
                raise ValueError('Runtime identity/name input invalid')
        for key, slot_key in [('local_initiator_name', 'service_slot'), ('peer_initiator_name', 'peer_slot')]:
            listener = (prefix + '.md1.' + values['namespace'].encode('utf-8').hex() +
                        '.' + format(int(values['domain']), '08x') + '.' +
                        format(int(values[slot_key]), '06x') + '.01')
            if values[key] == listener:
                raise ValueError('Runtime listener/initiator collision: ' + key)
    elif (package != 'host.ros.test' or int(values['port']) == 0 or
          int(values['peer_port']) == 0 or values['port'] == values['peer_port'] or
          int(values['port']) == int(values['peer_port'])):
        raise ValueError('Host package/port contract invalid')
    for key in ['bridge_library', 'fastdds_module', 'fastcdr_module'] + (['softbus_module', 'connect_release'] if native else []):
        if not Path(values[key]).is_absolute() and not values[key].startswith('/'):
            raise ValueError('Runtime absolute path required: ' + key)
    return values


def preflight(prefix, config_path, runtime_dirs, readobj, fastdds_include=None):
    """Return separate installation and platform statuses; configuration has no authority."""
    checks = []
    statuses = {'EXPERIMENTAL_ONLY', 'PLATFORM_CONTRACT_UNVERIFIED'}
    valid = True
    security_mode = 'NONE'

    def fail(code, detail, status='RUNTIME_PREREQUISITE_MISSING'):
        nonlocal valid
        valid = False
        statuses.add(status)
        checks.append(dict(code=code, result='FAIL', detail=detail))

    def note(code, detail):
        checks.append(dict(code=code, result='UNVERIFIED', detail=detail))

    def result(capability='unknown'):
        if valid:
            statuses.add('INSTALL_INPUTS_VALID')
        return dict(schema=1, statuses=sorted(statuses), checks=checks,
                    security_mode=security_mode,
                    production_ready=False, p4='NOT_RELEASED',
                    qualification_profile='NOT_GENERATED',
                    exit_code=0 if valid and capability != 'production' else 2,
                    scope='OFFLINE_INPUT_CHECK_NOT_RUNTIME_AUTHORIZATION',
                    side_effects=dict(grants=0, sdk_calls=0, target_library_loads=0, writes=0))

    try:
        if not prefix.is_absolute() or not config_path.is_absolute():
            raise ValueError('prefix and config must be explicit absolute paths')
        prefix = prefix.resolve(strict=True)
        manifests = list((prefix / 'share').glob('*/install-manifest.json'))
        if len(manifests) != 1:
            raise ValueError('prefix must contain exactly one installed install-manifest.json')
        manifest = load_json(manifests[0])
        config = load_json(config_path)
        if not isinstance(manifest, dict) or manifest.get('schema') != 1:
            raise ValueError('unsupported installation manifest schema')
        if not isinstance(config, dict) or config.get('schema') != 1:
            raise ValueError('unsupported public configuration schema')
        package = manifest['package']
        if package not in PACKAGES:
            raise ValueError('unknown installed package family')
        if (not isinstance(manifest.get('dependencies'), list) or not manifest['dependencies'] or
                not all(isinstance(x, dict) for x in manifest['dependencies']) or
                not isinstance(manifest.get('files'), list) or not manifest['files'] or
                not all(isinstance(x, str) for x in manifest['files'])):
            raise ValueError('manifest must bind actual dependency binaries and installed files')
        pins = {x['name']: x['version'] for x in manifest['dependencies']}
        dep_name, dep_version = PACKAGES[package]
        if package != 'FastDDSDSoftBusTransport' and pins.get(dep_name) == '2.14.7':
            expected = dict(version='2.14.7', upstream_commit='010b908dc3bb2b6f2818d61ac8db462d01b79a1f',
                            fastcdr_version='2.2.8', fastcdr_commit='1bc9c919311ffce555b445560bf7ae062a4e1c72', interface_abi=21405)
            if manifest.get('ros214_candidate') != expected or pins.get('fastcdr') != '2.2.8':
                raise ValueError('wrong maintenance candidate manifest/dependency identity')
            dep_version = '2.14.7'
        if pins.get(dep_name) != dep_version or not pins.get('fastcdr'):
            raise ValueError('installed dependency version does not match this package ABI')
        for dep in manifest['dependencies']:
            if not re.fullmatch(r'\d+(?:\.\d+){1,3}', dep['version']):
                raise ValueError('dependency version unknown: ' + dep['name'])
            if not re.fullmatch(r'[0-9a-f]{64}', dep['sha256']):
                raise ValueError('dependency hash absent: ' + dep['name'])
        capability = config.get('capability')
        if capability not in ('host', 'experimental', 'production'):
            fail('UNKNOWN_CAPABILITY', 'Choose host, experimental or production', 'UNSUPPORTED_PROFILE')
        allowed = {'schema', 'capability', 'profile', 'platform', 'namespace', 'domain',
                   'service_slot', 'initial_peer_slots', 'identity', 'socket_library',
                   'runtime_config', 'module_files', 'security'}
        if set(config) - allowed:
            fail('UNKNOWN_CONFIGURATION_FIELD', 'Unsupported fields; qualification/production flags cannot grant readiness')

        # DDS Security profile. This is a configuration and file-presence check only.
        # It never establishes that a certificate is used on a target, that a handshake
        # succeeded, or that any traffic was discarded.
        security = config.get('security')
        if security is not None:
            if not isinstance(security, dict):
                raise ValueError('security must be an object')
            allowed_security = {'mode', 'identity_ca', 'identity_certificate', 'private_key',
                                'permissions_ca', 'governance', 'permissions'}
            if set(security) - allowed_security:
                fail('UNKNOWN_SECURITY_FIELD', 'Unsupported security fields; readiness cannot be requested here')
            security_mode = security.get('mode')
            if security_mode not in ('NONE', 'DISCOVERY_SIGN', 'DISCOVERY_ENCRYPT'):
                fail('SECURITY_MODE_UNSUPPORTED',
                     'security.mode must be NONE, DISCOVERY_SIGN or DISCOVERY_ENCRYPT',
                     'UNSUPPORTED_PROFILE')
                security_mode = 'NONE'
            required_material = ('identity_ca', 'identity_certificate', 'private_key',
                                 'permissions_ca', 'governance', 'permissions')
            present_material = [name for name in allowed_security - {'mode'} if name in security]
            if security_mode == 'NONE':
                if present_material:
                    # A declared identity with no requirement to use it is the
                    # silent-fallback shape this checker must refuse.
                    fail('SECURITY_MODE_NONE_WITH_MATERIAL',
                         'Security material declared while security.mode is NONE')
            else:
                for name in required_material:
                    value = security.get(name)
                    if not isinstance(value, str) or not value:
                        fail('SECURITY_MATERIAL_MISSING', 'security.' + name + ' is required')
                        continue
                    path = Path(value)
                    if not path.is_absolute():
                        fail('SECURITY_PATH_NOT_ABSOLUTE', 'security.' + name + ' must be an explicit absolute path')
                    elif not path.is_file():
                        fail('SECURITY_FILE_MISSING', 'security.' + name + ' does not exist')
                if fastdds_include is None or not Path(fastdds_include).is_absolute():
                    fail('SECURITY_DEPENDENCY_NOT_INSPECTED',
                         'Explicit absolute --fastdds-include is required to check compiled security support')
                else:
                    config_header = Path(fastdds_include) / 'fastrtps/config.h'
                    if not config_header.is_file():
                        fail('SECURITY_DEPENDENCY_NOT_INSPECTED',
                             'fastrtps/config.h not found under --fastdds-include')
                    else:
                        text = config_header.read_text(encoding='utf-8', errors='replace')
                        match = re.search(r'^\s*#define\s+HAVE_SECURITY\s+(\d+)', text, re.M)
                        if match is None:
                            fail('SECURITY_SUPPORT_UNKNOWN',
                                 'HAVE_SECURITY is not defined in the installed dependency header')
                        elif match.group(1) == '0':
                            fail('SECURITY_NOT_COMPILED_INTO_DEPENDENCY',
                                 'The include tree named by --fastdds-include reports no '
                                 'security support; requesting DISCOVERY_' +
                                 security_mode.split('_')[-1] + ' against it would silently '
                                 'run unprotected. Point --fastdds-include at the same '
                                 'dependency build the manifest binds')
                        else:
                            checks.append(dict(code='SECURITY_COMPILED_IN', result='PASS',
                                               detail='HAVE_SECURITY=' + match.group(1) +
                                                      ' in the bound dependency header'))
                note('SECURITY_RUNTIME_UNPROVEN',
                     'File presence and compiled support do not prove that the identity is used on a '
                     'target, that an authenticated handshake succeeded, or that unauthenticated peer '
                     'traffic is discarded')
        for field in ('identity', 'module_files'):
            if field in config and not isinstance(config[field], dict):
                raise ValueError(field + ' must be an object')
        if config.get('profile') != 'BYTES_NATIVE':
            fail('PROFILE_NOT_SUPPORTED', 'Only BYTES_NATIVE has this installed experimental baseline; RAW requirements remain open',
                 'UNSUPPORTED_PROFILE')
        if not valid_bytes(config.get('namespace'), 116):
            fail('NAMESPACE_INVALID', 'Explicit namespace required')
        for name, maximum in [('domain', 232), ('service_slot', 0xfffffe)]:
            value = config.get(name)
            if type(value) is not int or not 0 <= value <= maximum:
                fail('CONFIGURATION_RANGE', name + ' outside supported input range')
        slots = config.get('initial_peer_slots')
        if (not isinstance(slots, list) or not slots or
                any(type(x) is not int or not 0 <= x <= 0xfffffe for x in slots) or len(set(slots)) != len(slots)):
            fail('PEER_SLOTS_INVALID', 'Explicit unique initial peer slots required')
        platform = config.get('platform', {})
        if not isinstance(platform, dict):
            raise ValueError('platform must be an object')
        for name in ('system', 'architecture', 'stl', 'sdk_api'):
            if platform.get(name) != manifest.get(name) or not manifest.get(name):
                fail('PLATFORM_ABI_MISMATCH', 'platform.' + name + ' must match installed build metadata')
        system = manifest['system']
        architecture = manifest['architecture']
        if system == 'OHOS':
            if (architecture != 'aarch64' or platform.get('sdk_api') != '23' or
                    platform.get('firmware') != 'OpenHarmony-6.1.0.31'):
                note('PLATFORM_NOT_OBSERVED', 'Tuple differs from bounded RK3588A baseline; supplier contract and device branch required')
        elif system != 'Windows' or architecture != 'x86_64':
            note('PLATFORM_NOT_OBSERVED', 'No verified installation/runtime baseline for this platform tuple')
        note('ABI_LIMITATION', 'Binary architecture/hash binding cannot prove full C++/CRT/firmware ABI or loader namespace compatibility')

        roots = [prefix / 'lib', prefix / 'bin']
        for root in runtime_dirs:
            if not root.is_absolute() or not root.is_dir():
                raise ValueError('runtime-dir must be an explicit existing absolute directory')
            roots.append(root.resolve())
        # No environment, registry, source checkout, system defaults or recursive fallback.
        def resolve_library(name):
            if Path(name).name != name or '/' in name or '\\' in name:
                raise ValueError('library names must be basenames')
            candidates = {p.resolve() for root in roots for p in [root / name] if p.is_file()}
            if not candidates:
                raise ValueError('Missing dynamic library: ' + name + '; supply its runtime-dir explicitly')
            if len(candidates) != 1:
                raise ValueError('Ambiguous dynamic library: ' + name)
            return candidates.pop()

        binaries = []
        for name in manifest['files']:
            path = Path(name)
            if path.is_absolute() or '..' in path.parts:
                raise ValueError('installed file path escapes prefix')
            path = (prefix / path).resolve()
            if not path.is_relative_to(prefix) or not path.is_file():
                raise ValueError('Missing installed package file: ' + name)
            # Archives are ABI-bearing inputs too; readobj inspects their members.
            binaries.append(path)
        for dep in manifest['dependencies']:
            path = resolve_library(dep['file'])
            if digest(path) != dep['sha256']:
                fail('DEPENDENCY_VERSION_OR_ABI_MISMATCH', 'Actual binary differs from build-bound ' + dep['name'] + ' ' + dep['version'])
            binaries.append(path)
            for implib in dep.get('link_files', []):
                link = resolve_library(implib['file'])
                if digest(link) != implib['sha256']:
                    fail('DEPENDENCY_LINK_ABI_MISMATCH', 'Import library differs from build-bound ' + dep['name'])

        if package != 'FastDDSDSoftBusTransport':
            runtime_path = config.get('runtime_config')
            if not isinstance(runtime_path, str) or not Path(runtime_path).is_absolute():
                raise ValueError('ROS frontend requires explicit runtime_config .conf path')
            native = package == 'FastDDSDSoftBusTransportRos2Target'
            values = runtime_configuration(Path(runtime_path), native)
            if int(values['domain']) != config['domain']:
                raise ValueError('Runtime domain differs from preflight input')
            if native and (values['namespace'] != config['namespace'] or
                    int(values['service_slot']) != config['service_slot'] or
                    int(values['peer_slot']) not in config['initial_peer_slots'] or
                    values['authorized_package'] != config.get('identity', {}).get('package') or
                    values['service_prefix'] != config.get('identity', {}).get('service_name_prefix')):
                raise ValueError('Runtime deployment identity differs from preflight input')
            modules = config.get('module_files', {})
            for key in ['bridge_library', 'fastdds_module', 'fastcdr_module'] + (['softbus_module'] if native else []):
                local = modules.get(key)
                if not isinstance(local, str) or not Path(local).is_absolute() or not Path(local).is_file():
                    raise ValueError('Explicit local module_files input missing: ' + key)
                path = Path(local).resolve()
                if path.name != values[key].replace('\\', '/').split('/')[-1]:
                    raise ValueError('Runtime/local module basename differs: ' + key)
                if key == 'bridge_library' and path not in binaries:
                    raise ValueError('Runtime bridge is outside selected installation')
                if key in ('fastdds_module', 'fastcdr_module'):
                    dep = next(x for x in manifest['dependencies'] if x['name'] == ('fastrtps' if key == 'fastdds_module' else 'fastcdr'))
                    if digest(path) != dep['sha256']:
                        raise ValueError('Runtime module does not match installed dependency ABI: ' + key)
                if key == 'softbus_module' and path != Path(config.get('socket_library', '')).resolve():
                    raise ValueError('Runtime softbus module differs from explicit platform input')
                binaries.append(path)
            checks.append(dict(code='RUNTIME_CONFIGURATION_SCALARS_VALID', result='PASS', detail='Actual .conf fields checked; loader/identity behavior remains unverified'))

        if capability != 'host':
            if system != 'OHOS' or manifest.get('experimental') is not True:
                fail('EXPERIMENTAL_COMPONENT_UNAVAILABLE', 'This installation does not contain the real OHOS experimental runtime',
                     'UNSUPPORTED_PROFILE')
            identity = config.get('identity', {})
            if not isinstance(identity, dict):
                raise ValueError('identity must be an object')
            pkg = identity.get('package', '')
            service = identity.get('service_name_prefix', '')
            permissions = identity.get('permissions')
            if (not valid_native_scope(pkg, service, config.get('namespace')) or
                    identity.get('process_name') != 'fastdds_dsoftbus_probe' or
                    not re.fullmatch(r'[1-9][0-9]*', str(identity.get('token_id', ''))) or
                    int(identity.get('token_id', '0')) > 0xffffffff or
                    not isinstance(permissions, list) or not all(isinstance(x, str) for x in permissions) or
                    'ohos.permission.DISTRIBUTED_DATASYNC' not in permissions):
                fail('IDENTITY_CONFIGURATION_MISSING', 'Owned normal-APL process identity, package-scoped name, token and DATASYNC declaration required')
            for role, value in [('identity', identity.get('library')), ('socket', config.get('socket_library'))]:
                if not isinstance(value, str) or not Path(value).is_absolute() or not Path(value).is_file():
                    fail('EXTERNAL_LIBRARY_MISSING', 'Explicit legal ' + role + ' library required; no SDK/worktree fallback')
                else:
                    binaries.append(Path(value).resolve())
            note('IDENTITY_AUTHORIZATION_UNVERIFIED', 'Declarations do not verify owned token, normal APL, actual grant, SA/bundle policy, pairing or permission restoration')
        elif package == 'FastDDSDSoftBusTransportRos2Target':
            fail('HOST_CAPABILITY_UNAVAILABLE', 'OHOS native frontend cannot be selected as Host/Fake capability', 'UNSUPPORTED_PROFILE')

        seen = set()
        while binaries:
            path = binaries.pop()
            if path in seen:
                continue
            seen.add(path)
            try:
                info = inspect_binary(path, readobj)
                expected_format = 'elf64-little' if system == 'OHOS' else 'COFF-'
                if info['architecture'] != architecture or not info['format'].startswith(expected_format):
                    raise ValueError('Wrong object format/endian/architecture: ' + path.name)
                for needed in info['needed']:
                    # Windows API-set imports are OS contracts, not redistributable DLL names.
                    # Their actual forwarding/OS compatibility remains unverified offline.
                    if system == 'Windows' and needed.lower().startswith(('api-ms-', 'ext-ms-')):
                        note('WINDOWS_API_SET_UNVERIFIED', needed)
                    else:
                        binaries.append(resolve_library(needed))
                for delayed in info.get('delay_needed', []):
                    # Delay-load imports are demand-loaded feature contracts. Missing
                    # optional Windows components are not process-start dependencies.
                    # Actual feature use must be qualified separately, never ignored.
                    note('DELAY_LOAD_FEATURE_UNVERIFIED', path.name + ' -> ' + delayed)
                checks.append(dict(code='BINARY_INPUT_VALID', result='PASS', detail=path.name,
                                   sha256=digest(path), architecture=info['architecture']))
            except (OSError, ValueError, subprocess.SubprocessError) as error:
                fail('BINARY_PREREQUISITE_INVALID', str(error))
        note('PF_01_UNCLOSED', 'Missing target-bound payload/native/owner/channel last-use and callback/work drain or invalidation guarantees')
        note('PF_02_UNCLOSED', 'Missing original native event identity protection before first resource effect; local generation is not SDK epoch')
        note('QUALIFICATION_UNAVAILABLE', 'No verified production QualificationProfile or P4 release exists in this tool')
        return result(capability)
    except (OSError, ValueError, KeyError, TypeError) as error:
        fail('INSTALL_INPUT_ERROR', str(error))
        return result()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--runtime-dir', type=Path, action='append', default=[])
    parser.add_argument('--readobj', type=Path, required=True,
                        help='Explicit absolute LLVM readobj executable; only static file inspection')
    parser.add_argument('--fastdds-include', type=Path, default=None,
                        help='Explicit absolute installed Fast DDS include directory; read only to '
                             'check compiled security support when the configuration declares it')
    args = parser.parse_args()
    if not args.readobj.is_absolute() or not args.readobj.is_file():
        parser.error('--readobj must be an explicit existing absolute tool path')
    report = preflight(args.prefix, args.config, args.runtime_dir, args.readobj, args.fastdds_include)
    print(json.dumps(report, indent=2))
    return report['exit_code']


if __name__ == '__main__':
    sys.exit(main())
