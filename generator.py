#

import io
import argparse
from pathlib import Path, PurePath
import os
import yaml

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

def definition(path):
    if path in DEFINITIONS:
        return DEFINITIONS[path]
    return {}

# Code to verify the node matches the specified type.
def emit_type_check(context, types):
    cond = ' && '.join('{}.{}()'.format(context['var'], SCHEMA_TYPES[x]) for x in types)
    context['src'].write('{}if (!({})) {{ return false; }}; // type check\n'.format(context['prefix'], cond))

def emit_minitems_check(context, count):
    src = context['src']
    prefix = context['prefix']
    var = context['var']

    src.write('{}if ({}.IsSequence() && {}.size() < {}) {{ return false; }}\n'.format(prefix, var, var, count))

def emit_maxitems_check(context, count):
    src = context['src']
    prefix = context['prefix']
    var = context['var']

    src.write('{}if ({}.IsSequence() && {}.size() > {}) {{ return false; }}\n'.format(prefix, var, var, count))

def emit_required_check(context, keys):
    src = context['src']
    prefix = context['prefix']
    level = context['level']
    var = context['var']
    init_list = ", ".join('"{}"'.format(key) for key in keys)
    src.write("""\
{}for ( auto key : {{ {} }} ) {{
{}  if (!{}[key]) {{
{}    return false;
{}    break;
{}  }}
{}}}
""".format(prefix, init_list, prefix, var, prefix, prefix, prefix, prefix))

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
        node.update(definition(node['$ref']))

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

        if 'properties' in node:
            src.write('{}// check properties\n'.format(prefix))
            emit_property_check(ctx, node['properties'])

        if 'required' in node:
            src.write('{}// check required type(s)\n'.format(prefix))
            emit_required_check(ctx, node['required'])

        if not must_be_type(node, 'object'):
            ctx['prefix'] = prefix
            src.write('{}}}\n'.format(prefix))

    if could_be_type(node, 'array') and SEQUENCE_RELATED_KEYS & set(node.keys()):
        if not must_be_type(node, 'array'):
            src.write('{}if ({}.IsSequence() {{\n'.format(prefix, ctx['var']))
            ctx['prefix'] = prefix + '  '

        if 'minItems' in node:
            emit_minitems_check(ctx, node['minItems'])

        if 'maxItems' in node:
            emit_maxitems_check(ctx, node['maxItems'])

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

    if 'oneOf' in node:
        schemas = node['oneOf']
        src.write('{}// oneOf\n'.format(prefix))
        src.write('{}std::array<Validator, {}> val = {{\n'.format(prefix, len(schemas)))
        for schema in schemas:
            src.write('{}  [] (const YAML::Node & node) -> bool {{\n'.format(prefix))
            ctx['prefix'] = prefix + '    '
            ctx['var'] = 'node'
            validate_node(ctx, schema)
            src.write('{}}},\n'.format(prefix))
        src.write('{}}};\n'.format(prefix))
        src.write('{}if (std::any_of(val.begin(), val.end(), [&] (const Validator & v) {{ return v({}); }})) {{\n'.format(prefix, var))
        src.write('{}  return false;\n'.format(prefix))
        src.write('{}}}\n'.format(prefix))

    src.write('{}return true;\n'.format(prefix))

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

    if type(root) is dict:
        context = { 'hdr' : out_header, 'src' : out_source, 'prefix' : '  ', 'level' : 0}
        out_source.write('#include <functional>\n#include <array>\n#include <algorithm>\n\n')
        out_source.write('#include "{}"\n\n'.format(args.header))
        out_source.write('using Validator = std::function<bool (const YAML::Node &)>;\n\n')
        out_header.write('#include "yaml-cpp/yaml.h"\n\n')
        out_header.write("class {} {{\n  bool operator()(const YAML::Node &n);\n".format(args.classname))
        out_source.write('bool {}::operator()(const YAML::Node& n_0) {{\n'.format(args.classname))
        context['var'] = 'n_0'

        if 'definitions' in root:
            prefix = '#/definitions/'
            for key,value in root['definitions'].items():
                DEFINITIONS[prefix + key] = value

        validate_node(context, root)
        out_source.write('}\n')
        out_header.write('};\n')
