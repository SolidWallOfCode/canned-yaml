#

import io
import argparse
from pathlib import Path, PurePath
import os
import re
# pip install ruamel.yaml for this
import ruamel.yaml
from ruamel.yaml import YAML

SEQUENCE_RELATED_KEYS = set(['items', 'minItems', 'maxItems'])
MAP_RELATED_KEYS = set(['require', 'properties'])

SCHEMA_TYPES = { 'null' : 'IsNull', 'boolean' : 'IsBoolean', 'object' : 'IsMap', 'array' : 'IsSequence', 'number' : 'IsNumber', 'string' : 'IsScalar'}

DEFINITIONS = {}

def could_be_type(node, candidate):
    if 'type' in node:
        tag = node['type']
        if type(tag) == str:
            return tag == candidate
        elif type(tag) == list:
            return candidate in list
    return True

def must_be_type(node, candidate):
    return 'type' in node and node['type'] == candidate

def description_of(node):
    if 'description' in node:
        return 'R"yzy({})yzy"'.format(node['description'])
    return '"No description available"'

def definition(path):
    if path in DEFINITIONS:
        return DEFINITIONS[path]
    return {}

# Code to verify the node matches the specified type.
def emit_type_check(context, types):
    cond = ' && '.join('{}.{}()'.format(context['var'], SCHEMA_TYPES[x]) for x in types)
    context['src'].write('{}if (!({})) {{ return false; }}; // type check\n'.format(context['prefix'], cond))

def emit_minitems_check(context, node, count):
    src = context['src']
    prefix = context['prefix']
    var = context['var']

    src.write('{}if ({}.IsSequence() && {}.size() < {}) {{ return boom({}, {}, "minItems"); }}\n'.format(prefix, var, var, count, var, description_of(node)))

def emit_maxitems_check(context, node, count):
    src = context['src']
    prefix = context['prefix']
    var = context['var']

    src.write('{}if ({}.IsSequence() && {}.size() > {}) {{ return boom({}, {}, "maxItems"); }}\n'.format(prefix, var, var, count, var, description_of(node)))

def emit_required_check(context, node, keys):
    src = context['src']
    prefix = context['prefix']
    level = context['level']
    var = context['var']
    init_list = ", ".join('"{}"'.format(key) for key in keys)
    src.write("""\
{}for ( auto key : {{ {} }} ) {{
{}  if (!{}[key]) {{
{}    return boom({}, {}, "Required tag missing");
{}  }}
{}}}
""".format(prefix, init_list, prefix, var, prefix, var, description_of(node), prefix, prefix))

def emit_property_check(context, properties):
    ctx = context.copy()
    src = context['src']
    prefix = context['prefix']
    level = context['level']
    var = context['var']
    ctx['level'] = level + 1
    ctx['prefix'] = prefix + '  '
    ctx['var'] = "n_{}".format(ctx['level'])

    for key, value in properties.items():
        src.write('{}if ({}["{}"]) {{\n'.format(prefix, var, key))
        src.write('{}  auto {} = {}["{}"];\n'.format(prefix, ctx['var'], var, key))
        validate_node(ctx, value)
        src.write('{}}}\n'.format(prefix))


