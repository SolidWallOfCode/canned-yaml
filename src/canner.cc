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
#include <tuple>

#include "swoc/TextView.h"
#include "swoc/bwf_base.h"

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

int main(int argc, char *argv[]) {
  int zret;
  int idx;
  std::string header_path;
  std::string source_path;
  std::string class_name;

  while (-1 != (zret = getopt_long(argc, argv, ":", Options.data(), &idx))) {
    switch (zret) {
      case ':' : Error("'{}' requires a value", argv[optind-1]); break;
      case 'h' : header_path = argv[optind-1]; break;
      case 's' : source_path = argv[optind-1]; break;
      case 'c' : class_name = argv[optind-1]; break;
      default:
        Error("Unknown option '{}' - ignored", char(zret), argv[optind-1]);
        break;
    }
  }

  if (optind >= argc) {
    Error("An input schema file is required");
    exit(1);
  }

  return 0;
}
