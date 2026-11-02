"""Execute the real generated shell using synthetic inputs only. No HDC/raw IO."""
import importlib.util
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('resource_query_test',ROOT/'scripts/hdc_goal/resource_query.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
spec=importlib.util.spec_from_file_location('resource_receipt_test',ROOT/'scripts/hdc_goal/resource_receipt.py')
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
SHELLS=(('C:/Program Files/Git/usr/bin/sh.exe',),('C:/Program Files/Git/bin/bash.exe','--posix'))
ROOT_PATH='/data/local/tmp/synthetic_new_root'


def need(value,reason):
    if not value:raise ValueError(reason)


class ShellQueryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for shell in SHELLS:
            p=subprocess.run([*shell,'-c',':'],capture_output=True,timeout=5)
            if p.returncode:raise RuntimeError('LOCAL_SHELL_INITIALIZATION_FAILED')

    def execute(self,content, *, fault='',missing=False):
        parent=ROOT/'build/resource-query-synthetic'
        self.assertTrue(parent.resolve().is_relative_to(ROOT.resolve()))
        parent.mkdir(exist_ok=True)
        td=Path(tempfile.mkdtemp(dir=parent,prefix='case-'))
        try:
            self.assertTrue(td.resolve().is_relative_to(parent.resolve()))
            path=Path(td)/'meminfo';path.write_bytes(content)
            if missing:path.unlink()
            # Functions replace only downstream device commands with fixed
            # synthetic records. The memory scan and shell/status logic are real.
            prelude="""df() { printf 'Filesystem 1K-blocks Used Available Use%% Mounted on\\n/dev/synthetic 1000000 1000 999000 1%% /data\\n'; }
readlink() { printf '/data/local/tmp\\n'; }
stat() { case "$*" in *synthetic_new_root*) printf 'stat: /data/local/tmp/synthetic_new_root: No such file or directory\\n' >&2; return 1 ;; *) printf 'directory|755|1|1\\ndirectory|755|1|1\\ndirectory|755|1|1\\n' ;; esac; }
"""
            if fault=='read':prelude+='read() { return 2; }\n'
            elif fault=='printf':prelude+='printf() { return 9; }\n'
            elif fault:prelude+=fault+'() { return 7; }\n'
            body=q.meminfo_records(path.as_posix())+" && df -k /data/local/tmp && readlink -f /data/local/tmp && stat -c '%F|%a|%u|%g' /data /data/local /data/local/tmp; rc=$?; printf '__RO_BASE_RC=%s\\n' \"$rc\"; if [ \"$rc\" -eq 0 ]; then stat -c '%F|%a|%u|%g' "+ROOT_PATH+"; x=$?; printf '__RO_ROOT_STAT_RC=%s\\n' \"$x\"; fi; exit 0"
            for shell in SHELLS:
                p=subprocess.run([*shell,'-c',prelude+body],capture_output=True,timeout=5)
                self.assertEqual(p.returncode,0)
                yield p
        finally:
            resolved=td.resolve()
            if not (resolved.is_relative_to(parent.resolve()) and resolved.is_relative_to(ROOT.resolve())):
                raise RuntimeError('SYNTHETIC_CLEANUP_OUTSIDE_WORKSPACE')
            shutil.rmtree(td)

    def accept(self,p):
        # These are the generated contract's actual thresholds, injected by the
        # caller below rather than a weaker reimplementation of the parser.
        return r.parse_resource(p.stdout,p.stderr,p.returncode,ROOT_PATH,self.limits(),need)

    @staticmethod
    def limits():
        return {'board_total_kib':4194304,'board_available_kib':1048576,'board_disk_kib':65536}

    def test_complete_records_and_whitespace(self):
        for body in (b'MemTotal: 8000000 kB\nMemAvailable: 4000000 kB\n',
                     b'Other: 9 kB\nMemTotal:\t8000000   kB\nBlank:\nMemAvailable: 4000000\tkB\nAfter: 0 kB\n'):
            for p in self.execute(body):
                self.assertIn(b'__RO_BASE_RC=0\n',p.stdout)
                self.accept(p)

    def test_missing_duplicate_and_wrong_order(self):
        good=b'MemTotal: 8000000 kB\nMemAvailable: 4000000 kB\n'
        for body in (b'',b'MemTotal: 8000000 kB\n',good+good,good+b'MemAvailable: 1 kB\n',
                     b'MemAvailable: 4000000 kB\nMemTotal: 8000000 kB\n'):
            for p in self.execute(body):
                self.assertIn(b'__RO_BASE_RC=1\n',p.stdout)
                self.assertNotIn(b'__RO_ROOT_STAT_RC',p.stdout)
                with self.assertRaises(ValueError):self.accept(p)

    def test_unterminated_selected_or_trailing_record(self):
        for body in (b'MemTotal: 8 kB\nMemAvailable: 4 kB',b'MemTotal: 8 kB\nMemAvailable: 4 kB\nOther: 1'):
            for p in self.execute(body):
                self.assertIn(b'__RO_BASE_RC=1\n',p.stdout)

    def test_numeric_units_and_low_resources_remain_rejected(self):
        for total,avail in (('no','4000000'),('8000000','-1'),('8000000','0'),('8000000','NaN'),('1','4000000')):
            for p in self.execute(('MemTotal: '+total+' kB\nMemAvailable: '+avail+' kB\n').encode()):
                with self.assertRaises(ValueError):self.accept(p)
        for unit in ('KB','B','kB extra'):
            for p in self.execute(('MemTotal: 8000000 '+unit+'\nMemAvailable: 4000000 kB\n').encode()):
                with self.assertRaises(ValueError):self.accept(p)

    def test_read_failure_and_open_failure(self):
        for p in self.execute(b'',fault='read'):
            self.assertIn(b'__RO_BASE_RC=2\n',p.stdout)
            self.assertNotIn(b'__RO_ROOT_STAT_RC',p.stdout)
        for p in self.execute(b'',missing=True):
            self.assertNotIn(b'__RO_BASE_RC=0\n',p.stdout)
            self.assertNotIn(b'__RO_ROOT_STAT_RC',p.stdout)

    def test_downstream_command_failure_is_not_defaulted(self):
        for fault in ('df','readlink','stat'):
            for p in self.execute(b'MemTotal: 8000000 kB\nMemAvailable: 4000000 kB\n',fault=fault):
                self.assertIn(b'__RO_BASE_RC=7\n',p.stdout)
                self.assertNotIn(b'__RO_ROOT_STAT_RC',p.stdout)
                with self.assertRaises(ValueError):self.accept(p)

    def test_failed_printf_cannot_make_accepted_receipt(self):
        for p in self.execute(b'MemTotal: 8000000 kB\nMemAvailable: 4000000 kB\n',fault='printf'):
            self.assertEqual(p.stdout,b'')
            with self.assertRaises(ValueError):self.accept(p)

    def test_replacement_is_exact_and_leaves_remaining_commands_unchanged(self):
        tail=" && df -k /data/local/tmp; rc=$?; printf '__RO_BASE_RC=%s\\n' \"$rc\"; exit 0"
        self.assertEqual(q.replace_meminfo_query(q.MEMINFO_AWK+tail),q.meminfo_records()+tail)
        for command in ('echo '+q.MEMINFO_AWK,q.MEMINFO_AWK+tail+q.MEMINFO_AWK,'printf bad'):
            with self.assertRaises(ValueError):q.replace_meminfo_query(command)


if __name__=='__main__':unittest.main(verbosity=2)
