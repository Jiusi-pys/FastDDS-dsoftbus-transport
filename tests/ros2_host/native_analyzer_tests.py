"""Synthetic SDK metadata added to real Host text for parser tests only.

These tests are not target or SDK evidence. Saved Host originals are never edited.
"""
import importlib.util
from pathlib import Path
import sys
import unittest

REPO=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(REPO/'scripts/public_socket'))
from analyze_ros2 import rows
from analyze_ros2_native import correlate

RUN='42ba5e959d97d571'
def inputs():
    texts={s:(Path(__file__).parent/'fixtures'/f'{s}.stdout').read_text() for s in 'AB'}
    native={s:[] for s in 'AB'}
    for side,peer in [('A','B'),('B','A')]:
        for n,take in enumerate(rows(texts[side],'ROS_TAKE'),1):
            for board,direction in [(side,'SDK_INGRESS'),(peer,'SDK_SEND')]:
                native[board].append(f'ROS_NATIVE_DATA run={RUN} side={board} observation={n} direction={direction} socket=1 writer={take["gid"]} sequence_hi=0 sequence_lo={take["publication_sequence"]} scope=STRUCTURAL_APP_OBSERVER')
    for side in 'AB':
        texts[side]+='\n'+'\n'.join(native[side])+f'\nROS_NATIVE_SUMMARY run={RUN} side={side} send_enter=20 send_return=20 send_error=0 ingress=20 detailed_stored={len(native[side])} metadata_limit=8192 overflow=0 malformed=0 sdk_epoch=NOT_PROVEN\n'
        texts[side]+=f'FDDST_REAL_SOCKET run={RUN} side={0 if side=="A" else 1} fake_sdk=0 rejected_callbacks=0\nROS_IDENTITY_FINAL restored=1 business_rc=0\n'
    return texts

class Native(unittest.TestCase):
    def test_synthetic_complete_metadata(self):
        result=correlate(inputs(),RUN);self.assertTrue(result['pass'],result['errors']);self.assertEqual(result['matched_topic_takes'],{'A':10,'B':10})
    def test_missing_send_or_ingress(self):
        for direction in ('SDK_SEND','SDK_INGRESS'):
            texts=inputs();lines=texts['A'].splitlines();index=next(i for i,x in enumerate(lines) if 'direction='+direction in x);lines[index]=lines[index].replace('sequence_lo=1','sequence_lo=999');texts['A']='\n'.join(lines);self.assertFalse(correlate(texts,RUN)['pass'])
    def test_bad_identity_or_summary(self):
        for old,new in [('writer=','writer=bad'),('send_return=20','send_return=19'),('overflow=0','overflow=1'),('metadata_limit=8192','metadata_limit=99999'),('restored=1','restored=0'),('fake_sdk=0','fake_sdk=1')]:
            with self.subTest(old=old):
                texts=inputs();texts['A']=texts['A'].replace(old,new);self.assertFalse(correlate(texts,RUN)['pass'])
    def test_host_text_without_sdk_evidence(self):
        texts={s:(Path(__file__).parent/'fixtures'/f'{s}.stdout').read_text() for s in 'AB'};self.assertFalse(correlate(texts,RUN)['pass'])
    def test_local_producer_explicitly_rejected(self):
        texts=inputs();texts['A']+='ROS_LOCAL_MODEL scope=LOCAL_FAKE_CONSUMER_NOT_ROS_OR_DEVICE\n';self.assertFalse(correlate(texts,RUN)['pass'])
    def test_distinct_topic_takes_cannot_reuse_one_sdk_sequence(self):
        import re
        texts=inputs()
        for side in 'AB':texts[side]=re.sub(r'publication_sequence=[0-9]+','publication_sequence=1',texts[side])
        self.assertFalse(correlate(texts,RUN)['pass'])

if __name__=='__main__':unittest.main()
