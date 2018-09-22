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
#include <vector>
#include <limits>

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

/// JSON Schema types.
enum class SchemaType {
  NIL,
  BOOL,
  OBJECT,
  ARRAY,
  NUMBER,
  STRING,
  INVALID
};

std::string Valid_Type_List;

swoc::Lexicon<SchemaType> SchemaTypeName{{
                                               {SchemaType::NIL, "null"},
                                               {SchemaType::BOOL, "boolean"},
                                               {SchemaType::OBJECT, "object"},
                                               {SchemaType::ARRAY, "array"},
                                               {SchemaType::NUMBER, "number"},
                                               {SchemaType::STRING, "string"},
                                       }};

std::map<SchemaType, std::string> SchemaTypeCheck{{
                                               {SchemaType::NIL, "IsNull"},
                                               {SchemaType::BOOL, "IsScalar"},
                                               {SchemaType::OBJECT, "IsMap"},
                                               {SchemaType::ARRAY, "IsSequence"},
                                               {SchemaType::NUMBER, "IsScalar"},
                                               {SchemaType::STRING, "IsScalar"},
                                       }};

/// Tags that apply to an Object.
enum class ObjectTag {
  PROPERTIES,
  REQUIRED,
  INVALID
};

swoc::Lexicon<ObjectTag> ObjectTagName {{
                                             { ObjectTag::PROPERTIES, "properties"},
                                             { ObjectTag::REQUIRED, "required" }
                                     }};

/// Tags that apply to an Array.
enum class ArrayTag {
  ITEMS,
  MIN_ITEMS,
  MAX_ITEMS,
  INVALID
};

swoc::Lexicon<ArrayTag> ArrayTagName {{
                                             { ArrayTag::ITEMS, "items" },
                                             { ArrayTag::MIN_ITEMS, "minItems" },
                                           { ArrayTag::MAX_ITEMS, "maxItems"}
                                     }};

// File scope initialization.
[[maybe_unused]] static bool INITIALIZED  = (

            SchemaTypeName.set_default(SchemaType::INVALID).set_default("INVALID"),
            ObjectTagName.set_default(ObjectTag::INVALID).set_default("INVALID"),
            ArrayTagName.set_default(ArrayTag::INVALID).set_default("INVALID"),

            // The set of type strings doesn't change, set up a global with the list.
            []() -> void {
      swoc::LocalBufferWriter<1024> w;
              for ( auto && [ value, name ] : SchemaTypeName ) {
w.print("'{}', ", name);
              }
              w.discard(2);
              Valid_Type_List.assign(w.view());
    }(),
    true);

using TypeSet = std::bitset<static_cast<size_t>(SchemaType::INVALID)>;
};

namespace YAML
{
// Need these to pass views in to node indexing.
template <> struct convert<std::string_view> {
  static Node
  encode(std::string_view const &sv)
  {
    Node zret;
    zret = std::string(sv.data(), sv.size());
    return zret;
  }
  static bool
  decode(const Node &node, std::string_view &sv)
  {
    if (!node.IsScalar()) {
      return false;
    }
    sv = std::string_view{node.Scalar()};
    return true;
  }
};

template <> struct convert<swoc::TextView> {
  static Node
  encode(swoc::TextView const &tv)
  {
    Node zret;
    zret = std::string(tv.data(), tv.size());
    return zret;
  }
  static bool
  decode(const Node &node, swoc::TextView &tv)
  {
    if (!node.IsScalar()) {
      return false;
    }
    tv.assign(node.Scalar());
    return true;
  }
};

} // namespace YAML

// BufferWriter formatting for specific types.
namespace swoc {
BufferWriter & bwformat(BufferWriter & w, const bwf::Spec & spec, const file::path & path) {
  return bwformat(w, spec, std::string_view{path.c_str()});
}
}

// Context carried between the various parsing steps.
struct Context {

  std::string hdr_path;
  std::ofstream hdr_file;
  std::string src_path;
  std::ofstream src_file;
  std::string class_name;
  Errata notes;

  int src_indent {0}; // Indent level.
  bool src_sol_p {true}; // Start of line
  int hdr_indent {0};
  bool hdr_sol_p {true};

  int var_idx {1}; ///< Index suffix for locally declared nodes.
  std::string var_name();

  void indent_src();
  void exdent_src();
  void indent_hdr();
  void exdent_hdr();

