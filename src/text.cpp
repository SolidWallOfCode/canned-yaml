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
    } catch (YAML::BadConversion&)
    {
        return false;
    }
    return true;
}
bool is_string(YAML::Node const &node, string temp)
{
    try
    {
        // convert
        string tmp = node.as<string>();
        // add validate           
    } catch (YAML::BadConversion&)
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
    } catch (YAML::BadConversion&)
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
    } catch (YAML::BadConversion&)
    {
        return false;
    }
    return true;
}

bool is_double(YAML::Node const &node)
{
    return false;
}

void verify_version(YAML::Node const &node)
{
    try
    {
        if (!node.IsScalar() && !is_string(node))
        {
            throw "version failed not a string";
        }
    } catch (char const* e)
    {
        cout << e;
    }
}
bool verify_range(YAML::Node const &node)
{
    auto count = 0;
    try
    {
        if (is_string(node))
        {
               ++count;
        }
        else if(node.IsSequence() && (node.size() ==2))
        {
            for (auto const i : node)
            {
                if(!is_string(i))
                    throw "invalid type of array in range";
            }
            ++count;
        }
        else
            throw "invalid range value";
        if(count < 1)
            throw "atleast one of values should be present";
    } catch (char const* e)
    {
        cout << e;
        return false;
    }
    return true;
}
bool verify_action(YAML::Node const &node)
{
    try
    {
        if (!is_string(node))
        {
                throw "invalid action type";
        }
        auto key = node.as<string>();
        if(key!="allow" || key!="deny")
            throw "invalid action value";

    } catch (char const* e)
    {
        cout << e;
        return false;
    }
    return true;
}
bool verify_methods(YAML::Node const &node)
{
    auto count = 0;
    try
    {
        if (is_string(node))
        {
            ++count;
        }
        else if (node.IsSequence())
        {
            if(node.size() > 0)
            {
                for (auto const i : node)
                {
                    if(!is_string(i))
                        throw "invalid type of array in methods";
                }
                ++count;
            }
            else
            {   throw "array should contain atl east 1 element";
            }
        }
        else
        {
            throw "invalid method type";
        }

        if(count !=1)
            throw "method should contain only 1";
    } catch (char const* e)
    {
        cout << e;
        return false;
    }
    return true;
}

bool verify_outboundrule(YAML::Node const &node)
{
    auto found_action_outbound = 0;
    try
    {
        if (!node.IsMap())
        {
            throw "outboundrule requires a map";
        }
        for (auto const &n : node)
        {
            auto key = n.first.Scalar();
            YAML::Node value = n.second;

            if (key == "outbound")
            {
                ++found_action_outbound;
                verify_range(value);
            }
            else if (key == "action")
            {
                ++found_action_outbound;
                verify_action(value);
            }
            else if (key == "methods")
            {
                verify_methods(value);
            }
            else
            {
                throw "unrecognised property in outbound";
            }
        }
        if (found_action_outbound < 2)
            throw "action and outbound need to be present";
    } catch (char const* e)
    {
        cout << e;
        return false;
    }
    return true;

}
bool verify_inboundrule(YAML::Node const &node)
{
    auto found_action_inbound = 0;
    try
    {
        if (!node.IsMap())
        {
            throw "inboundrule requires a map";
        }
        for (auto const &n : node)
        {
            auto key = n.first.Scalar();
            YAML::Node value = n.second;

            if (key == "inbound")
            {
                ++found_action_inbound;
                verify_range(value);
            }
            else if (key == "action")
            {
                ++found_action_inbound;
                verify_action(value);
            }
            else if (key == "methods")
            {
                verify_methods(value);
            }
            else
            {
                throw "unrecognised property in inbound";
            }
        }

    if (found_action_inbound < 2)
        throw "action and inbound need to be present";
} catch (char const* e)
{
    cout << e;
    return false;
}
return true;

}
bool verify_rule(YAML::Node const &node)
{
try
{
    if(verify_inboundrule(node))
    {
        return true;
    }
    else if(verify_outboundrule(node))
    {
        return true;
    }
    else
    {
        throw "Not a valid rule";
    }
}
    catch(char const* e)
    {
        cout << e;
        return false;
    }
}
bool verify_ip_addr_acl(YAML::Node const &node)
{
    try
    {
        if (!node.IsSequence())
        {
            throw "ip_addr_acl is not a array";
        }
        for (auto const &n : node)
        {
            if(!verify_rule(n));
            throw "rule failed";
        }
    }
    catch(char const* e)
    {
        cout << e;
        return false;
    }
    return true;
}

bool verify_ip_allow(YAML::Node const &node)
{
    try
    {
        bool found_ip_addr_acl = false;
        for (auto const &n : node)
        {
            auto key = n.first.Scalar();
            YAML::Node value = n.second;

            if(key == "version")
            {
                verify_version(value);
            }
            else if(key == "ip_addr_acl")
            {
                found_ip_addr_acl=true;
                verify_ip_addr_acl(value);
            }
            else
            {
                throw "unknown value";
            }
        }
        if(!found_ip_addr_acl) throw "ip_addr_acl not found";
    }
    catch(char const* e)
    {
        cout << e;
        return false;
    }
    return true;
}

std::string
node_type(YAML::Node const &node)
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
//cout << node_type(config) << true << endl;
//dump_node(config, 1);
try
{
    verify_ip_allow(config);
    throw "hii";
}
catch (char const* e)
{
    cout << e;
}
return 0;
}
