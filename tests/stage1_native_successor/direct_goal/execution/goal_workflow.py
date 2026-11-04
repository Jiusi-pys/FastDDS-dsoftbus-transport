"""The fixed first-current-DDS procedure; no HDC/Native operation at import."""
from pathlib import Path
import os,re,time
from goal_contract import RUN_ROOT,OWN,need,sha
from goal_records import admission,pair_record,delivery_receipt,qualify_window_result
from goal_authorization import safe_input

def publish_local(path,data):
    need(not path.exists(),'LOCAL_RECORD_EXISTS');path.parent.mkdir(parents=True,exist_ok=True)
    temporary=path.with_name(path.name+'.incoming')
    with temporary.open('xb') as out:out.write(data);out.flush();os.fsync(out.fileno())
    os.replace(temporary,path)

def one_record(receipt,format):
    rows=receipt['evidence']['parsed']['records'];matches=[r for r in rows if r['format']==format]
    need(len(matches)==1,'EXACT_RECORD_REQUIRED');return matches[0]

class FixedGoalWorkflow:
    def __init__(self,driver,artifacts,manifest_sha):
        self.driver=driver;self.artifacts=artifacts;self.manifest_sha=manifest_sha;self.owns={};self.native_results={}
    def step(self,identifier):self.driver.start(identifier);return self.driver.wait(identifier)
    def publish(self,window,side,name,data):
        path=RUN_ROOT/'control'/window/name;publish_local(path,data)
        prefix=window+'.'+side.lower()+'.'+name.lower()
        self.step(prefix+'.send');verified=self.step(prefix+'.verify');self.step(prefix+'.publish');return verified
    def created(self,window,side):
        root=self.driver.live[window+'.'+side.lower()+'.launch']
        # BOOT fetch completion is not proof that the separate live stdout pipe
        # has been observed. Pump the same owned roots before reading BEGIN.
        self.driver.pump()
        text=bytes(root.output[0]).decode('utf-8','strict').replace('\r\n','\n')
        rows=re.findall(r'^DIRECT_SUP_BEGIN parent=(\d+) child=(\d+) birth=(\d+) scope=DIRECT_CHILD_ONLY descendants=UNKNOWN$',text,re.M)
        need(len(rows)==1,'LIVE_NATIVE_CREATION_PROOF');return dict(zip(['created_parent','created_child','birth'],map(int,rows[0])))
    def window_inputs(self,window,run,intent):
        boots={};created={}
        for side in ['A','B']:self.driver.start(window+'.'+side.lower()+'.launch')
        for side in ['A','B']:
            receipt=self.step(window+'.'+side.lower()+'.boot');boots[side]=one_record(receipt,'S1GOALBOOT1');created[side]=self.created(window,side)
            data=admission(boots[side],self.owns[side],side,created[side],self.artifacts,run,self.manifest_sha,intent)
            self.publish(window,side,'admission.'+side,data)
        return boots,created
    def run(self):
        self.driver.start('server');self.driver.wait_accepted('server');self.step('targets')
        for n in range(1,5):self.step('readonly.'+str(n))
        for side in ['A','B']:
            prefix='stage.'+side.lower();self.step(prefix+'.mkdir')
            for n in range(11):self.step(prefix+'.file'+str(n))
            self.step(prefix+'.verified');self.owns[side]=self.step('own.'+side.lower())['evidence']['parsed']['own']
        probe_boots,probe_created=self.window_inputs('probe','5c7a9e2130d0aa60','own')
        for side in ['A','B']:
            launched=self.driver.wait('probe.'+side.lower()+'.launch');result=self.step('probe.'+side.lower()+'.result')
            self.native_results['probe.'+side]=qualify_window_result('probe','5c7a9e2130d0aa60',side,probe_boots[side],probe_created[side],self.artifacts,launched,result)
        boots,created=self.window_inputs('data','5c7a9e2130d0aa61','traffic');nodes={}
        for side in ['A','B']:nodes[side]=one_record(self.step('data.'+side.lower()+'.node'),'S1GOALNODE1')
        data=pair_record(boots,nodes,self.owns,created,self.artifacts,'5c7a9e2130d0aa61',self.manifest_sha)
        # The Windows controller and both Native consumers use the same bytes.
        publish_local(RUN_ROOT/'control/data/pair',data)
        for side in ['A','B']:
            prefix='data.'+side.lower()+'.pair';self.step(prefix+'.send');self.step(prefix+'.verify');self.step(prefix+'.publish')
        self.driver.start('controller');next_release=1
        for side in ['A','B']:self.step('data.'+side.lower()+'.journal-ready')
        for poll in range(12):
            self.driver.pump()
            for side in ['A','B']:
                self.step('poll.'+str(poll)+'.'+side.lower())
                source=RUN_ROOT/'snapshots'/('events.'+side+'.'+str(poll))
                data=safe_input(source,65536)
                destination=RUN_ROOT/'control/data'/('events.'+side)
                temporary=destination.with_name(destination.name+'.incoming')
                with temporary.open('xb') as out:out.write(data);out.flush();os.fsync(out.fileno())
                os.replace(temporary,destination)
            paths=[RUN_ROOT/'control/data'/('release.'+side+'.'+str(next_release)) for side in ['A','B']]
            if next_release<=4 and all(path.is_file() for path in paths):
                frames=[safe_input(path,920) for path in paths];accepted={}
                for side,frame in zip(['A','B'],frames):
                    prefix='data.'+side.lower()+'.release.'+side.lower()+'.'+str(next_release)
                    self.step(prefix+'.send');proof=self.step(prefix+'.verify')['evidence']['parsed'];self.step(prefix+'.publish')
                    accepted[side]={'accepted':True,'side':side,'sequence':next_release,**proof}
                receipt=delivery_receipt('5c7a9e2130d0aa61',next_release,frames,accepted)
                publish_local(RUN_ROOT/'control/data'/('delivered.'+str(next_release)),receipt);next_release+=1
            if 'controller' in self.driver.terminal:break
            # Finite observation opportunities, not a claim that DDS must match
            # by an arbitrary poll count. Exhaustion is a retained failure.
            self.driver.backend.pause(1)
        need(next_release==5 and 'controller' in self.driver.terminal,'JOURNAL_POLL_BUDGET_EXHAUSTED')
        self.driver.wait('controller')
        for side in ['A','B']:
            launched=self.driver.wait('data.'+side.lower()+'.launch');result=self.step('data.'+side.lower()+'.result')
            self.native_results['data.'+side]=qualify_window_result('data','5c7a9e2130d0aa61',side,boots[side],created[side],self.artifacts,launched,result)
        return {'procedure':'FIRST_CURRENT_STOCK_DDS_AB','Native_results':self.native_results,'control_transport':'evidence/releases only; DDS payload not transported by this code','all_four_windows_and_controller_accepted':True,'evidence_kind':self.driver.backend.evidence_kind}
