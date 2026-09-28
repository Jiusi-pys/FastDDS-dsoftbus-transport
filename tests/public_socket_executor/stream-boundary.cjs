'use strict';
const assert=require('node:assert/strict');const {createLines,createBoundedSink}=require('../../scripts/public_socket/bounded-output.cjs');
const rows=[],errors=[];const parser=createLines(x=>rows.push(x),x=>errors.push(x));
const bytes=Buffer.from('first 模拟\r\nlast 无换行');for(const b of bytes)parser(Buffer.from([b]));assert.deepEqual(rows,['first 模拟']);parser.end(true);parser.end(true);assert.deepEqual(rows,['first 模拟','last 无换行']);parser(Buffer.from('late\n'));assert.deepEqual(rows,['first 模拟','last 无换行']);assert.deepEqual(errors,['late_output_after_end']);
const incomplete=[],bad=[];const open=createLines(x=>incomplete.push(x),x=>bad.push(x));open(Buffer.from('unconfirmed tail'));open.end(false);assert.deepEqual(incomplete,[]);assert.deepEqual(bad,['stream_incomplete']);
let overflow=0;const cap=createLines(()=>{},()=>overflow++);cap(Buffer.alloc(65537,65));cap.end(true);assert.equal(overflow,1);
const sink=createBoundedSink({cap:8,write:()=>{}});sink.feed(Buffer.alloc(9));assert.deepEqual(sink.state(),{received:9,retained:8,overflow:1,complete:false});
console.log('PASS R4 split UTF8/CRLF/tail/idempotent close/late input/incomplete/unchanged bounds');
