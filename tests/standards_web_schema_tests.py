"""Offline schema provenance, explicit repair and rejection controls for C1."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import unittest
import uuid
from lxml import etree as E
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'/'standards'))
from prepare_web_schema import prepare

class WebSchemaTests(unittest.TestCase):
    def setUp(self):
        self.out=ROOT/'build'/'c1-20261113'/('schema-test-'+uuid.uuid4().hex)
        self.source=ROOT/'integrations'/'standards'/'web_gateway'/'schemas'
        self.manifest=prepare(self.source,self.out)
        self.schema=E.XMLSchema(E.parse(str(self.out/'rest-bindings.xsd'),E.XMLParser(no_network=True)))
    def test_original_hashes_and_explicit_repairs(self):
        self.assertEqual(len(self.manifest['originals']),3)
        for item in self.manifest['originals']:
            self.assertEqual(hashlib.sha256((self.source/'original'/item['name']).read_bytes()).hexdigest(),item['sha256'])
        self.assertEqual(len(self.manifest['patches']),4)
        self.assertTrue(self.schema.validate(E.fromstring(b'<application xmlns="http://www.omg.org/dds/" name="App"/>')))
        self.assertTrue(self.schema.validate(E.fromstring(b'<subscriber xmlns="http://www.omg.org/dds/" name="Sub"/>')))
    def test_original_namespace_defect_is_preserved(self):
        with self.assertRaises(E.XMLSchemaParseError):E.XMLSchema(E.parse(str(self.source/'original'/'dds-xtypes_type_definition.xsd')))
    def test_type_constraints_are_not_disabled(self):
        for body in [
            b'<domain_participant xmlns="http://www.omg.org/dds/" name="P" domain_id="-1"/>',
            b'<topic xmlns="http://www.omg.org/dds/" name="T"/>',
            b'<data_writer xmlns="http://www.omg.org/dds/" name="W"/>',
            b'<application xmlns="http://www.omg.org/dds/" name="A"><unknown/></application>',
            b'<application name="A"/>',
        ]:self.assertFalse(self.schema.validate(E.fromstring(body)),body)
    def test_sample_bounds_and_namespace(self):
        schema=E.XMLSchema(E.parse(str(self.out/'record.xsd')))
        base='<BoundedRecord xmlns="ddstype://www.omg.org/Standards"><id>{}</id><sequence_number>0</sequence_number><label>{}</label><payload>{}</payload></BoundedRecord>'
        self.assertTrue(schema.validate(E.fromstring(base.format(4294967295,'','').encode())))
        for args in [(-1,'',''),(4294967296,'',''),(1,'x'*65,''),(1,'','<item>256</item>'),(1,'','<item>0</item>'*257)]:
            self.assertFalse(schema.validate(E.fromstring(base.format(*args).encode())))
    def test_changed_original_cannot_publish_bindings(self):
        other=self.out/'changed';shutil.copytree(self.source,other)
        with (other/'original'/'webdds_rest1.xsd').open('ab') as f:f.write(b' ')
        target=self.out/'not-published'
        with self.assertRaises(ValueError):prepare(other,target)
        self.assertFalse(target.exists())

if __name__=='__main__':unittest.main()
