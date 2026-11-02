"""Synthetic public resource records only. No file, HDC or SDK access."""
import importlib.util
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('resource_parser_tests',ROOT/'scripts/hdc_goal/resource_receipt.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
DEVICE='/data/local/tmp/SYNTHETIC_RESOURCE_ROOT'
LIMITS={'board_total_kib':4194304,'board_available_kib':1048576,'board_disk_kib':65536}
ROWS=['MemTotal: 8000000 kB','MemAvailable: 4000000 kB',
      'Filesystem 1K-blocks Used Available Use% Mounted on','/dev/test 1000000 1000 999000 1% /data',
      '/data/local/tmp','directory|755|1|1','directory|755|1|1','directory|755|1|1',
      '__RO_BASE_RC=0','__RO_ROOT_STAT_RC=1']
ERROR='stat: '+DEVICE+': No such file or directory'

class Rejected(Exception):pass
def need(ok,code):
    if not ok:raise Rejected(code)
def wire(rows,ending=b'\n'):return ending.join(x.encode() for x in rows)+ending if rows else b''
def parse(rows=ROWS,errors=(ERROR,),ending=b'\n'):
    return p.parse_resource(wire(rows,ending),wire(errors,ending),0,DEVICE,LIMITS,need)

class ResourceTests(unittest.TestCase):
    def test_valid_separate_and_merged_with_all_supported_line_endings(self):
        for end in (b'\n',b'\r\n',b'\r\r\n'):
            for merged in (False,True):
                with self.subTest(ending=end,merged=merged):
                    rows=ROWS[:9]+[ERROR]+ROWS[9:] if merged else ROWS
                    result=parse(rows,() if merged else (ERROR,),end)
                    self.assertEqual(result['remote_base_RC'],0)
                    self.assertEqual(result['root_absence'],'ENOENT_WITH_BASE_ACCESS')
                    self.assertEqual(result['data_available_KiB'],999000)

    def test_exact_supported_root_diagnostic_formats(self):
        for prefix in ('','cannot stat '):
            for quote in ('',"'",'"'):
                error=f'stat: {prefix}{quote}{DEVICE}{quote}: No such file or directory'
                self.assertEqual(parse(errors=(error,))['root_stat_RC'],1)

    def test_missing_nonzero_malformed_and_duplicate_base_are_distinct_diagnostics(self):
        cases=[([], 'MISSING'),(['__RO_BASE_RC=127'],'NONZERO'),(['__RO_BASE_RC=0'],'ZERO'),
               (['__RO_BASE_RC=00'],'MALFORMED'),(['__RO_BASE_RC=-0'],'MALFORMED'),
               (['__RO_BASE_RC=256'],'MALFORMED'),(['__RO_BASE_RC=0 extra'],'MALFORMED'),
               (['__RO_BASE_RC=0','__RO_BASE_RC=127'],'DUPLICATE_OR_CONFLICTING')]
        for rows,expected in cases:
            with self.subTest(expected=expected,rows=rows):self.assertEqual(p.marker_state(rows,'__RO_BASE_RC'),expected)

    def test_missing_or_nonzero_base_never_proves_resources(self):
        for value in ('__RO_BASE_RC=127','__RO_BASE_RC=1','__RO_BASE_RC=-1','__RO_BASE_RC=00','MISSING'):
            rows=list(ROWS);rows[8]=value
            with self.subTest(value=value),self.assertRaises(Rejected):parse(rows)

    def test_duplicate_base_and_root_markers_reject(self):
        for rows in (ROWS+['__RO_BASE_RC=0'],ROWS+['__RO_ROOT_STAT_RC=1']):
            with self.subTest(rows=rows),self.assertRaises(Rejected):parse(rows)

    def test_merged_error_requires_exact_position(self):
        for index in (0,8,10):
            rows=list(ROWS);rows.insert(index,ERROR)
            with self.subTest(index=index),self.assertRaises(Rejected):parse(rows,())

    def test_error_duplicated_across_channels_rejects(self):
        with self.assertRaises(Rejected):parse(ROWS[:9]+[ERROR]+ROWS[9:],(ERROR,))

    def test_unknown_noise_or_stderr_is_never_discarded(self):
        for rows,errors in ((ROWS+['UNRECOGNIZED'],(ERROR,)),(ROWS,(ERROR,'UNRECOGNIZED')),
                            (ROWS,('stat: '+DEVICE+': Unknown error',)),(ROWS,())):
            with self.subTest(rows=rows,errors=errors),self.assertRaises(Rejected):parse(rows,errors)

    def test_wrong_path_or_mismatched_quote_rejects(self):
        for error in ('stat: /different: No such file or directory',
                      'stat: "'+DEVICE+': No such file or directory',ERROR+' extra'):
            with self.subTest(error=error),self.assertRaises(Rejected):parse(errors=(error,))

    def test_permissions_reject_in_either_channel(self):
        for text in ('Permission denied','Operation not permitted','unauthorized'):
            for merged in (False,True):
                with self.subTest(text=text,merged=merged),self.assertRaisesRegex(Rejected,'REMOTE_DENIED'):
                    parse(ROWS[:9]+[text]+ROWS[9:] if merged else ROWS,() if merged else (text,))

    def test_existing_root_or_invalid_root_rc_rejects(self):
        for rc in ('0','00','-1','256','1 extra'):
            rows=list(ROWS);rows[9]='__RO_ROOT_STAT_RC='+rc
            with self.subTest(rc=rc),self.assertRaises(Rejected):parse(rows)

    def test_resource_limits_still_apply(self):
        for index,value in ((0,'MemTotal: 1000 kB'),(1,'MemAvailable: 1 kB'),
                            (3,'/dev/test 1000000 999999 1 99% /data')):
            rows=list(ROWS);rows[index]=value
            with self.subTest(index=index),self.assertRaises(Rejected):parse(rows)

    def test_root_and_parent_proofs_still_apply(self):
        for index,value in ((4,'/other'),(5,'regular file|755|1|1'),(8,'__RO_BASE_RC=0 extra')):
            rows=list(ROWS);rows[index]=value
            with self.subTest(index=index),self.assertRaises(Rejected):parse(rows)

    def test_partial_bare_cr_extra_cr_nul_and_invalid_utf8_reject(self):
        for data in (b'line',b'line\r\n\r',b'line\r\r\r\n',b'li\rne\n',b'line\x00\n',b'\xff\n',
                     b'line\v\n',b'line\f\n',b'line\x7f\n','line\u0085\n'.encode(),
                     'line\u2028\n'.encode(),'line\u2029\n'.encode()):
            with self.subTest(data=data),self.assertRaises((Rejected,UnicodeDecodeError)):p.receipt_text(data,need)
        self.assertEqual(p.receipt_text(b'valid\tfield\r\r\n',need),'valid\tfield\n')

    def test_local_exit_nonzero_rejects(self):
        with self.assertRaisesRegex(Rejected,'CLIENT_NATIVE_NONZERO'):
            p.parse_resource(wire(ROWS),wire((ERROR,)),1,DEVICE,LIMITS,need)


if __name__=='__main__':unittest.main(verbosity=2)
