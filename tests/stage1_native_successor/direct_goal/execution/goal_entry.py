"""Fixed Goal entry. Preparation has no grant; no plan/fixture unlock exists."""
import json,re,sys,os
from pathlib import Path
from goal_contract import ACTIVE_ROOT,RUN_ROOT,build_registry,validate_registry,ContractError,need
from goal_authorization import safe_input

def main(argv=None):
    args=list(sys.argv[1:] if argv is None else argv)
    if sys.flags.optimize or len(args)!=5 or args[0]!='--actual-once' or args[1]!='--manifest-sha256' or args[3]!='--binding-sha256' or not all(re.fullmatch('[0-9a-f]{64}',args[n]) for n in (2,4)):
        print('GOAL_NOT_GRANTED HDC=0 Native=0');return 78
    driver=None;procedure=None;failure=None;cleanup=None
    try:
        need(Path(__file__).resolve().parent==ACTIVE_ROOT.resolve(),'FIXED_ENTRY_LOCATION')
        inputs=json.loads(safe_input(ACTIVE_ROOT/'GOAL_INPUTS_D01.json',65536).decode('utf-8-sig'))
        registry=validate_registry(build_registry(inputs['payloads'],inputs['readonly_commands'],inputs['controller']))
        from goal_driver import actual_driver
        driver=actual_driver(ACTIVE_ROOT,args[2],args[4],registry)
        from goal_workflow import FixedGoalWorkflow
        procedure=FixedGoalWorkflow(driver,inputs['artifacts'],args[2]).run()
    except BaseException:
        # Preserve fixed classification; never serialize exception text or
        # arbitrary class names from SDK/tool/output or treat failure as PASS.
        failure='GOAL_FAILED_OR_UNKNOWN'
        if driver:driver.halt(failure)
    finally:
        if driver:cleanup=driver.finish()
    clean=cleanup is not None and not cleanup['failure'] and not any(row.get('state')!='TERMINAL' or row.get('EOF') is not True for row in cleanup['cleanup'])
    passed=failure is None and procedure is not None and clean
    result={'scope':'FIRST_CURRENT_STOCK_DDS_AB','status':'PASS' if passed else 'FAIL_OR_HOLD','failure':failure,'procedure':procedure,'cleanup':cleanup,'Native_SDK_callback_quiet':'UNKNOWN','worker_parent_identity_restore':'UNKNOWN','no_retry':True}
    if driver:
        try:
            path=RUN_ROOT/'evidence/RESULT.json'
            with path.open('x',encoding='utf-8') as out:json.dump(result,out,sort_keys=True);out.flush();os.fsync(out.fileno())
        except BaseException:passed=False
    print('GOAL_COMPLETE' if passed else 'GOAL_FAILED_OR_UNKNOWN')
    return 0 if passed else 1

if __name__=='__main__':raise SystemExit(main())
