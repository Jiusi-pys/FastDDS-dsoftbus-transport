"""Offline fixed observation-package adapter. Never creates an execution grant."""
import ast
import importlib.util
from pathlib import Path
import sys

_path=Path(__file__).with_name('prepare_probe_pair.py')
_spec=importlib.util.spec_from_file_location('_observation_private_pair_builder',_path)
builder=importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(builder)

# A private builder instance preserves all defaults and historical packages.
builder.PACKAGE=builder.REPO/'build/hdc-a-observe-package-20261102-d01'
builder.ACTIVE=builder.PACKAGE/'active'
builder.RUN=builder.REPO/'build/hdc-a-observe-once-20261102_b6f13d90'
builder.SCOPE='ONE_NEW_FIXED_A_SCOPED_SERVER_TELEMETRY_60S_D01'
builder.NONCE='B6F13D90_OBSERVE_A_60S_D01'
builder.CARD_PATH='docs/HDC_A_OBSERVATION_EXECUTION.md'
builder.PACKAGE_FORMAT='FIXED_A_SCOPED_OBSERVATION_PACKAGE1'
_pair_sources=builder.SOURCES
builder.SOURCES=(*_pair_sources,'probe_observe.py')
builder.CODE_PATHS=[*builder.CODE_PATHS,
 'scripts/hdc_goal/probe_observe.py','scripts/hdc_goal/prepare_observation.py',
 'tests/test_hdc_observation.py','docs/HDC_A_OBSERVATION_EXECUTION.md']
_pair_source_buffers=builder.source_buffers


def source_buffers():
    # The original adapter iterates its own finite source grammar. Its private
    # instance is synchronous and not shared with the historical builder.
    builder.SOURCES=_pair_sources
    try:buffers,old=_pair_source_buffers()
    finally:builder.SOURCES=(*_pair_sources,'probe_observe.py')
    for name in ('goal_contract.py','goal_evidence.py'):
        buffers[name]=buffers[name].replace(b'A8D3F641',b'B6F13D90')
    text=buffers['goal_contract.py'].decode('utf-8')
    buffers['goal_contract.py']=builder.replace_assignment(text,'SOURCE_NAMES',repr(builder.SOURCES)).encode('utf-8')
    buffers['probe_observe.py']=builder.read_safe(builder.REPO/'scripts/hdc_goal/probe_observe.py')
    text=buffers['goal_authorization.py'].decode('utf-8')
    tree=ast.parse(text)
    effects=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign)
                 and any(isinstance(t,ast.Name) and t.id=='EFFECTS' for t in n.targets))
    effects.add('server_auth_word_unknown_observation')
    text=builder.replace_assignment(text,'EFFECTS','{'+','.join(repr(s) for s in sorted(effects))+'}')
    text=builder.replace_once(text,'grant.server_unknown_continue=False',
        "grant.server_unknown_continue=False\n grant.scoped_server_keyword_observation=approval['effects']['server_auth_word_unknown_observation']")
    buffers['goal_authorization.py']=text.encode('utf-8')
    for name in ('goal_driver.py','goal_entry.py'):
        text=buffers[name].decode('utf-8').replace('from probe_pair import actual_driver','from probe_observe import actual_driver')
        buffers[name]=text.encode('utf-8')
    text=buffers['goal_windows_backend.py'].decode('utf-8')
    text=builder.replace_once(text,"type(self).__name__=='PairBackend'","type(self).__name__=='ObservationBackend'")
    buffers['goal_windows_backend.py']=text.encode('utf-8')
    for name,data in buffers.items():ast.parse(data,filename=name)
    # The delegated strict module keeps its old factory's scope and nonce. It
    # cannot construct the new live backend; only make_types is reused.
    builder.need(builder.SCOPE.encode() not in buffers['probe_pair.py'],'STRICT_FACTORY_UNCHANGED')
    return buffers,old


builder.source_buffers=source_buffers


if __name__=='__main__':raise SystemExit(builder.main(sys.argv[1:]))
