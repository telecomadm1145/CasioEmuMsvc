#pragma once
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <stringhelper.h>
#include <vector>
struct Label {
	uint32_t address;
	std::string name;
};
// Function to parse the file
inline std::vector<Label> parseFile(const std::string& filename) {
	std::vector<Label> labels;
	std::ifstream file(filename);
	if (!file.is_open()) {
		return labels;
	}

	std::string line;
	bool inBlockComment = false;

	while (std::getline(file, line)) {
		// Trim the line
		line = trim(line);

		// Skip empty lines
		if (line.empty())
			continue;

		// Handle block comments
		if (inBlockComment) {
			if (line.find("*/") != std::string::npos) {
				inBlockComment = false;
			}
			continue;
		}
		else if (line.find("/*") != std::string::npos) {
			if (line.find("*/") == std::string::npos) {
				inBlockComment = true;
			}
			continue;
		}

		// Handle line comments
		size_t commentPos = line.find('#');
		if (commentPos != std::string::npos) {
			line = line.substr(0, commentPos);
			line = trim(line);
			if (line.empty())
				continue;
		}

		// Parse the line
		std::string address;
		std::string functionName;

		size_t commaPos = line.find(',');
		if (commaPos != std::string::npos) {
			// Format: 0x01234,function or 0x01234,"function with space"
			address = line.substr(0, commaPos);
			functionName = line.substr(commaPos + 1);
		}
		else {
			// Format: 01234 function
			std::istringstream iss(line);
			iss >> address;
			std::getline(iss, functionName);
		}

		// Trim extracted strings
		address = trim(address);
		functionName = trim(functionName);

		// Remove quotes from function name if present
		if (!functionName.empty() && functionName.front() == '"') {
			functionName = functionName.substr(1, functionName.size() - 2);
		}

		// Print the result
		if (!address.empty() && !functionName.empty()) {
			auto addr_p = (uint32_t)0;
			if (address.length() > 2) {
				if (address.starts_with("0x")) {
					addr_p = std::strtoul(address.c_str() + 2, 0, 16);
				}
				else {
					addr_p = std::strtoul(address.c_str(), 0, 16);
				}
			}
			else {
				addr_p = std::strtoul(address.c_str(), 0, 16);
			}
			labels.push_back({addr_p, functionName});
		}
	}

	file.close();
	return labels;
}