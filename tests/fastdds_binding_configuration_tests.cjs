'use strict';

// Configuration-only checks. Positive runtime/ABI tests use the actual consumer;
// these tests neither replace a library nor execute a device/permission action.
const assert = require('node:assert/strict');
const cp = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const root = path.resolve(__dirname, '..');
const prefix = process.argv[2] && path.resolve(process.argv[2]);
if (!prefix || !fs.existsSync(path.join(prefix, 'share/fastdds/cmake/fastdds-config.cmake'))) {
  throw new Error('usage: node tests/fastdds_binding_configuration_tests.cjs <actual-installed-prefix>');
}
const output = fs.mkdtempSync(path.join(os.tmpdir(), 'mdds-binding-config-'));
// Deliberately invalid package fixtures are used only for rejection tests.
const wrongVersion = path.join(output, 'wrong-version/share/fastdds/cmake');
fs.mkdirSync(wrongVersion, {recursive: true});
fs.writeFileSync(path.join(wrongVersion, 'fastdds-config.cmake'), 'set(fastdds_VERSION 9.9.9.0)\n');
fs.writeFileSync(path.join(wrongVersion, 'fastdds-config-version.cmake'),
  'set(PACKAGE_VERSION 9.9.9.0)\nset(PACKAGE_VERSION_COMPATIBLE FALSE)\nset(PACKAGE_VERSION_EXACT FALSE)\n');
const noSharedRoot = path.join(output, 'no-shared');
const noShared = path.join(noSharedRoot, 'share/fastdds/cmake');
for (const dir of [noShared, path.join(noSharedRoot, 'include'), path.join(noSharedRoot, 'lib'), path.join(noSharedRoot, 'bin')]) {
  fs.mkdirSync(dir, {recursive: true});
}
for (const name of ['fastdds-config.cmake', 'fastdds-config-version.cmake']) {
  fs.copyFileSync(path.join(prefix, 'share/fastdds/cmake', name), path.join(noShared, name));
}
const env = {};
const paths = [];
for (const [key, value] of Object.entries(process.env)) {
  if (key.toLowerCase() !== 'path') env[key] = value;
  else for (const entry of value.split(path.delimiter)) {
    if (entry && !paths.some(old => old.toLowerCase() === entry.toLowerCase())) paths.push(entry);
  }
}
env.Path = paths.join(path.delimiter);
const base = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF',
  '-DBUILD_SOFTBUS_CAPABILITY_PROBE=OFF', '-DFASTDDS_DSOFTBUS_FASTDDS_MODE=CONFIG',
  `-Dfastdds_DIR=${prefix}/share/fastdds/cmake`, `-DCMAKE_PREFIX_PATH=${prefix}`];
