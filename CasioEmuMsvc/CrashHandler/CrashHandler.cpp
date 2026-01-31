#if 0
#ifdef __ANDROID__
#include <SDL.h>
#include <SDL_system.h>
#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <iomanip>
#include <jni.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <unwind.h>
#include <vector>

struct BacktraceState {
	void** current;
	void** end;
};

static _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* context, void* arg) {
	BacktraceState* state = static_cast<BacktraceState*>(arg);
	uintptr_t pc = _Unwind_GetIP(context);
	if (pc) {
		if (state->current == state->end) {
			return _URC_END_OF_STACK;
		}
		else {
			*state->current++ = reinterpret_cast<void*>(pc);
		}
	}
	return _URC_NO_REASON;
}

static void android_signal_handler(int signum, siginfo_t* info, void* context) {
	const size_t max_stack_frames = 64;
	void* buffer[max_stack_frames];

	BacktraceState state = {buffer, buffer + max_stack_frames};
	_Unwind_Backtrace(unwindCallback, &state);
	size_t count = state.current - buffer;

	std::stringstream ss;
	ss << "Signal " << signum << " caught!\n";
	ss << "Fault address: " << info->si_addr << "\n";
	ss << "Stack trace:\n";

	for (size_t i = 0; i < count; ++i) {
		const void* addr = buffer[i];
		const char* symbol = "";
		Dl_info info;
		if (dladdr(addr, &info) && info.dli_sname) {
			symbol = info.dli_sname;
		}
		ss << "#" << std::setw(2) << i << " " << addr << " " << symbol << "\n";
	}

	std::string message = ss.str();

	void* liblog = dlopen("liblog.so", RTLD_NOW);
	if (liblog) {
		typedef int (*p_log_print)(int, const char*, const char*, ...);
		p_log_print log_print = (p_log_print)dlsym(liblog, "__android_log_print");
		if (log_print) {
			log_print(ANDROID_LOG_FATAL, "CasioEmu", "%s", message.c_str());
		}
		dlclose(liblog);
	}
	else {
		fprintf(stderr, "CasioEmu FATAL: %s\n", message.c_str());
	}

	JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
	if (env) {
		jobject activity = (jobject)SDL_AndroidGetActivity();
		if (activity) {
			jclass clazz = env->GetObjectClass(activity);
			if (clazz) {
				jmethodID method = env->GetMethodID(clazz, "onNativeCrash", "(Ljava/lang/String;)V");
				if (method) {
					jstring jMsg = env->NewStringUTF(message.c_str());
					env->CallVoidMethod(activity, method, jMsg);
					env->DeleteLocalRef(jMsg);
				}
				env->DeleteLocalRef(clazz);
			}
		}
	}

	// Hang here so the UI thread can show the dialog
	while (true) {
		sleep(1);
	}
}

class GlobalCrashHandler {
public:
	GlobalCrashHandler() {
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_sigaction = android_signal_handler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_SIGINFO;
		sigaction(SIGSEGV, &sa, nullptr);
		sigaction(SIGFPE, &sa, nullptr);
		sigaction(SIGILL, &sa, nullptr);
		sigaction(SIGABRT, &sa, nullptr);
		sigaction(SIGBUS, &sa, nullptr);
	}
} g_crashhandler;
#endif
#endif

#ifdef _WIN32
#include <windows.h>
#include <TlHelp32.h>
#include <dbghelp.h>
#include <iostream>
#pragma comment(lib, "dbghelp.lib")
#include "Config.hpp"
#ifdef ENABLE_SENTRY
#include "sentry.h"
#endif

void CreateMiniDump(EXCEPTION_POINTERS* pExceptionPointers);
LONG WINAPI CustomUnhandledExceptionFilter(EXCEPTION_POINTERS* pExceptionPointers);
void PrintStackTrace(EXCEPTION_POINTERS* pExceptionPointers);

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
	std::cerr << "Exception code: 0x" << std::hex << pExceptionPointers->ExceptionRecord->ExceptionCode << "\n";
	std::cerr << "Exception address: 0x" << std::hex << pExceptionPointers->ExceptionRecord->ExceptionAddress << "\n";

	PrintStackTrace(pExceptionPointers);
#ifndef ENABLE_SENTRY
	std::cerr << "Core dumped.\n";
	std::cerr << "Tips: please send me these files: CasioEmuMsvc.exe, CasioEmuMsvc.pdb, and the crashdump.dmp.\n";
	std::cerr << "Press any key to close...\n";
#else
	std::cerr << "Error has been reported to developer.\n";
#endif
	std::cerr.flush();
#ifndef ENABLE_SENTRY
	std::cin.get();
	TerminateProcess(GetCurrentProcess(), pExceptionPointers->ExceptionRecord->ExceptionCode);
#endif
	return EXCEPTION_CONTINUE_SEARCH;
}

void CreateMiniDump(EXCEPTION_POINTERS* pExceptionPointers) {
	HANDLE hFile = CreateFile(TEXT("crashdump.dmp"), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile != INVALID_HANDLE_VALUE) {
		MINIDUMP_EXCEPTION_INFORMATION mdei;
		mdei.ThreadId = GetCurrentThreadId();
		mdei.ExceptionPointers = pExceptionPointers;
		mdei.ClientPointers = FALSE;

		MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, (MINIDUMP_TYPE)(MiniDumpWithFullMemory | MiniDumpScanMemory | MiniDumpWithPrivateReadWriteMemory | MiniDumpWithCodeSegs | MiniDumpWithModuleHeaders | MiniDumpWithProcessThreadData | MiniDumpWithHandleData | MiniDumpWithAvxXStateContext | MiniDumpWithIptTrace | MiniDumpScanInaccessiblePartialPages), pExceptionPointers ? &mdei : NULL, NULL, NULL);

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

	while (StackWalk64(machineType, hProcess, hThread, &stackFrame, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
		DWORD64 address = stackFrame.AddrPC.Offset;

		if (address == 0) {
			break;
		}

		DWORD64 displacementSym = 0;
		DWORD64 displacementLine = 0;

		char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
		PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
		pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		pSymbol->MaxNameLen = MAX_SYM_NAME;

		if (SymFromAddr(hProcess, address, &displacementSym, pSymbol)) {
			std::cerr << " Function: " << pSymbol->Name << "(0x" << std::hex << address << ")" << std::dec;
		}
		else {
			std::cerr << std::hex << address << std::dec;
		}

		IMAGEHLP_LINE64 line;
		line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

		if (SymGetLineFromAddr64(hProcess, address, (PDWORD)&displacementLine, &line)) {
			std::cerr << " (" << line.FileName << ":" << line.LineNumber << ")";
		}
		std::cerr << '\n';
	}

	SymCleanup(hProcess);
}
#endif