import unittest
from host_receive_capture import validate_receive_capture


def events():
    base = dict(scope=1, record=1, gate=0, correlation=7, application=1, receiver=2,
                logical_token=3, local=0, size=3, records=1, bytes=3, copying=0,
                queued=0, taken=0, delivering=0, local_records=1, local_bytes=3,
                records_limit=4, bytes_limit=32, local_records_limit=4, local_bytes_limit=8,
                copying_limit=2, phase=0, stage=-1, ingress=22, capacity_known=0)
    result = []
    for gate, kind, phase, counter in ((1, "CHARGE", 0, "copying"), (2, "QUEUE", 1, "queued"),
                                       (3, "TAKE", 2, "taken"), (4, "ENTER", 3, "delivering")):
        row = dict(base, gate=gate, kind=kind, phase=phase)
        row[counter] = 1
        result.append(row)
    result.append(dict(base, gate=5, kind="REFUND_RECORD", phase=3, records=0, bytes=0,
                       local_records=0, local_bytes=0))
    # Callback arrival may be overtaken; it never reconstructs budget order.
    result += [dict(base, gate=0, kind="STAGE", stage=6), dict(base, gate=0, kind="STAGE", stage=2)]
    return result


def text(rows, complete=1):
    lines = []
    for index, row in enumerate(rows):
        fields = dict(schema=1, run="0123456789abcdef", side="A", pid=42, index=index, **row)
        lines.append("HOST_RX_EVENT " + " ".join(f"{key}={value}" for key, value in fields.items()))
    lines.append(f"HOST_RX_CAPTURE schema=1 run=0123456789abcdef side=A pid=42 capacity=100 reserved={len(rows)} published={len(rows)} complete={complete} ordering=BUDGET_GATE_PER_SCOPE payload=NOT_CAPTURED native=NOT_VERIFIED")
    return "\n".join(lines)


class ReceiveCaptureTests(unittest.TestCase):
    def validate(self, rows, **kwargs):
        return validate_receive_capture(text(rows, **kwargs), "0123456789abcdef", "A", 42, profile="budget_only")

    def test_actual_order_not_callback_order(self):
        result = self.validate(events())
        self.assertTrue(result["reconciled"])
        self.assertEqual((result["charged"], result["refunded"]), (1, 1))

    def test_incomplete(self):
        with self.assertRaises(ValueError):
            self.validate(events(), complete=0)

    def test_missing_gate(self):
        rows = events()
        del rows[2]
        with self.assertRaises(ValueError):
            self.validate(rows)

    def test_duplicate_refund(self):
        rows = events()
        rows.append(dict(rows[4], gate=6))
        with self.assertRaises(ValueError):
            self.validate(rows)

    def test_outstanding_record(self):
        with self.assertRaises(ValueError):
            self.validate(events()[:4])

    def test_occupancy_mismatch(self):
        rows = events()
        rows[1]["bytes"] = 4
        with self.assertRaises(ValueError):
            self.validate(rows)

    def test_orphan_stage(self):
        rows = events()
        rows[-1]["record"] = 99
        with self.assertRaises(ValueError):
            self.validate(rows)

    def test_reused_identity(self):
        rows = events()
        rows.append(dict(rows[0], gate=6))
        with self.assertRaises(ValueError):
            self.validate(rows)

    def test_wrong_ingress(self):
        rows = events()
        rows[-1]["ingress"] = 999
        with self.assertRaises(ValueError):
            self.validate(rows)

    def capacity_rows(self):
        rows = events()
        refusal = dict(rows[1], kind="CAPACITY_REFUSAL", record=0, gate=3, phase=0, size=6,
                       application_copying_known=1, application_copying=0,
                       application_copying_limit=18446744073709551615,
                       gate_capacity_mask=8, gate_capacity_reason=28)
        rows.insert(2, refusal)
        rows[3]["gate"] = 4
        rows[4]["gate"] = 5
        rows[5]["gate"] = 6
        decision = dict(refusal, kind="DECISION", capacity_known=1, capacity_mask=8,
                        capacity_records=1, capacity_bytes=3, capacity_local_records=1,
                        capacity_local_bytes=3, capacity_copying=0, layer=0, branch=7, reason=28)
        rows.append(decision)
        return rows

    def test_capacity_original_snapshot(self):
        self.assertEqual(self.validate(self.capacity_rows())["refusals"], 1)

    def test_capacity_snapshot_mismatch(self):
        rows = self.capacity_rows()
        rows[-1]["capacity_bytes"] = 99
        with self.assertRaises(ValueError):
            self.validate(rows)

    def test_capacity_wrong_predicate(self):
        rows = self.capacity_rows()
        rows[2]["gate_capacity_mask"] = 16
        with self.assertRaises(ValueError):
            self.validate(rows)