const cases = [
  ['mode', ['-DFASTDDS_DSOFTBUS_FASTDDS_MODE=typo'], /FASTDDS_DSOFTBUS_FASTDDS_MODE must be SOURCE_PROBE or CONFIG/],
  ['absent', ['-Dfastdds_DIR='], /explicit absolute fastdds_DIR/],
  ['relative', ['-Dfastdds_DIR=relative/path'], /explicit absolute fastdds_DIR/],
  ['nonexistent', [`-Dfastdds_DIR=${output}/does-not-exist`], /explicit absolute fastdds_DIR/],
  ['debug', ['-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_MAP_IMPORTED_CONFIG_DEBUG=Release'], /Release only/],
  ['empty-build-type', ['-DCMAKE_BUILD_TYPE='], /Release only/],
  ['relwithdebinfo', ['-DCMAKE_BUILD_TYPE=RelWithDebInfo'], /Release only/],
  ['multi-config', ['-G', 'Ninja Multi-Config', '-DCMAKE_CONFIGURATION_TYPES=Debug;Release'], /Release only/],
  ['wrong-version', [`-Dfastdds_DIR=${wrongVersion}`], /not accepted|requested version/],
  ['no-shared', [`-Dfastdds_DIR=${noShared}`], /shared.*libraries were requested but not found/],
  ['missing-dependency', ['-DCMAKE_DISABLE_FIND_PACKAGE_fastcdr=TRUE'], /REQUIRED package cannot\s+be disabled/],
  ['queue-dependency', ['-DBUILD_DSOFTBUS_DIRECTORY=OFF', '-DBUILD_DSOFTBUS_SEND_QUEUE=ON'], /BUILD_DSOFTBUS_SEND_QUEUE requires/],
  ['input-dependency', ['-DBUILD_DSOFTBUS_DIRECTORY=OFF', '-DBUILD_DSOFTBUS_SEND_QUEUE=OFF',
    '-DBUILD_DSOFTBUS_RUNTIME_SEND_PATH=OFF', '-DBUILD_DSOFTBUS_INPUT_CHANNELS=ON'], /BUILD_DSOFTBUS_INPUT_CHANNELS requires/],
  ['runtime-dependency', ['-DBUILD_DSOFTBUS_SEND_QUEUE=OFF', '-DBUILD_DSOFTBUS_RUNTIME_SEND_PATH=ON'],
    /BUILD_DSOFTBUS_RUNTIME_SEND_PATH requires/],
  ['conflicting-import', ['-DBINDING_CONFLICTING_IMPORT=ON'], /conflicting fastdds include outside/,
    'tests/cmake/real_fastdds_consumer'],
  ['tools-only-no-package', ['-DBUILD_INTERFACE_PROBE=OFF', '-DBUILD_DSOFTBUS_DIRECTORY=OFF',
    '-DBUILD_DSOFTBUS_SEND_QUEUE=OFF', '-DBUILD_DSOFTBUS_INPUT_CHANNELS=OFF',
    '-DBUILD_DSOFTBUS_RUNTIME_SEND_PATH=OFF', '-Dfastdds_DIR=', '-DCMAKE_BUILD_TYPE=Debug'], null],
];
if (process.platform === 'win32') {
  cases.push(['crt', ['-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded'], /Release DLL CRT/]);
  cases.push(['crt-driver', ['-DCMAKE_CXX_FLAGS_RELEASE=/MT /O2 /DNDEBUG'], /incompatible MSVC CRT/]);
}
const preloadedSource = 'tests/cmake/preloaded_fastdds_consumer';
cases.push(['preloaded-local', [], /requires GLOBAL imported dependency/, preloadedSource]);
cases.push(['preloaded-global', ['-DBINDING_PRELOAD_GLOBAL=ON'], null, preloadedSource]);
for (const dependency of ['fastdds', 'fastcdr', 'foonathan_memory']) {
  for (const [label, mapping] of [['debug', 'DEBUG'], ['empty', ''], ['list', 'RELEASE;DEBUG']]) {
    cases.push([`mapping-${dependency}-${label}`, ['-DBINDING_PRELOAD_GLOBAL=ON',
      `-DBINDING_MAP_TARGET=${dependency}`, `-DBINDING_RELEASE_MAPPING=${mapping}`],
      /requires unset or single RELEASE mapping/, preloadedSource]);
  }
  cases.push([`mapping-${dependency}-release`, ['-DBINDING_PRELOAD_GLOBAL=ON',
    `-DBINDING_MAP_TARGET=${dependency}`, '-DBINDING_RELEASE_MAPPING=Release'], null, preloadedSource]);
}
let failures = 0;
for (const [name, extra, expectedFailure, source = '.'] of cases) {
  const args = ['-S', path.resolve(root, source), '-B', path.join(output, name), ...base, ...extra];
  const result = cp.spawnSync('cmake', args, {cwd: root, env, encoding: 'utf8', timeout: 30000, maxBuffer: 16 * 1024 * 1024});
  fs.writeFileSync(path.join(output, `${name}.json`), JSON.stringify({args, status: result.status,
    signal: result.signal, error: result.error?.message, stdout: result.stdout, stderr: result.stderr}, null, 2));
  try {
    assert.equal(result.error, undefined, 'configure did not finish normally');
    assert.equal(result.signal, null);
    if (expectedFailure) {
      assert.notEqual(result.status, 0, 'negative configuration unexpectedly succeeded');
      assert.match(`${result.stdout}\n${result.stderr}`, expectedFailure, 'wrong failure reason');
    } else {
      assert.equal(result.status, 0, 'supported configuration failed');
      if (name === 'tools-only-no-package') {
        assert.doesNotMatch(`${result.stdout}\n${result.stderr}`, /MDDS CONFIG fastdds/);
      } else {
        assert.match(`${result.stdout}\n${result.stderr}`, /MDDS Fast DDS binding: CONFIG/);
      }
    }
    console.log(`PASS ${name}`);
  } catch (error) {
    ++failures;
    console.error(`FAIL ${name}: ${error.message}`);
  }
}
console.log(`BINDING_CONFIG ${cases.length - failures}/${cases.length}; evidence=${output}`);
process.exitCode = failures ? 1 : 0;
