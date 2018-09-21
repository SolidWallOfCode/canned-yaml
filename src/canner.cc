/** @file

    C++ Code generator for schemas for YAML files.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one
    or more contributor license agreements.  See the NOTICE file
    distributed with this work for additional information
    regarding copyright ownership.  The ASF licenses this file
    to you under the Apache License, Version 2.0 (the
    "License"); you may not use this file except in compliance
    with the License.  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
 */

#include <unistd.h>
#include <getopt.h>
#include <array>
#include <iostream>
#include <fstream>
#include <tuple>
#include <unordered_map>
#include <bitset>

#include "swoc/TextView.h"
#include "swoc/bwf_base.h"
#include "swoc/swoc_file.h"
#include "swoc/Errata.h"
#include "swoc/Lexicon.h"

#include "yaml-cpp/yaml.h"

using swoc::Severity;
using swoc::Errata;
using swoc::TextView;

namespace {
std::array<option, 4> Options = {{
                                     {"hdr", 1, nullptr, 'h'},
                                     {"src", 1, nullptr, 's'},
                                     {"class", 1, nullptr, 'c'},
                                     {nullptr, 0, nullptr, 0}
                                 }};

static const std::string DEFINITION_PREFIX{"#/definitions/"};

enum PRIMITIVE {
  NIL,
  BOOL,
  OBJECT,
  ARRAY,
  NUMBER,
  STRING,
  INVALID
};

swoc::Lexicon<PRIMITIVE> PrimitiveType{
    {{NIL, "null"},
        {BOOL, "boolean"},
        {OBJECT, "object"},
        {ARRAY, "array"},
        {NUMBER, "number"},
        {STRING, "string"},
        {INVALID, "invalid"}}
};

[[maybe_unused]] static bool INITIALIZED  = (
    PrimitiveType.set_default(INVALID).set_default("INVALID"),
    true);

using TypeSet = std::bitset<PRIMITIVE::INVALID>;
};

// BufferWriter formatting for specific types.
namespace swoc {
BufferWriter & bwformat(BufferWriter & w, const bwf::Spec & spec, const file::path & path) {
  return bwformat(w, spec, std::string_view{path.c_str()});
}
}

struct Context {

  std::string hdr_path;
  std::ofstream hdr_file;
  std::string src_path;
  std::ofstream src_file;
  std::string class_name;
  Errata notes;

  int src_indent {0}; // Indent level.
  int hdr_indent {0};

  void indent_src();
  void exdent_src();
  void indent_hdr();
  void exdent_hdr();

  // Working methods.
  Errata generate_define(YAML::Node const& key, YAML::Node const& value);
  // Base node validation - handles top level tags and dispatches as needed.
  Errata validate_node(YAML::Node const& node);

  template < typename ... Args > void src_out(std::string_view fmt, Args && ... args);

  template < typename ... Args > void hdr_out(std::string_view fmt, Args && ... args);

  void out(std::ofstream & s, TextView text, int indent);

  // Error message generators
  Errata Err_Bad_Type(const YAML::Node & node);

  using Definitions = std::unordered_map<std::string, std::string>;
  Definitions definitions;
};

void Context::exdent_hdr() { --hdr_indent; }
void Context::exdent_src() { --src_indent; }
void Context::indent_hdr() { ++hdr_indent; }
void Context::indent_src() { ++hdr_indent; }

template < typename ... Args > void Context::src_out(std::string_view fmt, Args && ... args) {
  static std::string tmp; // static makes for better memory reuse.
  swoc::bwprintv(tmp, fmt, std::forward_as_tuple(args...));
  this->out(src_file, tmp, src_indent);
}

template < typename ... Args > void Context::hdr_out(std::string_view fmt, Args && ... args) {
  static std::string tmp; // static makes for better memory reuse.
  swoc::bwprintv(tmp, fmt, std::forward_as_tuple(args...));
  this->out(hdr_file, tmp, hdr_indent);
}

void Context::out(std::ofstream& s, TextView text, int indent) {
  while (text) {
    auto line = text.take_prefix_at('\n');
    if (!line.empty()) {
      for ( int i = indent ; i > 0 ; --i ) {
        s << "  ";
      }
      s << line;
    }
    s << std::endl;
  }
}

Errata Context::Err_Bad_Type(const YAML::Node &node) {
  swoc::LocalBufferWriter<1024> w;

}

