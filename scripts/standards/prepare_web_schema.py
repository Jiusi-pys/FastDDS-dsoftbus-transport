"""Hash-bound, offline DDS-WEB schema bindings and explicit PIM corrections."""
import argparse
import hashlib
import json
from pathlib import Path
from lxml import etree as E

XS='http://www.w3.org/2001/XMLSchema'
XT='http://www.omg.org/ptc/2011/01/07/XML_Type_Representation/'
DDS='http://www.omg.org/dds/'
BINDINGS={'application':'application','application_list':'applicationList',
 'domain_participant':'domainParticipant','domain_participant_list':'domainParticipantList',
 'register_type':'registerType','topic':'topic','topic_list':'topicList',
 'publisher':'publisher','publisher_list':'publisherList',
 'subscriber':'subscriber','subscriber_list':'subscriberList',
 'data_writer':'dataWriter','data_writer_list':'dataWriterList',
 'data_reader':'dataReader','data_reader_list':'dataReaderList',
 'write_sample_seq':'writeSampleSeq','read_sample_seq':'readSampleSeq','waitset':'waitset'}

def prepare(source,output):
 source,output=Path(source),Path(output)
 provenance=json.loads((source/'provenance.json').read_text())
 for item in provenance:
  raw=(source/'original'/item['name']).read_bytes()
  if hashlib.sha256(raw).hexdigest()!=item['sha256']:raise ValueError('Original schema hash mismatch '+item['name'])
 output.mkdir(parents=True,exist_ok=True)
 patches=[]
 for item in provenance:
  name=item['name']; tree=E.parse(str(source/'original'/name))
  if name=='dds-xtypes_type_definition.xsd':
   old=tree.getroot(); ns=dict(old.nsmap);ns[None]=XT
   root=E.Element(old.tag,nsmap=ns,attrib=dict(old.attrib));root.set('targetNamespace',XT)
   root.text=old.text
   for n in list(old):root.append(n)
   tree._setroot(root)
   patches.append({'file':name,'change':'Qualify unprefixed QNames and align targetNamespace with REST import URI; no declaration/constraint removed'})
   seq=root.find('{%s}complexType[@name="moduleDecl"]/{%s}sequence'%(XS,XS))
   duplicate=seq.find('{%s}element[@name="include"]'%XS)
   assert duplicate is not None
   seq.remove(duplicate)
   patches.append({'file':name,'change':'Remove redundant leading include particle in moduleDecl; moduleElements already permits the same include sequence (fixes XSD UPA ambiguity)'})
  if name=='webdds_rest1.xsd':
   for kind in ('application','subscriber'):
    node=tree.getroot().find('{%s}complexType[@name="%s"]/{%s}choice'%(XS,kind,XS))
    assert node is not None and node.get('minOccurs','1')=='1'
    node.set('minOccurs','0')
    patches.append({'file':name,'type':kind,'change':'choice minOccurs 1 to 0 to represent empty entity containers permitted by PIM 7.3.1.1 and 7.4.6'})
  for n in tree.getroot():
   if n.tag in ('{'+XS+'}include','{'+XS+'}import'):
    n.set('schemaLocation',n.get('schemaLocation').rsplit('/',1)[-1])
  tree.write(str(output/name),encoding='UTF-8',xml_declaration=True)
 root=E.Element('{'+XS+'}schema',nsmap={'xs':XS,None:DDS},
                targetNamespace=DDS,elementFormDefault='qualified')
 E.SubElement(root,'{'+XS+'}include',schemaLocation='webdds_rest1.xsd')
 for name,type_ in BINDINGS.items(): E.SubElement(root,'{'+XS+'}element',name=name,type=type_)
 # The standard omits these global declarations while its Table 6 names them.
 for name,child,type_ in [('register_type_list','register_type','registerType'),('waitset_list','waitset','waitset')]:
  n=E.SubElement(root,'{'+XS+'}element',name=name)
  c=E.SubElement(n,'{'+XS+'}complexType');q=E.SubElement(c,'{'+XS+'}sequence')
  E.SubElement(q,'{'+XS+'}element',name=child,type=type_,minOccurs='0',maxOccurs='unbounded')
 n=E.SubElement(root,'{'+XS+'}element',name='condition_name_list')
 c=E.SubElement(n,'{'+XS+'}complexType');q=E.SubElement(c,'{'+XS+'}sequence')
 E.SubElement(q,'{'+XS+'}element',name='item',type='xs:string',minOccurs='0',maxOccurs='unbounded')
 E.ElementTree(root).write(str(output/'rest-bindings.xsd'),encoding='UTF-8',xml_declaration=True)
 for filename,uri in [('record.xsd','ddstype://www.omg.org/Standards'),('record-plain.xsd','')]:
  ns={'xs':XS};attrs={'elementFormDefault':'qualified'}
  if uri:ns[None]=uri;attrs['targetNamespace']=uri
  r=E.Element('{'+XS+'}schema',nsmap=ns,attrib=attrs)
  e=E.SubElement(r,'{'+XS+'}element',name='BoundedRecord');c=E.SubElement(e,'{'+XS+'}complexType');s=E.SubElement(c,'{'+XS+'}sequence')
  for name in ['id','sequence_number']:E.SubElement(s,'{'+XS+'}element',name=name,type='xs:unsignedInt')
  label=E.SubElement(s,'{'+XS+'}element',name='label');typ=E.SubElement(label,'{'+XS+'}simpleType');restriction=E.SubElement(typ,'{'+XS+'}restriction',base='xs:string')
  E.SubElement(restriction,'{'+XS+'}maxLength',value='64')
  payload=E.SubElement(s,'{'+XS+'}element',name='payload');seq=E.SubElement(E.SubElement(payload,'{'+XS+'}complexType'),'{'+XS+'}sequence')
  E.SubElement(seq,'{'+XS+'}element',name='item',type='xs:unsignedByte',minOccurs='0',maxOccurs='256')
  E.ElementTree(r).write(str(output/filename),encoding='UTF-8',xml_declaration=True)
 E.XMLSchema(E.parse(str(output/'rest-bindings.xsd'),E.XMLParser(no_network=True)))
 result={'originals':provenance,'patches':patches,'bindings':'DDS-WEB 1.0 Table 6 global elements; register-type/waitset list containers',
   'generated':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob('*.xsd')}}
 (output/'schema-set.json').write_text(json.dumps(result,indent=2)+'\n')
 return result

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('source');p.add_argument('output')
 a=p.parse_args();prepare(a.source,a.output);print('SCHEMA_BINDINGS PASS')