  // Working methods.
  Errata process_definitions(YAML::Node const& node);
  Errata generate_define(YAML::Node const& key, YAML::Node const& value);
  // Base node validation - handles top level tags and dispatches as needed.
  Errata validate_node(YAML::Node const& node, std::string_view const& var);

  // property checks
  Errata process_type_value(const YAML::Node & node, TypeSet & types);

  // code generation
  void emit_required_check(YAML::Node const& node, std::string_view const & var);
  void emit_type_check(TypeSet const& types, std::string_view const& var);
  void emit_min_items_check(std::string_view const& node, uintmax_t limit);

  // output
  template < typename ... Args > void src_out(std::string_view fmt, Args && ... args);

  template < typename ... Args > void hdr_out(std::string_view fmt, Args && ... args);

  void out(std::ofstream & s,TextView text, bool & sol_p,  int indent);

  using Definitions = std::unordered_map<std::string, std::string>;
  Definitions definitions;
};

void Context::exdent_hdr() { --hdr_indent; }
void Context::exdent_src() { --src_indent; }
void Context::indent_hdr() { ++hdr_indent; }
void Context::indent_src() { ++src_indent; }

template < typename ... Args > void Context::src_out(std::string_view fmt, Args && ... args) {
  static std::string tmp; // static makes for better memory reuse.
  swoc::bwprintv(tmp, fmt, std::forward_as_tuple(args...));
  this->out(src_file, tmp, src_sol_p, src_indent);
}

template < typename ... Args > void Context::hdr_out(std::string_view fmt, Args && ... args) {
  static std::string tmp; // static makes for better memory reuse.
  swoc::bwprintv(tmp, fmt, std::forward_as_tuple(args...));
  this->out(hdr_file, tmp, hdr_sol_p, hdr_indent);
}

void Context::out(std::ofstream& s, TextView text, bool & sol_p, int indent) {
  while (text) {
    auto n = text.size();
    auto line = text.split_prefix_at('\n');
    if (line.empty() && n > text.size()) {
      s << std::endl;
      sol_p = true;
    } else { // non-empty line
      if (sol_p) {
        for (int i = indent; i > 0; --i) {
          s << "  ";
        }
        sol_p = false;
      }
      if (!line.empty()) {
        s << line << std::endl;
        sol_p = true;
      } else if (!text.empty()) { // no terminal newline, ship it without a newline.
        s << text;
        text.clear();
        sol_p = false;
      }
    }
  }
}

std::string Context::var_name() {
  std::string var;
  swoc::bwprint(var, "node_{}", var_idx++);
  return std::move(var);
}

void Context::emit_min_items_check(std::string_view const& var, uintmax_t limit) {
  src_out("if ({}.size <= {}) return erratum.error(\"Array at line {{}} has only {{}} items instead of the required {} items\", {}.Mark().line, {}.size());", var, limit, limit, var, var);
}

void Context::emit_required_check(YAML::Node const &node, std::string_view const& var) {
  src_out("// check for required tags\nfor ( auto && tag : {{ ");
  TextView delimiter;
  for ( auto && n : node ) {
    src_out(R"non({}"{}")non", delimiter, n.Scalar());
    delimiter.assign(", ");
  }
  src_out(" }} ) {{\n");
  indent_src();
  src_out("if (!{}[tag]) {{\n", var);
  indent_src();
  src_out("return erratum.error(\"Required tag '{{}}' at line {{}} was not found.\", tag, {}.Mark().line);\n", var);
  exdent_src();
  src_out("}}\n");
  exdent_src();
  src_out("}}\n");
}

void Context::emit_type_check(TypeSet const &types, std::string_view const &var) {
  TextView delimiter;

  src_out("// validate value type\n");
  src_out("if (! ");
  if (types.count() == 1) {
    int type;
    while (!types[type]) ++type;
    src_out("{}.{}()) return erratum.error(\"value at line {{}} was not '{}'\", {}.Mark().line);\n", var, SchemaTypeCheck[SchemaType(type)], SchemaTypeName[SchemaType(type)], var);
  } else {
    src_out("(");
    for (auto[value, func] : SchemaTypeCheck) {
      if (types[int(value)]) {
        src_out("{}{}.{}()", delimiter, var, func);
        delimiter.assign(" || ");
      }
    }
    src_out(")) {{\n");
    indent_src();
    src_out("return erratum.error(\"value at line {{}} was not one of the required types ");
    delimiter.clear();
    for (auto[value, name] : SchemaTypeName) {
      if (types[int(value)]) {
        src_out("{}'{}'", delimiter, name);
        delimiter.assign(", ");
      }
    }
    exdent_src();
    src_out("}}\n");
  }
}

