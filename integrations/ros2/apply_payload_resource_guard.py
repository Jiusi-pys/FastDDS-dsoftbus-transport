"""Exact 2.14.7 Subscriber public-IPayloadPool assembly; no RTPS/core patch."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil

HERE = Path(__file__).resolve().parent
HEADERS = ['ReaderPayloadGuard.hpp', 'PayloadResourceConfig.hpp', 'ReaderPayloadBinding.hpp']
MEMBER = 'src/cpp/fastdds/subscriber/SubscriberImpl.cpp'


def apply(source):
    spec=importlib.util.spec_from_file_location('payload_path_contract',HERE/'apply_topic_rtti_key.py')
    paths=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(paths)
    unlinked_member=paths.unlinked_member
    source = Path(source)
    meta = json.loads((HERE/'candidates/2.14.7/UPSTREAM.json').read_text())
    row = next(row for row in meta['files'] if row['path'] == MEMBER)
    file = source/MEMBER
    unlinked_member(source,file)
    before = file.read_bytes()
    if hashlib.sha256(before).hexdigest() != row['after_sha256']:
        raise ValueError('payload Subscriber prerequisite drift/already patched')
    text = before.decode('utf-8')
    anchor = '    topic->get_impl()->reference();'
    if text.count(anchor) != 1:
        raise ValueError('payload Subscriber assembly anchor')
    text = '#include <fastdds/domain/ReaderPayloadBinding.hpp>\n'+text
    text = text.replace(anchor, '    if (!fastdds_dsoftbus::payload_resource::select_reader_pool(payload_pool)) return nullptr;\n\n'+anchor)
    anchor = '    return reader;\n}'
    if text.count(anchor) != 1:
        raise ValueError('payload Reader evidence anchor')
    text = text.replace(anchor, '    fastdds_dsoftbus::payload_resource::reader_created(payload_pool, reader, topic->get_name().c_str());\n'+anchor)
    text += '''
#if defined(_WIN32)
#define FDDST_PAYLOAD_EXPORT __declspec(dllexport)
#else
#define FDDST_PAYLOAD_EXPORT __attribute__((visibility("default")))
#endif
extern "C" FDDST_PAYLOAD_EXPORT int FastddsDsoftbusPayloadSnapshot(
        fastdds_dsoftbus::payload_resource::Snapshot* output) noexcept {
    try {return output && fastdds_dsoftbus::payload_resource::snapshot(*output) ? 0 : 1;}
    catch (...) {return 1;}
}
'''
    domain = source/'src/cpp/fastdds/domain'
    headers=[]
    for name in HEADERS:
        target=domain/name
        unlinked_member(source,target)
        if target.exists():raise ValueError('payload header already present')
        data=(HERE/name).read_bytes()
        headers.append(dict(name=name,source_raw_sha256=hashlib.sha256(data).hexdigest(),
                            lf_sha256=hashlib.sha256(data.replace(b'\r\n',b'\n')).hexdigest()))
    file.write_bytes(text.encode('utf-8'))
    for name in HEADERS:shutil.copyfile(HERE/name,domain/name)
    return dict(step='PUBLIC_IPAYLOADPOOL_SUBSCRIBER_ASSEMBLY',version='2.14.7',
        upstream_commit=meta['upstream_commit'],path=MEMBER,before_raw_sha256=hashlib.sha256(before).hexdigest(),
        after_raw_sha256=hashlib.sha256(file.read_bytes()).hexdigest(),eol_mapping='NONE_LF_SOURCE',headers=headers,
        scope='USER_DDS_READER_PAYLOAD_ONLY; NO_BUILTIN_CRYPTO_CDR_SDK_GUARANTEE')
