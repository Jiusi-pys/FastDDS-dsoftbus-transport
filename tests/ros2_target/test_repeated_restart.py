"""Host synthetic evidence validation; no SDK or board communication evidence."""
import base64
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
import repeated_restart_oracle as O
import run_repeated_restart as H
spec=importlib.util.spec_from_file_location('repeated_probe',HERE.parents[1]/'integrations/ros2/target/repeated_restart_probe.py')
P=importlib.util.module_from_spec(spec);spec.loader.exec_module(P)
SCENE='1234567890abcdef'
RUNS=dict(zip(O.INSTANCES,('a'*16,'b'*16,'c'*16,'d'*16)))

def transcript(instance):
    out=[]
    def emit(event,**kw):
        out.append(dict(schema=1,scenario=SCENE,instance=instance,run=RUNS[instance],side=instance[0],
            pid=100+O.INSTANCES.index(instance),role='requester' if instance=='A' else 'responder',
            seq=len(out)+1,monotonic_ns=(len(out)+1)*100000000,event=event,**kw))
    emit('NODE_READY');emit('ENTITY_READY')
    for phase in (range(3) if instance=='A' else (int(instance[1]),)):
        if instance=='A' and phase:emit('PHASE_ACCEPTED',phase=phase)
        for n in range(phase*5,(phase+1)*5):
            text='r28_'+SCENE+'_'+str(n).zfill(2)
            data=b'\0\1\0\0'+struct.pack('<I',len(text)+1)+text.encode()+b'\0'
            for event in (('SEND','RECEIVE') if instance=='A' else ('RECEIVE','ECHO')):
                emit(event,sequence=n,text=text,cdr_base64=base64.b64encode(data).decode(),
                    cdr_sha256=hashlib.sha256(data).hexdigest(),cdr_bytes=len(data))
        emit('PHASE_COMPLETE',phase=phase,received=(phase+1)*5)
    emit('FINISH_ACCEPTED',reason='NORMAL_COMPLETION');emit('NODE_DESTROY_RETURN',returned=True)
    emit('SHUTDOWN_RETURN',returned=True);emit('FINAL',success=True,node_created=True)
    return out

def owner_records():
    out=[]
    for round in (1,2):
        old=round*10;new=old+10
        for stage in ('ARMED','OLD_BOTH_READY','PERMIT_CONSUMED','NEW_BOTH_ACTIVE'):
            out.append(dict(schema='1',run=RUNS['A'],side='A',round=str(round),stage=stage,
                listener='1',listener_entry='1',client_entry='2',old_dial=str(old+1),old_accepted=str(old+2),
                old_dial_owner=str(old+101),old_accepted_owner=str(old+102),new_dial=str(new+1),new_accepted=str(new+2),
                new_dial_owner=str(new+101),new_accepted_owner=str(new+102),retired_dial=str(old+1),retired_accepted=str(old+2),
                retired_dial_owner=str(old+101),retired_accepted_owner=str(old+102),old_callbacks='0',old_receives='0',
                dial_shutdown_returned='0',accepted_shutdown_returned='0',dial_on_shutdown='1',accepted_on_shutdown='1',
                permit_consumed='1' if stage in ('PERMIT_CONSUMED','NEW_BOTH_ACTIVE') else '0',
                wall_ms=str(1000+len(out)),source='SDK_PUBLIC_SOCKET',sdk_epoch='NOT_PROVEN',sdk_drain='NOT_PROVEN'))
    return out

def lines(rows,prefix='NATIVE_REPEAT_EVENT'):
    return '\n'.join(prefix+' '+' '.join(k+'='+v for k,v in row.items()) for row in rows)

def resource_records():
    return [{k:str(v) for k,v in dict(schema=1,run=RUNS['A'],side='A',phase=p,
        owners=3+2*p,owned_ids=3+2*p,high_water=3+2*p,namespaces=1,callbacks=0,receives=0,
        ready=2,listening=1,retained=2*p,failed_retained=0,setup_reserved=0,setup_entered=0,
        send_reserved=0,send_entered=0,close_reserved=0,close_entered=0,owner_limit=12,
        dial=11+10*p,accepted=12+10*p,listener=1,socket_calls=2+p,shutdown_calls=0,shutdown_returns=0,
        wall_ms=(999,1003,1007)[p],source='SDK_PUBLIC_SOCKET',sdk_drain='NOT_PROVEN').items()} for p in range(3)]