Errata Context::validate_node(YAML::Node const &node) {
  if (node["$ref"]) {
  } else {
    TypeSet types;
    if (node["type"]) {
      auto type_node { node["type"] };
      if (type_node.IsScalar()) {
        auto ptype = PrimitiveType[type_node.Scalar()];
        if (INVALID == ptype) {
          notes.error("Type '{}' in 'type' node at line {} is not a valid type.",
                      type_node.Scalar(), type_node.Mark().line);
        } else {
          types[ptype] = true;
        }
      } else if (type_node.IsSequence()) {
        for ( auto n : type_node ) {
          auto ptype = PrimitiveType[n.Scalar()];
          if (INVALID == ptype) {
            notes.error("Type '{}' in 'type' node at line {} is not a valid type.",
                        n.Scalar(), type_node.Mark().line);
          } else {
            types[ptype] = true;
          }
        }
      } else {
        return notes.error("'type' node at line {} is neither a string nor a sequence of strings", node.Mark().line);
      }
    };
  }
  return notes;
}

Errata Context::generate_define(YAML::Node const& key, YAML::Node const& value) {
  std::string name = key.Scalar();
  std::transform(name.begin(), name.end(), name.begin(), [](char c) { return isalnum(c) ? c : '_'; });
  definitions[DEFINITION_PREFIX + key.Scalar()] = name;
  hdr_out("bool {} (swoc::Errata erratum, YAML::Node const& node);", name);

  src_out("bool {}::definition::{} (swoc::Errata erratum, YAML::Node const& node) {{", class_name, name);
  indent_src();
  validate_node(value);
  exdent_src();
  src_out("}}");
  return notes;
}

Errata process_definitions(Context& ctx, YAML::Node const& node) {
  if (! node.IsMap()) {
    return ctx.notes.error("'definitions' node is not a map");
  }
  ctx.hdr_out("struct definition {{\n");
  ctx.indent_hdr();
  for ( auto && pair: node ) {
    ctx.generate_define(pair.first, pair.second);
  }
  ctx.exdent_hdr();
  ctx.hdr_out("}};\n");
  return ctx.notes;
}

Errata process(int argc, char *argv[]) {
  int zret;
  int idx;
  Context ctx;
  std::string tmp;

  while (-1 != (zret = getopt_long(argc, argv, ":", Options.data(), &idx))) {
    switch (zret) {
      case ':' : ctx.notes.error("'{}' requires a value", argv[optind-1]); break;
      case 'h' : ctx.hdr_path = argv[optind-1]; break;
      case 's' : ctx.src_path = argv[optind-1]; break;
      case 'c' : ctx.class_name = argv[optind-1]; break;
      default:
        ctx.notes.warn("Unknown option '{}' - ignored", char(zret), argv[optind-1]);
        break;
    }
  }

  if (!ctx.notes.is_ok()) {
    return ctx.notes;
  }

  if (optind >= argc) {
    return ctx.notes.error("An input schema file is required");
  }

  swoc::file::path schema_path{argv[optind]};
  std::error_code ec;
  std::string content = swoc::file::load(schema_path, ec);

  ctx.notes.info("Loaded schema file '{}' - {} bytes", schema_path, content.size());

  YAML::Node root;
  try {
    root = YAML::Load(content);
  } catch (std::exception & ex) {
    return ctx.notes.error("Loading failed: {}", ex.what());
  }

  ctx.hdr_file.open(ctx.hdr_path.c_str(), std::ofstream::trunc);
  if (!ctx.hdr_file.is_open()) {
    return ctx.notes.error("Failed to open header output file '{}'", ctx.hdr_path);
  }
  ctx.src_file.open(ctx.src_path.c_str(), std::ofstream::trunc);
  if (!ctx.src_file.is_open()) {
    return ctx.notes.error("Failed to open source output file '{}'", ctx.src_path);
  }

  if (!root.IsMap()) {
    return ctx.notes.error("Root node must be a map");
  }

  ctx.src_out("#include <functional>\n#include <array>\n#include <algorithm>\n#include <iostream>\n\n"
              "#include \"{}\"\n\n"
              "using Validator = std::function<bool (const YAML::Node &)>;\n\n"
              "extern bool equal(const YAML::Node &, const YAML::Node &);\n\n", ctx.hdr_path);

  ctx.hdr_out("#include <string_view>\n#include \"yaml-cpp/yaml.h\"\n\n");
  ctx.hdr_out("class {} {{\npublic:\n", ctx.class_name);
  ctx.indent_hdr();
  ctx.hdr_out("bool operator()(const YAML::Node &n);\n", ctx.class_name);


  if (root["definitions"]) {
    process_definitions(ctx, root["definitions"]);
  }
  return ctx.notes;
}

int main(int argc, char * argv[]) {
  auto result = process(argc, argv);
}
