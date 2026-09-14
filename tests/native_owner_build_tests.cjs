'use strict';

// Root option/target contracts and real external consumers. No SDK calls.
const assert = require('node:assert/strict');
const cp = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const prefix = process.argv[2] && path.resolve(process.argv[2]);
if (process.argv.length !== 3 || !prefix ||
        !fs.existsSync(path.join(prefix, 'share/fastdds/cmake/fastdds-config.cmake'))) {
    throw new Error('Usage: node tests/native_owner_build_tests.cjs <actual-installed-host-prefix>');
}
fs.mkdirSync(path.join(root, '.artifacts'), {recursive: true});
const output = fs.mkdtempSync(path.join(root, '.artifacts', 'n1-build-'));
const env = {}, paths = [path.join(prefix, 'bin')];
for (const [key, value] of Object.entries(process.env)) {
    if (key.toLowerCase() !== 'path') env[key] = value;
    else for (const entry of value.split(path.delimiter)) {
        if (entry && !paths.some(old => old.toLowerCase() === entry.toLowerCase())) paths.push(entry);
    }
}
env[process.platform === 'win32' ? 'Path' : 'PATH'] = paths.join(path.delimiter);
const fixture = `cmake_minimum_required(VERSION 3.22)
project(native_owner_option_observer LANGUAGES C CXX)
add_subdirectory("\${MDDS_SOURCE_ROOT}" mdds)
get_property(option_type CACHE BUILD_DSOFTBUS_NATIVE_OWNER PROPERTY TYPE)
if(NOT option_type STREQUAL "BOOL")
    message(FATAL_ERROR "N1_CONTRACT: missing BOOL option")
endif()
if(EXPECT_OWNER)
    if(NOT BUILD_DSOFTBUS_NATIVE_OWNER OR NOT TARGET mdds_dsoftbus_native_owner)
        message(FATAL_ERROR "N1_CONTRACT: missing public target")
    endif()
    get_target_property(kind mdds_dsoftbus_native_owner TYPE)
    get_target_property(links mdds_dsoftbus_native_owner INTERFACE_LINK_LIBRARIES)
    get_target_property(features mdds_dsoftbus_native_owner INTERFACE_COMPILE_FEATURES)
    if(NOT kind STREQUAL "STATIC_LIBRARY" OR NOT "mdds_dsoftbus_directory" IN_LIST links
            OR NOT "Threads::Threads" IN_LIST links OR NOT "cxx_std_17" IN_LIST features)
        message(FATAL_ERROR "N1_CONTRACT: missing static/PUBLIC dependency contract")
    endif()
elseif(BUILD_DSOFTBUS_NATIVE_OWNER OR TARGET mdds_dsoftbus_native_owner)
    message(FATAL_ERROR "N1_CONTRACT: disabled target present")
endif()
get_property(root_tests DIRECTORY "\${MDDS_SOURCE_ROOT}" PROPERTY TESTS)
if(EXPECT_TEST)
    if(NOT TARGET native_owner_table_tests OR NOT "dsoftbus.native_owner_model" IN_LIST root_tests)
        message(FATAL_ERROR "N1_CONTRACT: missing test target/CTest")
    endif()
    get_property(timeout TEST dsoftbus.native_owner_model DIRECTORY "\${MDDS_SOURCE_ROOT}" PROPERTY TIMEOUT)
    if(NOT timeout STREQUAL "20")
        message(FATAL_ERROR "N1_CONTRACT: changed model watchdog")
    endif()
elseif(TARGET native_owner_table_tests OR "dsoftbus.native_owner_model" IN_LIST root_tests)
    message(FATAL_ERROR "N1_CONTRACT: disabled test present")
endif()
if(EXPECT_LIBRARY_ONLY)
    get_property(targets DIRECTORY "\${MDDS_SOURCE_ROOT}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(t IN LISTS targets)
        get_target_property(kind "\${t}" TYPE)
        if(kind STREQUAL "EXECUTABLE")
            message(FATAL_ERROR "N1_CONTRACT: library-only contains executable")
        endif()
    endforeach()
endif()
if(EXPECT_NO_BINDING AND (TARGET fastdds OR TARGET mdds_fastdds_binding))
    message(FATAL_ERROR "N1_CONTRACT: tools-only loaded a binding")
endif()
if(BUILD_CONSUMER)
    add_executable(n1_consumer main.cpp)
    target_link_libraries(n1_consumer PRIVATE mdds_dsoftbus_native_owner)
endif()
message(STATUS "N1_CONTRACT: expected targets observed")
`;
const consumer = `#include <mdds/dsoftbus/NativeOwnerTable.hpp>
int main() {
    using namespace mdds::dsoftbus;
    EndpointId id{}; id[15] = 2; Locator local;
    if (!LocatorCodec::Encode(id, local)) return 1;
    NativeTableConfiguration config;
    config.service_pkg_name = "com.kaihong.mdds";
    config.directory.locals.push_back({local, "consumer.ns", "consumer.listen"});
    auto result = NativeOwnerTable::Build(config);
    if (!result || !*result.value) return 2;
    auto& table = **result.value;
    auto space = table.AcquireNamespace(local, config);
    if (!space) return 3;
    {
        auto reservation = table.ReserveListener(*space.value, local);
        if (!reservation || !reservation.value->socket_call) return 4;
        // Never enter an SDK call: ticket destruction abandons the reservation.
    }
    auto usage = table.SnapshotUsage();
    return usage.owners == 0 && usage.setup.reserved == 0 ? 0 : 5;
}
`;
const base = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF',
    '-DBUILD_INTERFACE_PROBE=OFF', '-DBUILD_SOFTBUS_CAPABILITY_PROBE=OFF',
    '-DBUILD_NATIVE_PERMISSION_PROBE=OFF', '-DBUILD_DSOFTBUS_DIRECTORY=ON',
    '-DBUILD_DSOFTBUS_SEND_QUEUE=OFF', '-DBUILD_DSOFTBUS_INPUT_CHANNELS=OFF',
    '-DBUILD_DSOFTBUS_APPLICATION_ATTACHMENTS=OFF', '-DBUILD_DSOFTBUS_RUNTIME_SEND_PATH=OFF',
    '-DBUILD_DSOFTBUS_TRANSPORT_FACADE=OFF', '-DMDDS_FASTDDS_MODE=CONFIG',
    `-Dfastdds_DIR=${prefix}/share/fastdds/cmake`, `-DCMAKE_PREFIX_PATH=${prefix}`,
    `-DMDDS_SOURCE_ROOT=${root}`];
