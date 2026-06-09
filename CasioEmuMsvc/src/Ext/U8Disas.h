#pragma once

#include <iostream>
#include <string>
#include <map>
extern std::map<int,bool> p_labels;

void decode(std::ostream& out,uint8_t*& buf, uint32_t pc);