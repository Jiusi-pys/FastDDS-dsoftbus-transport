'use strict';
const assert=require('node:assert/strict');
const {buildCommands}=require('../../scripts/public_socket/shared_reattach/executor/runner-template.cjs');
const run='1234567890abcdef',tokens={A:'101',B:'102'};
const c={scenario:'ROS2_DSOFTBUS',run,main_member:'ordinary_ros2_transport_consumer',main_mode:'dual',main_prefix:'ROS_MAIN',timeout_seconds:300,remote:{A:`/data/local/tmp/mdds_dds_${run}_A`,B:`/data/local/tmp/mdds_dds_${run}_B`},token:tokens,system_library_dirs:['/system/lib64/platformsdk','/system/lib64/chipset-sdk-sp','/system/lib64/chipset-sdk','/system/lib64']};
for(const [side,command] of Object.entries(buildCommands(c,tokens))){
 assert(command.includes(` ROS_LOG_DIR=${c.remote[side]}/ros-log `),'Isolated ROS must declare its own log directory before startup');
 assert(command.includes('env -i '));
 assert(command.includes(`FASTDDS_DSOFTBUS_REQUIRED_CONFIG=${c.remote[side]}/ros.conf `));
 assert(!command.includes('HOME=')&&!command.includes('disable-logging'),'Do not inherit global home or disable logging');
 assert(command.includes(`timeout 300 ${c.remote[side]}/${c.main_member} ${side} ${run} dual`));
}
console.log('ROS_COMMAND_ENVIRONMENT_PASS: both sides; real command builder; no driver dispatch');
