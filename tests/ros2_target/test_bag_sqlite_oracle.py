"""Host-only SQLite fixtures; never substitute these for native recording."""
import base64
import copy
from contextlib import closing
import hashlib
from pathlib import Path
import sqlite3
import struct
import tempfile
import unittest

import yaml
from bag_sqlite_oracle import BagContentError, decode_string_cdr, verify_recorded_bag


class BagSqliteOracleControls(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='bag-oracle-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.db = self.root / 'bag_0.db3'
        self.run = '0123456789abcdef'
        self.topic = '/bag_' + self.run
        qos = [{'history': 'keep_last', 'depth': 10, 'reliability': 'reliable',
                'durability': 'volatile'}]
        type_hash = 'RIHS01_' + '1' * 64
        definition = {'encoding': 'ros2msg', 'encoded_message_definition': 'string data\n',
                      'type_description_hash': type_hash}
        published = []
        for index in range(10):
            text = f'r25bag_{self.run}_{index:03d}'
            encoded = text.encode('utf-8') + b'\0'
            cdr = b'\x00\x01\x00\x00' + struct.pack('<I', len(encoded)) + encoded
            published.append({'sequence': index, 'text': text,
                              'cdr_base64': base64.b64encode(cdr).decode('ascii'),
                              'cdr_sha256': hashlib.sha256(cdr).hexdigest()})
        self.expected = {'schema': 1, 'record_run': self.run, 'topic': self.topic,
                         'ros_distro': 'jazzy', 'published': published,
                         'offered_qos_profiles': qos, 'type_description_hash': type_hash,
                         'message_definition': definition}
        topic_metadata = {'name': self.topic, 'type': 'std_msgs/msg/String',
                          'serialization_format': 'cdr', 'offered_qos_profiles': copy.deepcopy(qos),
                          'type_description_hash': type_hash}
        self.metadata = {'version': 9, 'storage_identifier': 'sqlite3',
                         'duration': {'nanoseconds': 1800000000},
                         'starting_time': {'nanoseconds_since_epoch': 1000000000},
                         'message_count': 10,
                         'topics_with_message_count': [{'topic_metadata': topic_metadata,
                                                        'message_count': 10}],
                         'compression_format': '', 'compression_mode': '',
                         'relative_file_paths': ['bag_0.db3'],
                         'files': [{'path': 'bag_0.db3',
                                    'starting_time': {'nanoseconds_since_epoch': 1000000000},
                                    'duration': {'nanoseconds': 1800000000}, 'message_count': 10}],
                         'custom_data': {}, 'ros_distro': 'jazzy'}
        with closing(sqlite3.connect(self.db)) as db, db:
            db.executescript('''
                CREATE TABLE schema(schema_version INTEGER PRIMARY KEY,ros_distro TEXT NOT NULL);
                CREATE TABLE metadata(id INTEGER PRIMARY KEY,metadata_version INTEGER NOT NULL,metadata TEXT NOT NULL);
                CREATE TABLE topics(id INTEGER PRIMARY KEY,name TEXT NOT NULL,type TEXT NOT NULL,serialization_format TEXT NOT NULL,offered_qos_profiles TEXT NOT NULL,type_description_hash TEXT NOT NULL);
                CREATE TABLE message_definitions(id INTEGER PRIMARY KEY,topic_type TEXT NOT NULL,encoding TEXT NOT NULL,encoded_message_definition TEXT NOT NULL,type_description_hash TEXT NOT NULL);
                CREATE TABLE messages(id INTEGER PRIMARY KEY,topic_id INTEGER NOT NULL,timestamp INTEGER NOT NULL,data BLOB NOT NULL);
                CREATE INDEX timestamp_idx ON messages(timestamp ASC);
            ''')
            db.execute('INSERT INTO schema VALUES (4,?)', ('jazzy',))
            db.execute('INSERT INTO topics VALUES (1,?,?,?,?,?)',
                       (self.topic, 'std_msgs/msg/String', 'cdr', yaml.safe_dump(qos), type_hash))
            db.execute('INSERT INTO message_definitions VALUES (1,?,?,?,?)',
                       ('std_msgs/msg/String', 'ros2msg', 'string data\n', type_hash))
            for index, row in enumerate(published):
                db.execute('INSERT INTO messages VALUES (?,?,?,?)',
                           (index + 1, 1, 1000000000 + index * 200000000,
                            base64.b64decode(row['cdr_base64'])))
            db.execute('INSERT INTO metadata VALUES (1,9,?)', ('{}',))
            db.execute('INSERT INTO metadata VALUES (2,9,?)', (yaml.safe_dump(self.metadata),))
        self.write_metadata()
        self.rehash()

    def write_metadata(self):
        (self.root / 'metadata.yaml').write_text(
            yaml.safe_dump({'rosbag2_bagfile_information': self.metadata}), encoding='utf-8')

    def rehash(self):
        self.expected['files'] = {name: hashlib.sha256((self.root / name).read_bytes()).hexdigest()
                                  for name in ('bag_0.db3', 'metadata.yaml')}

    def sql(self, statement, parameters=()):
        with closing(sqlite3.connect(self.db)) as db, db:
            db.execute(statement, parameters)
        self.rehash()

    def reject(self):
        with self.assertRaises(BagContentError):
            verify_recorded_bag(self.root, self.expected)

    def test_closed_bag_content_and_no_write(self):
        before = copy.deepcopy(self.expected['files'])
        result = verify_recorded_bag(self.root, self.expected)
        self.assertEqual(result['status'], 'PASS_CLOSED_BAG_CONTENT_ONLY')
        self.assertFalse(result['device_execution_verified'])
        self.assertFalse(result['transport_close_verified'])
        self.rehash()
        self.assertEqual(before, self.expected['files'])
        self.assertEqual(set(p.name for p in self.root.iterdir()), set(before))

    def test_collection_hash_mismatch(self):
        self.expected['files']['bag_0.db3'] = '0' * 64
        self.reject()

    def test_journal_or_extra_file_rejected(self):
        for name in ('bag_0.db3-wal', 'bag_0.db3-shm', 'bag_0.db3-journal', 'extra.txt'):
            with self.subTest(name=name):
                path = self.root / name
                path.write_bytes(b'leftover')
                self.reject()
                path.unlink()

    def test_unsafe_file_name(self):
        self.expected['files']['../bag_0.db3'] = self.expected['files'].pop('bag_0.db3')
        self.reject()

    def test_missing_or_extra_message(self):
        self.sql('DELETE FROM messages WHERE id=10')
        self.reject()

    def test_extra_message(self):
        self.sql('INSERT INTO messages(topic_id,timestamp,data) SELECT topic_id,timestamp,data FROM messages WHERE id=10')
        self.reject()

    def test_wrong_order(self):
        self.sql('UPDATE messages SET timestamp=2900000000 WHERE id=1')
        self.reject()

    def test_duplicate_payload(self):
        self.sql('UPDATE messages SET data=(SELECT data FROM messages WHERE id=1) WHERE id=2')
        self.reject()

    def test_foreign_topic(self):
        self.sql('UPDATE messages SET topic_id=99 WHERE id=3')
        self.reject()

    def test_extra_topic(self):
        self.sql('INSERT INTO topics SELECT 2,name,type,serialization_format,offered_qos_profiles,type_description_hash FROM topics')
        self.reject()

    def test_qos_mismatch(self):
        self.sql('UPDATE topics SET offered_qos_profiles=?', ('[]',))
        self.reject()

    def test_definition_mismatch(self):
        self.sql('UPDATE message_definitions SET encoded_message_definition=?', ('uint32 data\n',))
        self.reject()

    def test_schema_version(self):
        self.sql('UPDATE schema SET schema_version=3')
        self.reject()

    def test_latest_metadata_not_old_row(self):
        self.sql('INSERT INTO metadata VALUES (3,9,?)', ('{}',))
        self.reject()

    def test_external_metadata_wrapper(self):
        (self.root / 'metadata.yaml').write_text(yaml.safe_dump(self.metadata), encoding='utf-8')
        self.rehash()
        self.reject()

    def test_external_timestamp_mismatch(self):
        self.metadata['duration']['nanoseconds'] += 1
        self.write_metadata()
        self.rehash()
        self.reject()

    def test_duplicate_yaml_keys(self):
        path = self.root / 'metadata.yaml'
        path.write_text(path.read_text(encoding='utf-8') + '\nrosbag2_bagfile_information: {}\n', encoding='utf-8')
        self.rehash()
        self.reject()

    def test_equal_floats_in_both_metadata_documents_are_rejected(self):
        original = copy.deepcopy(self.metadata)
        paths = [('starting_time', 'nanoseconds_since_epoch'),
                 ('duration', 'nanoseconds'),
                 ('topics_with_message_count', 0, 'message_count'),
                 ('files', 0, 'message_count'),
                 ('files', 0, 'starting_time', 'nanoseconds_since_epoch'),
                 ('files', 0, 'duration', 'nanoseconds'),
                 ('topics_with_message_count', 0, 'topic_metadata',
                  'offered_qos_profiles', 0, 'depth')]
        for source in ('external', 'database'):
            for path in paths:
                with self.subTest(source=source, path=path):
                    self.metadata = copy.deepcopy(original)
                    self.write_metadata()
                    self.sql('UPDATE metadata SET metadata=? WHERE id=2',
                             (yaml.safe_dump(original),))
                    changed = copy.deepcopy(original)
                    parent = changed
                    for key in path[:-1]:
                        parent = parent[key]
                    parent[path[-1]] = float(parent[path[-1]])
                    if source == 'external':
                        self.metadata = changed
                        self.write_metadata()
                        self.rehash()
                    else:
                        self.sql('UPDATE metadata SET metadata=? WHERE id=2',
                                 (yaml.safe_dump(changed),))
                    self.reject()
                    self.assertIs(type(self.expected['offered_qos_profiles'][0]['depth']), int)

    def test_equal_float_in_database_qos_is_rejected(self):
        qos = copy.deepcopy(self.expected['offered_qos_profiles'])
        qos[0]['depth'] = 10.0
        self.sql('UPDATE topics SET offered_qos_profiles=?', (yaml.safe_dump(qos),))
        self.reject()
        self.assertIs(type(self.expected['offered_qos_profiles'][0]['depth']), int)

    def test_yaml_aliases(self):
        self.sql('UPDATE topics SET offered_qos_profiles=?', ('[&item {}, *item]',))
        self.reject()

    def test_sqlite_views_not_accepted(self):
        self.sql('CREATE VIEW injected AS SELECT * FROM messages')
        self.reject()

    def test_publisher_records_are_not_arbitrary_expected_strings(self):
        self.expected['published'][0]['text'] = 'different'
        self.reject()

    def test_big_endian_cdr_and_malformed_lengths(self):
        value = b'hello\0'
        wire = b'\0\0\0\0' + struct.pack('>I', len(value)) + value
        self.assertEqual(decode_string_cdr(wire), 'hello')
        for malformed in (wire[:-1], wire + b'\0', wire[:4] + b'\xff' * 4 + value,
                          b'\0\7\0\0' + wire[4:], wire[:-1] + b'x'):
            with self.subTest(data=malformed), self.assertRaises(BagContentError):
                decode_string_cdr(malformed)


if __name__ == '__main__':
    unittest.main()
