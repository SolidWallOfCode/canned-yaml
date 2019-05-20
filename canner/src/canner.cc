/** @file

    C++ Code generator for schemas for YAML files.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
 */

#include <array>
#include <bitset>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <limits>
#include <tuple>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "swoc/Errata.h"
#include "swoc/Lexicon.h"
#include "swoc/TextView.h"
#include "swoc/bwf_base.h"
#include "swoc/swoc_file.h"

#include "yaml-cpp/yaml.h"

using swoc::Errata;
using swoc::Severity;
using swoc::TextView;
using swoc::Rv;
using namespace swoc::literals;

namespace std {
template<> class tuple_size<YAML::const_iterator::value_type> : public std::integral_constant<size_t, 2> {};
template<> class tuple_element<0, YAML::const_iterator::value_type> { public: using type = const YAML::Node; };
template<> class tuple_element<1, YAML::const_iterator::value_type> { public: using type = const YAML::Node; };
} // namespace std
template < size_t IDX > YAML::Node const& get(YAML::const_iterator::value_type const& v);
template <> YAML::Node const& get<0>(YAML::const_iterator::value_type  const& v) { return v.first; }
template <> YAML::Node const& get<1>(YAML::const_iterator::value_type  const& v) { return v.second; }

namespace
{
// Standard tags
const std::string REF_KEY{"$ref"};

// Command line options.
std::array<option, 4> Options = {
  {{"hdr", 1, nullptr, 'h'}, {"src", 1, nullptr, 's'}, {"class", 1, nullptr, 'c'}, {nullptr, 0, nullptr, 0}}};

/// JSON Schema types.
enum class SchemaType { NIL, BOOL, OBJECT, ARRAY, NUMBER, INTEGER, STRING, INVALID };
/// Bit set to represent a set of JSON schema types.
using TypeSet = std::bitset<static_cast<size_t>(SchemaType::INVALID)>;

// Error message support - in many cases the list of valid types is needed for an error message,
// so generate it once and store it here.
std::string Valid_Type_Name_List;

// Conversion between schema types and string representations. These are the strings that would be
// in the schema file.
swoc::Lexicon<SchemaType> SchemaTypeLexicon{{
  {SchemaType::NIL, "null"},
  {SchemaType::BOOL, "boolean"},
  {SchemaType::OBJECT, "object"},
  {SchemaType::ARRAY, "array"},
  {SchemaType::NUMBER, "number"},
  {SchemaType::INTEGER, "integer"},
  {SchemaType::STRING, "string"},
}};

// Type check functions. These are hand written and injected en masses in to the generated file.
// This is used to map from a schema type (above) to the appropriate type check function.
std::map<SchemaType, std::string_view> SchemaTypeCheck{{
  {SchemaType::NIL, "is_null_type"},
  {SchemaType::BOOL, "is_bool_type"},
  {SchemaType::OBJECT, "is_object_type"},
  {SchemaType::ARRAY, "is_array_type"},
  {SchemaType::NUMBER, "is_number_type"},
  {SchemaType::INTEGER, "is_integer_type"},
  {SchemaType::STRING, "is_string_type"},
}};

// Supported properties in the schema. All properties should be listed here.
enum class Property {
  TYPE,
  PROPERTIES,
  REQUIRED,
  ITEMS,
  MIN_ITEMS,
  MAX_ITEMS,
  ONE_OF,
  ANY_OF,
  ENUM,
  INVALID,
  // For looping over properties.
  BEGIN = PROPERTIES,
  END   = INVALID
};

// Bit set for set of properties.
using PropertySet = std::bitset<int(Property::INVALID) + 1>;

// Conversion between property type and the in schema string representation.
swoc::Lexicon<Property> PropName{
  {Property::TYPE, "type"},    {Property::PROPERTIES, "properties"}, {Property::REQUIRED, "required"},
  {Property::ITEMS, "items"},  {Property::MIN_ITEMS, "minItems"},    {Property::MAX_ITEMS, "maxItems"},
  {Property::ONE_OF, "oneOf"}, {Property::ANY_OF, "anyOf"},          {Property::ENUM, "enum"}};

// Lists of property names. There should be a list for each primary property, for which the list should
// be those other properties that are valid only for the primary property.
std::array<std::string_view, 2> ObjectPropNames = {{PropName[Property::PROPERTIES], PropName[Property::REQUIRED]}};
std::array<std::string_view, 3> ArrayPropNames  = {
  {PropName[Property::ITEMS], PropName[Property::MIN_ITEMS], PropName[Property::MAX_ITEMS]}};

// File scope initializations.
[[maybe_unused]] bool INITIALIZED = (

  SchemaTypeLexicon.set_default(SchemaType::INVALID).set_default("INVALID"),
  PropName.set_default(Property ::INVALID).set_default("INVALID"),

  // The set of type strings doesn't change, set up a global with the list.
  []() -> void {
    swoc::LocalBufferWriter<1024> w;
    for (auto &&[value, name] : SchemaTypeLexicon) {
      w.print("'{}', ", name);
    }
    w.discard(2);
    Valid_Type_Name_List.assign(w.view());
  }(),
  true);

}; // namespace

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
namespace swoc
{
BufferWriter &
bwformat(BufferWriter &w, const bwf::Spec &spec, const file::path &path)
{
  return bwformat(w, spec, std::string_view{path.c_str()});
}


} // namespace swoc

