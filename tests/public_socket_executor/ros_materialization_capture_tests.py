"""The reviewed executable closure must include the exact collector shell bytes."""
import unittest

import flow_tests as flow


class CaptureMaterialization(unittest.TestCase):
    setUp = flow.Flow.setUp
    tearDown = flow.Flow.tearDown

    def test_old_collector_cannot_replace_new_bound_copy(self):
        path = self.task / 'executor/capture-load.sh'
        current = path.read_bytes()
        stale = current.replace(b'|ordinary_ros2_transport_consumer', b'')
        self.assertNotEqual(current, stale)
        self.c.verify_materialized_execution(self.f)
        path.write_bytes(stale)
        self.assertRaises(ValueError, self.c.verify_materialized_execution, self.f)
        path.write_bytes(current)
        self.c.verify_materialized_execution(self.f)

    def test_truncated_collector_is_not_admitted(self):
        path = self.task / 'executor/capture-load.sh'
        path.write_bytes(path.read_bytes()[:40])
        self.assertRaises(ValueError, self.c.verify_materialized_execution, self.f)


if __name__ == '__main__':
    unittest.main()
