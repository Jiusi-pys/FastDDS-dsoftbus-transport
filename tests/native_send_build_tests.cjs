'use strict';
// Root N2 wiring and external source-subproject consumer; no duplicated implementation.
const assert = require('node:assert/strict');
const cp = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const prefix = process.argv[2] && path.resolve(process.argv[2]);
if (process.argv.length !== 3 || !prefix ||
    !fs.existsSync(path.join(prefix, 'share/fastdds/cmake/fastdds-config.cmake'))) {
    throw new Error('Usage: node tests/native_send_build_tests.cjs <installed-host-prefix>');
}
fs.mkdirSync(path.join(root, '.artifacts'), {recursive: true});
const output = fs.mkdtempSync(path.join(root, '.artifacts', 'n3-wiring-'));
const env = {}, paths = [path.join(prefix, 'bin')];
for (const [key, value] of Object.entries(process.env)) {
    if (key.toLowerCase() !== 'path') env[key] = value;
    else for (const entry of value.split(path.delimiter)) {
        if (entry && !paths.some(p => p.toLowerCase() === entry.toLowerCase())) paths.push(entry);
    }
}
env[process.platform === 'win32' ? 'Path' : 'PATH'] = paths.join(path.delimiter);
const fixture = `cmake_minimum_required(VERSION 3.22)
project(n2_root_consumer LANGUAGES C CXX)
add_subdirectory("\${MDDS_SOURCE_ROOT}" mdds)
get_property(option_type CACHE BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION PROPERTY TYPE)
if(NOT option_type STREQUAL "BOOL")
    message(FATAL_ERROR "N3_WIRING: missing BOOL option")
endif()
if(EXPECT_SEND)
    if(NOT BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION OR NOT TARGET fastdds_dsoftbus_native_send_execution)
        message(FATAL_ERROR "N3_WIRING: missing production target")
    endif()
    get_target_property(kind fastdds_dsoftbus_native_send_execution TYPE)
    get_target_property(links fastdds_dsoftbus_native_send_execution INTERFACE_LINK_LIBRARIES)
    get_target_property(features fastdds_dsoftbus_native_send_execution INTERFACE_COMPILE_FEATURES)
    get_target_property(sources fastdds_dsoftbus_native_send_execution SOURCES)
    if(NOT kind STREQUAL "STATIC_LIBRARY" OR
       NOT "fastdds_dsoftbus_native_owner" IN_LIST links OR
       NOT "fastdds_dsoftbus_runtime_send_path" IN_LIST links OR
       NOT "cxx_std_17" IN_LIST features OR
       NOT sources STREQUAL "src/NativeSendExecution.cpp")
        message(FATAL_ERROR "N3_WIRING: incorrect production source/dependencies")
    endif()
elseif(BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION OR TARGET fastdds_dsoftbus_native_send_execution)
    message(FATAL_ERROR "N3_WIRING: disabled target exists")
endif()
get_property(tests DIRECTORY "\${MDDS_SOURCE_ROOT}" PROPERTY TESTS)
if(EXPECT_TEST)
    if(NOT TARGET native_send_execution_tests OR NOT "dsoftbus.native_send_execution" IN_LIST tests)
        message(FATAL_ERROR "N3_WIRING: missing CTest")
    endif()
    get_target_property(test_sources native_send_execution_tests SOURCES)
    get_target_property(test_links native_send_execution_tests LINK_LIBRARIES)
    if(NOT test_sources STREQUAL "tests/native_send_execution_tests.cpp" OR
       NOT "fastdds_dsoftbus_native_send_execution" IN_LIST test_links)
        message(FATAL_ERROR "N3_WIRING: shadow test implementation")
    endif()
    get_property(timeout TEST dsoftbus.native_send_execution DIRECTORY "\${MDDS_SOURCE_ROOT}" PROPERTY TIMEOUT)
    if(NOT timeout STREQUAL "30")
        message(FATAL_ERROR "N3_WIRING: incorrect watchdog")
    endif()
elseif(TARGET native_send_execution_tests OR "dsoftbus.native_send_execution" IN_LIST tests)
    message(FATAL_ERROR "N3_WIRING: disabled test exists")
endif()
if(EXPECT_NO_BINDING AND (TARGET fastdds OR TARGET fastdds_dsoftbus_fastdds_binding))
    message(FATAL_ERROR "N3_WIRING: tools-only loaded Fast DDS")
endif()
if(BUILD_CONSUMER)
    add_executable(n2_consumer main.cpp)
    target_link_libraries(n2_consumer PRIVATE fastdds_dsoftbus_native_send_execution)
endif()
message(STATUS "N3_WIRING: observed expected targets")
`;
const consumer = `#include <mdds/dsoftbus/NativeSendExecution.hpp>
int main() {
    using namespace fastdds_dsoftbus::dsoftbus;
    NativeTableConfiguration c;
    c.service_pkg_name = "com.kaihong.mdds";
    EndpointId id{}; id[15] = 2; Locator local;
    if (!LocatorCodec::Encode(id, local)) return 1;
    c.directory.locals.push_back({local, "consumer.ns", "consumer.listen"});
    auto table = NativeOwnerTable::Build(c);
    if (!table) return 2;
    NativeSendExecution execution(**table.value);
    if (execution.Revoke({}) != NativeStatus::InvalidHandle) return 3;
    auto usage = execution.SnapshotUsage();
    return usage.runtimes == 0 && usage.records == 0 && usage.bytes == 0 ? 0 : 4;
}
`;
const base = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF',
    '-DBUILD_INTERFACE_PROBE=OFF', '-DBUILD_SOFTBUS_CAPABILITY_PROBE=OFF',
    '-DBUILD_DSOFTBUS_DIRECTORY=ON', '-DBUILD_DSOFTBUS_NATIVE_OWNER=ON',
    '-DBUILD_DSOFTBUS_SEND_QUEUE=ON', '-DBUILD_DSOFTBUS_RUNTIME_SEND_PATH=ON',
    '-DBUILD_DSOFTBUS_INPUT_CHANNELS=OFF', '-DBUILD_DSOFTBUS_APPLICATION_ATTACHMENTS=OFF',
    '-DBUILD_DSOFTBUS_TRANSPORT_FACADE=OFF', '-DFASTDDS_DSOFTBUS_FASTDDS_MODE=CONFIG',
    `-Dfastdds_DIR=${prefix}/share/fastdds/cmake`, `-DCMAKE_PREFIX_PATH=${prefix}`,
    `-DFASTDDS_DSOFTBUS_SOURCE_ROOT=${root}`];
