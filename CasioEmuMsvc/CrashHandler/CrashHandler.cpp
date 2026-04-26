#ifdef _WIN32
#include <windows.h>

#include <TlHelp32.h>
#include <algorithm>
#include <dbghelp.h>
#include <fstream>
#include <iostream>

#pragma comment(lib, "dbghelp.lib")
#include "Config.hpp"
#ifdef ENABLE_SENTRY
#include "sentry.h"
#endif

static const char* kCrashLockFile = ".crash.switch_renderer";
static void TouchCrashLock() {
	std::ofstream f(kCrashLockFile, std::ios::trunc);
}

void CreateMiniDump(EXCEPTION_POINTERS* pExceptionPointers);
LONG WINAPI CustomUnhandledExceptionFilter(EXCEPTION_POINTERS* pExceptionPointers);
void PrintStackTrace(EXCEPTION_POINTERS* pExceptionPointers);

// ============================================================
// 检测崩溃堆栈中是否涉及 GPU ICD（显卡驱动模块）
// ============================================================
static bool IsGpuDriverInvolved(EXCEPTION_POINTERS* pExceptionPointers) {
	// 常见 GPU ICD / 驱动模块名（小写匹配）
	// NVIDIA: nvoglv32.dll, nvoglv64.dll, nvd3dumx.dll, nvwgf2umx.dll, nvcuda.dll, nvapi64.dll ...
	// AMD/ATI: atioglxx.dll, atig6pxx.dll, atidxx64.dll, amdxc64.dll, amdvlk64.dll ...
	// Intel: ig75icd64.dll, igdumdim64.dll, igd10iumd64.dll, ig9icd64.dll, igc64.dll ...
	// Mesa/Lavapipe/GLon12: opengl32sw.dll, vulkan_lvp.dll, d3d12.dll
	// Generic OpenGL/Vulkan/D3D: opengl32.dll, vulkan-1.dll, d3d11.dll, dxgi.dll
	static const char* kGpuModulePrefixes[] = {
		// NVIDIA
		"nvogl", // nvoglv32.dll, nvoglv64.dll
		"nvd3d", // nvd3dumx.dll, nvd3dum.dll
		"nvwgf", // nvwgf2umx.dll, nvwgf2um.dll
		"nvcuda",
		"nvapi",
		"nvopencl",
		"_nvjitlink",
		// AMD / ATI
		"atiogl", // atioglxx.dll
		"atig",	  // atig6pxx.dll, atig6txx.dll
		"atidxx", // atidxx64.dll, atidxx32.dll
		"amdxc",  // amdxc64.dll
		"amdvlk", // amdvlk64.dll
		"amddxn",
		"amdihk",
		"amdmmcl",
		"amdlvr",
		// Intel
		"ig75icd",
		"ig9icd",
		"igdumd",	 // igdumdim64.dll
		"igd10iumd", // igd10iumd64.dll
		"igd11iumd",
		"igd12iumd",
		"igc32",
		"igc64",
		"igdfcl",
		"intel_gfx",
		// Generic GPU-related system modules
		"opengl32",
		"d3d11",
		"d3d12",
		"d3d10",
		"d3d9",
		"dxgi",
		"vulkan-1",
		"vulkan_lvp",
		"opengl32sw",
	};

	HANDLE hProcess = GetCurrentProcess();
	HANDLE hThread = GetCurrentThread();
	CONTEXT context = *pExceptionPointers->ContextRecord;

	// SymInitialize may already have been called; ignore failure
	SymInitialize(hProcess, NULL, TRUE);

	STACKFRAME64 stackFrame;
	ZeroMemory(&stackFrame, sizeof(STACKFRAME64));

	DWORD machineType;
#ifdef _M_X64
	machineType = IMAGE_FILE_MACHINE_AMD64;
	stackFrame.AddrPC.Offset = context.Rip;
	stackFrame.AddrPC.Mode = AddrModeFlat;
	stackFrame.AddrFrame.Offset = context.Rsp;
	stackFrame.AddrFrame.Mode = AddrModeFlat;
	stackFrame.AddrStack.Offset = context.Rsp;
	stackFrame.AddrStack.Mode = AddrModeFlat;
#elif _M_IX86
	machineType = IMAGE_FILE_MACHINE_I386;
	stackFrame.AddrPC.Offset = context.Eip;
	stackFrame.AddrPC.Mode = AddrModeFlat;
	stackFrame.AddrFrame.Offset = context.Ebp;
	stackFrame.AddrFrame.Mode = AddrModeFlat;
	stackFrame.AddrStack.Offset = context.Esp;
	stackFrame.AddrStack.Mode = AddrModeFlat;
#endif

	bool found = false;

	while (StackWalk64(machineType, hProcess, hThread, &stackFrame,
		&context, NULL, SymFunctionTableAccess64,
		SymGetModuleBase64, NULL)) {
		DWORD64 address = stackFrame.AddrPC.Offset;
		if (address == 0)
			break;

		// 获取该地址所属的模块
		HMODULE hModule = NULL;
		GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)(uintptr_t)address,
			&hModule);

		if (!hModule)
			continue;

		char modulePath[MAX_PATH] = {};
		if (!GetModuleFileNameA(hModule, modulePath, MAX_PATH))
			continue;

		// 提取文件名部分（去掉路径）
		const char* fileName = modulePath;
		const char* lastSlash = strrchr(modulePath, '\\');
		if (lastSlash)
			fileName = lastSlash + 1;
		const char* lastFwdSlash = strrchr(fileName, '/');
		if (lastFwdSlash)
			fileName = lastFwdSlash + 1;

		// 转小写
		char fileNameLower[MAX_PATH] = {};
		for (int i = 0; fileName[i] && i < MAX_PATH - 1; ++i)
			fileNameLower[i] = (char)tolower((unsigned char)fileName[i]);

		// 匹配已知 GPU 驱动前缀
		for (const char* prefix : kGpuModulePrefixes) {
			if (strncmp(fileNameLower, prefix, strlen(prefix)) == 0) {
				found = true;
				std::cerr << "[CrashHandler] GPU driver module detected in stack: "
						  << fileName << "\n";
				break;
			}
		}

		if (found)
			break;
	}

	// 额外检查: 崩溃地址本身所在的模块
	if (!found) {
		DWORD64 faultAddr = (DWORD64)(uintptr_t)pExceptionPointers->ExceptionRecord->ExceptionAddress;
		HMODULE hFaultModule = NULL;
		GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)(uintptr_t)faultAddr,
			&hFaultModule);

		if (hFaultModule) {
			char modulePath[MAX_PATH] = {};
			if (GetModuleFileNameA(hFaultModule, modulePath, MAX_PATH)) {
				const char* fileName = strrchr(modulePath, '\\');
				fileName = fileName ? fileName + 1 : modulePath;

				char fileNameLower[MAX_PATH] = {};
				for (int i = 0; fileName[i] && i < MAX_PATH - 1; ++i)
					fileNameLower[i] = (char)tolower((unsigned char)fileName[i]);

				for (const char* prefix : kGpuModulePrefixes) {
					if (strncmp(fileNameLower, prefix, strlen(prefix)) == 0) {
						found = true;
						std::cerr << "[CrashHandler] GPU driver module at fault address: "
								  << fileName << "\n";
						break;
					}
				}
			}
		}
	}

	SymCleanup(hProcess);
	return found;
}