def validate_node(context, node):
    ctx = context.copy()

    src = context['src']
    prefix = context['prefix']
    level = context['level']
    var = context['var']

    if '$ref' in node:
        src.write('{}if (! definition::{}({})) {{ return false; }}\n'.format(prefix, DEFINITIONS[node['$ref']], var))
        return; # "All other properties in a "$ref" object MUST be ignored."

    if 'type' in node:
        t = node['type']
        if type(t) == str:
            emit_type_check(ctx, [ t ])
        elif type(t) == list:
            emit_type_check(ctx, t)

    if could_be_type(node, 'object') and MAP_RELATED_KEYS & set(node.keys()):
        if not must_be_type(node, 'object'):
            src.write('{}if ({}.IsMap() {{\n'.format(prefix, ctx['var']))
            ctx['prefix'] = prefix + '  '

        if 'required' in node:
            src.write('{}// check required key(s)\n'.format(prefix))
            emit_required_check(ctx, node, node['required'])

        if 'properties' in node:
            src.write('{}// check properties\n'.format(prefix))
            emit_property_check(ctx, node['properties'])

        if not must_be_type(node, 'object'):
            ctx['prefix'] = prefix
            src.write('{}}}\n'.format(prefix))

    if could_be_type(node, 'array') and SEQUENCE_RELATED_KEYS & set(node.keys()):
        if not must_be_type(node, 'array'):
            src.write('{}if ({}.IsSequence() {{\n'.format(prefix, ctx['var']))
            ctx['prefix'] = prefix + '  '

        if 'minItems' in node:
            emit_minitems_check(ctx, node, node['minItems'])

        if 'maxItems' in node:
            emit_maxitems_check(ctx, node, node['maxItems'])

        if 'items' in node:
            items = node['items']
            if type(items) is dict:
                ctx['var'] = 'n_{}'.format(level+1)
                ctx['level'] = level + 1
                ctx['prefix'] = prefix + '  '
                src.write('{}// check items\n'.format(prefix))
                src.write('{}for ( const auto & {} : {} ) {{\n'.format(prefix, ctx['var'], var))
                validate_node(ctx, items);
                src.write('{}}}\n'.format(prefix))
            elif type(items) is list:
                pass

        if not must_be_type(node, 'array'):
            ctx['prefix'] = prefix
            src.write('{}}}\n'.format(prefix))

    if 'oneOf' in node or 'anyOf' in node:
        if 'oneOf' in node:
            schemas = node['oneOf']
            tag = 'oneOf'
        else:
            schemas = node['anyOf']
            tag = 'anyOf'
        ctx['prefix'] = prefix + '    '
        ctx['var'] = 'node'
        src.write('{}// {}\n'.format(prefix, tag))
        src.write('{}std::array<Validator, {}> valid = {{\n'.format(prefix, len(schemas)))
        for schema in schemas:
            src.write('{}  [] (const YAML::Node & node) -> bool {{\n'.format(prefix))
            validate_node(ctx, schema)
            src.write('{}    return true;\n{}  }},\n'.format(prefix, prefix, prefix))
        src.write('{}}};\n'.format(prefix))
        if tag == 'anyOf':
            src.write('{}if (! std::any_of(valid.begin(), valid.end(), [&] (const Validator & v) {{ return v({}); }})) {{ return boom({}, {}, "any_of"); }};\n'.format(prefix, var, var, description_of(node)))
        else:
            src.write('{}size_t count = 0;\n{}for ( const auto & v : valid ) {{ if (v({}) && ++count > 1) return false; }}\n{}if (count == 0) {{ return boom({}, {}, "oneOf"); }}\n'.format(prefix, prefix, var, prefix, var, description_of(node)))


    if 'enum' in node:
        values = node['enum']
        vdefs = ', '.join('YAML::Load(R"uthira_m({})uthira_m")'.format(n) for n in values)
        src.write('{}static std::array<YAML::Node, {}> values = {{{}}};\n'.format(prefix, len(values), vdefs, prefix))
        src.write('{}if (! std::any_of(values.begin(), values.end(), [&] (const YAML::Node& enum_node) -> bool {{ return equal(enum_node, {}); }})) {{ return false; }}\n'.format(prefix, var))

#    src.write('{}return true;\n'.format(prefix))

# ------

parser = argparse.ArgumentParser(description="Process schema")
parser.add_argument('--header', help="The header file to be generated.")
parser.add_argument('--source', help="The source file top be generated.")
parser.add_argument('--class', dest='classname', help='The name of the validator class.')
parser.add_argument('schema', help='The input schema file')

args = parser.parse_args()
schema_stem = PurePath(args.schema).stem.split('.')[0]
if not args.classname:
    args.classname = "{}".format(schema_stem)
if not args.header:
    args.header = "{}.h".format(schema_stem)
if not args.source:
    args.source = "{}.cc".format(schema_stem)

print("Processing {} to {},{} in class {}\n".format(args.schema, args.header, args.source, args.classname))

with open(args.schema, 'r') as input:
    out_header = None
    out_source = None
    yaml = YAML()
    try:
        root = yaml.load(input)
    except yaml.YAMLError as ex:
        print("Fail to parse schema '{}' - {}", args.schema, ex)
        os.exit(1)

    try:
        out_header = open(args.header, 'w')
    except ex:
        print("Unable to open header file '{}' - {}", args.header, ex)

    try:
        out_source = open(args.source, 'w')
    except ex:
        print("Unable to open source file '{}' - {}", args.source, ex)
    debug = type(root)
    if isinstance(root, ruamel.yaml.comments.CommentedMap):
        context = { 'hdr' : out_header, 'src' : out_source, 'prefix' : '  ', 'level' : 0}
        out_source.write('#include <functional>\n#include <array>\n#include <algorithm>\n#include <iostream>\n\n')
        out_source.write('#include "{}"\n\n'.format(args.header))
        out_source.write('using Validator = std::function<bool (const YAML::Node &)>;\n\n')
        out_header.write('#include <string_view>\n#include "yaml-cpp/yaml.h"\n\n')
        out_header.write('class {} {{\npublic:\n  bool operator()(const YAML::Node &n);\n'.format(args.classname))
        out_source.write('extern bool equal(const YAML::Node &, const YAML::Node &);\n\n')

        out_source.write('''\
bool boom(const YAML::Node & node, std::string_view desc, std::string_view reason) {
  std::cout << "Validation of " << desc << " failed: " << reason << " at line " << node.Mark().line << std::endl;
  return false;          
};

''')

        if 'definitions' in root:
            prefix = '#/definitions/'
            cleanup = re.compile('((?:^[0-9])|[^0-9a-zA-Z])')
            out_source.write('namespace definition {\n')
            context['var'] = 'n'
            for key,value in root['definitions'].items():
                func = 'v_' + cleanup.sub('_', key)
                DEFINITIONS[prefix + key] = func
                out_source.write('bool {} (const YAML::Node & n) {{\n'.format(func))
                validate_node(context, value)
                out_source.write('  return true;\n}\n\n')
            out_source.write('} // definition\n\n')

        out_source.write('bool {}::operator()(const YAML::Node& n_0) {{\n'.format(args.classname))
        context['var'] = 'n_0'

        validate_node(context, root)
        out_source.write('  return true;\n}\n')
        out_header.write('};\n')
