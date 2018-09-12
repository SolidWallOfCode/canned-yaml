#include "yaml-cpp/yaml.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

bool def_version(YAML::Node ip)
{
    std::cout<<"reached here";
    if(!ip.IsSequence())
    {
        std::cout<<"ip_addr_acl defined is not an array";
        return false;
    }
    
    if(!def_rules(ip))
    {   
         std::cout<<"ip_addr_acl doesn't staisfy rule";
         return false;
    }
    return true;
}

bool def_ip_addr_acl(YAML::Node ip)
{
    std::cout<<"reached here";
    if(!ip.IsSequence())
    {
        std::cout<<"ip_addr_acl defined is not an array";
        return false;
    }
    
    if(!def_rules(ip))
    {   
         std::cout<<"ip_addr_acl doesn't staisfy rule";
         return false;
    }
    return true;
}

bool def_inbound(YAML::Node ip)
{
  
    std::cout<<"\n reached inbound";
   /* if(ip.IsSequence())
    {
        std::cout<<"\n ip_addr_acl an array";
    }
    if(ip.IsMap())
        {
        std::cout<<"\n ip_addr_acl an map";
        }
    if(ip.IsScalar())
          {
          std::cout<<"\n ip_addr_acl SCALAR";
          }
    if(ip.IsDefined())
              {
              std::cout<<"\n ip_addr_acl def";
              }*/
    return true;
}
bool def_outbound(YAML::Node ip)
{
  
    std::cout<<"\n reached outbound";
   /* if(ip.IsSequence())
    {
        std::cout<<"\n ip_addr_acl an array";
    }
    if(ip.IsMap())
        {
        std::cout<<"\n ip_addr_acl an map";
        }
    if(ip.IsScalar())
          {
          std::cout<<"\n ip_addr_acl SCALAR";
          }
    if(ip.IsDefined())
              {
              std::cout<<"\n ip_addr_acl def";
              }*/
    return true;
}
bool def_action(YAML::Node ip)
{
  
    std::cout<<"\n reached action";
 
    return true;
}
bool def_methods(YAML::Node ip)
{
  
    std::cout<<"\n reached methods";
 
    return true;
}
int main()
{
    YAML::Node config = YAML::LoadFile("./config.json");
    if(config["ip_addr_acl"])
    {
        std::cout<<"ip_addr_acl\n";
        def_ip_addr_acl(config["ip_addr_acl"]);
    }
    else
    {
       std::cout<<"ip_addr_acl is missing";
       return 0;
    }
 
    for (auto dir : config["ip_addr_acl"])
    {
        if(dir["inbound"]) {
	    def_inbound(dir["inbound"]);
	}
        if(dir["outbound"]){
	    def_outbound(dir["outbound"]);
	}
        if(dir["action"]){
	    def_action(dir["action"]);}
         if(dir["methods"]){
	  def_methods(dir["methods"]);} 
    }
            
           // std::cout<<config["ip_addr_acl"];

    //    YAML::Node config = YAML::LoadFile("./config.yaml");

return 0;
}
/*

   /*YAML::Node test = (config["ip_addr_acl"]);
    if(test["inbound"])
    {
        std::cout<<"SEEEEEEEEEEEEEEEEEEEEEEEEEE:WQ:wqe\n";
        def_ip_addr_acl(config["ip_addr_acl"]);
    }*
            std::cout<<"SSSSS:wqe\n";
*/