// Process a 'type' node.
Errata Context::process_type_value(const YAML::Node &node, TypeSet & types) {
  auto check = [&](YAML::Node const& node) {
    auto & name = node.Scalar();
    auto primitive = SchemaTypeName[name];
    if (SchemaType::INVALID == primitive) {
      notes.error("Type '{}' at line {} is not a valid type. It must be one of {}.",
                  name, node.Mark().line, Valid_Type_List);
   } else if (types[int(primitive)]) {
      notes.warn("Type '{}' in 'type' value at line {} is duplicated.",
                    name, node.Mark().line);
   } else {
      types[int(primitive)] = true;
    }
  };

  if (node.IsScalar()) {
    check(node);
  } else if (node.IsSequence()) {
    for ( auto n : node ) {
      check(n);
    }
  } else {
    return notes.error("'type' value at line {} is neither a string nor a sequence of strings", node.Mark().line);
  }
  return notes;
}

Errata Context::validate_node(YAML::Node const &node, std::string_view const& var) {
  if (node.IsMap()) {
    if (node["$ref"]) {
      auto n { node["$ref"] };
      if (node.size() > 1) {
        notes.warn("Ignoring tags in value at line {} - use of '$ref' tag at line {} requires ignoring all other tags.", node.Mark().line, n.Mark().line);
      }
      if (auto spot = definitions.find(n.Scalar()) ; spot != definitions.end()) {
        src_out("if (! definition::{}(erratum, {}).is_ok()) return notes;\n", spot->second, var);
      } else {
        notes.error("Invalid '$ref' at line {} in value at line {} - '{}' not found.",
                    n.Mark().line, node.Mark().line, n.Scalar());
      }
      return notes;
    }
    TypeSet types;
    if (node["type"]) {
      process_type_value(node["type"], types);
      if (notes.severity() >= Severity::ERROR) return notes;
      emit_type_check(types, var);
    }

    bool single_type_p = types.count() == 1;

    if (types[int(SchemaType::OBJECT)]) { // could be an object.
      bool has_tags_p = std::any_of(ObjectTagName.begin(), ObjectTagName.end(), [&](decltype(ObjectTagName)::Pair const& pair) -> bool { return node[std::get<1>(pair)]; });
      if (!single_type_p && has_tags_p) {
        src_out("if ({}.{}()) {{\n", var, SchemaTypeCheck[SchemaType::OBJECT]);
        indent_src();
      }
      if (node[ObjectTagName[ObjectTag::REQUIRED]]) {
        auto required_node = node[ObjectTagName[ObjectTag::REQUIRED]];
        if (!required_node.IsSequence()) {
          return notes.error("'{}' value at line {} is not type {}.", ObjectTagName[ObjectTag::REQUIRED], required_node.Mark().line, SchemaTypeName[SchemaType::ARRAY]);
        }
        emit_required_check(required_node, var);
      }
      if (node[ObjectTagName[ObjectTag::PROPERTIES]]) {
        auto n_1 = node[ObjectTagName[ObjectTag::PROPERTIES]];
        if (!n_1.IsMap()) {
          return notes.error("'{}' value at line {} is not type {}.",
                             ObjectTagName[ObjectTag::PROPERTIES], n_1.Mark().line,
                             SchemaTypeName[SchemaType::OBJECT]);
        }
        auto nvar = this->var_name();
        for ( auto && pair : n_1 ) {
          src_out("if ({}[\"{}\"]) {{\n", var, pair.first.Scalar());
          indent_src();
          src_out("auto {} = {}[\"{}\"];\n", nvar, var, pair.first.Scalar());
          this->validate_node(pair.second, nvar);
          exdent_src();
          src_out("}}\n");
        }
      }
      if (!single_type_p && has_tags_p) {
        exdent_src();
        src_out("}}\n");
      }
    }

    if (types[int(SchemaType::ARRAY)]) { // could be an array
      unsigned min_items(0), max_items(std::numeric_limits<unsigned>::max());

      bool has_tags_p = std::any_of(ObjectTagName.begin(), ObjectTagName.end(),
                                    [&](decltype(ObjectTagName)::Pair const &pair) -> bool {
                                      return node[std::get<1>(pair)];
                                    });
      if (!single_type_p && has_tags_p) {
        src_out("if ({}.{}()) {{\n", var, SchemaTypeCheck[SchemaType::ARRAY]);
        indent_src();
      }

      if (node[ArrayTagName[ArrayTag::MIN_ITEMS]]) {
        auto n_1 = node[ArrayTagName[ArrayTag::MIN_ITEMS]];
        TextView value = TextView{n_1.Scalar()}.trim_if(&isspace);
        TextView parsed;
        auto x = swoc::svtoi(value, &parsed);
        if (parsed.size() != value.size()) {
          return notes.error(
              "{} value '{}' at line {} for type {} at line {} is invalid - it must be a positive integer.",
              ArrayTagName[ArrayTag::MIN_ITEMS], value, n_1.Mark().line, SchemaTypeName[SchemaType::ARRAY], node.Mark().line);
        }
        emit_min_items_check(var, x);
      }

      if (node[ArrayTagName[ArrayTag::ITEMS]]) {
        auto n_1 = node[ArrayTagName[ArrayTag::ITEMS]];
        if (n_1.IsMap()) {
          auto nvar = var_name();
          src_out("for ( auto && {} : {} ) {{\n", nvar, var);
          indent_src();
          validate_node(n_1, nvar);
          exdent_src();
        } else if (n_1.IsSequence()) {
          auto nvar = var_name();
          auto limit = n_1.size();
          if (limit >= max_items) {
            notes.warn("Type '{}' at line {} has schemas for {} items (line {}) but can have at most {} items (line {}). Extra schemas ignored."
                , SchemaTypeName[SchemaType::ARRAY], node.Mark().line, limit, n_1.Mark().line, max_items, node[ArrayTagName[ArrayTag::MAX_ITEMS]].Mark().line);
            limit = max_items;
          }
          if (limit <= min_items) {
            for (int idx = 0; idx < n_1.size(); ++idx) {
              src_out("{} = {}[{}];\n", nvar, var, idx);
              this->validate_node(n_1[idx], nvar);
            }
          } else {
            src_out("switch ({}.size()) {{\n", var);
            indent_src();
            for (int idx = 0; idx < n_1.size(); ++idx) {
              src_out("case {}: {{\n");
              indent_src();
              src_out("auto {} = {}[{}];\n", nvar, var, idx);
              this->validate_node(n_1[idx], nvar);
              src_out("}}\n");
              exdent_src();
            }
            exdent_src();
            src_out("}}\n");
          }
        } else {
          return notes.error("Invalid value for '{}' at line {}: must be a {} or {}.", ArrayTagName[ArrayTag::ITEMS], n_1.Mark().line, SchemaTypeName[SchemaType::ARRAY], SchemaTypeName[SchemaType::OBJECT]);
        }
      }

      if (!single_type_p && has_tags_p) {
        exdent_src();
        src_out("}}\n");
      }
    }

  } else {
    notes.error("Value at line {} must be a {}.", node.Mark().line, SchemaTypeName[SchemaType::OBJECT]);
  }
  return notes;
}