/// Context carried between the various parsing steps.
/// This maintains the parsing state as the schema is generated.
struct Context {
  YAML::Node root_node;

  std::string hdr_path;   ///< Path to the generated header file.
  std::ofstream hdr_file; ///< File object for the generated header file.
  std::string src_path;   ///< Path to the generated source file.
  std::ofstream src_file; ///< File object for the generated source file.
  std::string class_name; ///< Class name of the generated class.
  Errata notes;           ///< Errors / notes encountered during parsing.

  int _src_indent{0};    ///< Indent level of the generated source file.
  bool _src_sol_p{true}; ///< (at) start of line flag for generated source file.
  int _hdr_indent{0};    ///< Indent level of the header file.
  bool _hdr_sol_p{true}; /// (at) start of line flag for generated header file.

  /// Generated variable name index. This enables generating unique names whenever a local node
  /// variable is required.
  int var_idx{1};

  /// Map of local definition URIs. When a '$ref' is found, this table is consulted to find the
  /// correct validation function to invoke.
  using Definitions = std::unordered_map<std::string, std::string>;
  Definitions definitions;

  /// Allocate a new variable name.
  std::string var_name();

  void indent_src(); ///< Increase the indent level of the generated source file.
  void exdent_src(); ///< Decrease the indent level of the generated source file.
  void indent_hdr(); ///< Increase the indent level of the generated header file.
  void exdent_hdr(); ///< Decrease the indent level of the generated header file.

  Rv<YAML::Node> locate(TextView path);

  // Working methods.
  Errata process_definitions(YAML::Node const& node);
  /// Generate validation logic for a specific node.
  Errata validate_node(YAML::Node const &node, std::string_view const &var);

  /// Process properties. Each function process the value for a specific property and is responsible
  /// for generating the appropriate code or dispatching the appropriate "emit_..." functions.
  Errata process_type_value(const YAML::Node &value, TypeSet &types);
  Errata process_object_value(YAML::Node const &node, std::string_view const &var, TypeSet const &types);
  Errata process_array_value(YAML::Node const &node, std::string_view const &var, TypeSet const &types);
  Errata process_any_of_value(YAML::Node const &node, std::string_view const &var);
  Errata process_one_of_value(YAML::Node const &node, std::string_view const &var);
  Errata process_enum_value(YAML::Node const &node, std::string_view const &var);

  /// Direct code generation. Each "emit_..." function emits validation code for a specific property.
  void emit_type_check(TypeSet const &types, std::string_view const &var);
  void emit_required_check(YAML::Node const &node, std::string_view const &var);
  void emit_min_items_check(std::string_view const &var, uintmax_t limit);
  void emit_max_items_check(std::string_view const &var, uintmax_t limit);

  /// Output. These functions send text to the generated source and header files respectively.
  /// Internally the text is checked for new lines and the approrpriate indentation is applied.
  template <typename... Args> void src_out(std::string_view fmt, Args &&... args);
  template <typename... Args> void hdr_out(std::string_view fmt, Args &&... args);

  /// Internal output functions which does the real work. @c src_out and @c hdr_out are responsible
  /// for passing the appropriate arguments to this method to send the output to the right place.
  void out(std::ofstream &s, TextView text, bool &sol_p, int indent);
};

void
Context::exdent_hdr()
{
  --_hdr_indent;
}
void
Context::exdent_src()
{
  --_src_indent;
}
void
Context::indent_hdr()
{
  ++_hdr_indent;
}
void
Context::indent_src()
{
  ++_src_indent;
}

template <typename... Args>
void
Context::src_out(std::string_view fmt, Args &&... args)
{
  static std::string tmp; // static makes for better memory reuse.
  swoc::bwprint_v(tmp, fmt, std::forward_as_tuple(args...));
  this->out(src_file, tmp, _src_sol_p, _src_indent);
}

