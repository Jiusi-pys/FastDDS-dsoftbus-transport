"""A previous ROS or 3.6 result cannot substitute for the direct 2.14 layer."""
import importlib.util
from pathlib import Path
import re
import unittest
spec=importlib.util.spec_from_file_location('direct214',Path(__file__).with_name('run_pair.py'))
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class Scope(unittest.TestCase):
    def test_empty_or_ros_records_are_not_direct_dds(self):
        fixtures=Path(__file__).resolve().parents[1]/'ros2_host/fixtures'
        for texts in ({s:'' for s in 'AB'},{s:(fixtures/(s+'.stdout')).read_text() for s in 'AB'}):
            result=module.analyze(texts,'1234567890abcdef','normal')
            self.assertFalse(result['pass']);self.assertEqual(result['observed_writes'],0);self.assertEqual(result['observed_takes'],0)
    def test_duplicate_field_is_rejected(self):
        with self.assertRaises(ValueError):module.records('DDS214_WRITE run=a run=b','DDS214_WRITE')
    def test_actual_host_excerpt_passes(self):
        self.assertTrue(module.analyze(self.actual_texts(),'71997f4878cde186','normal')['pass'])
    def actual_texts(self):
        return {s:(Path(__file__).with_name('fixtures')/(s+'.stdout')).read_text() for s in 'AB'}
    def test_reused_sample_sequence_cannot_fill_five_ordinals(self):
        texts={s:re.sub(r'\bsn=\d+\b','sn=1',text) for s,text in self.actual_texts().items()}
        self.assertFalse(module.analyze(texts,'71997f4878cde186','normal')['pass'])
    def test_writer_sequence_must_increase_in_write_order(self):
        # Change the matching WRITE/TAKE SN consistently, preserving uniqueness.
        texts={s:re.sub(r'\bsn=(\d+)\b',lambda m:'sn='+str(6-int(m[1])),text) for s,text in self.actual_texts().items()}
        self.assertFalse(module.analyze(texts,'71997f4878cde186','normal')['pass'])
if __name__=='__main__':unittest.main()
