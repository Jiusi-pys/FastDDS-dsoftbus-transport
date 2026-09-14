'use strict';

// Configure-time contract tests. The separate external consumer builds and runs
// the public library; these fixtures never execute a Socket or permission API.
const assert = require('node:assert/strict');
const cp = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const prefix = process.argv[2] && path.resolve(process.argv[2]);
if (process.argv.length !== 3 || !prefix ||
        !fs.existsSync(path.join(prefix, 'share/fastdds/cmake/fastdds-config.cmake'))) {
    throw new Error('Usage: node tests/application_attachments_build_tests.cjs <actual-installed-prefix>');
}
const output = fs.mkdtempSync(path.join(root, '.artifacts', 'awb-'));
const env = {}, paths = [];
for (const [key, value] of Object.entries(process.env)) {
    if (key.toLowerCase() !== 'path') env[key] = value;
    else for (const entry of value.split(path.delimiter)) {
        if (entry && !paths.some(previous => previous.toLowerCase() === entry.toLowerCase())) paths.push(entry);
    }
}
env[process.platform === 'win32' ? 'Path' : 'PATH'] = paths.join(path.delimiter);

const fixture = `cmake_minimum_required(VERSION 3.22)
project(attachments_option_observer LANGUAGES C CXX)
add_subdirectory("\${MDDS_SOURCE_ROOT}" mdds)
get_property(option_type CACHE BUILD_DSOFTBUS_APPLICATION_ATTACHMENTS PROPERTY TYPE)
if(NOT option_type STREQUAL "BOOL")
    message(FATAL_ERROR "ATTACHMENTS_OPTION_CONTRACT: root must declare its BOOL option")
endif()
if(EXPECT_ATTACHMENTS)
    if(NOT BUILD_DSOFTBUS_APPLICATION_ATTACHMENTS OR NOT TARGET mdds_dsoftbus_application_attachments)
        message(FATAL_ERROR "ATTACHMENTS_OPTION_CONTRACT: requested public attachment target is missing")
    endif()
    get_target_property(library_type mdds_dsoftbus_application_attachments TYPE)
    get_target_property(public_links mdds_dsoftbus_application_attachments INTERFACE_LINK_LIBRARIES)
    get_target_property(public_features mdds_dsoftbus_application_attachments INTERFACE_COMPILE_FEATURES)
    if(NOT library_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "ATTACHMENTS_OPTION_CONTRACT: public target must be a static library")
    endif()
    if(NOT "mdds_dsoftbus_input_channels" IN_LIST public_links OR NOT "cxx_std_17" IN_LIST public_features)
        message(FATAL_ERROR "ATTACHMENTS_OPTION_CONTRACT: PUBLIC input-channel/C++17 dependency is missing")
    endif()
elseif(BUILD_DSOFTBUS_APPLICATION_ATTACHMENTS OR TARGET mdds_dsoftbus_application_attachments)
    message(FATAL_ERROR "ATTACHMENTS_OPTION_CONTRACT: disabled attachment target is present")
endif()
if(EXPECT_TEST_TARGET)
    if(NOT TARGET application_attachments_tests)
        message(FATAL_ERROR "ATTACHMENTS_OPTION_CONTRACT: enabled attachment test target is missing")
    endif()
    get_property(root_tests DIRECTORY "\${MDDS_SOURCE_ROOT}" PROPERTY TESTS)
    if(NOT "dsoftbus.application_attachments" IN_LIST root_tests)
        message(FATAL_ERROR "ATTACHMENTS_OPTION_CONTRACT: attachment CTest is missing")
    endif()
elseif(TARGET application_attachments_tests)
    message(FATAL_ERROR "ATTACHMENTS_OPTION_CONTRACT: disabled attachment test target is present")
endif()
if(EXPECT_LIBRARY_ONLY)
    get_property(root_targets DIRECTORY "\${MDDS_SOURCE_ROOT}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS root_targets)
        get_target_property(target_type "\${target}" TYPE)
        if(target_type STREQUAL "EXECUTABLE")
            message(FATAL_ERROR "ATTACHMENTS_OPTION_CONTRACT: library-only configuration contains executable \${target}")
        endif()
    endforeach()
endif()
if(EXPECT_NO_BINDING AND (TARGET fastdds OR TARGET mdds_fastdds_binding))
    message(FATAL_ERROR "ATTACHMENTS_OPTION_CONTRACT: tools-only configuration created a Fast DDS binding")
endif()
message(STATUS "ATTACHMENTS_OPTION_CONTRACT: observed expected actual targets")
`;
const base = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF',
    '-DBUILD_INTERFACE_PROBE=OFF', '-DBUILD_SOFTBUS_CAPABILITY_PROBE=OFF',
    '-DBUILD_NATIVE_PERMISSION_PROBE=OFF', '-DBUILD_DSOFTBUS_DIRECTORY=ON',
    '-DBUILD_DSOFTBUS_SEND_QUEUE=OFF', '-DBUILD_DSOFTBUS_INPUT_CHANNELS=ON',
    '-DBUILD_DSOFTBUS_RUNTIME_SEND_PATH=OFF', '-DBUILD_DSOFTBUS_TRANSPORT_FACADE=OFF',
    '-DMDDS_FASTDDS_MODE=CONFIG', `-Dfastdds_DIR=${prefix}/share/fastdds/cmake`,
    `-DCMAKE_PREFIX_PATH=${prefix}`, `-DMDDS_SOURCE_ROOT=${root}`];