Errata Context::generate_define(YAML::Node const& key, YAML::Node const& value) {
  std::string name = "v_"; // prefix to avoid reserved word issues.
  name += key.Scalar();
  std::transform(name.begin(), name.end(), name.begin(), [](char c) { return isalnum(c) ? c : '_'; });
  definitions[DEFINITION_PREFIX + key.Scalar()] = name;
  hdr_out("bool {} (swoc::Errata erratum, YAML::Node const& node);\n", name);

  src_out("bool {}::definition::{} (swoc::Errata erratum, YAML::Node const& node) {{\n", class_name, name);
  indent_src();
  validate_node(value, "node");
  exdent_src();
  src_out("}}\n\n");
  return notes;
}

Errata Context::process_definitions(YAML::Node const& node) {
  if (! node.IsMap()) {
    return notes.error("'definitions' node is not a map");
  }
  hdr_out("struct definition {{\n");
  indent_hdr();
  for ( auto && pair: node ) {
    generate_define(pair.first, pair.second);
  }
  exdent_hdr();
  hdr_out("}};\n");
  return notes;
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
    ctx.process_definitions(root["definitions"]);
  }
  return ctx.notes;
}

int main(int argc, char * argv[]) {
  auto result = process(argc, argv);
  for ( auto && note : result ) {
    std::cout << note.text() << std::endl;
  }
}
