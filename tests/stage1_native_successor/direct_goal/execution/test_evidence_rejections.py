"""Synthetic bytes + actual evaluator/backend/driver; no Win32/live constructor."""
import copy
import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import goal_contract as contract
import goal_evidence as evidence
from goal_contract import ContractError, GoalStep
from goal_driver import GoalDriver
from goal_notice import Marker
from goal_windows_backend import GoalWindowsBackend, Journal


def resource():
    return (b'MemTotal: 4194304 kB\nMemAvailable: 1048576 kB\n'
            b'Filesystem 1K-blocks Used Available Use% Mounted on\n'
            b'fixture 1000000 1 65536 1% /data\n/data/local/tmp\n'
            b'directory|755|0|0\ndirectory|755|0|0\ndirectory|755|0|0\n'
            b'__RO_BASE_RC=0\n__RO_ROOT_STAT_RC=1\n')


def absent():
    return ('stat: '+contract.DEVICE_ROOT+': No such file or directory\n').encode()


class Root:
    """Only in-memory state; never owns or signals an OS process."""
    def __init__(self, directory, case, stdout, stderr, rc=0):
        self.case=case; self.dest=directory; self.output=[stdout,stderr]
        self.closed=False
        self.reads=[{'name':s,'eof':True} for s in ('stdout','stderr')]
        self.receipt=dict(EOF=True,terminal=True,native_exit=rc,
                          cleanup_errors=[],stop_attempts=0)
    def state(self): return 'TERMINAL'
    def identity(self): return 'TERMINAL'
    def close(self): self.closed=True
    def drain(self,*args): return {'bytes':0,'EOF':True,'status':'EOF'}


