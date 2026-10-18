"""Read-only content oracle for the bounded ten-String, closed SQLite bag.

This checks collected files against source-bound expectations and actual publisher
CDR records. A caller must independently prove normal recorder close, collection
hashes, device execution and transport lifecycle; this module grants none of them.
"""
import base64
import hashlib
from pathlib import Path
import re
import sqlite3
import stat
import struct

import yaml

_SCHEMA = {
    'schema': [('schema_version', 'INTEGER', 0, 1), ('ros_distro', 'TEXT', 1, 0)],
    'metadata': [('id', 'INTEGER', 0, 1), ('metadata_version', 'INTEGER', 1, 0),
                 ('metadata', 'TEXT', 1, 0)],
    'topics': [('id', 'INTEGER', 0, 1), ('name', 'TEXT', 1, 0), ('type', 'TEXT', 1, 0),
               ('serialization_format', 'TEXT', 1, 0), ('offered_qos_profiles', 'TEXT', 1, 0),
               ('type_description_hash', 'TEXT', 1, 0)],
    'message_definitions': [('id', 'INTEGER', 0, 1), ('topic_type', 'TEXT', 1, 0),
                            ('encoding', 'TEXT', 1, 0),
                            ('encoded_message_definition', 'TEXT', 1, 0),
                            ('type_description_hash', 'TEXT', 1, 0)],
    'messages': [('id', 'INTEGER', 0, 1), ('topic_id', 'INTEGER', 1, 0),
                 ('timestamp', 'INTEGER', 1, 0), ('data', 'BLOB', 1, 0)],
}


class BagContentError(ValueError):
    pass


def _require(condition, message):
    if not condition:
        raise BagContentError(message)


class _UniqueLoader(yaml.SafeLoader):
    def construct_mapping(self, node, deep=False):
        result = {}
        for key_node, value_node in node.value:
            key = self.construct_object(key_node, deep=deep)
            _require(isinstance(key, str) and key not in result, 'Duplicate/non-string YAML key')
            result[key] = self.construct_object(value_node, deep=deep)
        return result


def _yaml(text):
    _require(isinstance(text, str) and len(text.encode('utf-8')) <= 1048576, 'YAML size/type')
    try:
        _require(not any(isinstance(event, yaml.AliasEvent) for event in yaml.parse(text)),
                 'YAML aliases are outside the selected writer format')
        return yaml.load(text, Loader=_UniqueLoader)
    except yaml.YAMLError as error:
        raise BagContentError('Malformed YAML') from error


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _plain_path(path, directory=False):
    info = path.lstat()
    _require(not stat.S_ISLNK(info.st_mode) and
             not (getattr(info, 'st_file_attributes', 0) & 0x400), 'Link/reparse path')
    _require(stat.S_ISDIR(info.st_mode) if directory else stat.S_ISREG(info.st_mode),
             'Wrong file kind')
    return info


def _snapshot(root, names):
    _plain_path(root, directory=True)
    _require({p.name for p in root.iterdir()} == set(names), 'Extra/missing bag files')
    result = {}
    for name in names:
        path = root / name
        info = _plain_path(path)
        limit = 1048576 if name == 'metadata.yaml' else 67108864
        _require(0 < info.st_size <= limit, 'Bag file size outside bounded fixture')
        result[name] = _sha(path.read_bytes())
    return result


def decode_string_cdr(data):
    """Independently decode the selected XCDR1 String wire representation."""
    _require(isinstance(data, bytes) and 9 <= len(data) <= 4096, 'CDR size/type')
    _require(data[:4] in (b'\x00\x01\x00\x00', b'\x00\x00\x00\x00'),
             'Unsupported CDR encapsulation')
    length = struct.unpack('<I' if data[1] else '>I', data[4:8])[0]
    _require(length >= 1 and len(data) == 8 + length and data[-1:] == b'\0',
             'CDR length/terminator/trailing bytes')
    try:
        return data[8:-1].decode('utf-8', errors='strict')
    except UnicodeDecodeError as error:
        raise BagContentError('Invalid String UTF-8') from error


def _integer(value, expected, label):
    _require(type(value) is int and value == expected, label)


def _typed_equal(value, expected):
    """Compare YAML values without Python's bool/int/float coercion."""
    if type(value) is not type(expected):
        return False
    if isinstance(expected, dict):
        return value.keys() == expected.keys() and all(
            _typed_equal(value[key], item) for key, item in expected.items())
    if isinstance(expected, list):
        return len(value) == len(expected) and all(
            _typed_equal(actual, wanted) for actual, wanted in zip(value, expected))
    return value == expected


