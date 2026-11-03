'use strict';
function createBoundedSink({cap=8388608,write,overflow=()=>{}}){
 let received=0,retained=0,excess=0,failed=false;
 return {feed(data){received+=data.length;const n=Math.min(data.length,Math.max(0,cap-retained));if(n)write(data.subarray(0,n));retained+=n;excess+=data.length-n;if(excess&&!failed){failed=true;overflow();}},state(){return {received,retained,overflow:excess,complete:!excess};}};
}
// Split incrementally: neither a giant incoming chunk nor a missing newline can
// enlarge retained parser memory beyond one 64 KiB fragment.
function createLines(line,failure=()=>{}){let pending='',discard=false,ended=false;const decoder=new (require('node:string_decoder').StringDecoder)('utf8');const feed=data=>{
 if(ended){if(data.length)failure('late_output_after_end');return;}
 for(let i=0;i<data.length;i+=4096){pending+=decoder.write(data.subarray(i,i+4096));let n;
  while((n=pending.indexOf('\n'))>=0){const row=pending.slice(0,n).replace(/\r$/,'');pending=pending.slice(n+1);if(!discard)line(row);discard=false;}
  if(pending.length>65536){pending='';discard=true;failure('line_overflow');}
 }
};feed.end=(confirmed=true)=>{if(ended)return;ended=true;if(!confirmed){pending='';failure('stream_incomplete');return;}pending+=decoder.end();if(pending&&!discard)line(pending.replace(/\r$/,''));pending='';};return feed;}
module.exports={createBoundedSink,createLines};
