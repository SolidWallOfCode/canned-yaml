#include <iostream>
#include <string>

#include "IpAllowSchema.h"

using namespace std;

int main()
{
  YAML::Node config = YAML::LoadFile("./config.json");
  try {
    IpAllowSchema schema;
    bool valid_p = schema(config);
    std::cout << (valid_p ? "Nice job!" : "It's Leif's fault") << std::endl;
    if (!valid_p) {
      std::cout << schema.erratum.count() << " issues" << std::endl;
    }
    for ( auto && note : schema.erratum ) {
      std::cout << note.text() << std::endl;
    }
  } catch (std::invalid_argument& ex) {
    cout << "Failed validation - " << ex.what() << endl;
  }

  return 0;
}
