#include "yaml-cpp/yaml.h"

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
