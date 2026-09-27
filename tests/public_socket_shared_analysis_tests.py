import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/public_socket'))
import analyze_shared as a

def fixture(case='SHARED_W1'):
    boards=[];run='0123456789abcdef'
    def guid(side,life,kind):return f'{side+1:02x}{life:02x}'+'0'*20+kind
    for side in range(2):
        b={'DDS_MAIN':[dict(run=run,side='AB'[side])],'SHARED_ID':[],'SHARED_MATCH':[],'SHARED_CONFIG':[],'SHARED_WRITE':[],'SHARED_TAKE':[],'DDS_FRAME':[],'SHARED_WAIT':[]}
        order=['P1','Q1','P2'] if case=='SHARED_W1' else ['Q1','P1','P2']
        for k in order:
            life={'P1':1,'Q1':2,'P2':3}[k]
            b['SHARED_ID'].append(dict(run=run,side=str(side),group=k[0],life=k[1],participant=guid(side,life,'000001c1'),writer=guid(side,life,'00000102'),reader=guid(side,life,'00000207'),app=str(life*3-2),receiver=str(life*3),token=str(life*3-1),name=k[0]+str(side)))
            b['SHARED_CONFIG'].append(dict(reliability='RELIABLE',durability='VOLATILE',history='16',samples='32',instances='1',per_instance='32',builtin='0',intraprocess='0',sharing='0',max_record='8192',local_records='32',detail='16384',payload='32',meta_user_same='1'))
        ns=dict(runtime='runtime'+str(side),provider='provider'+str(side),listener_entry='1',outgoing_entry='2',listener='10',outgoing='11',accepted='12',listener_record='21',accepted_record='21',listener_owner='31',outgoing_owner='32',accepted_owner='33',socket_calls='2',listen_calls='1',bind_calls='1',shutdown_calls='0',records='0')
        b['SHARED_NATIVE']=[dict(ns,stage=stage,views=str(n),active_apps=str(n)) for stage,n in [('INITIAL',2),('GAP',1),('REATTACHED',2),('FINAL',0)]]
        b['SHARED_RESULT']=[dict(success='1',factories='3',writes='15',takes='15',drained='1')];b['SHARED_SETTLE']=[dict(drained='1',fallback='0')]
        boards.append(b)
    ids=[{r['group']+r['life']:r for r in b['SHARED_ID']} for b in boards]
    for side,b in enumerate(boards):
        for k in ['P1','Q1','P2','Q1']:
            x=ids[side][k];y=ids[1-side][k];b['SHARED_MATCH'].append(dict(group=k[0],life=k[1],writer=x['writer'],reader=x['reader'],remote_writer=y['writer'],remote_reader=y['reader']))
        def frame(**fields):
            r=dict(event=str(len(b['DDS_FRAME'])+1),packet=str(len(b['DDS_FRAME'])+1),kind='DATA',hash='123',length='140',ingress='0',correlation='1',app='0',receiver='0',owner='0',admitted='0',entered='0',writer='',sn='0',socket='12');r.update(fields);b['DDS_FRAME'].append(r);return r
        for kind in ['SPDP','SEDP']:frame(stage='SDK_SEND',kind=kind);frame(stage='RX_RUNNING',kind=kind)
        old_boundary=0
        for group,life,phase,ns in [('P','1','INITIAL',range(1,4)),('Q','1','INITIAL',range(1,4)),('Q','1','GAP',range(4,7)),('P','2','REATTACH',range(1,4)),('Q','1','REATTACH',range(7,10))]:
            own=ids[side][group+life];peer=ids[1-side][group+life]
            for n in ns:
                common=dict(run=run,side=str(side),group=group,life=life,phase=phase,ordinal=str(n),sn=str(n))
                b['SHARED_WRITE'].append(dict(common,writer=own['writer'],rc='0',serialized='60'))
                b['SHARED_TAKE'].append(dict(common,reader=own['reader'],writer=peer['writer'],valid='1',app=own['app'],receiver=own['receiver']))
                frame(stage='SDK_SEND',writer=own['writer'],sn=str(n),socket='11',owner='32',admitted='1',entered='1')
                ingress=frame(stage='SDK_INGRESS',writer=peer['writer'],sn=str(n))
                frame(stage='RX_OWNED',writer=peer['writer'],sn=str(n),app=own['app'],receiver=own['receiver'],owner='33',ingress=ingress['packet'])
                frame(stage='RX_RUNNING',writer=peer['writer'],sn=str(n),app=own['app'],receiver=own['receiver'],owner='33')
            b['SHARED_WAIT'].append(dict(rc='0'))
            if phase=='INITIAL':old_boundary=len(b['DDS_FRAME'])
        b['SHARED_DELETE']=[dict(group=k[0],life=k[1],participant=ids[side][k]['participant'],entities_rc='0',participant_rc='0',input_open='0',frames=str(old_boundary if k=='P1' else len(b['DDS_FRAME']))) for k in ['P1','P2','Q1']]
        b['DDS_FRAME_SUMMARY']=[dict(count=str(len(b['DDS_FRAME'])),limit='16384',overflow='0')]
    return boards

