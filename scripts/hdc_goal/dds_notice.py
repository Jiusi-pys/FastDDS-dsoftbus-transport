"""Exact public DDS tool records that contain authentication-like words.

This is classification, not evidence acceptance. The normal step evaluator
still checks source hashes, supervised identities, remote status and records.
No prefix/sub-string exemption and no weakening of requests/denials/UNKNOWN.
"""
import re
import goal_notice as notice

DIAGNOSTICS = frozenset(('DIAG_NATIVE_CREATED', 'DIAG_FIXED_TRANSFER_HASH', 'DIAG_FIXED_STEP_STATUS'))
notice.RULES.update(DIAGNOSTICS)


class DDSMarker(notice.Marker):
    def __init__(self,*args,registry,**kwargs):
        self.registry=registry
        super().__init__(*args,**kwargs)

    def _record(self,case,stream,phase,state,data,complete,before,after):
        rule=None
        # Full ASCII physical record, at most Windows' two CRs before LF.
        # Any suffix, encoding error or additional text falls through unchanged.
        if complete and stream=='stdout' and phase in ('MONITOR','CLEANUP'):
            try:text=data.decode('ascii')
            except UnicodeDecodeError:text=''
            text=text[:-2] if text.endswith('\r\r') else text.removesuffix('\r')
            if self._blocking(text) is None:
                step=self.registry.get(case)
                if step and text=='S1_GOAL_STEP:'+case+':'+str(step.expected_remote):
                    rule='DIAG_FIXED_STEP_STATUS'
                if step and (step.kind=='long' or case in ('own.a','own.b')):
                    match=re.fullmatch(r'DIRECT_SUP_CREATED parent=([1-9][0-9]{0,9}) child=([1-9][0-9]{0,9}) generation=UNVERIFIED no_signal_authority=1',text)
                    if match and all(int(x)<=4294967295 for x in match.groups()):rule='DIAG_NATIVE_CREATED'
                if step and step.byte_source:
                    command=step.args[-1]
                    match=re.fullmatch(r"\( sha256sum '([^'\r\n]+)' \); rc=\$\?; printf .*",command)
                    if match and re.fullmatch(r'[0-9a-fA-F]{64}  '+re.escape(match[1]),text):
                        rule='DIAG_FIXED_TRANSFER_HASH'
        if rule:
            self._emit(case,stream,phase,'DIAGNOSTIC',rule,state['offset']-len(data),
                       state['offset'],before,after,True)
        else:
            super()._record(case,stream,phase,state,data,complete,before,after)
