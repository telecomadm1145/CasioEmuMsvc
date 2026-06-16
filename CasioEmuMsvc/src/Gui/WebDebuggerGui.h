#pragma once

#ifdef CASIOEMU_CORE_WEB_GUI
const char* WebDebuggerLabelsPath();
const char* WebDebuggerExportDir();
void WebDebuggerQueueDownload(const char* path, const char* name);
void InitWebDebuggerGuiWindows();
void RenderWebDebuggerGuiWindows();
void CleanupWebDebuggerGuiWindows();
#endif