def _metadata(value, expected, filename, first, last):
    _require(isinstance(value, dict), 'Metadata mapping')
    _integer(value.get('version'), 9, 'Metadata version')
    _integer(value.get('message_count'), 10, 'Metadata message count')
    _require(value.get('storage_identifier') == 'sqlite3', 'Metadata storage')
    _require(value.get('relative_file_paths') == [filename], 'Metadata file paths')
    _require(value.get('compression_format') == '' and value.get('compression_mode') == '',
             'Unexpected compression')
    _require(value.get('ros_distro') == expected['ros_distro'], 'ROS distro mismatch')
    _require(value.get('custom_data') == {}, 'Unexpected custom metadata')
    _require(_typed_equal(value.get('starting_time'), {'nanoseconds_since_epoch': first}),
             'Start timestamp')
    _require(_typed_equal(value.get('duration'), {'nanoseconds': last - first}), 'Duration')
    topic = {'name': expected['topic'], 'type': 'std_msgs/msg/String',
             'serialization_format': 'cdr', 'offered_qos_profiles': expected['offered_qos_profiles'],
             'type_description_hash': expected['type_description_hash']}
    _require(_typed_equal(value.get('topics_with_message_count'),
             [{'topic_metadata': topic, 'message_count': 10}]), 'Metadata topic/count/QoS/hash')
    _require(_typed_equal(value.get('files'), [{'path': filename,
             'starting_time': {'nanoseconds_since_epoch': first},
             'duration': {'nanoseconds': last - first}, 'message_count': 10}]),
             'Metadata per-file timing/count')


