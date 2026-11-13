"""Independent websocket-client/requests client; no gateway modules imported."""
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import ssl
import struct
import subprocess
import threading
import time
import unittest
import uuid
import xml.etree.ElementTree as X
import requests
import websocket
from lxml import etree
from standards_web_gateway_tests import GatewayFixture as RestFixture, D,S,M,record

ROOT=Path(__file__).resolve().parents[1]
FIXTURES=ROOT/'integrations/standards/web_gateway/tls_test_fixtures'

class Fixture(RestFixture):
    def __init__(self,case,certificate='server.pem',ttl=300):
        self.case=case;self.closed=False
        self.out=Path(os.environ['C2_EVIDENCE_DIR']).resolve()/(case+'-'+uuid.uuid4().hex[:8])
        if not self.out.is_relative_to((ROOT/'build').resolve()):raise RuntimeError('Evidence must be inside build/')
        self.out.mkdir(parents=True,exist_ok=False)
        for name,digest in json.loads((FIXTURES/'manifest.json').read_text(encoding='utf-8'))['hashes'].items():
            if hashlib.sha256((FIXTURES/name).read_bytes()).hexdigest()!=digest:raise RuntimeError('TLS fixture hash mismatch '+name)
        self.keys={n:uuid.uuid4().hex+uuid.uuid4().hex for n in ('manager','reader','other','limited')}
        self.env=os.environ.copy()
        for name,key in self.keys.items():self.env['C1_'+name.upper()+'_KEY']=key
        policy=self.out/'policy.xml';policy.write_text('<gateway>'
            '<principal name="Manager" owner="Team" key_env="C1_MANAGER_KEY" domains="219,223" permissions="read write manage"/>'
            '<principal name="Reader" owner="Team" key_env="C1_READER_KEY" domains="219,223" permissions="read"/>'
            '<principal name="Other" owner="Other" key_env="C1_OTHER_KEY" domains="219,223" permissions="read write manage"/>'
            f'<principal name="Limited" owner="Team" key_env="C1_LIMITED_KEY" domains="{os.environ["C1_DOMAIN"]}" permissions="read write manage"/>'
            '</gateway>',encoding='utf-8')
        self.stop=self.out/'stop';self.stdout=(self.out/'gateway.stdout.log').open('w',encoding='utf-8');self.stderr=(self.out/'gateway.stderr.log').open('w',encoding='utf-8')
        self.command=[os.environ['C2_GATEWAY'],'--policy',str(policy),'--port','0','--tls-port','0',
            '--tls-cert',str(FIXTURES/certificate),'--tls-key',str(FIXTURES/'server.key'),
            '--representation',os.environ['C1_REPRESENTATION'],'--ttl',str(ttl),'--stop-file',str(self.stop)]
        self.proc=subprocess.Popen(self.command,stdout=self.stdout,stderr=self.stderr,env=self.env,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        self.peers=[];self.sockets=[];self.receipts=[];self.ws_receipts=[];self.receipts_lock=threading.Lock()
        for _ in range(300):
            found=re.search(r'C2_READY http_port=(\d+) tls_port=(\d+)',(self.out/'gateway.stdout.log').read_text(encoding='utf-8'))
            if found:self.port,self.tls_port=map(int,found.groups());break
            if self.proc.poll() is not None:raise AssertionError('Startup failed: '+str(self.out))
            time.sleep(.05)
        else:self.proc.terminate();self.proc.wait(timeout=5);raise AssertionError('Startup timeout: '+str(self.out))
        self.url=f'http://127.0.0.1:{self.port}/dds/rest1'
        self.schema=etree.XMLSchema(etree.parse(str(Path(os.environ['C1_SCHEMAS'])/'rest-bindings.xsd'),etree.XMLParser(no_network=True)))
        self.ws_schema=etree.XMLSchema(etree.parse(str(ROOT/'integrations/standards/web_gateway/schemas/original/webdds_websockets1.xsd'),etree.XMLParser(no_network=True)))
    def hello(self,key='manager'):
        return f'Accept: {M}\r\nContent-Type: {M}\r\nOMG-DDS-API-Key: {self.keys.get(key,key)}\r\nVersion: 1\r\n'
    def connect(self,secure=None,name=None,key='manager',hello=True,sslopt=None,protocols=None):
        secure=os.environ['C2_TRANSPORT']=='wss' if secure is None else secure
        url=f'{"wss" if secure else "ws"}://127.0.0.1:{self.tls_port if secure else self.port}/dds/v1/{name or uuid.uuid4().hex}'
        options={'ca_certs':str(FIXTURES/'ca.pem'),'cert_reqs':ssl.CERT_REQUIRED,'check_hostname':True}
        options.update(sslopt or {})
        s=websocket.WebSocket(enable_multithread=True,sslopt=options)
        s.settimeout(6)
        try:s.connect(url,subprotocols=protocols or ['dds-web'],sslopt=options,timeout=6,http_no_proxy=['127.0.0.1','localhost'],suppress_origin=True)
        except BaseException:s.shutdown();raise
        self.sockets.append(s)
        if s.getsubprotocol()!='dds-web':raise AssertionError('Subprotocol not selected')
        entry={'url':url,'secure':secure,'subprotocol':s.getsubprotocol()}
        if secure:entry.update(tls_version=s.sock.version(),cipher=s.sock.cipher()[0],verified=True)
        self.ws_receipts.append(entry)
        if hello:
            s.send(self.hello(key));message=s.recv()
            if not isinstance(message,str) or not message.startswith('HELLO_OK:'):raise AssertionError('HELLO failed: '+str(message))
        return s
    def read(self,s):
        text=s.recv()
        if not isinstance(text,str) or not text:raise AssertionError('WebSocket closed unexpectedly')
        doc=etree.fromstring(text.encode(),etree.XMLParser(no_network=True,resolve_entities=False))
        if not self.ws_schema.validate(doc):raise AssertionError(str(self.ws_schema.error_log))
        return X.fromstring(text)
    def request_ws(self,s,method,path,body=None,expected='OK'):
        rid='R'+uuid.uuid4().hex
        s.send(f'<request><id>{rid}</id><uri>{path}</uri><method>{method}</method>'+('' if body is None else '<body>'+body.decode()+'</body>')+'</request>')
        pushes=[]
        for _ in range(100):
            doc=self.read(s)
            if doc.tag=='b_push':pushes.append(doc);continue
            if doc.tag!='response' or doc.findtext('id')!=rid:raise AssertionError('Uncorrelated response')
            if doc.findtext('return_code')!=expected:raise AssertionError('Wrong return code '+str(doc.findtext('return_code')))
            return doc,pushes
        raise AssertionError('Response starved')
    def bind(self,s,kind,id,uri):
        s.send(f'<bind><bind_data{kind}><bind_id>{id}</bind_id><uri>{uri}</uri></bind_data{kind}></bind>')
    def records(self,doc):
        result={}
        for data in doc.iter('{'+S+'}BoundedRecord'):
            n=int(data.findtext('{'+S+'}id'))-17
            if X.tostring(data)!=X.tostring(X.fromstring(record(n))):raise AssertionError('DDS values differ')
            result[n]=data
        return result
    def collect(self,s,id,count=10):
        result={}
        for _ in range(50):
            doc=self.read(s)
            if doc.tag!='b_push' or doc.findtext('bind_id')!=id:raise AssertionError('Wrong notification endpoint')
            result.update(self.records(doc))
            if len(result)>=count:return result
        raise AssertionError('No complete notification batch')
    def expect_close(self,s,code=1008):
        frame=s.recv_frame()
        if frame.opcode!=websocket.ABNF.OPCODE_CLOSE or struct.unpack('!H',frame.data[:2])[0]!=code:raise AssertionError('Wrong close frame '+str((frame.opcode,frame.data)))
        self.ws_receipts.append({'received_close_code':code})
        try:s.send_close(status=code)
        except (OSError,websocket.WebSocketException):pass  # the peer may already abort after a protocol error
        finally:s.shutdown()
    def shutdown_live(self):
        self.stop.touch();code=self.proc.wait(timeout=15)
        if code!=0:raise AssertionError('Shutdown failed '+str(self.out))
    def close(self):
        if self.closed:return
        self.closed=True
        for s in self.sockets:
            try:s.close(timeout=1)
            except (OSError,websocket.WebSocketException):pass
            finally:s.shutdown()
        for p,a,b,log,cmd in self.peers:
            if p.poll() is None:p.terminate();p.wait(timeout=5)
            a.close();b.close()
        self.stop.touch();forced=False
        try:code=self.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:self.proc.terminate();code=self.proc.wait(timeout=5);forced=True
        self.stdout.close();self.stderr.close()
        (self.out/'process-receipts.json').write_text(json.dumps({'gateway_command':self.command,'gateway_exit':code,'forced':forced,'peers':[{'command':x[4],'exit':x[0].returncode} for x in self.peers]},indent=2),encoding='utf-8')
        (self.out/'websocket-receipts.json').write_text(json.dumps(self.ws_receipts,indent=2),encoding='utf-8')
        if code!=0 or forced or 'C2_SHUTDOWN PASS connections=0 endpoints=0' not in (self.out/'gateway.stdout.log').read_text(encoding='utf-8'):
            raise AssertionError('Gateway did not drain: '+str(self.out))

class WebSocketTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for n in ('C2_GATEWAY','C2_EVIDENCE_DIR','C2_TRANSPORT','C1_PEER','C1_DOMAIN','C1_REPRESENTATION','C1_SCHEMAS'):
            if not os.environ.get(n):raise RuntimeError(n+' required; native acceptance cannot be skipped')
        if os.environ['C2_TRANSPORT'] not in ('ws','wss'):raise RuntimeError('Select actual WS or WSS lane')
    def setUp(self):self.g=Fixture(self._testMethodName)
    def tearDown(self):self.g.close()
    def test_handshake_request_and_authorization(self):
        g=self.g;g.app();s=g.connect();reply,_=g.request_ws(s,'GET','/applications');self.assertIn(b'App',X.tostring(reply))
        g.request_ws(s,'GET','/applications/Absent',expected='INVALID_OBJECT')
        reader=g.connect(key='reader');g.request_ws(reader,'DELETE','/applications/App',expected='PERMISSIONS_ERROR')
        other=g.connect(key='other');g.request_ws(other,'GET','/applications/App',expected='PERMISSIONS_ERROR')
    def test_ws_request_to_native_dds_reader(self):
        g=self.g;_,_,topic,writer,_=g.graph();peer=g.ready_reader(topic,writer);s=g.connect()
        for n in range(10):g.request_ws(s,'POST',writer,record(n,n%2==0))
        g.finish_peer(peer)
    def test_bound_writer_to_native_dds_reader(self):
        g=self.g;_,_,topic,writer,_=g.graph();peer=g.ready_reader(topic,writer);s=g.connect();g.bind(s,'writer','W',writer);g.request_ws(s,'GET','/types')
        for n in range(10):s.send('<b_req><bind_id>W</bind_id><body>'+record(n,n%2==0).decode()+'</body></b_req>')
        g.request_ws(s,'GET','/types');g.finish_peer(peer)
    def test_native_dds_unsolicited_push_and_cancellation(self):
        g=self.g;_,_,topic,_,reader=g.graph();s=g.connect();g.bind(s,'reader','Notify',reader);g.request_ws(s,'GET','/types')
        peer=g.peer('writer',topic);values=g.collect(s,'Notify');g.finish_peer(peer);self.assertEqual(set(values),set(range(10)))
        g.bind(s,'reader','Notify','');g.request_ws(s,'GET','/types')
        peer=g.peer('writer',topic);g.finish_peer(peer)
        s.settimeout(.3)
        with self.assertRaises(websocket.WebSocketTimeoutException):s.recv()
        remaining=g.request('GET',reader,params={'minSamples':'10','maxWait':'2'});self.assertEqual(set(g.records(X.fromstring(remaining.content))),set(range(10)))
    def test_stale_endpoint_is_not_rebound_to_recreated_reader(self):
        g=self.g;_,part,topic,_,reader=g.graph();s=g.connect();g.bind(s,'reader','Old',reader);g.request_ws(s,'GET','/types')
        g.request('DELETE',reader,204);g.request('POST',part+'/subscribers/Sub/data_readers',201,f'<data_reader xmlns="{D}" name="R" topic_ref="{topic}"/>'.encode())
        g.expect_close(s);peer=g.peer('writer',topic);g.finish_peer(peer)
        result=g.request('GET',reader,params={'minSamples':'10','maxWait':'2'});self.assertEqual(set(g.records(X.fromstring(result.content))),set(range(10)))
    def test_hello_rejections_and_duplicate_connection_name(self):
        g=self.g
        for text in [g.hello('x'*64),g.hello().replace('Version: 1','Version: 2'),g.hello().replace('Content-Type: '+M,'Content-Type: text/plain'),g.hello()+'Version: 1\r\n']:
            s=g.connect(hello=False);s.send(text);self.assertTrue(s.recv().startswith('HELLO_FAIL:'));g.expect_close(s)
        first=g.connect(name='Same');second=g.connect(name='Same',hello=False);second.send(g.hello());self.assertTrue(second.recv().startswith('HELLO_FAIL:'));g.expect_close(second)
        first.close();time.sleep(.1);g.connect(name='Same')

    def test_opaque_ids_and_xsd_whitespace_collapse(self):
        g=self.g;s=g.connect()
        for value,expected in [('Request:1.0','Request:1.0'),('查询一','查询一'),('x'*65,'x'*65),('A&B<client>','A&B<client>'),('',''),(' A\t B\nC ','A B C'),('A\u00a0B','A\u00a0B')]:
            root=X.Element('request');X.SubElement(root,'id').text=value;X.SubElement(root,'uri').text=' \n/types\t ';X.SubElement(root,'method').text=' \t GET\r\n '
            wire=X.tostring(root,encoding='utf-8');self.assertTrue(g.ws_schema.validate(etree.fromstring(wire)))
            s.send(wire.decode());reply=g.read(s);self.assertEqual(reply.findtext('return_code'),'OK');self.assertIsNotNone(reply.find('id'));self.assertEqual(reply.findtext('id'),expected)
        s.send('<request><uri>/types</uri><method>GET</method></request>');reply=g.read(s);self.assertIsNone(reply.find('id'))

    def test_general_binding_ids_and_whitespace_cancellation(self):
        g=self.g;_,_,_,writer,reader=g.graph();s=g.connect()
        logical='绑定:一.0 & channel'
        root=X.Element('bind');item=X.SubElement(root,'bind_datareader');X.SubElement(item,'bind_id').text=' \t绑定:一.0 &\nchannel ';X.SubElement(item,'uri').text=' '+reader+' '
        s.send(X.tostring(root,encoding='unicode'));g.request_ws(s,'GET','/types');g.request('POST',writer,200,record(0))
        notification=g.read(s);self.assertEqual(notification.tag,'b_push');self.assertEqual(notification.findtext('bind_id'),logical);self.assertEqual(set(g.records(notification)),{0})
        root=X.Element('bind');item=X.SubElement(root,'bind_datareader');X.SubElement(item,'bind_id').text=logical;X.SubElement(item,'uri').text=' \t\n '
        s.send(X.tostring(root,encoding='unicode'));g.request_ws(s,'GET','/types');g.request('POST',writer,200,record(1));s.settimeout(.3)
        with self.assertRaises(websocket.WebSocketTimeoutException):s.recv()
        s.settimeout(6)
        # An explicit empty binding id is valid; an absent B_REQUEST id is not.
        s.send(f'<bind><bind_datawriter><bind_id/><uri>{writer}</uri></bind_datawriter></bind>');g.request_ws(s,'GET','/types')
        s.send('<b_req><bind_id/><body>'+record(2).decode()+'</body></b_req>');g.request_ws(s,'GET','/types')
        remaining=g.request('GET',reader,params={'minSamples':'2','maxWait':'2'});self.assertEqual(set(g.records(X.fromstring(remaining.content))),{1,2})
        s.send('<b_req><body>'+record(3).decode()+'</body></b_req>');g.expect_close(s)

    def test_hello_vendor_header_tokens(self):
        g=self.g;s=g.connect(hello=False);s.send(g.hello()+"X.Vendor!Trace: enabled\r\n");self.assertTrue(s.recv().startswith('HELLO_OK:'));g.request_ws(s,'GET','/types')

    def test_body_data_whitespace_is_preserved(self):
        g=self.g;_,_,_,writer,reader=g.graph();s=g.connect()
        body=record(0).replace(b'C1&amp;&lt;0','测&#9;试&#10;&#13;X&lt;&amp;'.encode())
        g.request_ws(s,'POST',writer,body);reply,_=g.request_ws(s,'GET',reader+'?maxWait=2')
        self.assertEqual(next(reply.iter('{'+S+'}label')).text,'测\t试\n\rX<&')
    def test_subprotocol_uri_and_hello_fragment_rules(self):
        g=self.g
        with self.assertRaises(websocket.WebSocketBadStatusException):g.connect(protocols=['other'])
        s=g.connect(hello=False);s.send_frame(websocket.ABNF.create_frame(g.hello()[:30],websocket.ABNF.OPCODE_TEXT,fin=0));s.send_frame(websocket.ABNF.create_frame(g.hello()[30:],websocket.ABNF.OPCODE_CONT,fin=1))
        self.assertTrue(s.recv().startswith('HELLO_FAIL:'));g.expect_close(s)
        s=g.connect(hello=False);wire=websocket.ABNF.create_frame(g.hello(),websocket.ABNF.OPCODE_TEXT).format()
        for at in range(0,len(wire),7):s.sock.sendall(wire[at:at+7]);time.sleep(.001)
        self.assertTrue(s.recv().startswith('HELLO_OK:'))
        text='<request><id>F</id><uri>/types</uri><method>GET</method></request>'
        s.send_frame(websocket.ABNF.create_frame(text[:15],websocket.ABNF.OPCODE_TEXT,fin=0));s.send_frame(websocket.ABNF.create_frame(text[15:],websocket.ABNF.OPCODE_CONT,fin=1))
        self.assertEqual(g.read(s).findtext('id'),'F')
    def test_invalid_xml_binary_and_binding_authorization(self):
        g=self.g;_,_,_,writer,reader=g.graph()
        s=g.connect();s.send('<!DOCTYPE request><request/>');g.expect_close(s)
        s=g.connect();s.send_binary(b'bad');g.expect_close(s,1003)
        s=g.connect(key='reader');g.bind(s,'writer','W',writer);g.expect_close(s)
        s=g.connect(key='other');g.bind(s,'reader','R',reader);g.expect_close(s)
        s=g.connect();s.send('<b_req><bind_id>Missing</bind_id></b_req>');g.expect_close(s)
    def test_binding_quota_and_message_size_limit(self):
        g=self.g;_,_,_,writer,_=g.graph();s=g.connect()
        s.send('<bind>'+''.join(f'<bind_datawriter><bind_id>W{n}</bind_id><uri>{writer}</uri></bind_datawriter>' for n in range(16))+'</bind>')
        g.request_ws(s,'GET','/types');g.bind(s,'writer','Overflow',writer);g.expect_close(s)
        s=g.connect();s.send('x'*65537);g.expect_close(s,1009)
        g.request('GET','/types')
    def test_tls_verification_and_https(self):
        g=self.g;s=g.connect(secure=True);self.assertIn(s.sock.version(),('TLSv1.2','TLSv1.3'))
        with self.assertRaises(ssl.SSLCertVerificationError):g.connect(secure=True,sslopt={'ca_certs':str(FIXTURES/'untrusted-ca.pem')})
        for certificate in ('wrong-host.pem','expired.pem'):
            extra=Fixture('negative-'+certificate,certificate)
            try:
                with self.assertRaises(ssl.SSLCertVerificationError):extra.connect(secure=True)
            finally:extra.close()
        with requests.Session() as client:
            client.trust_env=False
            result=client.get(f'https://127.0.0.1:{g.tls_port}/dds/rest1/types',headers={'OMG-DDS-API-Key':g.keys['manager'],'Accept':M,'Cache-Control':'no-cache'},verify=str(FIXTURES/'ca.pem'),timeout=6)
            self.assertEqual(result.status_code,200);self.assertIn('Last-Modified',result.headers)
        with self.assertRaises((ssl.SSLError,websocket.WebSocketException,OSError)):
            websocket.create_connection(f'wss://127.0.0.1:{g.port}/dds/v1/WrongTransport',subprotocols=['dds-web'],sslopt={'ca_certs':str(FIXTURES/'ca.pem')},timeout=2,http_no_proxy=['127.0.0.1'])
    def test_hello_deadline_and_live_shutdown(self):
        g=self.g;s=g.connect(hello=False);start=time.monotonic();g.expect_close(s);self.assertGreaterEqual(time.monotonic()-start,4.5)
        _,_,_,_,reader=g.graph();s=g.connect();g.bind(s,'reader','R',reader);g.request_ws(s,'GET','/types');g.shutdown_live()

    def test_connection_quota_releases_closed_connections(self):
        g=self.g
        sockets=[g.connect() for _ in range(32)]
        with self.assertRaises((OSError,websocket.WebSocketException)):g.connect()
        sockets[0].close();sockets[0].shutdown();time.sleep(.1)
        g.connect()

    def test_pending_request_flood_is_bounded_and_service_recovers(self):
        g=self.g;_,_,_,_,reader=g.graph();s=g.connect()
        text=f'<request><id>Wait</id><uri>{reader}?minSamples=64&amp;maxWait=2</uri><method>GET</method></request>'
        s.send(text)
        frames=b''.join(websocket.ABNF.create_frame(f'<request><id>Q{n}</id><uri>/types</uri><method>GET</method></request>',websocket.ABNF.OPCODE_TEXT).format() for n in range(20))
        s.sock.sendall(frames);g.expect_close(s);g.request('GET','/types')

    def test_tls_configuration_and_schema_tamper_fail_closed(self):
        g=self.g
        for name,extra in [('mismatched-key',['--tls-key',str(FIXTURES/'mismatched.key')]),('schema-tamper',[])]:
            command=g.command.copy();command[command.index('--stop-file')+1]=str(g.out/(name+'-stop'))
            if extra:command[command.index('--tls-key')+1]=extra[1]
            else:
                altered=g.out/'changed-websocket.xsd';altered.write_bytes((ROOT/'integrations/standards/web_gateway/schemas/original/webdds_websockets1.xsd').read_bytes()+b' ')
                command+=['--ws-schema',str(altered)]
            process=subprocess.run(command,env=g.env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=15)
            (g.out/(name+'.log')).write_text(process.stdout,encoding='utf-8')
            (g.out/(name+'-receipt.json')).write_text(json.dumps({'command':command,'exit_code':process.returncode}),encoding='utf-8')
            self.assertNotEqual(process.returncode,0);self.assertNotIn('C2_READY',process.stdout)

    def test_tls_protocol_floor_and_plaintext_rejection(self):
        g=self.g
        context=ssl.create_default_context(cafile=str(FIXTURES/'ca.pem'))
        context.minimum_version=ssl.TLSVersion.TLSv1_2;context.maximum_version=ssl.TLSVersion.TLSv1_2
        with socket.create_connection(('127.0.0.1',g.tls_port),timeout=4) as raw:
            with context.wrap_socket(raw,server_hostname='localhost') as tls:self.assertEqual(tls.version(),'TLSv1.2')
        command=['C:/pixi_ws/.pixi/envs/default/Library/bin/openssl.exe','s_client','-connect',f'127.0.0.1:{g.tls_port}','-tls1_1','-cipher','DEFAULT:@SECLEVEL=0','-CAfile',str(FIXTURES/'ca.pem'),'-verify_return_error','-brief']
        process=subprocess.run(command,input='',stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=8)
        (g.out/'tls11.log').write_text(process.stdout,encoding='utf-8')
        (g.out/'tls11-receipt.json').write_text(json.dumps({'command':command,'exit_code':process.returncode}),encoding='utf-8')
        self.assertNotEqual(process.returncode,0);self.assertIn('alert protocol version',process.stdout.lower())
        with requests.Session() as client:
            client.trust_env=False
            with self.assertRaises(requests.ConnectionError):client.get(f'http://127.0.0.1:{g.tls_port}/dds/rest1/types',timeout=3)

if __name__=='__main__':unittest.main()
