'use strict';
const assert=require('node:assert/strict');
const {controller}=require('../../scripts/public_socket/shared_reattach/executor/ros-control.cjs');
const {createLines}=require('../../scripts/public_socket/bounded-output.cjs');
const run='1234567890abcdef';
const summary=`ROS_RESIDUAL_SUMMARY schema=1 run=${run} side=A pid=101 snapshot=1 participant=123 delete_call=59 reason=ACTIVE_ENTITIES pubs=0 subs=0 topics=1 topic_factories=1 filtered_topics=0 object_stored=1 object_total=1 object_limit=32 truncated=0 selected_return=4 return_scope=SELECTED_BRANCH_OR_NOT_OBSERVED sampling=SEQUENTIAL_NONATOMIC completeness=CAPTURED`;
const object=`ROS_RESIDUAL_OBJECT schema=1 run=${run} side=A pid=101 snapshot=1 ordinal=1 kind=2 sample_order=3 object=456 implementation=789 name_known=1 name_hex=746f706963 type_known=1 type_hex=74797065 referenced=0 text_truncated=0 reference_scope=FAST_DDS_ENDPOINT_REFERENCE_BOOL`;
async function test(kind){
 const ctl=controller({scenario:'ROS2_DSOFTBUS',run},async()=>{},async()=>({}));
 for(const side of ['A','B'])ctl.line(side,`ROS_MAIN scenario=ROS2_DSOFTBUS run=${run} side=${side} pid=${side==='A'?101:102} mode=dual`);
 const parser=createLines(line=>ctl.line('A',line),reason=>ctl.stop(reason));
 let text=summary+'\r\n'+object;
 if(kind==='joined')text='Failed to delete participant'+text;
 if(kind==='wrong-pid')text=text.replace('pid=101','pid=999');
 if(kind==='duplicate')text+='\n'+object;
 if(kind==='missing-summary')text=object;
 const bytes=Buffer.from(text);for(let i=0;i<bytes.length;i+=7)parser(bytes.subarray(i,i+7));parser.end(true);
 const result=await ctl.finish();
 if(['legal','joined'].includes(kind)){assert.equal(result.residual_summaries.A.length,1);assert.equal(result.residual_objects.A.length,1);assert.equal(result.capture_failure.length,0);}
 else assert(result.capture_failure.length);
 assert.equal(result.pass,false,'diagnostic snapshots never manufacture business PASS');
 return {kind,result};
}
(async()=>{const results=[];for(const kind of ['legal','joined','wrong-pid','duplicate','missing-summary'])results.push(await test(kind));console.log(JSON.stringify({scope:'ACTUAL_RAW_BYTE_CONTROLLER_NOT_DEVICE',results}));})().catch(error=>{console.error(error);process.exitCode=1;});