template <typename... Args>
void
Context::hdr_out(std::string_view fmt, Args &&... args)
{
  static std::string tmp; // static makes for better memory reuse.
  swoc::bwprint_v(tmp, fmt, std::forward_as_tuple(args...));
  this->out(hdr_file, tmp, _hdr_sol_p, _hdr_indent);
}

void
Context::out(std::ofstream &s, TextView text, bool &sol_p, int indent)
{
  while (text) {
    auto n    = text.size();
    auto line = text.split_prefix_at('\n');
    if (line.empty() && n > text.size()) { // empty line, don't write useless indentation.
      s << std::endl;
      sol_p = true;
    } else { // non-empty line -> one of @a line or @a text has content.
      if (sol_p) {
        for (int i = indent; i > 0; --i) {
          s << "  ";
        }
        sol_p = false;
      }
      if (!line.empty()) { // entire line, ship it and reset the indentation flag.
        s << line << std::endl;
        sol_p = true;
      } else if (!text.empty()) { // no terminal newline, ship it without a
                                  // newline.
        s << text;
        text.clear();
        sol_p = false;
      }
    }
  }
}

std::string
Context::var_name()
{
  std::string var;
  swoc::bwprint(var, "node_{}", var_idx++);
  return std::move(var);
}

void
Context::emit_min_items_check(std::string_view const &var, uintmax_t limit)
{
  src_out("if ({}.size() < {}) {{ erratum.error(\"Array at line {{}} has only "
          "{{}} items instead of the required {} items\", {}.Mark().line, "
          "{}.size()); return false; }}\n",
          var, limit, limit, var, var);
}

void
Context::emit_max_items_check(std::string_view const &var, uintmax_t limit)
{
  src_out("if ({}.size() > {}) {{ erratum.error(\"Array at line {{}} has {{}} "
          "items instead of the maximum {} items\", {}.Mark().line, "
          "{}.size()); return false; }}\n",
          var, limit, limit, var, var);
}

void
Context::emit_required_check(YAML::Node const &node, std::string_view const &var)
{
  src_out("// check for required tags\nfor ( auto && tag : {{ ");
  TextView delimiter;
  for (auto &&n : node) {
    src_out(R"non({}"{}")non", delimiter, n.Scalar());
    delimiter.assign(", ");
  }
  src_out(" }} ) {{\n");
  indent_src();
  src_out("if (!{}[tag]) {{\n", var);
  indent_src();
  src_out("erratum.error(\"Required tag '{{}}' at line {{}} was not found.\", "
          "tag, {}.Mark().line);\nreturn false;\n",
          var);
  exdent_src();
  src_out("}}\n");
  exdent_src();
  src_out("}}\n");
}

void
Context::emit_type_check(TypeSet const &types, std::string_view const &var)
{
  TextView delimiter;

  src_out("// validate value type\n");
  src_out("if (! ");
  if (types.count() == 1) {
    auto &&[value, name] = *std::find_if(SchemaTypeLexicon.begin(), SchemaTypeLexicon.end(),
                                         [&](auto &&v) -> bool { return types[int(std::get<0>(v))]; });
    src_out("{}({})) {{ erratum.error(\"'{{}}' value at line {{}} was not {}\", name, "
            "{}.Mark().line); return false; }}\n",
            SchemaTypeCheck[value], var, name, var);
  } else {
    src_out("(");
    for (auto &&[value, func] : SchemaTypeCheck) {
      if (types[int(value)]) {
        src_out("{}{}({})", delimiter, func, var);
        delimiter.assign(" || ");
      }
    }

    src_out(")) {{\n");
    indent_src();
    src_out("erratum.error(\"value at line {{}} was not one of the "
            "required types ");
    delimiter.clear();
    for (auto [value, name] : SchemaTypeLexicon) {
      if (types[int(value)]) {
        src_out("{}'{}'", delimiter, name);
        delimiter.assign(", ");
      }
    }
    src_out("\");\nreturn false;\n");
    exdent_src();
    src_out("}}\n");
  }
}

// Process a 'type' node.
Errata
Context::process_type_value(const YAML::Node &value, TypeSet &types)
{
  Errata zret;
  auto check = [&](YAML::Node const &node) {
    auto &name     = node.Scalar();
    auto primitive = SchemaTypeLexicon[name];
    if (SchemaType::INVALID == primitive) {
      zret.error("Type value '{}' at line {} is not a valid type. It must be one of {}.", name, value.Mark().line,
                 Valid_Type_Name_List);
    } else if (types[int(primitive)]) {
      zret.warn("Type value '{}' at line {} has already been specified.", name, node.Mark().line);
    } else {
      types[int(primitive)] = true;
    }
  };

  if (value.IsScalar()) {
    check(value);
  } else if (value.IsSequence()) {
    for (auto &&n : value) {
      check(n);
    }
  } else {
    zret.error("Type value at line {} must be a string or array of strings but is not.", value.Mark().line);
  }
  return zret;
}

