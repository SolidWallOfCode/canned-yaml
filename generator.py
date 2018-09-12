#

import io
import argparse
from pathlib import Path, PurePath
import os
import yaml

SCHEMA_TYPES = { 'null' : 'IsNull', 'boolean' : 'IsBoolean', 'object' : 'IsMap', 'array' : 'IsSequence', 'number' : 'IsNumber', 'string' : 'IsString'}

def emit_type_check(context, types):
    cond = ' && '.join('n_{}.{}()'.format(context['level'], SCHEMA_TYPES[x]) for x in types)
    context['src'].write('{}if (!({})) {{ zret = false; }};\n'.format(context['prefix'], cond))

def validate_node(context, key, node):
    old = context
    context = context.copy()

    src = context['src']
    prefix = context['prefix']
    level = context['level']

    context['level'] = level + 1
    context['prefix'] = prefix + '  '

    src.write('{}auto n_{} = n_{}["{}"];\n'.format(prefix, level+1, level, key))
    t = node['type']
    if type(t) == str:
        emit_type_check(context, [ t ])
    elif type(t) == list:
        emit_type_check(context, t)

    context = old


def object_node(context, properties):
    for key, value in properties.items():
        src = context['src']
        prefix = context['prefix']
        src.write('{}if (n_{}["{}"]) {{\n'.format(prefix, context['level'], key))
        validate_node(context, key, value)
        src.write('{}}}\n'.format(prefix))

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
        out_header.write("class {} {{\n  bool operator()(const YAML::Node &n);\n".format(args.classname))
        out_source.write('bool {}::operator()(const YAML::Node& n_0) {{\n'.format(args.classname))
        if root['type'] == 'object':
            object_node(context, root['properties'])
        out_source.write('}\n')
        out_header.write('};\n')