class Tests(unittest.TestCase):
    def reject(self,change):
        b=fixture();change(b)
        with self.assertRaises((ValueError,KeyError)):a.analyze_documents('SHARED_W1',b)
    def test_good(self):
        # The reported sample count is what the analyzer derived from the parsed Reader
        # ledgers; the test counts the same thing independently from its own document.
        b=fixture();r=a.analyze_documents('SHARED_W1',b)
        observed=sum(len(side['SHARED_TAKE']) for side in b)
        self.assertEqual(observed,30)
        self.assertEqual(r['reader_samples'],observed)
        self.assertEqual(r['expected_reader_samples'],30)
        self.assertEqual(r['writes'],observed)
        self.assertEqual(r['dds_wait_api_operations'],sum(len(side['SHARED_WAIT']) for side in b))
    def test_other_order(self):self.assertTrue(a.analyze_documents('SHARED_W2',fixture('SHARED_W2'))['pass_'])
    def test_missing_q_gap(self):self.reject(lambda b:b[0]['SHARED_TAKE'].pop(6))
    def test_wrong_writer(self):self.reject(lambda b:b[0]['SHARED_TAKE'][0].update(writer='f'*32))
    def test_wrong_receiver(self):self.reject(lambda b:b[0]['SHARED_TAKE'][0].update(reader='f'*32))
    def test_old_match(self):self.reject(lambda b:b[0]['SHARED_MATCH'][2].update(remote_writer=b[0]['SHARED_MATCH'][0]['remote_writer']))
    def test_old_p_identity(self):self.reject(lambda b:b[0]['SHARED_ID'][2].update(app=b[0]['SHARED_ID'][0]['app']))
    def test_old_p_token(self):self.reject(lambda b:b[0]['SHARED_ID'][2].update(token=b[0]['SHARED_ID'][0]['token']))
    def test_native_restart(self):self.reject(lambda b:b[0]['SHARED_NATIVE'][2].update(socket_calls='4'))
    def test_gap_shutdown(self):self.reject(lambda b:b[0]['SHARED_NATIVE'][1].update(shutdown_calls='1'))
    def test_extra_take(self):self.reject(lambda b:b[0]['SHARED_TAKE'].append(b[0]['SHARED_TAKE'][0].copy()))
    def test_wait_only(self):self.reject(lambda b:b[0].update(SHARED_TAKE=[]))
    def test_no_sdk_send(self):self.reject(lambda b:[r.update(stage='NOT_SENT') for r in b[0]['DDS_FRAME'] if r['stage']=='SDK_SEND' and r['kind']=='DATA'])
    def test_wrong_app_lease(self):self.reject(lambda b:[r.update(app='999') for r in b[0]['DDS_FRAME'] if r['stage']=='RX_OWNED'])
    def test_diagnostic_overflow(self):self.reject(lambda b:b[0]['DDS_FRAME_SUMMARY'][0].update(overflow='1'))
    def test_changed_capacity(self):self.reject(lambda b:b[0]['SHARED_CONFIG'][0].update(local_records='64'))
    def test_mixed_run(self):self.reject(lambda b:b[1]['DDS_MAIN'][0].update(run='f'*16))
    def test_repeated_sn(self):
        def change(b):
            for side in b:
                for tag in ['SHARED_WRITE','SHARED_TAKE']:
                    side[tag][1]['sn']=side[tag][0]['sn']
        self.reject(change)
    def test_old_receiver_after_delete(self):
        def change(b):
            r=next(r.copy() for r in b[0]['DDS_FRAME'] if r['stage']=='RX_RUNNING' and r['app']=='1');r['event']=str(len(b[0]['DDS_FRAME'])+1);b[0]['DDS_FRAME'].append(r);b[0]['DDS_FRAME_SUMMARY'][0]['count']=str(len(b[0]['DDS_FRAME']))
        self.reject(change)

    def test_wrong_life(self):
        self.reject(lambda b:[r.update(life='2') for r in b[0]['SHARED_TAKE'] if r['group']=='P' and r['life']=='1'])
    def test_wrong_ordinal(self):
        self.reject(lambda b:b[0]['SHARED_TAKE'][0].update(ordinal='9'))
    def test_wrong_ordinal_write(self):
        self.reject(lambda b:b[0]['SHARED_WRITE'][2].update(ordinal='7'))
    def test_wrong_phase(self):
        self.reject(lambda b:[r.update(phase='GAP') for r in b[0]['SHARED_TAKE'] if r['phase']=='INITIAL' and r['group']=='P'])
    def test_missing_one_side(self):
        self.reject(lambda b:b[1].update(SHARED_TAKE=[]))
    def test_missing_all_gap(self):
        def change(b):b[0]['SHARED_TAKE']=[r for r in b[0]['SHARED_TAKE'] if r['phase']!='GAP']
        self.reject(change)
    def test_total_unchanged_wrong_allocation(self):
        # The total stays 30, but one row now claims a (group, life, phase, ordinal) that
        # the frozen expectation does not contain: a count-only check would pass here.
        self.reject(lambda b:b[0]['SHARED_TAKE'][0].update(group='P',life='2',phase='REATTACH',ordinal='4'))
    def test_total_unchanged_swapped_groups(self):
        def change(b):
            for r in b[0]['SHARED_TAKE']:
                if r['group']=='P':r.update(group='Q')
                elif r['group']=='Q':r.update(group='P')
        self.reject(change)