const cases = [
    {name: 'default-on', send: true, tests: true, extra: ['-DBUILD_TESTING=ON']},
    {name: 'library-only', send: true, extra: []},
    {name: 'explicit-off', extra: ['-DBUILD_DSOFTBUS_NATIVE_SEND_EXECUTION=OFF', '-DBUILD_TESTING=ON']},
    {name: 'owner-off-default-off', extra: ['-DBUILD_DSOFTBUS_NATIVE_OWNER=OFF']},
    {name: 'runtime-off-default-off', extra: ['-DBUILD_DSOFTBUS_RUNTIME_SEND_PATH=OFF']},
    {name: 'requires-owner', extra: ['-DBUILD_DSOFTBUS_NATIVE_OWNER=OFF', '-DBUILD_DSOFTBUS_NATIVE_SEND_EXECUTION=ON'],
        rejection: /BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION\s+requires\s+BUILD_DSOFTBUS_NATIVE_OWNER/},
    {name: 'requires-runtime', extra: ['-DBUILD_DSOFTBUS_RUNTIME_SEND_PATH=OFF', '-DBUILD_DSOFTBUS_NATIVE_SEND_EXECUTION=ON'],
        rejection: /BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION\s+requires\s+BUILD_DSOFTBUS_RUNTIME_SEND_PATH/},
    {name: 'tools-only', noBinding: true, extra: ['-DBUILD_DSOFTBUS_DIRECTORY=OFF',
        '-DBUILD_DSOFTBUS_NATIVE_OWNER=OFF', '-DBUILD_DSOFTBUS_SEND_QUEUE=OFF',
        '-DBUILD_DSOFTBUS_RUNTIME_SEND_PATH=OFF', '-Dfastdds_DIR=', '-DCMAKE_PREFIX_PATH=',
        '-DCMAKE_DISABLE_FIND_PACKAGE_fastdds=TRUE']},
    {name: 'source-consumer', send: true, consumer: true,
        extra: ['-DFASTDDS_DSOFTBUS_FASTDDS_MODE=SOURCE_PROBE', '-Dfastdds_DIR=', '-DCMAKE_PREFIX_PATH=']},
    {name: 'config-consumer', send: true, consumer: true, extra: []},
];
function run(label, command, args) {
    const started = new Date().toISOString();
    const r = cp.spawnSync(command, args, {cwd: root, env, encoding: 'utf8', windowsHide: true,
        timeout: 30000, maxBuffer: 16 * 1024 * 1024});
    fs.writeFileSync(path.join(output, label + '.json'), JSON.stringify({
        started, ended: new Date().toISOString(), command, args, status: r.status,
        signal: r.signal, error: r.error?.message, stdout: r.stdout, stderr: r.stderr
    }, null, 2), {flag: 'wx'});
    assert.equal(r.error, undefined, 'spawn/timeout is not an expected rejection');
    assert.equal(r.signal, null);
    assert.equal(typeof r.status, 'number');
    return r;
}
let failed = 0;
for (const t of cases) {
    const source = path.join(output, t.name), build = path.join(source, 'b');
    fs.mkdirSync(source);
    fs.writeFileSync(path.join(source, 'CMakeLists.txt'), fixture, {flag: 'wx'});
    fs.writeFileSync(path.join(source, 'main.cpp'), consumer, {flag: 'wx'});
    try {
        const r = run(t.name, 'cmake', ['-S', source, '-B', build, ...base, ...t.extra,
            `-DEXPECT_SEND=${t.send ? 'ON' : 'OFF'}`, `-DEXPECT_TEST=${t.tests ? 'ON' : 'OFF'}`,
            `-DEXPECT_NO_BINDING=${t.noBinding ? 'ON' : 'OFF'}`, `-DBUILD_CONSUMER=${t.consumer ? 'ON' : 'OFF'}`]);
        if (t.rejection) {
            assert.notEqual(r.status, 0);
            assert.match(r.stdout + r.stderr, t.rejection);
        } else {
            assert.equal(r.status, 0, r.stdout + r.stderr);
            assert.match(r.stdout, /N3_WIRING: observed expected targets/);
            if (t.consumer) {
                const b = run(t.name + '-build', 'cmake', ['--build', build, '--target', 'n2_consumer', '-j', '4']);
                assert.equal(b.status, 0, b.stdout + b.stderr);
                const e = run(t.name + '-run', path.join(build, process.platform === 'win32' ? 'n2_consumer.exe' : 'n2_consumer'), []);
                assert.equal(e.status, 0, e.stdout + e.stderr);
            }
        }
        console.log('PASS ' + t.name);
    } catch (e) {
        ++failed; console.error('FAIL ' + t.name + ': ' + e.message);
    }
}
console.log(`tests=${cases.length} failed=${failed} evidence=${output}`);
process.exitCode = failed ? 1 : 0;
