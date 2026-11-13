"""Copy the unchanged, offline OMG DDS-WEB WebSocket envelope schema."""
import argparse
import hashlib
import json
from pathlib import Path
from lxml import etree

def prepare(source,output):
    source,output=Path(source),Path(output)
    item=json.loads((source/'websocket-provenance.json').read_text(encoding='utf-8'))
    raw=(source/'original'/item['name']).read_bytes()
    if hashlib.sha256(raw).hexdigest()!=item['sha256'] or len(raw)!=item['bytes']:
        raise ValueError('WebSocket original schema hash mismatch')
    etree.XMLSchema(etree.fromstring(raw,etree.XMLParser(no_network=True,resolve_entities=False)))
    output.mkdir(parents=True,exist_ok=True)
    (output/item['name']).write_bytes(raw)
    (output/'websocket-schema-set.json').write_text(json.dumps(item,indent=2)+'\n',encoding='utf-8')
    return item

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source');p.add_argument('output')
    args=p.parse_args();prepare(args.source,args.output);print('WEBSOCKET_SCHEMA PASS')