# --- T-B: the real text -> parse -> validate entry, on independent copies of the
# captured device logs. The logs are raw device evidence and stay outside the project
# source; when they are absent these cases SKIP with their recorded digest, so a missing
# log is never silently read as a pass. Edits here are made to the raw text, not to a
# parsed document, so the parse step itself is under test.
LOGS = Path(__file__).resolve().parents[1] / '.artifacts/public-socket-shared-reattach/run-20260927T113439Z/windows'
REAL = {
    'SHARED_W1': 'w1-r8',
    'SHARED_W2': 'w2-r8',
}
DIGEST = {
    ('w1-r8', 'A'): '1daef2a146964ddcf7cd79138e0b491f2ac5017a55c1b0f1eb8b564fd2840d80',
    ('w1-r8', 'B'): '5aef191143e38bc4657825d0ee5e3d1706b4407ef5306a9ddf091ee6cf4f39df',
    ('w2-r8', 'A'): 'd91c7c58c3bb820ad2d334489be3ed85879af1d0340021998567202db824f564',
    ('w2-r8', 'B'): '8344b4bcb4bc81631beff97998d15e28c2ee0596d5751f6f8e0793c4bff4d6d8',
}


def real_pair(window):
    texts = []
    for side in 'AB':
        path = LOGS / window / (side + '.raw.stdout.log')
        if not path.is_file():
            raise unittest.SkipTest('captured log not present: ' + str(path))
        import hashlib
        if hashlib.sha256(path.read_bytes()).hexdigest() != DIGEST[(window, side)]:
            raise unittest.SkipTest('captured log digest differs from the recorded one: ' + str(path))
        texts.append(path.read_text(encoding='utf-8', errors='replace'))
    return texts


class RealText(unittest.TestCase):
    """The entry that actually consumes device output, with edits made to the raw text."""

    def analyze(self, case, texts):
        return a.analyze(case, texts)

    def reject(self, window, edit):
        case = [k for k, v in REAL.items() if v == window][0]
        texts = real_pair(window)
        texts[0] = edit(texts[0])
        with self.assertRaises((ValueError, KeyError)):
            self.analyze(case, texts)

    def test_captured_pair_still_passes(self):
        for case, window in REAL.items():
            r = self.analyze(case, real_pair(window))
            self.assertTrue(r['pass_'], case)
            self.assertEqual(r['reader_samples'], r['expected_reader_samples'], case)

    def test_deleted_reader_row(self):
        def edit(text):
            row = [l for l in text.splitlines() if l.startswith('SHARED_TAKE')][0]
            return text.replace(row + chr(10), '', 1)
        self.reject('w1-r8', edit)

    def test_duplicated_reader_row(self):
        def edit(t):
            row = [l for l in t.splitlines() if l.startswith('SHARED_TAKE')][0]
            return t.replace(row + chr(10), row + chr(10) + row + chr(10), 1)
        self.reject('w1-r8', edit)

    def test_wrong_run(self):
        def edit(t):
            row = [l for l in t.splitlines() if l.startswith('SHARED_TAKE')][0]
            import re
            return t.replace(row, re.sub(r'run=[0-9a-f]{16}', 'run=0000000000000000', row), 1)
        self.reject('w1-r8', edit)

    def test_wrong_writer(self):
        def edit(t):
            row = [l for l in t.splitlines() if l.startswith('SHARED_TAKE')][0]
            import re
            return t.replace(row, re.sub(r'writer=[0-9a-f]{32}', 'writer=' + 'f' * 32, row), 1)
        self.reject('w1-r8', edit)

    def test_wrong_life(self):
        def edit(t):
            row = [l for l in t.splitlines() if l.startswith('SHARED_ID') and 'group=P' in l and 'life=1' in l][0]
            return t.replace(row, row.replace('life=1', 'life=9'), 1)
        self.reject('w1-r8', edit)

    def test_wrong_phase(self):
        def edit(t):
            row = [l for l in t.splitlines() if l.startswith('SHARED_WRITE') and 'phase=INITIAL' in l][0]
            return t.replace(row, row.replace('phase=INITIAL', 'phase=REATTACH'), 1)
        self.reject('w1-r8', edit)

    def test_missing_required_field(self):
        def edit(t):
            row = [l for l in t.splitlines() if l.startswith('SHARED_TAKE')][0]
            import re
            return t.replace(row, re.sub(r' sn=[0-9]+', '', row), 1)
        self.reject('w1-r8', edit)

    def test_structurally_invalid_target_row(self):
        def edit(t):
            row = [l for l in t.splitlines() if l.startswith('SHARED_TAKE')][0]
            return t.replace(row, row.replace('group=P', 'group=', 1).replace('group=Q', 'group=', 1), 1)
        self.reject('w1-r8', edit)

if __name__=='__main__':unittest.main()
