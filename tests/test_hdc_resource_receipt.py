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

    def test_df_display_mount_is_not_the_queried_path(self):
        # Public Toybox df(file) selects by st_dev; a bind mount of the same
        # filesystem can have a different printed directory. No actual output.
        for mount in ('/','/data','/data/local','/data/local/tmp','/storage/bind_alias','/mnt/shared-volume'):
            rows=list(ROWS);rows[3]='/dev/synthetic 1000000 1000 999000 1% '+mount
            self.assertEqual(parse(rows)['data_available_KiB'],999000)

    def test_df_long_device_single_line_and_horizontal_separators(self):
        for device in ('none','overlay','/dev/block/by-name/'+('x'*400)):
            for gap in (' ','   ','\t'):
                rows=list(ROWS);rows[3]=gap.join((device,'1000000','1000','999000','1%','/storage/bind_alias'))
                self.assertEqual(parse(rows)['data_available_KiB'],999000)

    def test_df_available_boundary_and_unsigned_decimal_contract(self):
        for free,accepted in (('65536',True),('65535',False),('0',False),('-1',False),
                              ('+65536',False),('65536.0',False),('65,536',False),
                              ('65536k',False),('\u0666\u0665\u0665\u0663\u0666',False)):
            rows=list(ROWS);rows[3]='/dev/synthetic 1000000 1000 '+free+' 1% /storage/bind_alias'
            if accepted:self.assertEqual(parse(rows)['data_available_KiB'],65536)
            else:
                with self.subTest(free=free),self.assertRaises(Rejected):parse(rows)

    def test_df_mount_requires_bounded_lexical_absolute_path(self):
        invalid=('data','//','/data/','/data//tmp','/data/./tmp','/data/../tmp',
                 '/data/space here','/data/back\\040space','/data/\tvalue',
                 '/data/\u00a0value','/'+('x'*4095),'/data/\x01value')
        for mount in invalid:
            rows=list(ROWS);rows[3]='/dev/synthetic 1000000 1000 999000 1% '+mount
            with self.subTest(mount=mount),self.assertRaises(Rejected):parse(rows)
        rows=list(ROWS);rows[3]='/dev/synthetic 1000000 1000 999000 1% /'+('x'*4094)
        self.assertEqual(parse(rows)['data_available_KiB'],999000)

    def test_df_no_strip_or_invented_missing_fields(self):
        for line in (' /dev/test 1000000 1000 999000 1% /data',
                     '/dev/test 1000000 1000 999000 1% /data ',
                     '/dev/test 1000000 1000 999000 /data',
                     '/dev/test 1000000 1000 999000 1 /data',
                     '/dev/test 1000000 1000 999000 1 % /data',
                     '/dev/test 1000000 1000 999000 1% /data extra'):
            rows=list(ROWS);rows[3]=line
            with self.subTest(line=line),self.assertRaises(Rejected):parse(rows)

    def test_df_wrapped_duplicate_or_truncated_records_remain_rejected(self):
        wrapped=ROWS[:3]+['/dev/very-long-name',' 1000000 1000 999000 1% /data']+ROWS[4:]
        for rows in (wrapped,ROWS[:4]+[ROWS[3]]+ROWS[4:],ROWS[:3]+ROWS[4:]):
            with self.assertRaises(Rejected):parse(rows)
        with self.assertRaises(Rejected):p.parse_resource(wire(ROWS)[:-1],wire((ERROR,)),0,DEVICE,LIMITS,need)

    def test_df_k_units_and_original_path_proofs_are_still_fixed(self):
        for header in ('Filesystem 1024-blocks Used Available Capacity Mounted on',
                       'Filesystem 512-blocks Used Available Capacity Mounted on',
                       'Filesystem Size Used Avail Use% Mounted on'):
            rows=list(ROWS);rows[2]=header
            with self.assertRaisesRegex(Rejected,'DF_HEADER_UNKNOWN'):parse(rows)
        for index,value in ((4,'/storage/bind_alias'),(5,'regular file|755|1|1'),
                            (8,'__RO_BASE_RC=1'),(9,'__RO_ROOT_STAT_RC=0')):
            rows=list(ROWS);rows[3]='/dev/test 1000000 1000 999000 1% /storage/bind_alias';rows[index]=value
            with self.assertRaises(Rejected):parse(rows)


if __name__=='__main__':unittest.main(verbosity=2)
