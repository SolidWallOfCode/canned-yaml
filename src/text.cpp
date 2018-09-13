#include "yaml-cpp/yaml.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

using namespace std;

bool is_string(YAML::Node const &node)
{
    try
    {
        // convert
        string tmp = node.as<string>();
        // add validate
    }
    catch (YAML::BadConversion)
    {
        return false;
    }
    return true;
}

bool is_bool(YAML::Node const &node)
{
    try
    {
        // convert
        bool tmp = node.as<bool>();
    }
    catch (YAML::BadConversion)
    {
        return false;
    }
    return true;
}

bool is_int(YAML::Node const &node)
{
    try
    {
        // convert
        int tmp = node.as<int>();
    }
    catch (YAML::BadConversion)
    {
        return false;
    }
    return true;
}

bool is_double(YAML::Node const &node)
{
}

void verify_version(YAML::Node const &node)
{
    if (!node.IsScalar() && !is_string(node))
    {
        throw "failed";
    }
}

void verify_ip_addr_acl(YAML::Node const &node)
{

    if (!node.IsSequence())
    {
        throw "failed";
    }
    for (auto const &n : node)
    {
        verify_rule(n);
    }
}

bool verify_ip_allow(YAML::Node const &node)
{
    try {
    bool found_ip_addr_acl = false;
    for (auto const &n : node)
    {
        string key = n.first;
        YAML::Node value = n.second;

        if(key == "version"){
            verify_version(value);
        }
        else if(key == "ip_addr_acl"){
            found_ip_addr_acl=true;
            verify_ip_addr_acl(value);
        }
        else{
            throw "unknown value";
        }
    }
    if(!found_ip_addr_acl) throw "ip_addr_acl not found";
    }
    catch(...){
        return false;
    }
    return true;
}

std::string node_type(YAML::Node const &node)
{

    switch (node.Type())
    {
    case (YAML::NodeType::Null):
        return "NULL";
    case (YAML::NodeType::Undefined):
        return "Undefined";
    case (YAML::NodeType::Scalar):
        return "Scalar";
    case (YAML::NodeType::Sequence):
        return "Sequence";
    case (YAML::NodeType::Map):
        return "Map";
    }
    return "Unknown";
}

void dump_node(YAML::Node node, int indent)
{
    for (auto n : node)
    {
        cout << n.first << " " << node_type(n.second) << " " << std::boolalpha << n.second.as<int>() << endl;
        break;
    }
}

int main()
{
    YAML::Node config = YAML::LoadFile("./config.json");
    cout << node_type(config) << true << endl;
    dump_node(config, 1);

    verify_ip_allow(config);

    return 0;
}

Message Input