class GlobalCrashHandler {
public:
	GlobalCrashHandler() {
		AddVectoredExceptionHandler(1, CustomUnhandledExceptionFilter);
		SetUnhandledExceptionFilter(CustomUnhandledExceptionFilter);
	}
} g_crashhandler;

LONG WINAPI CustomUnhandledExceptionFilter(EXCEPTION_POINTERS* pExceptionPointers) {
	if (IsDebuggerPresent())
		return EXCEPTION_CONTINUE_SEARCH;

	auto code = pExceptionPointers->ExceptionRecord->ExceptionCode;
	if (code < 0x80000000)
		return EXCEPTION_CONTINUE_SEARCH;

#ifndef ENABLE_SENTRY
	CreateMiniDump(pExceptionPointers);
#endif

	std::cerr << "\n\n\n!!!\n\nCasioEmuMsvc crashed!\n";
	std::cerr << "Exception code: 0x" << std::hex
			  << pExceptionPointers->ExceptionRecord->ExceptionCode << "\n";
	std::cerr << "Exception address: 0x" << std::hex
			  << pExceptionPointers->ExceptionRecord->ExceptionAddress << "\n";

	PrintStackTrace(pExceptionPointers);

	// ====== 只有当 GPU 驱动参与崩溃时才切换渲染器 ======
	if (IsGpuDriverInvolved(pExceptionPointers)) {
		std::cerr << "[CrashHandler] GPU driver detected in crash stack. "
					 "Will switch renderer on next launch.\n";
		TouchCrashLock();
	}
	else {
		std::cerr << "[CrashHandler] Crash does NOT appear to involve GPU drivers. "
					 "Renderer will NOT be switched.\n";
	}

#ifndef ENABLE_SENTRY
	std::cerr << "Core dumped.\n";
	std::cerr << "Tips: please send me these files: CasioEmuMsvc.exe, "
				 "CasioEmuMsvc.pdb, and the crashdump.dmp.\n";
	std::cerr << "Press any key to close...\n";
#else
	std::cerr << "Error has been reported to developer.\n";
#endif
	std::cerr.flush();

#ifndef ENABLE_SENTRY
	std::cin.get();
	TerminateProcess(GetCurrentProcess(),
		pExceptionPointers->ExceptionRecord->ExceptionCode);
#endif
	return EXCEPTION_CONTINUE_SEARCH;
}

