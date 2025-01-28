#pragma once
#include <filesystem>
#include <functional>
class SystemDialogs {
public:
	static void OpenFileDialog(std::function<void(std::filesystem::path)>);
	static void SaveFileDialog(std::string prefered_name, std::function<void(std::filesystem::path)>);
	static void OpenFolderDialog(std::function<void(std::filesystem::path)>);
	static void SaveFolderDialog(std::function<void(std::filesystem::path)>);
};
