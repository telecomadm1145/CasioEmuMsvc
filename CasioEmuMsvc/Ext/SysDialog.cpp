#include "SysDialog.h"

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#include <filesystem>
#include <functional>

void SystemDialogs::OpenFileDialog(std::function<void(std::filesystem::path)> callback) {
	IFileDialog* pfd;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&pfd));
	if (SUCCEEDED(hr)) {
		// 设置选项
		DWORD dwOptions;
		if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
			pfd->SetOptions(dwOptions | FOS_FORCEFILESYSTEM);
		}

		// 设置文件类型过滤器
		COMDLG_FILTERSPEC rgSpec[] = {
			{L"All Files", L"*.*"}};
		pfd->SetFileTypes(1, rgSpec);

		// 显示对话框
		if (SUCCEEDED(pfd->Show(NULL))) {
			IShellItem* psi;
			if (SUCCEEDED(pfd->GetResult(&psi))) {
				PWSTR path;
				if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
					callback(std::filesystem::path(path));
					CoTaskMemFree(path);
				}
				psi->Release();
			}
		}
		pfd->Release();
	}
}

void SystemDialogs::SaveFileDialog(std::string preferred_name, std::function<void(std::filesystem::path)> callback) {
	IFileSaveDialog* pfd;
	HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&pfd));
	if (SUCCEEDED(hr)) {
		// 设置选项
		DWORD dwOptions;
		if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
			pfd->SetOptions(dwOptions | FOS_FORCEFILESYSTEM);
		}

		// 设置默认文件名
		if (!preferred_name.empty()) {
			int needed = MultiByteToWideChar(CP_UTF8, 0, preferred_name.c_str(), -1, NULL, 0);
			if (needed > 0) {
				std::wstring wname(needed - 1, 0);
				MultiByteToWideChar(CP_UTF8, 0, preferred_name.c_str(), -1, wname.data(), needed);
				pfd->SetFileName(wname.c_str());
			}
		}

		// 设置文件类型过滤器
		COMDLG_FILTERSPEC rgSpec[] = {
			{L"All Files", L"*.*"}};
		pfd->SetFileTypes(1, rgSpec);

		// 显示对话框
		if (SUCCEEDED(pfd->Show(NULL))) {
			IShellItem* psi;
			if (SUCCEEDED(pfd->GetResult(&psi))) {
				PWSTR path;
				if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
					callback(std::filesystem::path(path));
					CoTaskMemFree(path);
				}
				psi->Release();
			}
		}
		pfd->Release();
	}
}

void SystemDialogs::OpenFolderDialog(std::function<void(std::filesystem::path)> callback) {
	IFileDialog* pfd;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&pfd));
	if (SUCCEEDED(hr)) {
		DWORD dwOptions;
		if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
			pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
		}

		if (SUCCEEDED(pfd->Show(NULL))) {
			IShellItem* psi;
			if (SUCCEEDED(pfd->GetResult(&psi))) {
				PWSTR path;
				if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
					callback(std::filesystem::path(path));
					CoTaskMemFree(path);
				}
				psi->Release();
			}
		}
		pfd->Release();
	}
}

void SystemDialogs::SaveFolderDialog(std::function<void(std::filesystem::path)> callback) {
	IFileDialog* pfd;
	HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&pfd));
	if (SUCCEEDED(hr)) {
		DWORD dwOptions;
		if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
			pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
		}

		if (SUCCEEDED(pfd->Show(NULL))) {
			IShellItem* psi;
			if (SUCCEEDED(pfd->GetResult(&psi))) {
				PWSTR path;
				if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
					callback(std::filesystem::path(path));
					CoTaskMemFree(path);
				}
				psi->Release();
			}
		}
		pfd->Release();
	}
}
#endif
