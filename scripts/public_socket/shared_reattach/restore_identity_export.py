"""One scoped owned-export recovery; no SDK query, token action or business."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,subprocess

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def need(value,why):
 if not value:raise ValueError(why)
def put(p,value):
 p=Path(p);p.parent.mkdir(parents=True,exist_ok=True)
 with p.open('x',encoding='utf-8') as f:json.dump(value,f,indent=2);f.write('\n')
def module(path,name):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def restore(selection,review,output):
 selection=Path(selection).resolve();review=Path(review).resolve();output=Path(output).resolve()
 selected=read(selection);approved=read(review)
 need(approved.get('verdict')=='PASS' and approved.get('allow_owned_identity_restore') is True and approved.get('selection_sha256')==sha(selection),'Exact recovery review missing')
 need(selected.get('script_sha256')==sha(__file__),'Recovery source changed')
 window=Path(selected['window']).resolve();cfg=read(window/'window.json');repo=Path(cfg['repo']).resolve()
 need(sha(window/'window.json')==selected['window_sha256'] and cfg.get('identity_qualification') is True and cfg.get('scenario')=='readonly-directory-query','Foreign recovery window')
 need(output.parent==window/'identity-artifact-recovery' and output.name=='attempt-1' and not output.exists(),'Fresh single owned-artifact recovery required')
 need(selected.get('original_summary_sha256')==sha(window/'window-summary.json') and selected.get('no_sdk_queries') is True and selected.get('no_permission_actions') is True and selected.get('no_business') is True,'Recovery scope drift')
 need(sha(cfg['hdc_path'])==selected['driver_sha256'],'Recovery driver drift')
 collector_path=repo/'scripts/public_socket/pregrant_recovery.py';receipt_path=Path(__file__).parent/'executor-route-query/query_receipt.py'
 need(selected.get('collector_sha256')==sha(collector_path) and selected.get('receipt_source_sha256')==sha(receipt_path),'Recovery dependency drift')
 collector=module(collector_path,'owned_export_collector');receipts=module(receipt_path,'owned_export_receipts')
 for side in 'AB':
  guard=read(window/side/'final-guard.json')
  need(all(guard.get(k) is True for k in ('Passed','byte_match','metadata_match','process_absent')) and guard.get('service_permission')=='DENIED','Original normal restoration incomplete')
  need((window/side/'baseline.json').read_bytes()==(window/side/'after.json').read_bytes(),'Original token bytes not restored')
 output.mkdir(parents=True)
 result=dict(scope='OWNED_IDENTITY_ARTIFACT_RECOVERY_ONLY',run=cfg['run'],original_window_sha256=selected['window_sha256'],original_summary_sha256=selected['original_summary_sha256'],sdk_queries=0,permission_actions=0,business_dispatches=0,sides={},status='INCOMPLETE')
 def command(side,name,args,remote=False):
  folder=output/side/name;folder.mkdir(parents=True);argv=[cfg['hdc_path'],'-t',cfg['targets'][side],*args]
  put(folder/'command.json',argv);child=subprocess.Popen(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,cwd=repo)
  put(folder/'spawn.json',dict(pid=child.pid,identity=collector.process_identity(child.pid)))
  stdout,stderr,timed,complete=collector.collect_child(child,30)
  (folder/'stdout.log').write_bytes(stdout);(folder/'stderr.log').write_bytes(stderr);(folder/'exit-code.txt').write_text(str(child.poll()))
  put(folder/'result.json',dict(exit_code=child.poll(),timed_out=timed,capture_complete=complete,stdout_sha256=sha(folder/'stdout.log'),stderr_sha256=sha(folder/'stderr.log')))
  need(not timed and complete and child.poll()==0,'Recovery local command completion failed')
  text=stdout.decode('utf-8-sig').replace('\r\n','\n')
  return collector.remote_text(text,0) if remote else text
 def shell(side,name,core):return command(side,name,['shell',core+collector.WRAPPER],True)
 try:
  for side in 'AB':
   root=cfg['remote'][side];file=root+'/local-udid.bin';need(file==cfg['private_identity_output'][side],'Recovery path drift')
   # Current baseline and process absence are read-only prerequisites.
   baseline=shell(side,'current-token-hash','sha256sum '+collector.TOKEN)
   need(re.findall(r'^([0-9a-f]{64})\s+'+re.escape(collector.TOKEN)+r'$',baseline,re.M)==[cfg['permission_baseline_sha256'][side]],'Current token baseline changed')
   current_meta=collector.metadata(shell(side,'current-token-metadata',collector.metadata_command()))
   old_meta=read(window/side/'final-guard.json')['metadata_after']
   need(collector.stable_metadata(current_meta)==collector.stable_metadata(old_meta),'Current token metadata changed')
   ps=shell(side,'current-processes','ps -A');need(not re.search(collector.PROCESS_PATTERN,ps,re.I),'Task process still live')
   core='test -f '+file+' && test ! -L '+file+" && stat -c 'MDDS_IDENTITY mode=%a uid=%u bytes=%s links=%h inode=%i' "+file+' && sha256sum '+file+" && stat -c 'MDDS_IDENTITY_PARENT uid=%u' "+root
   observed=shell(side,'identity-file-metadata',core)
   fields=re.findall(r'^MDDS_IDENTITY mode=600 uid=([0-9]+) bytes=([1-9][0-9]?) links=1 inode=([1-9][0-9]*)$',observed,re.M)
   hashes=re.findall(r'^([0-9a-f]{64})\s+'+re.escape(file)+r'$',observed,re.M)
   need(len(fields)==1 and len(hashes)==1,'Owned identity metadata incomplete');uid,size,inode=fields[0];size=int(size);digest=hashes[0]
   need(1<=size<=64 and re.findall(r'^MDDS_IDENTITY_PARENT uid=([0-9]+)$',observed,re.M)==[uid],'Owned identity size/parent mismatch')
   destination=output/side/'identity/local-udid.bin';destination.parent.mkdir();command(side,'identity-file-receive',['file','recv',file,str(destination)])
   data=destination.read_bytes();need(len(data)==size and sha(destination)==digest and not any(ch in data for ch in (0,10,13)),'Owned identity raw transfer mismatch')
   original=window/side/'identity/local-udid.bin'
   if original.exists():need(original.read_bytes()==data,'Recovery export differs from original transfer')
   shell(side,'identity-file-remove-owned',receipts.identity_removal_command(file,inode,digest))
   result['sides'][side]=dict(local_path=str(destination),sha256=digest,bytes=size,mode='600',uid=uid,inode=inode,remote_absent=True,disposition='EXACT_OWNED_EXPORT_RECEIVED_AND_REMOVED')
  result['status']='PASS'
 except Exception as error:
  result['first_failure']=str(error);put(output/'RESULT.json',result);raise
 put(output/'RESULT.json',result);return dict(status=result['status'],sdk_queries=0,permission_actions=0,business_dispatches=0)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--selection',required=True);p.add_argument('--review',required=True);p.add_argument('--output',required=True);a=p.parse_args();print(json.dumps(restore(a.selection,a.review,a.output)))