class EvidenceRejectionTests(unittest.TestCase):
    def setUp(self):
        self.assertNotIn('goal_win32_owned',sys.modules)
        self.temp=tempfile.TemporaryDirectory()
        self.directory=Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)
        self.accepted=[]; self.persisted=[]
        self.backend=object.__new__(GoalWindowsBackend)  # __init__ MUST NOT run.
        self.backend.decoded=0; self.backend.stopped=False
        self.backend.marker=Marker(cases=('targets','readonly.1','readonly.2','readonly.3','readonly.4'))
        self.backend.journal=Journal(self.directory/'API.jsonl')
        self.addCleanup(self.backend.journal.close)
        self.backend.grant=SimpleNamespace(mark_terminal=lambda *a:None,
            accept=lambda *a:self.accepted.append(a),halt=lambda:None)
        self.backend.w=SimpleNamespace(put=lambda p,r:self.persisted.append(copy.deepcopy(r)))
        self.backend.captured={'readonly.2':0}

    def step(self,case='readonly.2'):
        return GoalStep(case,'preflight','A',('PURE_SYNTHETIC_NOT_EXECUTED',))

    def rejected(self,stdout,stderr,expected,**kwargs):
        root=Root(self.directory,'readonly.2',stdout,stderr,kwargs.get('rc',0))
        # Preserve original error propagation; decoding/unclassified failures
        # must not be silently converted to a successful/known predicate error.
        expected_type=kwargs.get('exception_type',evidence.EvidenceRejected)
        with self.assertRaises(expected_type) as raised:
            self.backend.outcome(root,self.step())
        self.assertIs(type(raised.exception),expected_type)
        self.assertFalse(root.receipt.get('accepted_evidence',True))
        row=root.receipt['evidence_rejection']
        self.assertEqual(row['reason'],expected)
        self.assertEqual(row['step'],'readonly.2')
        self.assertEqual(row['stage'],'EVIDENCE_EVALUATION')
        self.assertFalse(row['accepted'])
        self.assertTrue(root.receipt['evidence_rejection_recorded'])
        self.assertFalse(self.accepted)
        return root

    def test_valid_resource_targets_provider_keep_accepting(self):
        targets=('\n'.join(k+' USB Connected fixture' for k in contract.TARGETS.values())+'\n').encode()
        provider=('Linux fixture\n/system/lib/ld-musl-aarch64.so.1\n/system/lib/ld-musl-aarch64.so.1\n'
            '1|2|3|755\n1|2|3|755\n'+evidence.INTERPRETER_SHA+'  /lib/ld-musl-aarch64.so.1\n'+
            evidence.INTERPRETER_SHA+'  /system/lib/ld-musl-aarch64.so.1\n__RO_RC=0\n').encode()
        for case,out,err in [('targets',targets,b''),('readonly.1',provider,b''),
                             ('readonly.2',resource(),absent()),
                             ('readonly.4',resource().replace(b'\n',b'\r\n'),absent())]:
            with self.subTest(case=case):
                root=Root(self.directory,case,out,err)
                value=self.backend.outcome(root,self.step(case))
                self.assertTrue(value['accepted']); self.assertTrue(root.closed)
                self.assertNotIn('evidence_rejection',root.receipt)
        self.assertEqual(len(self.accepted),4)

    def test_resource_rejection_predicates_reach_real_backend_record(self):
        base=resource(); err=absent()
        cases=[
            (base.replace(b'__RO_BASE_RC=0',b'__RO_BASE_RC=1'),err,'REMOTE_BASE_RC_MISSING_OR_NONZERO'),
            (base+b'junk __RO_BASE_RC=\n',err,'REMOTE_DUPLICATE_MARKER'),
            (base.replace(b'__RO_ROOT_STAT_RC=1\n',b''),err,'ROOT_STAT_MARKER'),
            (base.replace(b'__RO_ROOT_STAT_RC=1',b'__RO_ROOT_STAT_RC=0'),err,'TEST_ROOT_ALREADY_EXISTS'),
            (base,b'not an approved absence\n','ROOT_ABSENCE_NOT_PROVED_ENOENT'),
            (base+b'extra\n',err,'RESOURCE_FULL_RECORD_COUNT'),
            (base.replace(b'MemTotal:',b'OtherField:'),err,'UNIQUE_ORDERED_MEMORY_FIELDS'),
            (base.replace(b'4194304',b'4194303'),err,'BOARD_MEMORY_MARGIN'),
            (base.replace(b'1048576',b'1048575'),err,'BOARD_MEMORY_MARGIN'),
            (base.replace(b'1K-blocks',b'unknown-header'),err,'DF_HEADER_UNKNOWN'),
            (base.replace(b' 1% /data',b' 1% /unknown'),err,'DF_RECORD_UNKNOWN'),
            (base.replace(b'65536',b'65535'),err,'BOARD_DISK_MARGIN'),
            (base.replace(b'\n/data/local/tmp\n',b'\n/other\n'),err,'RESOLVED_ROOT_UNKNOWN'),
            (base.replace(b'directory|',b'file|',1),err,'PARENT_PATH_METADATA_UNKNOWN'),
            (base.replace(b'__RO_BASE_RC=0\n__RO_ROOT_STAT_RC=1',b'__RO_ROOT_STAT_RC=1\n__RO_BASE_RC=0'),err,'ORDERED_RESOURCE_SENTINELS'),
            (base,b'Permission denied\n','REMOTE_DENIED')]
        for out,error,reason in cases:
            with self.subTest(reason=reason): self.rejected(out,error,reason)

    def test_local_exit_decode_and_unknown_exceptions_fail_without_text(self):
        # Nonzero local exit is rejected by the original pre-evaluator gate;
        # do not invent a parser observation for a parser that never ran.
        nonzero=Root(self.directory,'readonly.2',resource(),absent(),rc=1)
        self.assertFalse(self.backend.outcome(nonzero,self.step())['accepted'])
        self.assertNotIn('evidence_rejection',nonzero.receipt)
        self.rejected(b'\xff',absent(),'TEXT_DECODE_FAILED',exception_type=UnicodeDecodeError)
        self.rejected(resource(),b'\xff','TEXT_DECODE_FAILED',exception_type=UnicodeDecodeError)
        canary='SYNTHETIC_SECRET_NEVER_SERIALIZE_82417'
        with patch('goal_evidence.evaluate_step',side_effect=RuntimeError(canary)):
            root=self.rejected(resource(),absent(),'UNCLASSIFIED_EXCEPTION',exception_type=RuntimeError)
        self.rejected(resource()+canary.encode()+b'\n',absent(),'RESOURCE_FULL_RECORD_COUNT')
        data=(self.directory/'API.jsonl').read_bytes()
        self.assertNotIn(canary.encode(),data)
        self.assertNotIn(canary,json.dumps(root.receipt))
        self.assertNotIn('RuntimeError',data.decode())

    def test_closed_codes_do_not_accept_exception_args_or_input(self):
        canary='SYNTHETIC_SECRET_NEVER_SERIALIZE_94735'
        for failure in [ContractError(canary),evidence.EvidenceRejected(canary)]:
            with patch('goal_evidence.evaluate_step',side_effect=failure):
                root=self.rejected(resource(),absent(),
                    'UNCLASSIFIED_EVIDENCE' if type(failure) is evidence.EvidenceRejected else 'UNCLASSIFIED_EXCEPTION',
                    exception_type=type(failure))
            self.assertNotIn(canary,json.dumps(root.receipt))
        row=evidence.rejection_record(SimpleNamespace(id=canary),RuntimeError(canary))
        self.assertEqual(row['step'],'OTHER_FIXED_STEP')
        self.assertNotIn(canary,json.dumps(row))

    def test_journal_fsync_or_emit_failure_is_sticky_and_rejecting(self):
        for operation in ['emit','fsync']:
            with self.subTest(operation=operation):
                if operation=='fsync':
                    self.backend.journal=Journal(self.directory/'API-fsync.jsonl')
                    self.addCleanup(self.backend.journal.close)
                root=Root(self.directory,'readonly.2',resource()+b'extra\n',absent())
                target='goal_windows_backend.os.fsync' if operation=='fsync' else None
                manager=patch(target,side_effect=OSError('SYNTHETIC_NOT_FOR_RECORD')) if target else patch.object(self.backend.journal,'emit',side_effect=OSError('SYNTHETIC_NOT_FOR_RECORD'))
                with manager,self.assertRaises(ContractError):
                    self.backend.outcome(root,self.step())
                self.assertFalse(root.receipt['accepted_evidence'])
                self.assertFalse(root.receipt['evidence_rejection_recorded'])
                self.assertTrue(self.backend.journal.failed)
                self.assertFalse(self.accepted)
        self.assertNotIn(b'SYNTHETIC_NOT_FOR_RECORD',(self.directory/'API.jsonl').read_bytes())
        self.assertNotIn(b'SYNTHETIC_NOT_FOR_RECORD',(self.directory/'API-fsync.jsonl').read_bytes())

    def test_actual_driver_halts_and_cleanup_preserves_rejection(self):
        first=self.step(); later=self.step('readonly.3')
        self.backend.observe_all=lambda *a:None
        root=Root(self.directory,'readonly.2',resource()+b'extra\n',absent())
        driver=GoalDriver({first.id:first,later.id:later},self.backend,clock=lambda:0)
        driver.live[first.id]=root;driver.used.add(first.id)
        with self.assertRaises(ContractError): driver.pump()
        self.assertEqual(driver.failure,'OBSERVATION_OR_RECEIPT_UNKNOWN')
        self.assertFalse(driver.accepted)
        with self.assertRaises(ContractError): driver.start(later.id)
        self.assertNotIn(later.id,driver.used)
        cleanup=self.backend.cleanup(root,time.monotonic()+1)
        self.assertEqual(cleanup['state'],'TERMINAL')
        self.assertTrue(root.closed);self.assertEqual(len(self.persisted),1)
        self.assertEqual(self.persisted[0]['evidence_rejection']['reason'],'RESOURCE_FULL_RECORD_COUNT')
        self.assertFalse(self.persisted[0]['accepted_evidence'])
        rows=[json.loads(line) for line in (self.directory/'API.jsonl').read_bytes().splitlines()]
        self.assertEqual(rows[0]['evidence_rejection'],self.persisted[0]['evidence_rejection'])
        self.assertNotIn('goal_win32_owned',sys.modules)


if __name__=='__main__': unittest.main()
