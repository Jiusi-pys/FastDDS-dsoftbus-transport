"""Build output binding for only the known nonbusiness fixture target."""
import pathlib,hashlib,sys
dll,out=map(pathlib.Path,sys.argv[1:3]);digest=hashlib.sha256(dll.read_bytes()).hexdigest()
out.write_text('#pragma once\n#define STAGE1_BOUND_FIXTURE_SHA "'+digest+'"\n',encoding='ascii')
