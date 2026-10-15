import unittest
from host_io_capture import validate_capture

RUN = '0123456789abcdef'
EVENT = f'HOST_IO_EVENT schema=1 run={RUN} side=A pid=42 seq=3 begin=0 last_call_begin=0 thread=4 kind=LINK_JOIN_RETURN phase=NONE termination=NONE send=0 socket=-1 epoch=0 handle=0 requested=0 result_known=0 result=0 wsa_known=0 wsa=0 calls=0 partial=0\n'
CAPTURE = f'HOST_IO_CAPTURE schema=1 run={RUN} side=A capacity=16384 reserved=1 published=1 overflow=0 unpublished=0 complete=1 ordering=PROCESS_OBSERVATION_ONLY runtime_owner=NOT_OBSERVED successful_syscalls=AGGREGATED sdk_epoch=NOT_PROVEN\n'


class CaptureTests(unittest.TestCase):
    def valid(self, text):
        return validate_capture(text, RUN, 'A', 42)['complete']

    def test_complete(self):
        self.assertTrue(self.valid(EVENT + CAPTURE))

    def test_missing(self):
        for text in ('', EVENT, CAPTURE):
            with self.subTest(text=text): self.assertFalse(self.valid(text))

    def test_negative_capture(self):
        for before, after in [('overflow=0','overflow=1'), ('unpublished=0','unpublished=1'), ('complete=1','complete=0'), ('published=1','published=2'), ('reserved=1','reserved=0'), ('capacity=16384','capacity=2'), ('reserved=1','reserved=-1')]:
            with self.subTest(after=after): self.assertFalse(self.valid(EVENT + CAPTURE.replace(before,after)))

    def test_identity_and_syntax(self):
        for text in ((EVENT+CAPTURE).replace(RUN,'wrong'), EVENT.replace('pid=42','pid=43')+CAPTURE, EVENT+CAPTURE.replace('side=A','side=B'), EVENT+'HOST_IO_EVENT nonsense\n'+CAPTURE, EVENT.replace(' seq=3',' seq=3 seq=4')+CAPTURE):
            with self.subTest(text=text): self.assertFalse(self.valid(text))

    def test_duplicate_capture(self):
        self.assertFalse(self.valid(EVENT+CAPTURE+CAPTURE))

    def test_sequence_and_join(self):
        self.assertFalse(self.valid(EVENT.replace('seq=3','seq=0')+CAPTURE))
        self.assertFalse(self.valid(EVENT.replace('LINK_JOIN_RETURN','LINK_JOIN_ENTER')+CAPTURE))
        twice=CAPTURE.replace('reserved=1','reserved=2').replace('published=1','published=2')
        self.assertFalse(self.valid(EVENT+EVENT+twice))

    def test_unordered_slots_allowed(self):
        earlier=EVENT.replace('seq=3','seq=1').replace('LINK_JOIN_RETURN','LINK_STOP_STORE_ENTER')
        twice=CAPTURE.replace('reserved=1','reserved=2').replace('published=1','published=2')
        self.assertTrue(self.valid(EVENT+earlier+twice))


if __name__ == '__main__': unittest.main()
