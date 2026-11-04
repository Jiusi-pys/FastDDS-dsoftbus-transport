"""Resource configuration is input validation, never runtime or production evidence."""
import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('installed_preflight',ROOT/'scripts/install_preflight.py')
preflight=importlib.util.module_from_spec(spec);spec.loader.exec_module(preflight)

class ResourceInputs(unittest.TestCase):
    def values(self):
        return dict(payload_guard_profile='TEST_ONLY_BOUNDED',payload_max_sample_bytes='98304',
                    payload_pool_budget_bytes='2097152',payload_max_allocations='64')
    def test_legacy_is_explicitly_unprotected(self):
        self.assertEqual(preflight.payload_resource_configuration({})['profile'],'UNPROTECTED_LEGACY')
    def test_test_contract_units_and_not_approval(self):
        row=preflight.payload_resource_configuration(self.values())
        self.assertEqual((row['M_bytes'],row['B_bytes'],row['N_allocations']),(98304,2097152,64))
        self.assertEqual(row['production_values'],'NEEDS_PRODUCT_VALUE')
        self.assertFalse(row['runtime_enforcement_verified'])
    def test_partial_and_forged_profile(self):
        for key in self.values():
            values=self.values();del values[key]
            with self.assertRaises(ValueError):preflight.payload_resource_configuration(values)
        for profile in ['PRODUCTION','true','TEST_ONLY_BOUNDED_APPROVED']:
            values=self.values();values['payload_guard_profile']=profile
            with self.assertRaises(ValueError):preflight.payload_resource_configuration(values)
    def test_overflow_zero_budget_and_numeric_syntax(self):
        for key,value in [('payload_max_sample_bytes','4294967295'),('payload_pool_budget_bytes','18446744073709551616'),
                          ('payload_max_allocations','4294967296'),('payload_max_allocations','0'),
                          ('payload_pool_budget_bytes','3'),('payload_max_sample_bytes','-1'),('payload_max_sample_bytes','4.0')]:
            values=self.values();values[key]=value
            with self.assertRaises(ValueError):preflight.payload_resource_configuration(values)

if __name__=='__main__':unittest.main()
