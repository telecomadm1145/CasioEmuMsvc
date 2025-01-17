#pragma once
#include <filesystem>
class SystemDialogs {
public:
	static std::filesystem::path OpenFileDialog();
	static std::filesystem::path SaveFileDialog(std::string prefered_name);
	static std::filesystem::path OpenFolderDialog();
	static std::filesystem::path SaveFolderDialog();
};