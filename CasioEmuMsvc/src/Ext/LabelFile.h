#pragma once
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <stringhelper.h>
#include <vector>
#include <algorithm>

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
				if (address.starts_with("0x") || address.starts_with("0X")) {
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

	// Sort labels by address to ensure std::lower_bound works correctly
	std::sort(labels.begin(), labels.end(), [](const Label& a, const Label& b) {
		return a.address < b.address;
	});

	return labels;
}

inline std::string lookup_symbol(uint32_t addr, const std::vector<Label>& labels) {
	if (labels.empty()) {
		char buf[32];
		snprintf(buf, sizeof(buf), "f_%x", addr);
		return buf;
	}

	auto iter = std::lower_bound(labels.begin(), labels.end(), addr,
		[](const Label& label, uint32_t a) { return label.address < a; });

	if (iter == labels.end() || iter->address > addr) {
		if (iter != labels.begin())
			--iter;
		else {
			char buf[32];
			snprintf(buf, sizeof(buf), "f_%x", addr);
			return buf;
		}
	}

	if (addr == iter->address) {
		return iter->name;
	}
	else {
		char buf[32];
		snprintf(buf, sizeof(buf), "%x", addr - iter->address);
		return iter->name + "+" + buf;
	}
}