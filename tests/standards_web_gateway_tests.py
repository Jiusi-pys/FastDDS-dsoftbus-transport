"""Independent requests/XML client against native C1 gateway and DDS peer.

No gateway implementation, schema-preparation helper or C++ model is imported.
All subprocesses and network endpoints are local acceptance fixtures.
"""
import concurrent.futures
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import time
import threading
from email.utils import parsedate_to_datetime
import unittest
import uuid
import xml.etree.ElementTree as X

import requests
from lxml import etree

D='http://www.omg.org/dds/'
S='ddstype://www.omg.org/Standards'
M='application/dds-web+xml'
ROOT=Path(__file__).resolve().parents[1]


def record(n, qualified=True):
    tag=(lambda s:'{'+S+'}'+s) if qualified else (lambda s:s)
    root=X.Element(tag('BoundedRecord'))
    for name,value in [('id',17+n),('sequence_number',100+n),('label','C1&<'+str(n))]:
        X.SubElement(root,tag(name)).text=str(value)
    payload=X.SubElement(root,tag('payload'))
    for i in range(256 if n%2 else 0): X.SubElement(payload,tag('item')).text=str((i+n)%256)
    return X.tostring(root,encoding='utf-8')


class GatewayFixture:
    def __init__(self, case, ttl=300):
        self.case=case
        self.out=Path(os.environ['C1_EVIDENCE_DIR']).resolve()/(case+'-'+uuid.uuid4().hex[:8])
        if not self.out.is_relative_to((ROOT/'build').resolve()): raise RuntimeError('Evidence must be inside workspace build/')
        self.out.mkdir(parents=True,exist_ok=False)
        self.keys={key:uuid.uuid4().hex+uuid.uuid4().hex for key in ('manager','reader','other','limited')}
        self.env=os.environ.copy()
        for name,value in self.keys.items(): self.env['C1_'+name.upper()+'_KEY']=value
        policy=self.out/'policy.xml'
        policy.write_text('<gateway>'
            '<principal name="Manager" owner="Team" key_env="C1_MANAGER_KEY" domains="219,223" permissions="read write manage"/>'
            '<principal name="Reader" owner="Team" key_env="C1_READER_KEY" domains="219,223" permissions="read"/>'
            '<principal name="Other" owner="Other" key_env="C1_OTHER_KEY" domains="219" permissions="read write manage"/>'
            f'<principal name="Limited" owner="Team" key_env="C1_LIMITED_KEY" domains="{os.environ["C1_DOMAIN"]}" permissions="read write manage"/>'
            '</gateway>')
        self.stop=self.out/'stop'
        self.stdout=(self.out/'gateway.stdout.log').open('w',encoding='utf-8')
        self.stderr=(self.out/'gateway.stderr.log').open('w',encoding='utf-8')
        self.command=[os.environ['C1_GATEWAY'],'--policy',str(policy),'--port','0',
            '--representation',os.environ['C1_REPRESENTATION'],'--ttl',str(ttl),'--stop-file',str(self.stop)]
        self.proc=subprocess.Popen(self.command,stdout=self.stdout,stderr=self.stderr,env=self.env,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        self.peers=[]
        end=time.monotonic()+15
        while time.monotonic()<end:
            text=(self.out/'gateway.stdout.log').read_text(encoding='utf-8',errors='replace')
            found=re.search(r'C1_READY port=(\d+)',text)
            if found: self.port=int(found[1]);break
            if self.proc.poll() is not None: raise AssertionError('Gateway startup failed; see '+str(self.out))
            time.sleep(.02)
        else: self.proc.terminate();self.proc.wait(timeout=5);raise AssertionError('Gateway startup timed out')
        self.url='http://127.0.0.1:'+str(self.port)+'/dds/rest1'
        self.receipts_lock=threading.Lock()
        parser=etree.XMLParser(no_network=True,resolve_entities=False)
        self.schema=etree.XMLSchema(etree.parse(str(Path(os.environ['C1_SCHEMAS'])/'rest-bindings.xsd'),parser))
        self.receipts=[]

    def request(self,method,path,status=200,body=None,key='manager',headers=None,params=None):
        h={'Accept':M,'Content-Type':M,'Cache-Control':'no-cache'}
        if key is not None: h['OMG-DDS-API-Key']=self.keys.get(key,key)
        for name,value in (headers or {}).items():
            if value is None: h.pop(name,None)
            else: h[name]=value
        with requests.Session() as client:
            client.trust_env=False
            response=client.request(method,self.url+path,data=body,headers=h,params=params,timeout=8)
        with self.receipts_lock:
            self.receipts.append({'method':method,'path':path,'status':response.status_code,'expected':status,
                'key_identity':key if key in self.keys else 'invalid-or-absent','body_bytes':len(response.content)})
            (self.out/'http-receipts.json').write_text(json.dumps(self.receipts,indent=2))
        if response.status_code!=status: raise AssertionError(f'{method} {path}: {response.status_code} != {status}; evidence {self.out}')
        if response.headers.get('Cache-Control')!='no-store': raise AssertionError('Missing no-store')
        if response.headers.get('Content-Type')!=M: raise AssertionError('Wrong XML media type')
        if method in ('GET','HEAD') and response.status_code==200:
            modified=parsedate_to_datetime(response.headers['Last-Modified'])
            date=parsedate_to_datetime(response.headers['Date'])
            if modified>date: raise AssertionError('Last-Modified is in the future')
        if response.content:
            doc=etree.fromstring(response.content,etree.XMLParser(no_network=True,resolve_entities=False))
            if not self.schema.validate(doc): raise AssertionError('Response schema mismatch: '+str(self.schema.error_log))
        return response

    def app(self,name='App'):
        return self.request('POST','/applications',201,f'<application xmlns="{D}" name="{name}"/>'.encode())

    def graph(self,name='App', condition=False):
        self.app(name)
        base='/applications/'+name
        self.request('POST',base+'/domain_participants',201,f'<domain_participant xmlns="{D}" name="P" domain_id="{os.environ["C1_DOMAIN"]}"/>'.encode())
        part=base+'/domain_participants/P'
        self.request('POST',part+'/registered_types',201,f'<register_type xmlns="{D}" name="Record" type_ref="Standards::BoundedRecord"/>'.encode())
        topic='Topic'+uuid.uuid4().hex[:12]
        self.request('POST',part+'/topics',201,f'<topic xmlns="{D}" name="{topic}" register_type_ref="Record"/>'.encode())
        self.request('POST',part+'/publishers',201,f'<publisher xmlns="{D}" name="Pub"/>'.encode())
        self.request('POST',part+'/subscribers',201,f'<subscriber xmlns="{D}" name="Sub"/>'.encode())
        writer=part+'/publishers/Pub/data_writers/W'
        reader=part+'/subscribers/Sub/data_readers/R'
        self.request('POST',part+'/publishers/Pub/data_writers',201,f'<data_writer xmlns="{D}" name="W" topic_ref="{topic}"/>'.encode())
        extra='<status_condition><status>DATA_AVAILABLE</status></status_condition>' if condition else ''
        self.request('POST',part+'/subscribers/Sub/data_readers',201,f'<data_reader xmlns="{D}" name="R" topic_ref="{topic}">{extra}</data_reader>'.encode())
        return base,part,topic,writer,reader

    def peer(self,role,topic):
        log=self.out/f'peer-{len(self.peers)}.log';err=self.out/f'peer-{len(self.peers)}.stderr.log'
        a=log.open('w',encoding='utf-8');b=err.open('w',encoding='utf-8')
        cmd=[os.environ['C1_PEER'],role,os.environ['C1_DOMAIN'],topic,os.environ['C1_REPRESENTATION'],'Record']
        p=subprocess.Popen(cmd,stdout=a,stderr=b,env=self.env,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        self.peers.append((p,a,b,log,cmd))
        until=time.monotonic()+15
        while time.monotonic()<until:
            if 'C1_PEER_MATCHED' in log.read_text(encoding='utf-8',errors='replace'): return p
            if p.poll() is not None: raise AssertionError('DDS peer failed before discovery; '+str(log))
            time.sleep(.02)
        raise AssertionError('DDS peer discovery deadline; '+str(log))

    def finish_peer(self,p):
        code=p.wait(timeout=20)
        row=next(x for x in self.peers if x[0] is p)
        if code!=0 or 'C1_PEER PASS' not in row[3].read_text(encoding='utf-8'): raise AssertionError('DDS peer failed: '+str(row[3]))

    def ready_reader(self,topic,writer):
        # Subscriber-side MATCHED alone does not prove writer-side discovery.
        # Only the readiness marker is retried. Every actual acceptance sample is sent once.
        peer=self.peer('reader-ready',topic);row=next(x for x in self.peers if x[0] is peer)
        marker=f'<BoundedRecord xmlns="{S}"><id>17</id><sequence_number>4294967295</sequence_number><label>C2_READY</label><payload/></BoundedRecord>'.encode()
        end=time.monotonic()+15;attempts=0
        while time.monotonic()<end:
            if 'C1_PEER_READY' in row[3].read_text(encoding='utf-8'):
                (self.out/'readiness.json').write_text(json.dumps({'status':'PASS','marker_attempts':attempts,'actual_samples_retried':False}),encoding='utf-8')
                return peer
            if peer.poll() is not None:raise AssertionError('Reader exited before readiness; '+str(row[3]))
            self.request('POST',writer,200,marker);attempts+=1;time.sleep(.05)
        raise AssertionError('Data-path readiness deadline; '+str(row[3]))

    def close(self):
        for p,a,b,log,cmd in self.peers:
            if p.poll() is None: p.terminate();p.wait(timeout=5)
            a.close();b.close()
        self.stop.touch()
        forced=False
        try: code=self.proc.wait(timeout=15)
        except subprocess.TimeoutExpired: self.proc.terminate();code=self.proc.wait(timeout=5);forced=True
        self.stdout.close();self.stderr.close()
        text=(self.out/'gateway.stdout.log').read_text(encoding='utf-8',errors='replace')
        (self.out/'process-receipts.json').write_text(json.dumps({'gateway_command':self.command,
            'gateway_exit':code,'forced':forced,'peers':[{'command':x[4],'exit':x[0].returncode} for x in self.peers]},indent=2))
        if code!=0 or forced or 'C1_SHUTDOWN PASS' not in text: raise AssertionError('Gateway cleanup failed; '+str(self.out))


class WebGatewayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for name in ('C1_GATEWAY','C1_PEER','C1_SCHEMAS','C1_DOMAIN','C1_REPRESENTATION','C1_EVIDENCE_DIR'):
            if not os.environ.get(name): raise RuntimeError(name+' is required; native integration cannot be skipped')
    def setUp(self): self.g=GatewayFixture(self._testMethodName,5 if self._testMethodName=='test_application_idle_expiry' else 300)
    def tearDown(self): self.g.close()

    def test_authorization_and_domain_grants(self):
        g=self.g;g.app()
        g.request('GET','/applications',401,key=None)
        g.request('GET','/applications',401,key='0'*64)
        g.request('GET','/applications',200,key='reader')
        g.request('POST','/applications',403,f'<application xmlns="{D}" name="Forbidden"/>'.encode(),key='reader')
        g.request('DELETE','/applications/App',403,key='other')
        g.request('GET','/applications/App',403,key='other')
        g.request('POST','/applications',403,f'<application xmlns="{D}" name="Denied"><domain_participant name="P" domain_id="222"/></application>'.encode())
        r=g.request('GET','/applications',200,params={'applicationNameExpression':'Denied'})
        self.assertEqual(len(X.fromstring(r.content)),0)

    def test_lifecycle_aliases_headers_and_update(self):
        g=self.g;base,part,topic,w,r=g.graph()
        g.request('POST','/applications',409,f'<application xmlns="{D}" name="App"/>'.encode())
        response=g.request('GET','/applications',params={'applicationNameExpression':'A*'})
        self.assertEqual(len(X.fromstring(response.content)),1)
        head=g.request('HEAD','/applications')
        self.assertEqual(head.content,b'');self.assertGreater(int(head.headers['Content-Length']),0)
        g.request('GET',part.replace('domain_participants','participants')+'/topics')
        g.request('GET',base+'/topics',404)
        g.request('PUT',w,204,f'<data_writer xmlns="{D}" name="W" topic_ref="{topic}"/>'.encode())
        g.request('PUT',w,422,f'<data_writer xmlns="{D}" name="Different" topic_ref="{topic}"/>'.encode())
        deleted=g.request('DELETE',part+'/publishers/Pub',204)
        self.assertNotIn('Content-Length',deleted.headers)
        g.request('GET',w,404)
        g.request('DELETE',base,204)
        g.request('GET',part,404)
        g.app()

    def test_independent_dds_reader_receives_http_writes(self):
        g=self.g;_,_,topic,w,_=g.graph();peer=g.ready_reader(topic,w)
        for n in range(10): g.request('POST',w,200,record(n,n%2==0))
        g.finish_peer(peer)

    def test_independent_dds_writer_read_take_waitset(self):
        g=self.g;base,_,topic,_,r=g.graph(condition=True)
        ref='P::Sub::R::status_condition'
        g.request('POST',base+'/waitsets',201,f'<waitset xmlns="{D}" name="Wait"><condition condition_ref="{ref}"/></waitset>'.encode())
        waiting=base+'/waitsets/Wait'
        before=time.monotonic();empty=g.request('GET',waiting,params={'timeout':'.15'})
        self.assertGreaterEqual(time.monotonic()-before,.12);self.assertEqual(len(X.fromstring(empty.content)),0)
        peer=g.peer('writer',topic);g.finish_peer(peer)
        active=g.request('GET',waiting,params={'timeout':'2'})
        self.assertEqual(X.fromstring(active.content)[0].text,ref)
        params={'removeFromReaderCache':'false','minSamples':'10','maxSamples':'64','maxWait':'2'}
        first=g.request('GET',r,params=params);second=g.request('GET',r,params=params)
        def values(response):
            found={}
            for s in X.fromstring(response.content):
                info=s.find('{'+D+'}read_sample_info')
                if info.find('{'+D+'}valid_data').text!='true': continue
                data=s.find('{'+D+'}data')[0]
                n=int(data.find('{'+S+'}id').text)-17
                expected=X.fromstring(record(n))
                self.assertEqual(X.tostring(data),X.tostring(expected))
                found[n]=data
            return set(found)
        self.assertEqual(values(first),set(range(10)));self.assertEqual(values(second),set(range(10)))
        taken=g.request('GET',r,params={'maxSamples':'64','minSamples':'0'})
        self.assertEqual(values(taken),set(range(10)))
        self.assertEqual(values(g.request('GET',r,params={'minSamples':'0'})),set())

    def test_sample_lexical_values_and_cr_are_preserved(self):
        g=self.g;_,_,_,writer,reader=g.graph()
        body=record(0).replace(b'<ns0:id>17</ns0:id>',b'<ns0:id>+00017</ns0:id>')
        body=body.replace(b'<ns0:sequence_number>100</ns0:sequence_number>',b'<ns0:sequence_number>-000</ns0:sequence_number>')
        body=body.replace(b'C1&amp;&lt;0',b'line1&#13;line2')
        body=body.replace(b'<ns0:payload />',b'<ns0:payload><ns0:item>+255</ns0:item><ns0:item>-0</ns0:item></ns0:payload>')
        self.assertIn(b'+00017',body);self.assertIn(b'+255',body)
        g.request('POST',writer,200,body)
        response=g.request('GET',reader,params={'maxWait':'2'})
        data=X.fromstring(response.content).find('.//{'+S+'}BoundedRecord')
        self.assertIsNotNone(data)
        self.assertEqual(data.find('{'+S+'}label').text,'line1\rline2')
        self.assertEqual(data.find('{'+S+'}id').text,'17')
        self.assertEqual(data.find('{'+S+'}sequence_number').text,'0')
        self.assertEqual([x.text for x in data.find('{'+S+'}payload')],['255','0'])

    def test_put_preserves_live_reader_history_waitset_and_pending_read(self):
        g=self.g;base,part,topic,writer,reader=g.graph(condition=True)
        ref='P::Sub::R::status_condition';wait=base+'/waitsets/Wait'
        g.request('POST',base+'/waitsets',201,f'<waitset xmlns="{D}" name="Wait"><condition condition_ref="{ref}"/></waitset>'.encode())
        original=g.request('GET',part).content
        bad=original.replace(b'register_type_ref="Record"',b'register_type_ref="Missing"')
        self.assertNotEqual(bad,original);g.request('PUT',part,422,bad)
        self.assertEqual(g.request('GET',part).content,original)
        peer=g.peer('writer',topic);g.finish_peer(peer)
        params={'removeFromReaderCache':'false','minSamples':'10','maxWait':'2'}
        before=g.request('GET',reader,params=params)
        reader_xml=f'<data_reader xmlns="{D}" name="R" topic_ref="{topic}"><status_condition><status>DATA_AVAILABLE</status></status_condition></data_reader>'.encode()
        g.request('PUT',reader,204,reader_xml)
        after=g.request('GET',reader,params={'removeFromReaderCache':'false','minSamples':'0'})
        def sequences(response):return sorted(int(x.text) for x in X.fromstring(response.content).iter('{'+S+'}sequence_number'))
        self.assertEqual(sequences(before),list(range(100,110)))
        self.assertEqual(sequences(after),sequences(before))
        g.request('GET',wait)
        g.request('GET',reader,params={'minSamples':'0'})
        with concurrent.futures.ThreadPoolExecutor() as pool:
            pending=pool.submit(g.request,'GET',reader,200,None,'manager',None,{'maxWait':'2'})
            time.sleep(.1);g.request('PUT',reader,204,reader_xml)
            g.request('PUT',writer,204,f'<data_writer xmlns="{D}" name="W" topic_ref="{topic}"/>'.encode())
            g.request('POST',writer,200,record(0))
            self.assertEqual(sequences(pending.result(timeout=5)),[100])
        current=g.request('GET',part).content
        g.request('PUT',part,204,current)
        self.assertEqual(g.request('GET',part).content,current)
        g.request('GET',wait)

    def test_waiting_read_accumulates_across_keep_last_rollover(self):
        g=self.g;_,_,_,writer,reader=g.graph()
        def value(n):return record(0).replace(b'>100<',('>'+str(n)+'<').encode())
        g.request('POST',writer,200,value(0))
        g.request('GET',reader,params={'removeFromReaderCache':'false','maxWait':'2'})
        with concurrent.futures.ThreadPoolExecutor() as pool:
            pending=pool.submit(g.request,'GET',reader,200,None,'manager',None,
                {'removeFromReaderCache':'false','minSamples':'20','maxSamples':'32','maxWait':'2'})
            time.sleep(.05)
            for n in range(1,30):
                g.request('POST',writer,200,value(n));time.sleep(.03)
            response=pending.result(timeout=5)
        values=[int(x.text) for x in X.fromstring(response.content).iter('{'+S+'}sequence_number')]
        self.assertGreaterEqual(len(values),20);self.assertLessEqual(len(values),32)
        self.assertEqual(len(values),len(set(values)))
        self.assertEqual(values,sorted(values));self.assertEqual(values[0],0)

    def test_waiting_read_does_not_recount_samples_after_writer_deletion(self):
        g=self.g;_,_,_,writer,reader=g.graph()
        for n in range(10):g.request('POST',writer,200,record(0).replace(b'>100<',('>'+str(n)+'<').encode()))
        g.request('GET',reader,params={'removeFromReaderCache':'false','minSamples':'10','maxWait':'2'})
        with concurrent.futures.ThreadPoolExecutor() as pool:
            start=time.monotonic()
            pending=pool.submit(g.request,'GET',reader,200,None,'manager',None,
                {'removeFromReaderCache':'false','minSamples':'20','maxSamples':'32','maxWait':'.6'})
            time.sleep(.1);g.request('DELETE',writer,204)
            response=pending.result(timeout=3)
        self.assertGreaterEqual(time.monotonic()-start,.5)
        values=[int(x.text) for x in X.fromstring(response.content).iter('{'+S+'}sequence_number')]
        self.assertEqual(sorted(values),list(range(10)))

    def test_xml_rejections_media_and_uri_controls(self):
        g=self.g;_,_,_,w,r=g.graph()
        g.request('GET','/applications',406,headers={'Accept':'application/dds-web+xml;q=0'})
        g.request('GET','/applications',400,headers={'Cache-Control':None})
        g.request('GET','/applications',406,headers={'Accept':'application/json'})
        g.request('GET','/applications',501,headers={'Upgrade':'websocket','Connection':'Upgrade'})
        g.request('POST','/applications?unknown=x',422,f'<application xmlns="{D}" name="NoQuery"/>'.encode())
        g.request('POST',w,415,b'{}',headers={'Content-Type':'application/json'})
        g.request('POST',w,422,b'<!DOCTYPE x [<!ENTITY y SYSTEM "file:///C:/Windows/win.ini">]><x>&y;</x>')
        g.request('POST',w,422,b'<BoundedRecord>')
        bad=X.fromstring(record(0));bad.find('{'+S+'}id').text='4294967296'
        g.request('POST',w,422,X.tostring(bad))
        bad=X.fromstring(record(0));bad.find('{'+S+'}label').text='x'*65
        g.request('POST',w,422,X.tostring(bad))
        bad=X.fromstring(record(1));X.SubElement(bad.find('{'+S+'}payload'),'{'+S+'}item').text='0'
        g.request('POST',w,422,X.tostring(bad))
        g.request('GET',r,422,params={'sampleSelector':'id > 0'})
        g.request('GET',r+'?maxWait=0&maxWait=1',422)
        g.request('GET','/applications/App%2fother',422)
        g.request('GET',r,422,params={'maxWait':'nan'})
        g.request('GET',r,422,params={'maxWait':'3'})

    def test_delete_cancels_pending_reader_without_global_lock(self):
        g=self.g;_,_,_,_,r=g.graph()
        with concurrent.futures.ThreadPoolExecutor() as pool:
            pending=pool.submit(g.request,'GET',r,404,params={'maxWait':'2'})
            time.sleep(.15);start=time.monotonic();g.request('DELETE',r,204)
            self.assertLess(time.monotonic()-start,1.5);pending.result(timeout=5)

    def test_nested_creation_and_quota(self):
        g=self.g
        body=f'<application xmlns="{D}" name="Nested"><domain_participant name="P" domain_id="{os.environ["C1_DOMAIN"]}"><register_type name="Record" type_ref="Standards::BoundedRecord"/><topic name="T" register_type_ref="Record"/><publisher name="Pub"><data_writer name="W" topic_ref="T"/></publisher><subscriber name="Sub"><data_reader name="R" topic_ref="T"/></subscriber></domain_participant></application>'
        g.request('POST','/applications',201,body.encode());g.request('GET','/applications/Nested')
        for name in ('A','B','C'):g.app(name)
        g.request('POST','/applications',429,f'<application xmlns="{D}" name="Limit"/>'.encode())
        g.request('DELETE','/applications/Nested',204)
        g.app('Limit')

    def test_existing_application_domain_authorization(self):
        g=self.g;g.app('Shared')
        denied=223 if os.environ['C1_DOMAIN']=='219' else 219
        p='/applications/Shared/domain_participants'
        g.request('POST',p,201,f'<domain_participant xmlns="{D}" name="P" domain_id="{denied}"/>'.encode())
        for method,path in [('GET',p+'/P'),('GET','/applications/Shared'),('GET',p),('DELETE','/applications/Shared')]:
            g.request(method,path,403,key='limited')
        listing=g.request('GET','/applications',200,key='limited')
        self.assertNotIn(b'Shared',listing.content)
        g.app('Allowed')
        p='/applications/Allowed/domain_participants'
        g.request('POST',p,201,f'<domain_participant xmlns="{D}" name="P" domain_id="{os.environ["C1_DOMAIN"]}"/>'.encode(),key='limited')
        g.request('GET',p+'/P',200,key='limited')
        g.request('DELETE','/applications/Allowed',204,key='limited')

    def test_nested_capacity_rejects_before_any_dds_creation(self):
        g=self.g
        participants=''.join(f'<domain_participant name="P{i}" domain_id="{os.environ["C1_DOMAIN"]}"/>' for i in range(9))
        publishers=''.join(f'<publisher name="Pub{i}"/>' for i in range(129))
        for children in [participants,f'<domain_participant name="P" domain_id="{os.environ["C1_DOMAIN"]}">{publishers}</domain_participant>']:
            g.request('POST','/applications',429,f'<application xmlns="{D}" name="Large">{children}</application>'.encode())
            g.request('GET','/applications/Large',404)
        g.app('Keep')
        g.request('PUT','/applications/Keep',429,f'<application xmlns="{D}" name="Keep">{participants}</application>'.encode())
        g.request('GET','/applications/Keep',200)

    def test_dds_delete_failure_retains_owner_until_retry(self):
        g=self.g;base,part,topic,writer,reader=g.graph()
        g.request('DELETE',part+'/topics/'+topic,500)
        time.sleep(.25)  # periodic expiry must not clear this failure latch
        g.request('GET','/applications',503)
        g.request('DELETE',writer,204);g.request('DELETE',reader,204)
        g.request('DELETE',part+'/topics/'+topic,204)
        g.request('GET','/applications',200)
        g.request('DELETE',base,204)

    def test_application_idle_expiry(self):
        g=self.g;g.app();time.sleep(6)
        response=g.request('GET','/applications')
        self.assertEqual(len(X.fromstring(response.content)),0)

    def test_schema_tamper_rejected_before_listen(self):
        g=self.g;dest=g.out/'tampered-schemas';shutil.copytree(os.environ['C1_SCHEMAS'],dest)
        with (dest/'record.xsd').open('a') as f:f.write('\n<!-- altered -->')
        command=g.command+['--schema-dir',str(dest)]
        p=subprocess.run(command,env=g.env,capture_output=True,text=True,timeout=15)
        self.assertNotEqual(p.returncode,0);self.assertNotIn('C1_READY',p.stdout)
        (g.out/'tamper-stdout.log').write_text(p.stdout);(g.out/'tamper-stderr.log').write_text(p.stderr)

    def test_http_parser_limits_and_duplicate_key(self):
        g=self.g
        def raw(extra,method='POST'):
            request=(f'{method} /dds/rest1/applications HTTP/1.1\r\nHost: 127.0.0.1:{g.port}\r\nOMG-DDS-API-Key: {g.keys["manager"]}\r\nAccept: {M}\r\nCache-Control: no-cache\r\n'+extra+'\r\n').encode()
            with socket.create_connection(('127.0.0.1',g.port),timeout=5) as s:
                s.sendall(request);return s.recv(8192).split(b'\r\n',1)[0]
        self.assertIn(b' 413 ',raw('Content-Length: 70000\r\n'))
        self.assertIn(b' 431 ',raw('X-Large: '+'a'*9000+'\r\n'))
        self.assertIn(b' 401 ',raw('Content-Length: 0\r\nOMG-DDS-API-Key: '+g.keys['reader']+'\r\n'))
        self.assertIn(b' 411 ',raw('',method='DELETE'))
        message=(f'GET /dds/rest1/applications HTTP/1.1\r\nOMG-DDS-API-Key: {g.keys["manager"]}\r\nAccept: {M}\r\nCache-Control: no-cache\r\n\r\n').encode()
        with socket.create_connection(('127.0.0.1',g.port),timeout=5) as s:
            s.sendall(message);self.assertIn(b' 400 ',s.recv(8192).split(b'\r\n',1)[0])


if __name__=='__main__': unittest.main()
