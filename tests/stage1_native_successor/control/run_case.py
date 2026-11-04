"""Portable Host fixture driver entry. Never deploys or loads a Native ELF."""
import hashlib,json,pathlib,subprocess,sys
exe,dll=map(pathlib.Path,sys.argv[1:3])
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest={'scope':'HOST_FIXTURE_ONLY','frontend_sha256':digest(exe),'fixture_sha256':digest(dll)}
identity=hashlib.sha256(json.dumps(manifest,sort_keys=True).encode()).hexdigest()
argv=[str(exe.resolve()),str(dll.resolve()),manifest['frontend_sha256'],identity]
print(json.dumps({'argv':argv,'manifest':manifest,'manifest_sha256':identity}),flush=True)
sys.exit(subprocess.run(argv,timeout=23,check=False).returncode)
