#include <iostream>
#include <string>

#include "IpAllowSchema.h"

using namespace std;

int main()
{
  YAML::Node config = YAML::LoadFile("./config.json");
  try {
    bool valid_p = IpAllowSchema()(config);
    cout << (valid_p ? "Nice job!" : "Invisible error") << endl;
  } catch (std::invalid_argument& ex) {
    cout << "Failed validation - " << ex.what() << endl;
  }

  return 0;
}
