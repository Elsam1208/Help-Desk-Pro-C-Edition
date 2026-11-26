#pragma once
#include <nlohmann/json.hpp>
using json=nlohmann::json;
inline bool valid_ticket(const json& t){
 if(!t.is_object()) return false;
 for(auto [k,n]: {std::pair<const char*,int>{"title",160},{"customer",120},{"description",10000}}){
  if(!t.contains(k)||!t[k].is_string()) return false;
  auto s=t[k].get<std::string>(); if(s.size()>n||s.find_first_not_of(" \t\r\n")==std::string::npos)return false;
 }
 auto p=t.value("priority",json()),s=t.value("status",json());
 return (p=="low"||p=="normal"||p=="high"||p=="urgent")&&(s=="open"||s=="progress"||s=="resolved")&&
 (!t.contains("assigned_to")||t["assigned_to"].is_null()||(t["assigned_to"].is_number_integer()&&t["assigned_to"]>0&&t["assigned_to"]<=2147483647));
}