def verify_recorded_bag(root, expected):
    """Validate a collected immutable bag; return content evidence, never a device PASS.

    expected contains schema=1, record_run, topic, ros_distro, files (two received
    hashes), ten actual published records (sequence/text/cdr_base64/cdr_sha256),
    offered_qos_profiles, type_description_hash and message_definition with exact
    encoding/encoded_message_definition/type_description_hash from selected sources.
    """
    _require(isinstance(expected, dict) and expected.get('schema') == 1, 'Expectation schema')
    run = expected.get('record_run')
    _require(isinstance(run, str) and re.fullmatch('[0-9a-f]{16}', run), 'Record run')
    _require(expected.get('topic') == '/bag_' + run, 'Fixture topic')
    _require(isinstance(expected.get('ros_distro'), str), 'Expected ROS distro')
    hashes = expected.get('files')
    _require(isinstance(hashes, dict) and len(hashes) == 2 and 'metadata.yaml' in hashes,
             'Two-file collection hashes required')
    filename = next(name for name in hashes if name != 'metadata.yaml')
    _require(isinstance(filename, str) and
             re.fullmatch('[A-Za-z0-9_][A-Za-z0-9_.-]*\\.db3', filename), 'Unsafe SQLite filename')
    _require(all(isinstance(h, str) and re.fullmatch('[0-9a-f]{64}', h) for h in hashes.values()),
             'Collection hash format')
    qos = expected.get('offered_qos_profiles')
    _require(isinstance(qos, list) and len(qos) == 1 and isinstance(qos[0], dict),
             'Exactly one source-bound offered QoS required')
    type_hash = expected.get('type_description_hash')
    _require(isinstance(type_hash, str) and (type_hash == '' or
             re.fullmatch('RIHS01_[0-9a-f]{64}', type_hash)), 'Type hash format')
    definition = expected.get('message_definition')
    _require(isinstance(definition, dict) and set(definition) ==
             {'encoding', 'encoded_message_definition', 'type_description_hash'} and
             definition['encoding'] in ('ros2msg', 'ros2idl') and
             isinstance(definition['encoded_message_definition'], str) and
             bool(definition['encoded_message_definition']), 'Source-bound definition required')
    published = expected.get('published')
    _require(isinstance(published, list) and len(published) == 10, 'Ten publisher records required')
    cdr = []
    for sequence, row in enumerate(published):
        _require(isinstance(row, dict), 'Publisher record mapping')
        _integer(row.get('sequence'), sequence, 'Publisher sequence')
        text = 'r25bag_' + run + '_' + str(sequence).zfill(3)
        _require(row.get('text') == text, 'Publisher payload')
        try:
            data = base64.b64decode(row['cdr_base64'], validate=True)
        except (ValueError, TypeError, KeyError) as error:
            raise BagContentError('Publisher CDR base64') from error
        _require(_sha(data) == row.get('cdr_sha256') and decode_string_cdr(data) == text,
                 'Publisher CDR hash/content')
        cdr.append(data)
    root = Path(root).absolute()
    for parent in (root, *root.parents):
        _plain_path(parent, directory=True)
    before = _snapshot(root, hashes)
    _require(before == hashes, 'Collected file hash mismatch')
    external = _yaml((root / 'metadata.yaml').read_text(encoding='utf-8'))
    _require(isinstance(external, dict) and set(external) == {'rosbag2_bagfile_information'},
             'External metadata wrapper')
    connection = None
    try:
        connection = sqlite3.connect((root / filename).as_uri() + '?mode=ro&immutable=1', uri=True)
        connection.execute('PRAGMA query_only=ON')
        connection.execute('PRAGMA trusted_schema=OFF')
        _require(connection.execute('PRAGMA integrity_check').fetchall() == [('ok',)],
                 'SQLite integrity')
        objects = connection.execute("SELECT type,name FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'").fetchall()
        _require(set(objects) == {('table', name) for name in
                 ('schema', 'metadata', 'topics', 'message_definitions', 'messages')} |
                 {('index', 'timestamp_idx')}, 'Unexpected SQLite objects')
        for table, columns in _SCHEMA.items():
            actual = connection.execute('PRAGMA table_info(' + table + ')').fetchall()
            _require([(row[1], row[2], row[3], row[5]) for row in actual] == columns and
                     all(row[4] is None for row in actual), 'SQLite table shape: ' + table)
        _require(connection.execute('SELECT schema_version,ros_distro FROM schema').fetchall() ==
                 [(4, expected['ros_distro'])], 'SQLite schema version/distro')
        topics = connection.execute('SELECT id,name,type,serialization_format,offered_qos_profiles,type_description_hash FROM topics').fetchall()
        _require(len(topics) == 1, 'Exactly one topic required')
        topic_id, topic_name, topic_type, serialization, stored_qos, stored_hash = topics[0]
        _require(type(topic_id) is int and topic_id > 0 and
                 (topic_name, topic_type, serialization, stored_hash) ==
                 (expected['topic'], 'std_msgs/msg/String', 'cdr', type_hash) and
                 _typed_equal(_yaml(stored_qos), qos), 'SQLite topic identity/QoS/hash')
        definitions = connection.execute('SELECT topic_type,encoding,encoded_message_definition,type_description_hash FROM message_definitions').fetchall()
        _require(definitions == [('std_msgs/msg/String', definition['encoding'],
                 definition['encoded_message_definition'], definition['type_description_hash'])],
                 'SQLite message definition')
        rows = connection.execute('SELECT id,topic_id,timestamp,data FROM messages ORDER BY timestamp,id').fetchall()
        _require(len(rows) == 10, 'SQLite message count')
        _require(len({row[0] for row in rows}) == 10, 'Duplicate message IDs')
        for sequence, (row_id, row_topic, timestamp, data) in enumerate(rows):
            _require(type(row_id) is int and row_id > 0 and row_topic == topic_id and
                     type(timestamp) is int and timestamp > 0, 'Message row identity/timestamp')
            _require(data == cdr[sequence] and decode_string_cdr(data) == published[sequence]['text'],
                     'Persisted CDR content/order')
        first, last = rows[0][2], rows[-1][2]
        _require(last > first, 'Nonzero fixture duration required')
        metadata_rows = connection.execute('SELECT metadata_version,metadata FROM metadata ORDER BY id').fetchall()
        _require(bool(metadata_rows) and metadata_rows[-1][0] == 9, 'Missing final metadata')
        _metadata(_yaml(metadata_rows[-1][1]), expected, filename, first, last)
        _metadata(external['rosbag2_bagfile_information'], expected, filename, first, last)
    except sqlite3.Error as error:
        raise BagContentError('SQLite query failed') from error
    finally:
        if connection is not None:
            connection.close()
    _require(_snapshot(root, hashes) == before, 'Bag changed while verifying')
    return {'status': 'PASS_CLOSED_BAG_CONTENT_ONLY', 'record_run': run, 'topic': expected['topic'],
            'message_count': 10, 'first_timestamp': first, 'last_timestamp': last,
            'files': before, 'cdr_sha256': [_sha(data) for data in cdr],
            'device_execution_verified': False, 'transport_close_verified': False}