class RepeatedTests(unittest.TestCase):
    def test_resource_capture_binds_both_identities_and_deadline(self):
        stat='100 (worker name) '+' '.join(['S','99']+['0']*17+['777'])
        argv=['/owned/actionbag_python','--run',RUNS['A']]
        identity=('STAT\n'+stat+'\n\nEXE\n'+argv[0]+'\nCMD\n'+
                  ('\0'.join(argv)+'\0').encode().hex(' ')+'\n').encode()
        resource=(stat+'\nR28_STATUS\nVmRSS:\t42 kB\nThreads:\t3\nR28_FD\n0\n1\nR28_STAT_END\n'+stat+'\n').encode()
        capture=identity+b'\nR28_RESOURCE_BEGIN\n'+resource+b'\nR28_RESOURCE_END\n'+identity
        variants=[('valid',capture,None)]
        for index in (0,2):
            for before,after in [(b'777',b'778'),(b'S 99',b'S 98'),(b'100 (',b'101 ('),
                                 (b'/owned/actionbag_python',b'/other/actionbag_python'),
                                 (b'61 61 61',b'62 61 61'),(b'STAT',b'ABSENT')]:
                segments=[identity,resource,identity];segments[index]=segments[index].replace(before,after,1)
                variants.append(('identity',segments[0]+b'\nR28_RESOURCE_BEGIN\n'+segments[1]+b'\nR28_RESOURCE_END\n'+segments[2],ValueError))
        for bad in (capture[:-10],capture.replace(b'R28_RESOURCE_BEGIN',b'R28_RESOURCE_END',1),
                    capture.replace(resource,b'',1),capture.replace(b'\nEXE\n',b'\nMISSING\n',1)):
            variants.append(('incomplete',bad,ValueError))
        for label,data,error in variants:
            with self.subTest(label=label,data=data),tempfile.TemporaryDirectory() as directory:
                proc=H.RepeatedProcess.__new__(H.RepeatedProcess)
                proc.instance='A';proc.run=RUNS['A'];proc.output=Path(directory);proc.stage_deadline=35
                proc.owned={'A':{'child':dict(pid=100,starttime=777,ppid=99,exe=argv[0],argv=argv)}}
                with patch.object(proc,'shell',return_value=data) as shell,patch.object(H.time,'monotonic',return_value=34):
                    if error:
                        with self.assertRaises(error):proc.resource_sample('PHASE2')
                        self.assertFalse((proc.output/'resources/PHASE2.json').exists())
                    else:
                        proc.resource_sample('PHASE2')
                        self.assertEqual((proc.output/'resources/PHASE2.raw').read_bytes(),resource)
                        self.assertEqual((proc.output/'resources/PHASE2.capture.raw').read_bytes(),capture)
                        receipt=json.loads((proc.output/'resources/PHASE2.json').read_text())
                        self.assertEqual(receipt['capture_sha256'],hashlib.sha256(capture).hexdigest())
                    self.assertEqual(shell.call_count,1)
        for times in ([35], [34,34,35]):
            with self.subTest(times=times),tempfile.TemporaryDirectory() as directory:
                proc.output=Path(directory)
                with patch.object(proc,'shell',return_value=capture),patch.object(H.time,'monotonic',side_effect=times):
                    with self.assertRaisesRegex(ValueError,'crossed stage deadline'):proc.resource_sample('PHASE2')
        with patch.object(proc,'shell',side_effect=OSError('Remote capture command exit 1')):
            with self.assertRaises(OSError):proc.resource_sample('PHASE2')

    def test_workflow_rechecks_deadlines_after_stage_evidence(self):
        # Exercise the actual orchestration with host-only ready inputs; no SDK evidence.
        for stage in ('BUSINESS', 'FINAL_EXIT', 'ANALYZE'):
            for delta in (-0.1, 0.0, 0.1):
                with self.subTest(stage=stage, delta=delta):
                    events=[];clock=SimpleNamespace(now=100.0)
                    scene=object.__new__(H.RepeatedScene)
                    scene.plan={'scenario':SCENE};scene.observed=H.datetime.now(H.timezone.utc)
                    scene.deadline=None;scene.processes={}
                    def record(key,name):
                        def call(*args,**kwargs):
                            events.append((key,name,args))
                            if stage=='FINAL_EXIT' and key=='B2' and name=='confirm_absent':
                                clock.now=scene.deadline+delta
                            if stage=='ANALYZE' and name=='analyze':
                                clock.now=scene.deadline+delta
                        return call
                    for i,key in enumerate(O.INSTANCES):
                        side=key[0]
                        process=SimpleNamespace(side=side,scenario=SCENE,run=RUNS[key],
                            owned={side:{'child':{'pid':100*(i+1),'starttime':str(i+1)}}})
                        for name in ('deploy','launch','maps','publish','extra_control','finish_fixture',
                                     'close_release','confirm_absent','resource_sample'):
                            setattr(process,name,record(key,name))
                        process.collect=lambda:{'A':'','B':''}
                        scene.processes[key]=process
                    scene.sample_phase=record('A','sample_phase')
                    scene.analyze=record('all','analyze')
                    def write(name,value):
                        events.append(('host','write',(name,)))
                        if stage=='BUSINESS' and name=='BUSINESS.json':
                            clock.now=scene.origin+35+delta
                    scene.write=write
                    text=lines(owner_records())
                    scene.wait=lambda keys,predicate,**kwargs:{key:text for key in keys}
                    scene.records=lambda text,key:transcript(key)
                    with patch.object(H.time,'monotonic',lambda:clock.now), \
                            patch.object(O,'continuity',return_value={'scope':'HOST_FIXTURE_NOT_OBSERVED'}):
                        if delta<0:
                            scene.workflow()
                        else:
                            with self.assertRaisesRegex(ValueError,{'BUSINESS':'Business evidence crossed35s',
                                    'FINAL_EXIT':'Final exit evidence crossed20s',
                                    'ANALYZE':'Analysis evidence crossed60s'}[stage]):
                                scene.workflow()
                    if delta>=0 and stage=='BUSINESS':
                        self.assertFalse(any(name=='extra_control' and args==('COMPLETE',)
                                             for _,name,args in events))
                    if delta>=0 and stage=='FINAL_EXIT':
                        self.assertFalse(any(name=='analyze' for _,name,_ in events))

    def test_fifteen_roundtrips_three_distinct_responder_ranges(self):
        a=P.RepeatedLedger(SCENE,'A');peers=[P.RepeatedLedger(SCENE,'B'+str(i)) for i in range(3)]
        for n in range(15):
            value=a.send();self.assertEqual(peers[n//5].receive(value),n);self.assertEqual(a.receive(value),n)
        with self.assertRaises(ValueError):a.send()
        for peer in peers:
            with self.assertRaises(ValueError):peer.receive(peer.payload(0))

    def test_no_duplicate_retry_foreign_or_future_payload(self):
        for key in O.INSTANCES:
            ledger=P.RepeatedLedger(SCENE,key)
            for value in ('foreign',ledger.payload(14)):
                with self.assertRaises(ValueError):ledger.receive(value)
        a=P.RepeatedLedger(SCENE,'A');value=a.send()
        with self.assertRaises(ValueError):a.send()
        a.receive(value)
        with self.assertRaises(ValueError):a.receive(value)

    def test_four_transcripts_all_cdr_copies(self):
        records={}
        for key in O.INSTANCES:
            data=transcript(key)
            raw='\n'.join('R25_PROBE '+json.dumps(r) for r in data)
            records[key]=O.rows(raw,SCENE,key,RUNS[key],data[0]['pid'])
            O.content(records[key],SCENE,key,'ALL',final=True)
        for p in range(3):self.assertEqual(O.pair_content(records['A'],records['B'+str(p)],SCENE,p)['roundtrips'],5)

    def test_missing_release_duplicate_phase_and_false_final(self):
        source=transcript('A');variants=[]
        variants.append([r for r in source if not(r['event']=='PHASE_ACCEPTED' and r['phase']==2)])
        variants.append(source[:3]+[source[2]]+source[3:])
        bad=copy.deepcopy(source);bad[-1]['success']=False;variants.append(bad)
        bad=copy.deepcopy(source);bad[3]['cdr_sha256']='0'*64;variants.append(bad)
        bad=copy.deepcopy(source);next(r for r in bad if r['event']=='PHASE_COMPLETE')['phase']=True;variants.append(bad)
        for bad in variants:
            with self.assertRaises(ValueError):O.content(bad,SCENE,'A','ALL',final=True)

    def test_owner_two_cycles_and_historical_reuse(self):
        source=owner_records()
        self.assertIsNotNone(O.owners(lines(source),RUNS['A'],2,'NEW_BOTH_ACTIVE'))
        self.assertIsNone(O.owners(lines(source[:4]),RUNS['A'],2,'NEW_BOTH_ACTIVE'))
        for field,value in [('new_dial','11'),('new_accepted','22'),('new_dial_owner','111'),
                            ('permit_consumed','0'),('old_callbacks','1'),('retired_dial','11'),('source','HOST_MODEL')]:
            bad=copy.deepcopy(source);bad[-1][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):O.owners(lines(bad),RUNS['A'],2,'NEW_BOTH_ACTIVE')

    def test_skipped_duplicate_and_unconsumed_rounds(self):
        source=owner_records()
        for bad in (source[4:],source[:2]+source[3:],source+source[4:],source[:2]+[source[1]]+source[2:]):
            with self.assertRaises(ValueError):O.owners(lines(bad),RUNS['A'],2,'NEW_BOTH_ACTIVE')

    def test_resource_pid_reuse_and_missing_metrics(self):
        stat='100 (worker name) '+' '.join(['S']+['0']*18+['777'])
        raw=stat+'\nR28_STATUS\nVmRSS:\t42 kB\nThreads:\t3\nR28_FD\n0\n1\nR28_STAT_END\n'+stat
        sample=H.parse_resource(raw,100,777)
        self.assertEqual(sample['metrics'],dict(VmRSS=42,VmHWM='NOT_OBSERVED',Threads=3))
        self.assertEqual(sample['fd_count'],2);self.assertEqual(sample['leak_freedom'],'NOT_PROVEN')
        for bad in (raw.replace('777','778',1),raw.replace('1\nR28_STAT_END','0\nR28_STAT_END'),
                    raw.replace('42 kB','-1 kB'),raw.replace('Threads:\t3','Threads:\t3\nThreads:\t3')):
            with self.assertRaises(ValueError):H.parse_resource(bad,100,777)

    def test_resources_require_live_history_and_round_binding(self):
        anchors=lines(owner_records());source=resource_records()
        def check(data,events=anchors):
            return O.resources(events+'\n'+lines(data,'NATIVE_REPEAT_RESOURCE'),RUNS['A'],final=True)
        self.assertEqual(len(check(source)['samples']),3)
        # A bounded extra retained candidate is reported, not silently discarded.
        extra=copy.deepcopy(source);extra[-1].update(owners='8',high_water='8',retained='5')
        self.assertTrue(check(extra)['owners_include_retired_history'])
        for key,value in [('owners','0'),('owned_ids','0'),('high_water','0'),('ready','0'),('listening','0'),
                          ('retained','0'),('wall_ms','998'),('dial','11'),('accepted','12'),('listener','999'),
                          ('send_reserved','3'),('setup_entered','2'),('close_entered','2'),('namespaces','0')]:
            bad=copy.deepcopy(source);bad[-1][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):check(bad)
        for key in source[-1]:
            bad=copy.deepcopy(source);del bad[-1][key]
            with self.subTest(missing=key),self.assertRaises(ValueError):check(bad)
        with self.assertRaises(ValueError):check(source,events='')
        bad=copy.deepcopy(source);bad[-1]['future']='1'
        with self.assertRaises(ValueError):check(bad)

    def test_deadline_and_controls_remain_finite(self):
        self.assertEqual(H.LIMITS['business_seconds'],35);self.assertEqual(H.LIMITS['probe_seconds'],40)
        self.assertEqual(H.LIMITS['normal_close_release_seconds'],50)
        scene=H.RepeatedScene.__new__(H.RepeatedScene);scene.deadline=35;scene.processes={}
        with patch.object(H.time,'monotonic',side_effect=[34,35]),self.assertRaises(ValueError):scene.wait((),lambda t,s:True)
        for key,name in [('A','ARM3'),('A','REPLACE3'),('A','END'),('B1','ARM2'),('B2','SAMPLE2')]:
            proc=H.RepeatedProcess.__new__(H.RepeatedProcess);proc.instance=key
            with self.assertRaises(ValueError):proc.extra_control(name)

    def test_explicit_completion_requires_all_prior_evidence(self):
        base=lines(owner_records())+'\n'+lines(resource_records(),'NATIVE_REPEAT_RESOURCE')
        row=dict(schema='1',run=RUNS['A'],side='A',round='2',phase='2',permit_closed='1',wall_ms='1008',
                 source='SDK_PUBLIC_SOCKET',sdk_epoch='NOT_PROVEN',sdk_drain='NOT_PROVEN')
        event=lines([row],'NATIVE_REPEAT_COMPLETE')
        self.assertIsNone(O.completion(base,RUNS['A']))
        self.assertIsNotNone(O.completion(base+'\n'+event,RUNS['A']))
        for bad in (event,base+'\n'+event+'\n'+event):
            with self.assertRaises(ValueError):O.completion(bad,RUNS['A'])
        for key,value in [('wall_ms','999'),('permit_closed','0'),('round','1'),('source','HOST_MODEL')]:
            bad=dict(row);bad[key]=value
            with self.assertRaises(ValueError):O.completion(base+'\n'+lines([bad],'NATIVE_REPEAT_COMPLETE'),RUNS['A'])

if __name__=='__main__':unittest.main()