Errata
Context::process_any_of_value(YAML::Node const &node, std::string_view const &var)
{
  Errata zret;
  if (!node.IsSequence()) {
    return zret.error("'{}' value at line {} is invalid - it must be {} type.", node.Mark().line,
                      SchemaTypeLexicon[SchemaType::ARRAY]);
  } else if (node.size() < 1) {
    zret.warn("'{}' value at line {} has no items - ignored.", PropName[Property::ANY_OF], node.Mark().line);
  } else {
    auto nvar = var_name();
    std::string_view comma;
    src_out("// {}\nswoc::Errata any_of_err;\nstd::array<Validator, {}> "
            "any_of_verify = {{\n",
            PropName[Property::ANY_OF], node.size());
    indent_src();
    for (auto &&schema : node) {
      src_out("[&erratum = any_of_err, name, this] (YAML::Node const& node) -> bool {{\n");
      indent_src();
      auto r = validate_node(schema, "node");
      if (!r.empty()) {
        zret.note(r);
        zret.note(r.severity(), "Processing '{}' value at line '{}'", PropName[Property::ANY_OF], node.Mark().line);
        if (zret.severity() >= Severity::ERROR) {
          return zret;
        }
      }
      src_out("return true;\n");
      exdent_src();
      src_out("}},\n");
    }
    exdent_src();
    src_out("}};\n");
    src_out("if (! std::any_of(any_of_verify.begin(), any_of_verify.end(), "
            "[&] (Validator const& vf) {{ return vf({}); }})) {{\n",
            var);
    indent_src();
    src_out("erratum.note(any_of_err);\nerratum.error(\"Node at line {{}} was "
            "not valid for any of these schemas.\", "
            "{}.Mark().line);\nreturn false;\n",
            var);
    exdent_src();
    src_out("}}\n");
  }
  return zret;
}

Errata
Context::process_one_of_value(YAML::Node const &node, std::string_view const &var)
{
  if (!node.IsSequence()) {
    return notes.error("'{}' value at line {} is invalid - it must be {} type.", node.Mark().line,
                       SchemaTypeLexicon[SchemaType::ARRAY]);
  } else if (node.size() < 1) {
    notes.warn("'{}' value at line {} has no items - ignored.", PropName[Property::ONE_OF], node.Mark().line);
  } else {
    auto nvar = var_name();
    src_out("// {}\nswoc::Errata one_of_err;\nstd::array<Validator, {}> "
            "one_of_verify = {{\n",
            PropName[Property::ONE_OF], node.size());
    indent_src();
    for (auto &&schema : node) {
      src_out("[&erratum = one_of_err, name, this] (YAML::Node const& node) -> bool {{\n");
      indent_src();
      validate_node(schema, "node");
      src_out("return true;\n");
      exdent_src();
      src_out("}},\n");
    }
    exdent_src();
    src_out("}};\n");
    src_out("unsigned one_of_count = 0;\nfor ( auto && vf : one_of_verify "
            ") {{\n");
    indent_src();
    src_out("if (vf({}) && ++one_of_count > 1) {{\n", var);
    indent_src();
    src_out("erratum.error(\"Node at line {{}} was valid for more than one "
            "schema.\", {}.Mark().line);\nreturn false;\n",
            var);
    exdent_src();
    src_out("}}\n");
    exdent_src();
    src_out("}}\n");
    src_out("if (one_of_count != 1) {{\n");
    indent_src();
    src_out("erratum.note(one_of_err);\nerratum.error(\"'{{}}' value at line {{}} "
            "was not valid for any of these schemas.\", name,"
            "{}.Mark().line);\nreturn false;\n",
            var);
    exdent_src();
    src_out("}}\n");
  }
  return notes;
}