void CreateMiniDump(EXCEPTION_POINTERS* pExceptionPointers) {
	HANDLE hFile = CreateFile(TEXT("crashdump.dmp"), GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile != INVALID_HANDLE_VALUE) {
		MINIDUMP_EXCEPTION_INFORMATION mdei;
		mdei.ThreadId = GetCurrentThreadId();
		mdei.ExceptionPointers = pExceptionPointers;
		mdei.ClientPointers = FALSE;

		MiniDumpWriteDump(
			GetCurrentProcess(), GetCurrentProcessId(), hFile,
			(MINIDUMP_TYPE)(MiniDumpWithFullMemory | MiniDumpScanMemory |
							MiniDumpWithPrivateReadWriteMemory |
							MiniDumpWithCodeSegs | MiniDumpWithModuleHeaders |
							MiniDumpWithProcessThreadData |
							MiniDumpWithHandleData | MiniDumpWithAvxXStateContext |
							MiniDumpWithIptTrace |
							MiniDumpScanInaccessiblePartialPages),
			pExceptionPointers ? &mdei : NULL, NULL, NULL);

		CloseHandle(hFile);
	}
}

void PrintStackTrace(EXCEPTION_POINTERS* pExceptionPointers) {
	HANDLE hProcess = GetCurrentProcess();
	HANDLE hThread = GetCurrentThread();

	CONTEXT context = *pExceptionPointers->ContextRecord;

	SymInitialize(hProcess, NULL, TRUE);

	STACKFRAME64 stackFrame;
	ZeroMemory(&stackFrame, sizeof(STACKFRAME64));

	DWORD machineType = IMAGE_FILE_MACHINE_I386;
#ifdef _M_X64
	machineType = IMAGE_FILE_MACHINE_AMD64;
	stackFrame.AddrPC.Offset = context.Rip;
	stackFrame.AddrPC.Mode = AddrModeFlat;
	stackFrame.AddrFrame.Offset = context.Rsp;
	stackFrame.AddrFrame.Mode = AddrModeFlat;
	stackFrame.AddrStack.Offset = context.Rsp;
	stackFrame.AddrStack.Mode = AddrModeFlat;
#elif _M_IX86
	machineType = IMAGE_FILE_MACHINE_I386;
	stackFrame.AddrPC.Offset = context.Eip;
	stackFrame.AddrPC.Mode = AddrModeFlat;
	stackFrame.AddrFrame.Offset = context.Ebp;
	stackFrame.AddrFrame.Mode = AddrModeFlat;
	stackFrame.AddrStack.Offset = context.Esp;
	stackFrame.AddrStack.Mode = AddrModeFlat;
#endif

	while (StackWalk64(machineType, hProcess, hThread, &stackFrame, &context,
		NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
		DWORD64 address = stackFrame.AddrPC.Offset;

		if (address == 0)
			break;

		DWORD64 displacementSym = 0;
		DWORD64 displacementLine = 0;

		char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
		PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
		pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		pSymbol->MaxNameLen = MAX_SYM_NAME;

		if (SymFromAddr(hProcess, address, &displacementSym, pSymbol)) {
			std::cerr << " Function: " << pSymbol->Name << "(0x" << std::hex
					  << address << ")" << std::dec;
		}
		else {
			std::cerr << std::hex << address << std::dec;
		}

		IMAGEHLP_LINE64 line;
		line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

		if (SymGetLineFromAddr64(hProcess, address, (PDWORD)&displacementLine,
				&line)) {
			std::cerr << " (" << line.FileName << ":" << line.LineNumber << ")";
		}
		std::cerr << '\n';
	}

	SymCleanup(hProcess);
}
#endif