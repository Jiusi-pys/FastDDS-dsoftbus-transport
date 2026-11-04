"""Only local Windows nonbusiness children. No Native image or HDC execution."""
import pathlib,hashlib,subprocess,sys,os,json
exe,dll,out=map(pathlib.Path,sys.argv[1:4]);root=out/('case-'+str(os.getpid()));root.mkdir(parents=True,exist_ok=False)
sha=hashlib.sha256(dll.read_bytes()).hexdigest();argv=[str(exe.resolve()),str(dll.resolve()),sha,str(root.resolve())]
print(json.dumps({'argv':argv,'scope':'LOCAL_NONBUSINESS_FIXTURE_ONLY'}),flush=True)
sys.exit(subprocess.run(argv,timeout=23,check=False).returncode)
