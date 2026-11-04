'use strict';

// Build-option regression only; real ABI and behavior use the public consumer.
const assert = require('node:assert/strict');
const cp = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const prefix = process.argv[2] && path.resolve(process.argv[2]);
if (!prefix || !fs.existsSync(path.join(prefix, 'share/fastdds/cmake/fastdds-config.cmake'))) {
    throw new Error('Usage: node tests/transport_build_configuration_tests.cjs <actual-installed-prefix>');
}
const output = fs.mkdtempSync(path.join(os.tmpdir(), 'mdds-facade-options-'));
const env = {}, paths = [];
for (const [key, value] of Object.entries(process.env)) {
    if (key.toLowerCase() !== 'path') env[key] = value;
    else for (const entry of value.split(path.delimiter)) {
        if (entry && !paths.some(previous => previous.toLowerCase() === entry.toLowerCase())) paths.push(entry);
    }
}
env.Path = paths.join(path.delimiter);
const base = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF',
    '-DBUILD_INTERFACE_PROBE=OFF', '-DBUILD_SOFTBUS_CAPABILITY_PROBE=OFF',
    '-DBUILD_DSOFTBUS_TRANSPORT_FACADE=ON', '-DFASTDDS_DSOFTBUS_FASTDDS_MODE=CONFIG',
    `-Dfastdds_DIR=${prefix}/share/fastdds/cmake`, `-DCMAKE_PREFIX_PATH=${prefix}`];
const cases = [
    ['source-probe-forbidden', ['-DFASTDDS_DSOFTBUS_FASTDDS_MODE=SOURCE_PROBE'],
        /BUILD_DSOFTBUS_TRANSPORT_FACADE requires FASTDDS_DSOFTBUS_FASTDDS_MODE=CONFIG/],
    ['input-required', ['-DBUILD_DSOFTBUS_INPUT_CHANNELS=OFF'],
        /BUILD_DSOFTBUS_TRANSPORT_FACADE requires BUILD_DSOFTBUS_INPUT_CHANNELS/],
    ['runtime-required', ['-DBUILD_DSOFTBUS_RUNTIME_SEND_PATH=OFF'],
        /BUILD_DSOFTBUS_TRANSPORT_FACADE requires BUILD_DSOFTBUS_RUNTIME_SEND_PATH/],
    ['tools-only-no-package', ['-DBUILD_DSOFTBUS_TRANSPORT_FACADE=OFF',
        '-DBUILD_DSOFTBUS_DIRECTORY=OFF', '-DBUILD_DSOFTBUS_SEND_QUEUE=OFF',
        '-DBUILD_DSOFTBUS_INPUT_CHANNELS=OFF', '-DBUILD_DSOFTBUS_RUNTIME_SEND_PATH=OFF',
        '-Dfastdds_DIR=', '-DCMAKE_PREFIX_PATH=', '-DCMAKE_BUILD_TYPE=Debug'], null],
];
let failures = 0;
for (const [name, extra, expectedFailure] of cases) {
    const args = ['-S', root, '-B', path.join(output, name), ...base, ...extra];
    const start = new Date().toISOString();
    const result = cp.spawnSync('cmake', args, {cwd: root, env, encoding: 'utf8',
        windowsHide: true, timeout: 30000, maxBuffer: 16 * 1024 * 1024});
    fs.writeFileSync(path.join(output, `${name}.json`), JSON.stringify({start,
        end: new Date().toISOString(), command: 'cmake', args, status: result.status,
        signal: result.signal, error: result.error?.message, stdout: result.stdout, stderr: result.stderr}, null, 2));
    try {
        assert.equal(result.error, undefined, 'spawn/timeout is not an expected configuration rejection');
        assert.equal(result.signal, null);
        const log = result.stdout + result.stderr;
        if (expectedFailure) {
            assert.notEqual(result.status, 0, 'invalid facade configuration was accepted');
            assert.match(log, expectedFailure);
        } else assert.equal(result.status, 0, log);
        console.log(`PASS ${name}`);
    } catch (error) {
        ++failures;
        console.error(`FAIL ${name}: ${error.message}`);
    }
}
console.log(`tests=${cases.length} failed=${failures} scope=CONFIGURATION_ONLY evidence=${output}`);
process.exitCode = failures ? 1 : 0;