const cases = [
    {name: 'default-on', owner: true, tests: true, extra: ['-DBUILD_TESTING=ON']},
    {name: 'directory-off-default-off', extra: ['-DBUILD_DSOFTBUS_DIRECTORY=OFF']},
    {name: 'explicit-off', extra: ['-DBUILD_DSOFTBUS_NATIVE_OWNER=OFF']},
    {name: 'off-with-testing', extra: ['-DBUILD_DSOFTBUS_NATIVE_OWNER=OFF', '-DBUILD_TESTING=ON']},
    {name: 'requires-directory', extra: ['-DBUILD_DSOFTBUS_DIRECTORY=OFF', '-DBUILD_DSOFTBUS_NATIVE_OWNER=ON'],
        rejection: /BUILD_DSOFTBUS_NATIVE_OWNER\s+requires\s+BUILD_DSOFTBUS_DIRECTORY/},
    {name: 'library-only', owner: true, libraryOnly: true, extra: ['-DBUILD_DSOFTBUS_NATIVE_OWNER=ON']},
    {name: 'tools-only-no-package', libraryOnly: true, noBinding: true,
        extra: ['-DBUILD_DSOFTBUS_DIRECTORY=OFF', '-Dfastdds_DIR=', '-DCMAKE_PREFIX_PATH=',
            '-DCMAKE_DISABLE_FIND_PACKAGE_fastdds=TRUE', '-DCMAKE_DISABLE_FIND_PACKAGE_fastcdr=TRUE',
            '-DCMAKE_DISABLE_FIND_PACKAGE_foonathan_memory=TRUE']},
    {name: 'source-consumer', owner: true, libraryOnly: true, consumer: true,
        extra: ['-DMDDS_FASTDDS_MODE=SOURCE_PROBE', '-Dfastdds_DIR=', '-DCMAKE_PREFIX_PATH=']},
    {name: 'config-consumer', owner: true, libraryOnly: true, consumer: true, extra: []},
];
function run(label, command, args) {
    const start = new Date().toISOString();
    const r = cp.spawnSync(command, args, {cwd: root, env, encoding: 'utf8', windowsHide: true,
        timeout: 30000, maxBuffer: 16 * 1024 * 1024});
    fs.writeFileSync(path.join(output, label + '.json'), JSON.stringify({start,
        end: new Date().toISOString(), command, args, status: r.status, signal: r.signal,
        error: r.error?.message, stdout: r.stdout, stderr: r.stderr}, null, 2), {flag: 'wx'});
    assert.equal(r.error, undefined, 'spawn/timeout is not an expected rejection');
    assert.equal(r.signal, null);
    assert.equal(typeof r.status, 'number');
    return r;
}
let failed = 0;
for (const test of cases) {
    const source = path.join(output, test.name), build = path.join(source, 'b');
    fs.mkdirSync(source);
    fs.writeFileSync(path.join(source, 'CMakeLists.txt'), fixture, {flag: 'wx'});
    fs.writeFileSync(path.join(source, 'main.cpp'), consumer, {flag: 'wx'});
    try {
        const r = run(test.name, 'cmake', ['-S', source, '-B', build, ...base, ...test.extra,
            `-DEXPECT_OWNER=${test.owner ? 'ON' : 'OFF'}`, `-DEXPECT_TEST=${test.tests ? 'ON' : 'OFF'}`,
            `-DEXPECT_LIBRARY_ONLY=${test.libraryOnly ? 'ON' : 'OFF'}`,
            `-DEXPECT_NO_BINDING=${test.noBinding ? 'ON' : 'OFF'}`,
            `-DBUILD_CONSUMER=${test.consumer ? 'ON' : 'OFF'}`]);
        if (test.rejection) {
            assert.notEqual(r.status, 0);
            assert.match(r.stdout + r.stderr, test.rejection);
        } else {
            assert.equal(r.status, 0, r.stdout + r.stderr);
            assert.match(r.stdout, /N1_CONTRACT: expected targets observed/);
            if (test.consumer) {
                const b = run(test.name + '-build', 'cmake', ['--build', build, '--target', 'n1_consumer', '-j', '4']);
                assert.equal(b.status, 0, b.stdout + b.stderr);
                const e = run(test.name + '-run', path.join(build, process.platform === 'win32' ? 'n1_consumer.exe' : 'n1_consumer'), []);
                assert.equal(e.status, 0, e.stdout + e.stderr);
            }
        }
        console.log('PASS ' + test.name);
    } catch (error) {
        ++failed;
        console.error('FAIL ' + test.name + ': ' + error.message);
    }
}
console.log(`tests=${cases.length} failed=${failed} scope=ROOT_BUILD_AND_HOST_CONSUMER evidence=${output}`);
process.exitCode = failed ? 1 : 0;
