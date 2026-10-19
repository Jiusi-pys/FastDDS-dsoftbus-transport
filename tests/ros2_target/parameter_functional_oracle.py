"""V2 parameter functions; preserve standalone serialization compatibility OPEN.

These local samples are not DDS wire payload captures. Never trim or replace them.
"""
import base64
import hashlib
import parameter_native_oracle as V1

COMPATIBILITY = 'COMPAT_RMW_FASTRTPS_STANDALONE_LENGTH'


def audit_capture(row, kind):
    V1.require(row.get('native_roundtrip_equal') is True and
               row.get('cdr_origin') == 'LOCAL_RCLPY_SERIALIZE_MESSAGE',
               'Complete native local roundtrip evidence required')
    raw = base64.b64decode(row.get('cdr_base64', ''), validate=True)
    V1.require(type(row.get('cdr_bytes')) is int and row['cdr_bytes'] == len(raw) and
               row.get('cdr_sha256') == hashlib.sha256(raw).hexdigest(), 'CDR size/digest mismatch')
    V1.require(kind in V1.SCHEMAS, 'Known complete message schema required')
    reader = V1.CdrReader(raw)
    V1.exact(reader.read(kind), row.get('value'))
    consumed = 4 + reader.offset
    suffix = raw[consumed:]
    return dict(identity={k: row[k] for k in ('side', 'run', 'seq', 'event', 'service', 'call') if k in row},
                message_schema=kind, cdr_bytes=len(raw), cdr_sha256=hashlib.sha256(raw).hexdigest(),
                schema_consumed_bytes=consumed, suffix_bytes=len(suffix),
                suffix_base64=base64.b64encode(suffix).decode('ascii'),
                suffix_sha256=hashlib.sha256(suffix).hexdigest(),
                observation='UNCLASSIFIED_EXCESS_OPEN' if suffix else 'EXACT_LENGTH_SAMPLE_ONLY',
                decoded_fields_equal=True, native_roundtrip_equal=True,
                cdr_origin='LOCAL_RCLPY_SERIALIZE_MESSAGE')


def business(a, b, scenario):
    captures = [audit_capture(row, kind) for records in (a, b) for row, kind in V1._captures(records)]
    result = V1._verify_parameter_values(a, b, scenario)
    return dict(result, acceptance='FUNCTIONAL_V2_STOCK_RMW', functional_parameters='PASS',
                local_native_roundtrip='PASS_FULL_ORIGINAL_BYTES',
                standalone_serialization_compatibility='OPEN', compatibility_issue=COMPATIBILITY,
                captures=captures, wire_payload_mapping='NOT_DIRECTLY_MAPPED')
