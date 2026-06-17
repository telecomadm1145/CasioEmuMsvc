#pragma once

#ifdef CASIOEMU_CORE_WEB
const char* WebDebuggerExportDir();
void WebDebuggerQueueDownload(const char* path, const char* name);
void WebDebuggerQueueOpenFile(const char* target_path, const char* name);
bool WebDebuggerConsumeFileResult(const char* path, int* result);
void WebDebuggerRequestFsSync();
void InitWebDebuggerGuiWindows();
void RenderWebDebuggerGuiWindows();
void CleanupWebDebuggerGuiWindows();
#endif
