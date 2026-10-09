"""Synthetic fragment metadata only; never device or SDK evidence."""
import unittest
from native_analyzer_tests import inputs, RUN
from analyze_ros2_native import correlate


def fragmented():
    texts = inputs()
    for side in 'AB':
        lines = []
        for line in texts[side].splitlines():
            if line.startswith('ROS_NATIVE_DATA '):
                base = line.replace('ROS_NATIVE_DATA ', 'ROS_NATIVE_FRAGMENT ')
                for first, size in ((1, 50000), (2, 20032)):
                    lines.append(base + f' first={first} count=1 fragment_size=50000 sample_size=70032 payload_bounds_known=1 key_flag=0 payload_bytes={size}')
            else:
                lines.append(line.replace('detailed_stored=20', 'detailed_stored=40'))
        texts[side] = '\n'.join(lines)
    return texts


class FragmentNative(unittest.TestCase):
    def test_complete_send_and_ingress_ranges(self):
        result = correlate(fragmented(), RUN)
        self.assertTrue(result['pass'], result['errors'])
        self.assertEqual(result['matched_fragment_takes'], {'A': 10, 'B': 10})

    def test_missing_range_and_wrong_sequence(self):
        for field in (' first=2 ', ' sequence_lo=1 '):
            texts = fragmented()
            lines = texts['A'].splitlines()
            index = next(i for i, line in enumerate(lines) if line.startswith('ROS_NATIVE_FRAGMENT ') and field in line)
            if field == ' first=2 ':
                lines[index] = lines[index].replace('first=2', 'first=1').replace('payload_bytes=20032', 'payload_bytes=50000')
            else:
                lines[index] = lines[index].replace('sequence_lo=1', 'sequence_lo=999')
            texts['A'] = '\n'.join(lines)
            self.assertFalse(correlate(texts, RUN)['pass'])

    def test_invalid_geometry_payload_and_summary(self):
        for old, new in (('fragment_size=50000', 'fragment_size=0'),
                         ('count=1', 'count=0'), ('first=2', 'first=3'),
                         ('sample_size=70032', 'sample_size=70031'),
                         ('payload_bounds_known=1', 'payload_bounds_known=0'),
                         ('key_flag=0', 'key_flag=1'),
                         ('payload_bytes=20032', 'payload_bytes=20031'),
                         ('detailed_stored=40', 'detailed_stored=20')):
            texts = fragmented()
            texts['A'] = texts['A'].replace(old, new)
            self.assertFalse(correlate(texts, RUN)['pass'], (old, new))


if __name__ == '__main__':
    unittest.main()