const cases = [
    {name: 'default-on-with-input', extra: ['-DBUILD_TESTING=ON'], attachments: true, tests: true},
    {name: 'source-probe-default-on', extra: ['-DMDDS_FASTDDS_MODE=SOURCE_PROBE',
        '-Dfastdds_DIR=', '-DCMAKE_PREFIX_PATH='], attachments: true},
    {name: 'input-off-default-off', extra: ['-DBUILD_DSOFTBUS_INPUT_CHANNELS=OFF'], attachments: false},
    {name: 'explicit-off', extra: ['-DBUILD_DSOFTBUS_APPLICATION_ATTACHMENTS=OFF'], attachments: false},
    {name: 'explicit-on-requires-input', extra: ['-DBUILD_DSOFTBUS_INPUT_CHANNELS=OFF',
        '-DBUILD_DSOFTBUS_APPLICATION_ATTACHMENTS=ON'], attachments: true,
    rejection: /BUILD_DSOFTBUS_APPLICATION_ATTACHMENTS\s+requires\s+BUILD_DSOFTBUS_INPUT_CHANNELS/},
    {name: 'library-only', extra: ['-DBUILD_DSOFTBUS_APPLICATION_ATTACHMENTS=ON'],
        attachments: true, libraryOnly: true},
    {name: 'tools-only-no-package', extra: ['-DBUILD_DSOFTBUS_DIRECTORY=OFF',
        '-DBUILD_DSOFTBUS_INPUT_CHANNELS=OFF', '-Dfastdds_DIR=', '-DCMAKE_PREFIX_PATH=',
        '-DCMAKE_DISABLE_FIND_PACKAGE_fastdds=TRUE', '-DCMAKE_DISABLE_FIND_PACKAGE_fastcdr=TRUE',
        '-DCMAKE_DISABLE_FIND_PACKAGE_foonathan_memory=TRUE'], attachments: false, noBinding: true,
        libraryOnly: true},
];
let failures = 0;
for (const test of cases) {
    const source = path.join(output, test.name);
    fs.mkdirSync(source);
    fs.writeFileSync(path.join(source, 'CMakeLists.txt'), fixture, {flag: 'wx'});
    const args = ['-S', source, '-B', path.join(source, 'b'), ...base, ...test.extra,
        `-DEXPECT_ATTACHMENTS=${test.attachments ? 'ON' : 'OFF'}`,
        `-DEXPECT_TEST_TARGET=${test.tests ? 'ON' : 'OFF'}`,
        `-DEXPECT_LIBRARY_ONLY=${test.libraryOnly ? 'ON' : 'OFF'}`,
        `-DEXPECT_NO_BINDING=${test.noBinding ? 'ON' : 'OFF'}`];
    const start = new Date().toISOString();
    const result = cp.spawnSync('cmake', args, {cwd: root, env, encoding: 'utf8',
        windowsHide: true, timeout: 30000, maxBuffer: 16 * 1024 * 1024});
    fs.writeFileSync(path.join(output, `${test.name}.json`), JSON.stringify({start,
        end: new Date().toISOString(), command: 'cmake', args, status: result.status,
        signal: result.signal, error: result.error?.message, stdout: result.stdout, stderr: result.stderr}, null, 2),
    {flag: 'wx'});
    try {
        assert.equal(result.error, undefined, 'spawn/timeout is not an expected configuration rejection');
        assert.equal(result.signal, null);
        assert.equal(typeof result.status, 'number');
        const log = result.stdout + result.stderr;
        if (test.rejection) {
            assert.notEqual(result.status, 0, 'invalid attachment configuration was accepted');
            assert.match(log, test.rejection);
        } else {
            assert.equal(result.status, 0, log);
            assert.match(log, /ATTACHMENTS_OPTION_CONTRACT: observed expected actual targets/);
        }
        console.log(`PASS ${test.name}`);
    } catch (error) {
        ++failures;
        console.error(`FAIL ${test.name}: ${error.message}`);
    }
}
console.log(`tests=${cases.length} failed=${failures} scope=CONFIGURATION_ONLY evidence=${output}`);
process.exitCode = failures ? 1 : 0;