Errata
Context::process_enum_value(YAML::Node const &node, std::string_view const &var)
{
  if (!node.IsSequence()) {
    return notes.error("'{}' value at line {} is invalid - it must be {} type.", node.Mark().line,
                       SchemaTypeLexicon[SchemaType::ARRAY]);
  } else if (node.size() < 1) {
    notes.warn("'{}' value at line {} has no items - ignored.", PropName[Property::ENUM], node.Mark().line);
  } else {
    std::string usage;
    static const std::string separator{", "};
    src_out("bool enum_match_p = false;\nfor ( auto && vn : {{ ");
    // Because the enum can be any type, the only reliable approach is to serialized the enum values
    // and reconstitute them in the validator. Need to check if the initializer list reloads every
    // invocation or not - may need to move these to a static list for that reason.
    for (auto &&n : node) {
      YAML::Emitter e;
      e << n;
      src_out("YAML::Load(R\"uthira({})uthira\"), ", e.c_str());
      usage += e.c_str() + separator;
    }
    usage.resize(usage.size() - 2);
    src_out(" }} ) {{\n");
    indent_src();
    src_out("if ( equal(vn, {}) ) {{\n", var);
    indent_src();
    src_out("enum_match_p = true;\nbreak;\n");
    exdent_src();
    src_out("}}\n");
    exdent_src();
    src_out("}}\n");
    src_out("if (!enum_match_p) {{\n");
    indent_src();
    src_out(
      "YAML::Emitter yem;\nyem << {};\nerratum.error(\"'{{}}' value '{{}}' at line {{}} is invalid - it must be one of {{}}.\""
      ", name, yem.c_str(), {}.Mark().line, R\"uthira({})uthira\");\nreturn false;\n",
      var, var, usage);
    exdent_src();
    src_out("}}\n");
  }
  return notes;
}

Errata
Context::process_array_value(YAML::Node const &node, std::string_view const &var, TypeSet const &types)
{
  Errata zret;
  int min_items(0), max_items(std::numeric_limits<int>::max());

  bool single_type_p = types.count() == 1;
  bool has_tags_p =
    std::any_of(ArrayPropNames.begin(), ArrayPropNames.end(), [&](std::string_view const &name) -> bool { return node[name]; });

  // If this value can only be a single type, then all the type checking needed has already been done
  // so don't double up on the same check. Otherwise it may not be an array and the array properties
  // should not be applied.
  if (!single_type_p && has_tags_p) {
    src_out("if ({}({})) {{\n", SchemaTypeCheck[SchemaType::ARRAY], var);
    indent_src();
  }

  if (node[PropName[Property::MIN_ITEMS]]) {
    auto n_1       = node[PropName[Property::MIN_ITEMS]];
    TextView value = TextView{n_1.Scalar()}.trim_if(&isspace);
    TextView parsed;
    min_items = swoc::svtoi(value, &parsed);
    if (parsed.size() != value.size() || min_items < 0) {
      return zret.error("{} value '{}' at line {} for type {} at line {} is invalid - it "
                        "must be a positive integer.",
                        PropName[Property::MIN_ITEMS], value, n_1.Mark().line, SchemaTypeLexicon[SchemaType::ARRAY],
                        node.Mark().line);
    }
    emit_min_items_check(var, min_items);
  }

  if (node[PropName[Property::MAX_ITEMS]]) {
    auto n_1       = node[PropName[Property::MAX_ITEMS]];
    TextView value = TextView{n_1.Scalar()}.trim_if(&isspace);
    TextView parsed;
    max_items = swoc::svtoi(value, &parsed);
    if (parsed.size() != value.size() || max_items < 0) {
      return zret.error("{} value '{}' at line {} for type {} at line {} is invalid - it "
                        "must be a positive integer.",
                        PropName[Property::MAX_ITEMS], value, n_1.Mark().line, SchemaTypeLexicon[SchemaType::ARRAY],
                        node.Mark().line);
    }
    emit_max_items_check(var, max_items);
  }

  if (min_items > max_items) {
    return zret.error("For '{}' value at line {}, the '{}' value at line {} is larger than the '{}' value at line {}.",
                      SchemaTypeLexicon[SchemaType::ARRAY], node.Mark().line, PropName[Property::MIN_ITEMS],
                      node[PropName[Property::MIN_ITEMS]].Mark().line, PropName[Property::MAX_ITEMS],
                      node[PropName[Property::MAX_ITEMS]].Mark().line);
  }

  // Handle the items in the sequence.
  if (auto n_1{node[PropName[Property::ITEMS]]}; n_1) {
    if (n_1.IsMap()) {
      // The type values are objects, so each is a schema desciptor.
      auto nvar = var_name();
      src_out("for ( auto && {} : {} ) {{\n", nvar, var);
      indent_src();
      if (zret.note(validate_node(n_1, nvar)).severity() >= Severity::ERROR) {
        zret.note(zret.severity(), "Failed processing '{}' value for '{}' at line {}.", SchemaTypeLexicon[SchemaType::OBJECT],
                  PropName[Property::TYPE], node.Mark().line);
      }
      exdent_src();
      src_out("}}\n");
    } else if (n_1.IsSequence()) {
      auto nvar  = var_name();
      auto limit = n_1.size();
      if (limit >= max_items) {
        zret.warn("'{}' at line {} has schemas for {} items at line {} but "
                  "was specified to have at most {} items by line {}. Extra schemas ignored.",
                  SchemaTypeLexicon[SchemaType::ARRAY], node.Mark().line, limit, n_1.Mark().line, max_items,
                  node[PropName[Property::MAX_ITEMS]].Mark().line);
        limit = max_items;
      }
      if (limit <= min_items) {
        for (int idx = 0; idx < limit; ++idx) {
          if (zret.note(this->validate_node(n_1[idx], nvar)).severity() >= Severity::ERROR) {
            return zret.note(zret.severity(), "Failed to process item {} in '{}' at line {}.");
          }
        }
      } else {
        src_out("switch ({}.size()) {{\n", var);
        indent_src();
        for (int idx = 0; idx < n_1.size(); ++idx) {
          src_out("case {}: {{\n");
          indent_src();
          src_out("auto {} = {}[{}];\n", nvar, var, idx);
          if (zret.note(validate_node(n_1[idx], nvar)).severity() >= Severity::ERROR) {
            return zret.note(zret.severity(), "Failed to process value {} at line {} for '{}'.", idx, n_1.Mark().line,
                             PropName[Property::TYPE]);
          }
          src_out("}}\n");
          exdent_src();
        }
        exdent_src();
        src_out("}}\n");
      }
    } else {
      return zret.error("Invalid value for '{}' at line {}: must be a {} or {}.", PropName[Property::ITEMS], n_1.Mark().line,
                        SchemaTypeLexicon[SchemaType::ARRAY], SchemaTypeLexicon[SchemaType::OBJECT]);
    }
  }

  if (!single_type_p && has_tags_p) {
    exdent_src();
    src_out("}}\n");
  }

  if (!zret.empty()) {
    zret.note(zret.severity(), "Problems procssing '{}' at line {}", PropName[Property::TYPE], node.Mark().line);
  }
  return zret;
}

