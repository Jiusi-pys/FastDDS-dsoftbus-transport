"""IDL 4.2 section 9 profile composition and section 8 annotation signatures."""
BLOCKS = ('core', 'any', 'interfaces-basic', 'interfaces-full', 'value-types',
          'corba-interfaces', 'corba-values', 'components-basic', 'homes', 'ccm',
          'ports-connectors', 'templates', 'extended', 'anonymous', 'annotations')
GROUPS = ('general', 'modeling', 'units-ranges', 'implementation', 'generation', 'interfaces')
CORBA = {'core', 'interfaces-basic', 'interfaces-full', 'corba-interfaces'}
PLAIN_CORBA = CORBA | {'any', 'value-types', 'corba-values'}
CCM = PLAIN_CORBA | {'components-basic', 'ccm'}
PROFILES = {
    'b2': (set(BLOCKS), set(GROUPS)),
    'minimum-corba': (CORBA, set()),
    'plain-corba': (PLAIN_CORBA, set()),
    'ccm': (CCM, set()),
    'ccm-generic': (CCM | {'ports-connectors', 'templates'}, set()),
    'plain-dds': ({'core', 'anonymous'}, set()),
    'extensible-dds': ({'core', 'extended', 'anonymous', 'annotations'},
                       {'general', 'modeling', 'implementation', 'generation'}),
    'rpc-dds': ({'core', 'extended', 'anonymous', 'interfaces-basic', 'annotations'},
                {'general', 'interfaces'}),
}

# Signature: group, member -> (value category, default, required).
# Enum categories contain the exact allowed enumerator spellings.
ANNOTATIONS = {}


def signature(name, group, category=None, default=None, required=False, member='value'):
    ANNOTATIONS[name] = (group, {} if category is None else {member: (category, default, required)})


signature('id', 'general', 'uint32', required=True)
signature('autoid', 'general', ('SEQUENTIAL', 'HASH'), 'HASH')
signature('optional', 'general', 'boolean', True)
signature('position', 'general', 'uint16', required=True)
signature('value', 'general', 'any', required=True)
signature('extensibility', 'general', ('FINAL', 'APPENDABLE', 'MUTABLE'), required=True)
for name in ('final', 'appendable', 'mutable'):
    signature(name, 'general')
for name in ('key', 'must_understand'):
    signature(name, 'modeling', 'boolean', True)
signature('default_literal', 'modeling')
for name in ('default', 'min', 'max'):
    signature(name, 'units-ranges', 'any', required=True)
ANNOTATIONS['range'] = ('units-ranges', {'min': ('any', None, True), 'max': ('any', None, True)})
signature('unit', 'units-ranges', 'string', required=True)
signature('bit_bound', 'implementation', 'uint16', required=True)
for name in ('external', 'nested'):
    signature(name, 'implementation', 'boolean', True)
ANNOTATIONS['verbatim'] = ('generation', {
    'language': ('string', '*', False),
    'placement': (('BEGIN_FILE', 'BEFORE_DECLARATION', 'BEGIN_DECLARATION',
                   'END_DECLARATION', 'AFTER_DECLARATION', 'END_FILE'), 'BEFORE_DECLARATION', False),
    'text': ('string', None, True)})
signature('service', 'interfaces', 'string', '*', member='platform')
signature('oneway', 'interfaces', 'boolean', True)
signature('ami', 'interfaces', 'boolean', True)
