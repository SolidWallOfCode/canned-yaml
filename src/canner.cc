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

#include "swoc/TextView.h"
#include "swoc/bwf_base.h"
#include "swoc/swoc_file.h"
#include "swoc/Errata.h"

#include "yaml-cpp/yaml.h"

namespace {
std::array<option, 4> Options = {{
                                     {"hdr", 1, nullptr, 'h'},
                                     {"src", 1, nullptr, 's'},
                                     {"class", 1, nullptr, 'c'},
                                     {nullptr, 0, nullptr, 0}
                                 }};

template<typename ... Args>
void Error(const swoc::TextView &fmt, Args &&... args) {
  std::string text;
  swoc::bwprintv(text, fmt, std::forward_as_tuple(args...));
  std::cerr << text << std::endl;
}

}

// BufferWriter formatting for specific types.
namespace swoc {
BufferWriter & bwformat(BufferWriter & w, const bwf::Spec & spec, const file::path & path) {
  return bwformat(w, spec, std::string_view{path.c_str()});
}
}

using swoc::Severity;
using swoc::Errata;
using swoc::TextView;

Errata process(int argc, char *argv[]) {
  swoc::Errata notes;
  int zret;
  int idx;
  std::string hdr_path;
  std::string src_path;
  std::string class_name;
  std::string tmp;

  while (-1 != (zret = getopt_long(argc, argv, ":", Options.data(), &idx))) {
    switch (zret) {
      case ':' : Error("'{}' requires a value", argv[optind-1]); break;
      case 'h' : hdr_path = argv[optind-1]; break;
      case 's' : src_path = argv[optind-1]; break;
      case 'c' : class_name = argv[optind-1]; break;
      default:
        notes.note(Severity::ERROR, "Unknown option '{}' - ignored", char(zret), argv[optind-1]);
        break;
    }
  }

  if (optind >= argc) {
    return notes.error("An input schema file is required");
  }

  swoc::file::path schema_path{argv[optind]};
  std::error_code ec;
  std::string content = swoc::file::load(schema_path, ec);

  notes.info("Loaded schema file '{}' - {} bytes", schema_path, content.size());

  YAML::Node root;
  try {
    root = YAML::Load(content);
  } catch (std::exception & ex) {
    return notes.error("Loading failed: {}", ex.what());
  }

  std::ofstream hdr_file;
  std::ofstream src_file;
  hdr_file.open(hdr_path.c_str(), std::ofstream::trunc);
  if (!hdr_file.is_open()) {
    return notes.error("Failed to open header output file '{}'", hdr_path);
  }
  src_file.open(src_path.c_str(), std::ofstream::trunc);
  if (!src_file.is_open()) {
    return notes.error("Failed to open source output file '{}'", src_path);
  }

  if (!root.IsMap()) {
    return notes.error("Root node must be a map");
  }

  swoc::bwprint(tmp, "#include <functional>\n#include <array>\n#include <algorithm>\n#include <iostream>\n\n"
              "#include \"{}\"\n\n"
              "using Validator = std::function<bool (const YAML::Node &)>;\n\n"
              "extern bool equal(const YAML::Node &, const YAML::Node &);\n\n", hdr_path);
  src_file << tmp;
  swoc::bwprint(tmp, "#include <string_view>\n#include \"yaml-cpp/yaml.h\"\n\n"
                     "class {} {{\npublic:\n  bool operator()(const YAML::Node &n);\n", class_name);
  hdr_file << tmp;

  return notes;
}

int main(int argc, char * argv[]) {
  auto result = process(argc, argv);
}
