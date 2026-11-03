"""Actual cache/header/library origin binding; no compiler or device required."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('binding',ROOT/'integrations/ros2/target/bind_build.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class Binding(unittest.TestCase):
    def test_wrong_label_and_foreign_package_are_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as temp:
            root=Path(temp);dds=root/'dds';cdr=root/'cdr';ssl=root/'ssl'
            (ssl/'include/openssl').mkdir(parents=True);(ssl/'lib').mkdir()
            (ssl/'include/openssl/opensslv.h').write_text('#define OPENSSL_VERSION_STR "3.5.9"\n')
            for name in ('ssl','crypto'):(ssl/'lib'/('lib'+name+'.so')).write_bytes(name.encode())
            for prefix,header,version in [(dds,'fastrtps','2.14.7'),(cdr,'fastcdr','2.2.8')]:
                (prefix/'include'/header).mkdir(parents=True)
                (prefix/'include'/header/'config.h').write_text('#define '+header.upper()+'_VERSION_STR "'+version+'"\n#define HAVE_SECURITY 1\n')
                (prefix/'lib').mkdir();(prefix/'lib'/('lib'+header+'.so')).write_bytes(header.encode())
            members=[dict(name='libfastrtps.so.2.14',source=str(dds/'lib/libfastrtps.so'),sha256=m.sha(dds/'lib/libfastrtps.so')),
                     dict(name='libfastcdr.so.2',source=str(cdr/'lib/libfastcdr.so'),sha256=m.sha(cdr/'lib/libfastcdr.so'))]
            members += [dict(name='lib'+name+'.so.3',source=str(ssl/'lib'/('lib'+name+'.so')),sha256=m.sha(ssl/'lib'/('lib'+name+'.so'))) for name in ('ssl','crypto')]
            builds=[]
            for name in ('assembly','consumer'):
                build=root/name;build.mkdir();builds.append(build)
                (build/'CMakeCache.txt').write_text(f'fastrtps_DIR:PATH={dds}/share/fastrtps/cmake\nfastcdr_DIR:PATH={cdr}/lib/cmake/fastcdr\nOPENSSL_INCLUDE_DIR:PATH={ssl}/include\nOPENSSL_SSL_LIBRARY:FILEPATH={ssl}/lib/libssl.so\nOPENSSL_CRYPTO_LIBRARY:FILEPATH={ssl}/lib/libcrypto.so\n')
            m.verify_build_identity(builds,members,'2.14.7','ON')
            self.assertRaises(ValueError,m.verify_build_identity,builds,members,'2.14.5','OFF')
            foreign=root/'foreign.so';foreign.write_bytes(b'foreign')
            members[0]=dict(name='libfastrtps.so.2.14',source=str(foreign),sha256=m.sha(foreign))
            self.assertRaises(ValueError,m.verify_build_identity,builds,members,'2.14.7','ON')

if __name__=='__main__':unittest.main()