class FullHostCaptureTests(unittest.TestCase):
    # These are parser models, not claims of actual callback overtaking/reconnect.
    def make(self, copies=1, prior_refusal=False, capacity_refusal=False):
        rows, endpoints, receipts = [], [], []
        admitted = copies
        for number in range(1, copies + 1):
            ordinal = number + int(prior_refusal)
            original = events()[:5]
            for row in original:
                row.update(scope=number, ingress=ordinal, ingress_socket=number,
                           ingress_epoch=number + 10, ingress_handle=number + 100, ingress_size=3)
            base = original[0]
            original += [dict(base, kind="STAGE", gate=0, stage=stage, ingress=0,
                              ingress_socket=-1, ingress_epoch=0, ingress_handle=0, ingress_size=0)
                         for stage in reversed(range(7))]
            endpoint = dict(socket=number, epoch=number + 10, handle=number + 100)
            endpoints.append(endpoint)
            receipts.append(dict(ingress=ordinal, size=3, entered=1, returned=1, **endpoint))
            decision = dict(base, kind="DECISION", layer=0, branch=6, decision_sequence=1,
                            binding_record=number, decision_socket=number, role=0, decision_phase=3,
                            bridge_called=1, refused=0, refused_total=0, status_known=1, status=0,
                            admission_stage=10, reason=39, result_known=1, result=0,
                            capacity_known=0, capacity_mask=0)
            if prior_refusal:
                decision["decision_sequence"] = 2
            original.append(decision)
            adapter = dict(decision, layer=1, branch=14, scope=0, record=0, gate=0,
                           binding_record=0, bridge_called=0, admission_stage=0, reason=0,
                           result_known=0, result=-1, decision_sequence=ordinal)
            original.append(adapter)
            rows += original
        if prior_refusal:
            pre = dict(rows[-2], ingress=1, scope=0, record=0, gate=0, branch=2,
                       decision_sequence=1, bridge_called=0, refused=1, refused_total=1,
                       status=43, admission_stage=0, reason=0, result_known=0, result=-1)
            rows += [pre, dict(rows[-1], ingress=1, decision_sequence=1)]
            receipts.append(dict(receipts[0], ingress=1))
        if capacity_refusal:
            ordinal = len(receipts)+1
            # Insert refusal at the authoritative gate after QUEUE, before TAKE.
            for row in rows:
                if row['scope'] == 1 and row['kind'] not in ('STAGE','DECISION') and row['gate'] >= 3:
                    row['gate'] += 1
            refusal = dict(rows[1], kind="CAPACITY_REFUSAL", record=0, gate=3, phase=0, size=6,
                           ingress=ordinal, ingress_size=6, application_copying_known=1,
                           application_copying=0, application_copying_limit=100,
                           gate_capacity_mask=8, gate_capacity_reason=28)
            binding = dict(rows[12], kind="DECISION", layer=0, branch=7, record=0, gate=3,
                           ingress=ordinal, ingress_size=6, size=6, decision_sequence=ordinal,
                           reason=28, admission_stage=7, result=6, status=37, refused=1,
                           refused_total=1+int(prior_refusal), capacity_known=1, capacity_mask=8,
                           capacity_records=1, capacity_bytes=3, capacity_local_records=1,
                           capacity_local_bytes=3, capacity_copying=0)
            adapter = dict(rows[13], ingress=ordinal, ingress_size=6, size=6, decision_sequence=ordinal)
            rows += [refusal,binding,adapter]
            receipts.append(dict(receipts[0], ingress=ordinal, size=6))
        cov = dict(profile="EXCLUSIVE_BINDING", ingress=len(receipts), adapter_rejected=0, active=0,
                   seen=len(receipts), admitted=admitted, refused_phase=0, refused_native=0,
                   refused_receive=int(prior_refusal)+int(capacity_refusal))
        return rows, endpoints, receipts, cov

    def validate(self, model):
        rows, endpoints, receipts, cov = model
        def line(prefix, values):
            fields = dict(schema=1, run="0123456789abcdef", side="A", pid=42, **values)
            return prefix + " " + " ".join(f"{k}={v}" for k,v in fields.items())
        extra = [line("HOST_CALLBACK_EVENT", dict(index=i, **r)) for i,r in enumerate(receipts)]
        extra += [line("HOST_CALLBACK_CAPTURE", dict(capacity=8192, reserved=len(receipts),
                      ingress=cov['ingress'], complete=1)), line("HOST_RX_COVERAGE", cov)]
        return validate_receive_capture(text(rows) + "\n" + "\n".join(extra),
            "0123456789abcdef", "A", 42, endpoints=endpoints, expected_ingress=cov['ingress'])

    def test_complete_deferred_callbacks(self):
        self.assertTrue(self.validate(self.make())["full_host_complete"])

    def test_valid_two_connections(self):
        self.assertTrue(self.validate(self.make(copies=2))["full_host_complete"])

    def test_prior_prebudget_refusal_and_capacity(self):
        self.assertEqual(self.validate(self.make(prior_refusal=True,capacity_refusal=True))["refusals"],1)

    def test_omitted_all_decisions_or_stages(self):
        for kind in ('DECISION','STAGE'):
            model = self.make()
            model[0][:] = [r for r in model[0] if r['kind'] != kind]
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                self.validate(model)

    def test_missing_single_release(self):
        model = self.make()
        model[0][:] = [r for r in model[0] if not (r['kind']=='STAGE' and r['stage']==6)]
        with self.assertRaises(ValueError):self.validate(model)

    def test_refused_flags_and_totals(self):
        for field,value in [('refused',0),('refused_total',0),('refused_total',3),('branch',6),('layer',1)]:
            model = self.make(prior_refusal=True,capacity_refusal=True)
            decision = next(r for r in model[0] if r['kind']=='DECISION' and r['branch']==7)
            decision[field]=value
            with self.subTest(field=field,value=value), self.assertRaises(ValueError):self.validate(model)

    def test_independent_counters(self):
        for field in ('admitted','seen','refused_receive','adapter_rejected','ingress','active'):
            model=self.make();model[3][field]+=1
            with self.subTest(field=field),self.assertRaises(ValueError):self.validate(model)

    def test_each_original_identity_component(self):
        for field in ('ingress','ingress_socket','ingress_epoch','ingress_handle','ingress_size'):
            for kind in ('CHARGE','DECISION'):
                model=self.make()
                for row in model[0]:
                    if row['kind']==kind:row[field]=999999
                with self.subTest(field=field,kind=kind),self.assertRaises(ValueError):self.validate(model)

    def test_all_original_identity_corrupted_together(self):
        model=self.make()
        for row in model[0]:
            for field in ('ingress_socket','ingress_epoch','ingress_handle'):row[field]=999999
        with self.assertRaises(ValueError):self.validate(model)

    def test_missing_partial_identity(self):
        for field in ('ingress_socket','ingress_epoch','ingress_handle','ingress_size'):
            model=self.make();del model[0][0][field]
            with self.subTest(field=field),self.assertRaises(ValueError):self.validate(model)

    def test_capacity_identity_component(self):
        for field in ('ingress','ingress_socket','ingress_epoch','ingress_handle','ingress_size'):
            model=self.make(capacity_refusal=True)
            next(r for r in model[0] if r['kind']=='CAPACITY_REFUSAL')[field]=999999
            with self.subTest(field=field),self.assertRaises(ValueError):self.validate(model)

    def test_reconnect_endpoint_collision(self):
        model=self.make(copies=2);model[1][1]=dict(model[1][0])
        with self.assertRaises(ValueError):self.validate(model)

    def test_wrong_original_connection(self):
        model=self.make(copies=2)
        for r in model[0]:
            if r['scope']==1 and r['kind']=='CHARGE':
                r.update(ingress_socket=2,ingress_epoch=12,ingress_handle=102)
        with self.assertRaises(ValueError):self.validate(model)

    def test_duplicate_callback_ordinal(self):
        model=self.make(copies=2);model[2][1]['ingress']=1
        with self.assertRaises(ValueError):self.validate(model)

    def test_valid_all_adapter_refusals(self):
        model=self.make()
        a=next(r for r in model[0] if r['kind']=='DECISION' and r['layer']==1)
        a.update(branch=10,status=15)
        model[0][:]=[a];model[3].update(seen=0,admitted=0,adapter_rejected=1)
        self.assertTrue(self.validate(model)['full_host_complete'])

    def test_allocation_and_commit_rollback_lifecycle(self):
        for allocated,commit in ((False,False),(True,False),(True,True)):
            model=self.make()
            charge=model[0][0]
            refund=dict(model[0][4],kind='REFUND_RECORD' if allocated else 'REFUND_CAPTURE',gate=2,phase=0)
            b=next(r for r in model[0] if r['kind']=='DECISION' and r['layer']==0)
            a=next(r for r in model[0] if r['kind']=='DECISION' and r['layer']==1)
            b.update(branch=7,refused=1,refused_total=1,reason=36 if commit else 33,
                     admission_stage=10 if commit else 8,result=7 if commit else 8,status=29 if commit else 2)
            stages={0}|({6} if allocated else set())|({1} if commit else set())
            model[0][:]=[charge,refund,b,a]+[dict(charge,kind='STAGE',gate=0,stage=v) for v in stages]
            model[3].update(admitted=0,refused_receive=1)
            with self.subTest(allocated=allocated,commit=commit):
                self.assertTrue(self.validate(model)['full_host_complete'])

    def test_queued_discard_lifecycle(self):
        model=self.make()
        model[0][:]=[r for r in model[0] if r['kind'] not in ('TAKE','ENTER')
                     and not (r['kind']=='STAGE' and r['stage'] in (3,4,5))]
        refund=next(r for r in model[0] if r['kind']=='REFUND_RECORD')
        refund.update(gate=3,phase=1)
        self.assertTrue(self.validate(model)['full_host_complete'])

    def test_duplicate_lifecycle_stage(self):
        model=self.make();model[0].append(dict(next(r for r in model[0] if r['kind']=='STAGE')))
        with self.assertRaises(ValueError):self.validate(model)

    def test_old_log_requires_explicit_budget_mode(self):
        with self.assertRaises(ValueError):
            validate_receive_capture(text(events()),"0123456789abcdef","A",42)
        result=validate_receive_capture(text(events()),"0123456789abcdef","A",42,profile="budget_only")
        self.assertFalse(result['full_host_complete'])


if __name__ == "__main__":
    unittest.main()