Errata
Context::process_object_value(YAML::Node const &node, std::string_view const &var, TypeSet const &types)
{
  bool single_type_p = types.count() == 1;
  bool has_tags_p =
    std::any_of(ObjectPropNames.begin(), ObjectPropNames.end(), [&](std::string_view const &name) -> bool { return node[name]; });
  if (!single_type_p && has_tags_p) {
    src_out("if ({}({})) {{\n", var, SchemaTypeCheck[SchemaType::OBJECT]);
    indent_src();
  }
  if (node[PropName[Property::REQUIRED]]) {
    auto required_node = node[PropName[Property::REQUIRED]];
    if (!required_node.IsSequence()) {
      return notes.error("'{}' value at line {} is not type {}.", PropName[Property::REQUIRED], required_node.Mark().line,
                         SchemaTypeLexicon[SchemaType::ARRAY]);
    }
    emit_required_check(required_node, var);
  }
  if (node[PropName[Property::PROPERTIES]]) {
    auto n_1 = node[PropName[Property::PROPERTIES]];
    if (!n_1.IsMap()) {
      return notes.error("'{}' value at line {} is not type {}.", PropName[Property::PROPERTIES], n_1.Mark().line,
                         SchemaTypeLexicon[SchemaType::OBJECT]);
    }
    auto nvar = this->var_name();
    for (auto &&pair : n_1) {
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
  return notes;
}

Errata
Context::validate_node(YAML::Node const &value, std::string_view const &var)
{
  Errata zret;
  if (!value.IsMap()) {
    return zret.error("Value at line {} must be a {}.", value.Mark().line, SchemaTypeLexicon[SchemaType::OBJECT]);
  }

  if (auto n{value["$ref"]}; n) {
    if (value.size() > 1) {
      zret.warn("Ignoring tags in value at line {} - use of '$ref' tag at "
                "line {} requires ignoring all other tags.",
                value.Mark().line, n.Mark().line);
    }
    if (auto spot = definitions.find(n.Scalar()); spot != definitions.end()) {
      src_out("if (! defun.{}(erratum, {}, name)) return false;\n", spot->second, var);
    } else {
      zret.error("Invalid '$ref' at line {} in value at line {} - '{}' not found.", n.Mark().line, value.Mark().line, n.Scalar());
    }
    return zret;
  }

  TypeSet types;
  if (auto n{value[PropName[Property::TYPE]]}; n) {
    if (zret.note(process_type_value(n, types)).severity() >= Severity::ERROR) {
      return zret.note(zret.severity(), "Unable to process value at line {} for '{}' at line {}", n.Mark().line,
                       PropName[Property::TYPE], value.Mark().line);
    }
    emit_type_check(types, var);
  } else {
    types.set();
  }

  if (types[int(SchemaType::OBJECT)]) { // could be an object.
    if (zret.note(process_object_value(value, var, types)).severity() >= Severity::ERROR) {
      return zret.note(zret.severity(), "Unable to process value at line {} as {}", value.Mark().line,
                       SchemaTypeLexicon[SchemaType::OBJECT]);
    }
  }

  if (types[int(SchemaType::ARRAY)]) { // could be an array
    if (zret.note(process_array_value(value, var, types)).severity() >= Severity::ERROR) {
      return zret.note(zret.severity(), "Unable to process value at line {}", value.Mark().line);
    }
  }

  if (auto n{value[PropName[Property::ANY_OF]]}; n) {
    if (zret.note(process_any_of_value(n, var)).severity() >= Severity::ERROR) {
      return zret;
    }
  }

  if (auto n{value[PropName[Property::ONE_OF]]}; n) {
    if (zret.note(process_one_of_value(n, var)).severity() >= Severity::ERROR) {
      return zret;
    }
  }

  if (auto n{value[PropName[Property::ENUM]]}; n) {
    if (zret.note(process_enum_value(n, var)).severity() >= Severity::ERROR) {
      return zret;
    }
  }

  return zret;
}

Rv<YAML::Node> Context::locate(TextView path) {
  Rv<YAML::Node> zret;
  YAML::Node node { root_node }; // start here?
  TextView location { path }; // save original path.
  while (location) {
    auto elt { location.take_prefix_at('/') };
    if (elt.empty() || elt == "#") {
      node = root_node;
      continue;
    }
    if (node.IsMap()) {
      if ( node[elt] ) {
        node = node[elt];
      } else {
        zret.errata().error(R"("{}" is not in the map {} at {}.)", elt, path.prefix(path.size() - location.size()), node.Mark());
        break;
      }
    } else {
      zret.errata().error(R"("{}" is not a map.)", path.prefix(path.size() - location.size()));
      break;
    }
  };
  if (zret.is_ok()) {
    zret = node;
  }
  return std::move(zret);
}

Errata
Context::process_definitions(YAML::Node const& node) {
  Errata erratum;
  if (node.IsMap()) {
    if (auto ref_node{node[REF_KEY]} ; ref_node) {
      if (auto spot = definitions.find(ref_node.Scalar()) ; spot == definitions.end()) {
        auto def_rv { this->locate(ref_node.Scalar()) };
        if (def_rv.is_ok()) {
          TextView name { ref_node.Scalar() };
          if (name.starts_with("#/")) {
            name.remove_prefix(2);
          }
          std::string defun;
          swoc::bwprint(defun, "v_{}", name);
          std::transform(defun.begin(), defun.end(), defun.begin(), [](char c) { return isalnum(c) ? c : '_'; });
          definitions[ref_node.Scalar()] = defun;
          // Generate any dependent definitions.
          this->process_definitions(def_rv);
          // Generate this definition.
          hdr_out("bool {} (swoc::Errata &erratum, YAML::Node const& node, std::string_view const& name);\n", defun);

          src_out("bool {}::Definitions::{} (swoc::Errata &erratum, YAML::Node const& node, std::string_view const& name) {{\n", class_name,
              defun);
          indent_src();
          erratum = validate_node(def_rv, "node");
          src_out("return true;\n");
          exdent_src();
          src_out("}}\n\n");

          if (!erratum.is_ok()) {
            erratum.info(R"(Failed to generate definition "{}" at {}, used at {})", ref_node.Scalar(), def_rv.result(), ref_node.Mark());
          }
        } else {
          erratum.note(def_rv);
          erratum.error(R"(Unable to find ref "{}" used at {}.)", ref_node.Scalar(), ref_node.Mark());
        }
      } // else it's already been processed.
    } else {
      for ( [[maybe_unused]] auto const& [ key, value ] : node ) {
        erratum.note(this->process_definitions(value));
      }
    }
  } else if (node.IsSequence()) {
    for ( auto const& n : node ) {
      erratum.note(this->process_definitions(n));
    }
  }
  return erratum;
}


Errata
process(int argc, char *argv[])
{
  int zret;
  int idx;
  Context ctx;
  std::string tmp;

  ctx.class_name = "Schema";

  while (-1 != (zret = getopt_long(argc, argv, ":", Options.data(), &idx))) {
    switch (zret) {
    case ':':
      ctx.notes.error("'{}' requires a value", argv[optind - 1]);
      break;
    case 'h':
      ctx.hdr_path = argv[optind - 1];
      break;
    case 's':
      ctx.src_path = argv[optind - 1];
      break;
    case 'c':
      ctx.class_name = argv[optind - 1];
      break;
    default:
      ctx.notes.warn("Unknown option '{}' - ignored", char(zret), argv[optind - 1]);
      break;
    }
  }

  if (!ctx.notes.is_ok()) {
    return ctx.notes;
  }

  if (optind >= argc) {
    return ctx.notes.error("An input schema file is required");
  }

  if (ctx.hdr_path.empty()) {
    if (!ctx.src_path.empty()) {
      swoc::bwprint(ctx.hdr_path, "{}.h", TextView{ctx.src_path}.remove_suffix_at('.'));
    } else if (!ctx.class_name.empty()) {
      swoc::bwprint(ctx.hdr_path, "{}.h", ctx.class_name);
    } else {
      return ctx.notes.error("Unable to determine path for output header file.");
    }
  }

  if (ctx.src_path.empty()) {
    if (!ctx.hdr_path.empty()) {
      swoc::bwprint(ctx.src_path, "{}.cc", TextView{ctx.hdr_path}.remove_suffix_at('.'));
    } else if (!ctx.class_name.empty()) {
      swoc::bwprint(ctx.src_path, "{}.cc", ctx.class_name);
    } else {
      return ctx.notes.error("Unable to determine path for output source file.");
    }
  }

  swoc::file::path schema_path{argv[optind]};
  std::error_code ec;
  std::string content = swoc::file::load(schema_path, ec);

  ctx.notes.info("Loaded schema file '{}' - {} bytes", schema_path, content.size());

  YAML::Node root;
  try {
    root = YAML::Load(content);
  } catch (std::exception &ex) {
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

  ctx.src_out("#include <functional>\n#include <array>\n#include "
              "<algorithm>\n#include <iostream>\n\n"
              "#include \"{}\"\n\n"
              "using Validator = std::function<bool (YAML::Node const&)>;\n",
              ctx.hdr_path);

  ctx.hdr_out("#include <string_view>\n\n#include \"swoc/Errata.h\"\n#include \"yaml-cpp/yaml.h\"\n\n");
  ctx.hdr_out("class {} {{\npublic:\n", ctx.class_name);
  ctx.indent_hdr();
  ctx.hdr_out("swoc::Errata erratum;\n");
  ctx.hdr_out("bool operator()(const YAML::Node &n);\n\n", ctx.class_name);

  // These are hand rolled functions used by the generated code.
  ctx.src_file << (R"racecar(
namespace {

bool
equal(const YAML::Node &lhs, const YAML::Node &rhs)
{
  if (lhs.Type() == rhs.Type()) {
    if (lhs.IsSequence()) {
      if (lhs.size() != rhs.size()) {
        return false;
      }
      for (int i = 0, n = lhs.size(); i < n; ++i) {
        if (!equal(lhs[i], rhs[i])) {
          return false;
        }
        return true;
      }
    } else if (lhs.IsMap()) {
      if (lhs.size() != rhs.size()) {
        return false;
      }
      for (const auto &pair : lhs) {
        auto key   = pair.first;
        auto value = pair.second;
        if (!rhs[key] || !equal(value, rhs[key])) {
          return false;
        }
        return true;
      }
    } else {
      return lhs.Scalar() == rhs.Scalar();
    }
  }
  return false;
}

bool is_null_type(YAML::Node const& node) {
  return node.IsNull();
}

bool is_bool_type(YAML::Node const& node) {
  if (node.IsScalar()) {
    auto && value { node.Scalar() };
    return 0 == strcasecmp("true", value) || 0 == strcasecmp("false", value);
  }
  return false;
}

bool is_array_type(YAML::Node const& node) {
  return node.IsSequence();
}

bool is_object_type(YAML::Node const& node) {
  return node.IsMap();
}

bool is_integer_type(YAML::Node const& node) {
  if (node.IsScalar()) {
    swoc::TextView value { node.Scalar() };
    swoc::TextView parsed;
    if (value.trim_if(&isspace).size() < 1) {
      return false;
    }
    swoc::svtoi(value, &parsed);
    return value.size() == parsed.size();
  }
  return false;
}

bool is_string_type(YAML::Node const& node) {
  return node.IsScalar();
}

} // namespace

)racecar");

  ctx.process_definitions(root);
  ctx.exdent_hdr();
  ctx.hdr_out("}};\n");

  ctx.src_out("bool {}::operator()(YAML::Node const& node) {{\n", ctx.class_name);
  ctx.indent_src();
  ctx.src_out("static constexpr std::string_view name {{\"root\"}};\n");
  ctx.src_out("erratum.clear();\n\n");
  ctx.validate_node(root, "node");
  ctx.src_out("\nreturn erratum.severity() < swoc::Severity::ERROR;\n");
  ctx.exdent_src();
  ctx.src_out("}}\n");

  return ctx.notes;
}

int
main(int argc, char *argv[])
{
  auto result = process(argc, argv);
  for (auto &&note : result) {
    std::cout << note.text() << std::endl;
  }
  return result.severity() >= Severity::ERROR;
}
