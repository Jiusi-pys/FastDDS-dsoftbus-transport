import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from control_files import publish_control


class ControlPublication(unittest.TestCase):
    def test_reader_never_sees_open_writer(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / 'B-control.RUNTIME_CLOSE'
            original_open = Path.open

            def observe_open(path, *args, **kwargs):
                stream = original_open(path, *args, **kwargs)
                if 'w' in (args[0] if args else kwargs.get('mode', 'r')):
                    try:
                        self.assertFalse(destination.exists(), 'strict reader can see unfinished control')
                    except BaseException:
                        stream.close()
                        raise
                return stream

            with patch.object(Path, 'open', observe_open):
                publish_control(destination, 'complete validated control\n')
            self.assertEqual(destination.read_text(), 'complete validated control\n')
            self.assertEqual(list(Path(directory).iterdir()), [destination])


if __name__ == '__main__':
    unittest.main()
