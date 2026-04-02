// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include <Python.h>
#include <sstream>

// Global variables
PluginApi* g_api = nullptr;
ICPU* g_cpu = nullptr;
IMMU* g_mmu = nullptr;
IChipset* g_chipset = nullptr;
IEmulator* g_emulator = nullptr;
Hooks* g_hooks = nullptr;
IKeyboard* g_keyboard = nullptr;

// Callback storage
struct CallbackStorage {
	PyObject* onInstruction{ nullptr };
	PyObject* onCall{ nullptr };
	PyObject* onReturn{ nullptr };
	PyObject* onMemoryRead{ nullptr };
	PyObject* onMemoryWrite{ nullptr };
	PyObject* onBrk{ nullptr };
	PyObject* onInterrupt{ nullptr };
	PyObject* onReset{ nullptr };
};

CallbackStorage g_callbacks;

// Helper to safely call Python callbacks
bool CallPythonCallback(PyObject* callback, PyObject* args) {
	if (!callback || !PyCallable_Check(callback)) {
		return false;
	}

	PyObject* result = PyObject_CallObject(callback, args);
	if (result) {
		Py_DECREF(result);
		return true;
	}
	else {
		PyErr_Print();
		return false;
	}
}

// Hook handlers
void OnInstructionHandler(InstructionEventArgs& args) {
	if (g_callbacks.onInstruction) {
		PyObject* arglist = Py_BuildValue("(II)", args.pc_before, args.pc_after);
		if (CallPythonCallback(g_callbacks.onInstruction, arglist)) {
			// Check if Python wants to break
			PyObject* should_break = PyObject_GetAttrString(arglist, "should_break");
			if (should_break && PyBool_Check(should_break)) {
				args.should_break = (should_break == Py_True);
			}
			Py_XDECREF(should_break);
		}
		Py_XDECREF(arglist);
	}
}

void OnCallHandler(const FunctionEventArgs& args) {
	if (g_callbacks.onCall) {
		PyObject* arglist = Py_BuildValue("(II)", args.pc, args.lr);
		CallPythonCallback(g_callbacks.onCall, arglist);
		Py_XDECREF(arglist);
	}
}

void OnReturnHandler(const FunctionEventArgs& args) {
	if (g_callbacks.onReturn) {
		PyObject* arglist = Py_BuildValue("(II)", args.pc, args.lr);
		CallPythonCallback(g_callbacks.onReturn, arglist);
		Py_XDECREF(arglist);
	}
}

void OnMemoryReadHandler(MemoryEventArgs& args) {
	if (g_callbacks.onMemoryRead) {
		PyObject* arglist = Py_BuildValue("(IB)", args.offset, args.value);
		if (CallPythonCallback(g_callbacks.onMemoryRead, arglist)) {
			// Check if Python handled the read
			PyObject* handled = PyObject_GetAttrString(arglist, "handled");
			if (handled && PyBool_Check(handled)) {
				args.handled = (handled == Py_True);
			}
			Py_XDECREF(handled);
		}
		Py_XDECREF(arglist);
	}
}

void OnMemoryWriteHandler(MemoryEventArgs& args) {
	if (g_callbacks.onMemoryWrite) {
		PyObject* arglist = Py_BuildValue("(IB)", args.offset, args.value);
		if (CallPythonCallback(g_callbacks.onMemoryWrite, arglist)) {
			// Check if Python handled the write
			PyObject* handled = PyObject_GetAttrString(arglist, "handled");
			if (handled && PyBool_Check(handled)) {
				args.handled = (handled == Py_True);
			}
			Py_XDECREF(handled);
		}
		Py_XDECREF(arglist);
	}
}

void OnBrkHandler(InterruptEventArgs& args) {
	if (g_callbacks.onBrk) {
		PyObject* arglist = Py_BuildValue("(B)", args.index);
		if (CallPythonCallback(g_callbacks.onBrk, arglist)) {
			// Check if Python handled the BRK
			PyObject* handled = PyObject_GetAttrString(arglist, "handled");
			if (handled && PyBool_Check(handled)) {
				args.handled = (handled == Py_True);
			}
			Py_XDECREF(handled);
		}
		Py_XDECREF(arglist);
	}
}

void OnInterruptHandler(InterruptEventArgs& args) {
	if (g_callbacks.onInterrupt) {
		PyObject* arglist = Py_BuildValue("(B)", args.index);
		if (CallPythonCallback(g_callbacks.onInterrupt, arglist)) {
			// Check if Python handled the interrupt
			PyObject* handled = PyObject_GetAttrString(arglist, "handled");
			if (handled && PyBool_Check(handled)) {
				args.handled = (handled == Py_True);
			}
			Py_XDECREF(handled);
		}
		Py_XDECREF(arglist);
	}
}

void OnResetHandler() {
	if (g_callbacks.onReset) {
		CallPythonCallback(g_callbacks.onReset, PyTuple_New(0));
	}
}

// Python callback registration methods
static PyObject* py_set_instruction_callback(PyObject* self, PyObject* args) {
	PyObject* callback;
	if (!PyArg_ParseTuple(args, "O", &callback))
		return nullptr;

	if (!PyCallable_Check(callback)) {
		PyErr_SetString(PyExc_TypeError, "Parameter must be callable");
		return nullptr;
	}

	Py_XDECREF(g_callbacks.onInstruction);
	Py_INCREF(callback);
	g_callbacks.onInstruction = callback;

	Py_RETURN_NONE;
}

static PyObject* py_set_call_callback(PyObject* self, PyObject* args) {
	PyObject* callback;
	if (!PyArg_ParseTuple(args, "O", &callback))
		return nullptr;

	if (!PyCallable_Check(callback)) {
		PyErr_SetString(PyExc_TypeError, "Parameter must be callable");
		return nullptr;
	}

	Py_XDECREF(g_callbacks.onCall);
	Py_INCREF(callback);
	g_callbacks.onCall = callback;

	Py_RETURN_NONE;
}

static PyObject* py_set_return_callback(PyObject* self, PyObject* args) {
	PyObject* callback;
	if (!PyArg_ParseTuple(args, "O", &callback))
		return nullptr;

	if (!PyCallable_Check(callback)) {
		PyErr_SetString(PyExc_TypeError, "Parameter must be callable");
		return nullptr;
	}

	Py_XDECREF(g_callbacks.onReturn);
	Py_INCREF(callback);
	g_callbacks.onReturn = callback;

	Py_RETURN_NONE;
}

static PyObject* py_set_memory_read_callback(PyObject* self, PyObject* args) {
	PyObject* callback;
	if (!PyArg_ParseTuple(args, "O", &callback))
		return nullptr;

	if (!PyCallable_Check(callback)) {
		PyErr_SetString(PyExc_TypeError, "Parameter must be callable");
		return nullptr;
	}

	Py_XDECREF(g_callbacks.onMemoryRead);
	Py_INCREF(callback);
	g_callbacks.onMemoryRead = callback;

	Py_RETURN_NONE;
}

static PyObject* py_set_memory_write_callback(PyObject* self, PyObject* args) {
	PyObject* callback;
	if (!PyArg_ParseTuple(args, "O", &callback))
		return nullptr;

	if (!PyCallable_Check(callback)) {
		PyErr_SetString(PyExc_TypeError, "Parameter must be callable");
		return nullptr;
	}

	Py_XDECREF(g_callbacks.onMemoryWrite);
	Py_INCREF(callback);
	g_callbacks.onMemoryWrite = callback;

	Py_RETURN_NONE;
}

static PyObject* py_set_brk_callback(PyObject* self, PyObject* args) {
	PyObject* callback;
	if (!PyArg_ParseTuple(args, "O", &callback))
		return nullptr;

	if (!PyCallable_Check(callback)) {
		PyErr_SetString(PyExc_TypeError, "Parameter must be callable");
		return nullptr;
	}

	Py_XDECREF(g_callbacks.onBrk);
	Py_INCREF(callback);
	g_callbacks.onBrk = callback;

	Py_RETURN_NONE;
}

static PyObject* py_set_interrupt_callback(PyObject* self, PyObject* args) {
	PyObject* callback;
	if (!PyArg_ParseTuple(args, "O", &callback))
		return nullptr;

	if (!PyCallable_Check(callback)) {
		PyErr_SetString(PyExc_TypeError, "Parameter must be callable");
		return nullptr;
	}

	Py_XDECREF(g_callbacks.onInterrupt);
	Py_INCREF(callback);
	g_callbacks.onInterrupt = callback;

	Py_RETURN_NONE;
}

static PyObject* py_set_reset_callback(PyObject* self, PyObject* args) {
	PyObject* callback;
	if (!PyArg_ParseTuple(args, "O", &callback))
		return nullptr;

	if (!PyCallable_Check(callback)) {
		PyErr_SetString(PyExc_TypeError, "Parameter must be callable");
		return nullptr;
	}

	Py_XDECREF(g_callbacks.onReset);
	Py_INCREF(callback);
	g_callbacks.onReset = callback;

	Py_RETURN_NONE;
}
// Python module methods
static PyObject* py_read_memory(PyObject* self, PyObject* args) {
	unsigned int address;
	if (!PyArg_ParseTuple(args, "I", &address))
		return nullptr;

	uint8_t value = g_mmu->ReadData(address);
	return PyLong_FromLong(value);
}

static PyObject* py_write_memory(PyObject* self, PyObject* args) {
	unsigned int address;
	unsigned int value;
	if (!PyArg_ParseTuple(args, "II", &address, &value))
		return nullptr;

	g_mmu->WriteData(address, static_cast<uint8_t>(value));
	Py_RETURN_NONE;
}

static PyObject* py_read_register(PyObject* self, PyObject* args) {
	const char* reg_name;
	if (!PyArg_ParseTuple(args, "s", &reg_name))
		return nullptr;

	uint16_t* reg = g_cpu->Register(reg_name);
	if (!reg) {
		PyErr_SetString(PyExc_ValueError, "Invalid register name");
		return nullptr;
	}
	return PyLong_FromLong(*reg);
}

static PyObject* py_raise_interrupt(PyObject* self, PyObject* args) {
	int interrupt_index;
	if (!PyArg_ParseTuple(args, "i", &interrupt_index))
		return nullptr;

	g_chipset->RaiseInterrupt(interrupt_index);
	Py_RETURN_NONE;
}

static PyObject* py_get_solar_voltage(PyObject* self, PyObject* args) {
	float* voltage = g_emulator->SolarPanelVoltage();
	return PyFloat_FromDouble(*voltage);
}

static PyObject* py_get_battery_voltage(PyObject* self, PyObject* args) {
	float* voltage = g_emulator->BatteryVoltage();
	return PyFloat_FromDouble(*voltage);
}

static PyObject* py_pause_emulation(PyObject* self, PyObject* args) {
	g_emulator->Pause();
	Py_RETURN_NONE;
}

static PyObject* py_resume_emulation(PyObject* self, PyObject* args) {
	g_emulator->Resume();
	Py_RETURN_NONE;
}

// ---- Keyboard Python functions ----

static PyObject* py_key_press(PyObject* self, PyObject* args) {
	int ki, ko;
	if (!PyArg_ParseTuple(args, "ii", &ki, &ko))
		return nullptr;
	if (!g_keyboard) {
		PyErr_SetString(PyExc_RuntimeError, "Keyboard interface not available");
		return nullptr;
	}
	g_keyboard->Key(ki, ko, true);
	Py_RETURN_NONE;
}

static PyObject* py_key_release(PyObject* self, PyObject* args) {
	int ki, ko;
	if (!PyArg_ParseTuple(args, "ii", &ki, &ko))
		return nullptr;
	if (!g_keyboard) {
		PyErr_SetString(PyExc_RuntimeError, "Keyboard interface not available");
		return nullptr;
	}
	g_keyboard->Key(ki, ko, false);
	Py_RETURN_NONE;
}

static PyObject* py_key_press_code(PyObject* self, PyObject* args) {
	unsigned int code;
	if (!PyArg_ParseTuple(args, "I", &code))
		return nullptr;
	if (!g_keyboard) {
		PyErr_SetString(PyExc_RuntimeError, "Keyboard interface not available");
		return nullptr;
	}
	g_keyboard->PressCode(static_cast<uint8_t>(code), true);
	Py_RETURN_NONE;
}

static PyObject* py_key_release_code(PyObject* self, PyObject* args) {
	unsigned int code;
	if (!PyArg_ParseTuple(args, "I", &code))
		return nullptr;
	if (!g_keyboard) {
		PyErr_SetString(PyExc_RuntimeError, "Keyboard interface not available");
		return nullptr;
	}
	g_keyboard->PressCode(static_cast<uint8_t>(code), false);
	Py_RETURN_NONE;
}

static PyObject* py_key_release_all(PyObject* self, PyObject* args) {
	if (!g_keyboard) {
		PyErr_SetString(PyExc_RuntimeError, "Keyboard interface not available");
		return nullptr;
	}
	g_keyboard->ReleaseAll();
	Py_RETURN_NONE;
}

// Updated method table with new callback methods
static PyMethodDef EmulatorMethods[] = {
	// Previous methods
	{"read_memory", py_read_memory, METH_VARARGS, "Read a byte from memory"},
	{"write_memory", py_write_memory, METH_VARARGS, "Write a byte to memory"},
	{"read_register", py_read_register, METH_VARARGS, "Read a CPU register"},
	{"raise_interrupt", py_raise_interrupt, METH_VARARGS, "Raise an interrupt"},
	{"get_solar_voltage", py_get_solar_voltage, METH_NOARGS, "Get solar panel voltage"},
	{"get_battery_voltage", py_get_battery_voltage, METH_NOARGS, "Get battery voltage"},
	{"pause", py_pause_emulation, METH_NOARGS, "Pause emulation"},
	{"resume", py_resume_emulation, METH_NOARGS, "Resume emulation"},

	// Callback methods
	{"set_instruction_callback", py_set_instruction_callback, METH_VARARGS, "Set instruction execution callback"},
	{"set_call_callback", py_set_call_callback, METH_VARARGS, "Set function call callback"},
	{"set_return_callback", py_set_return_callback, METH_VARARGS, "Set function return callback"},
	{"set_memory_read_callback", py_set_memory_read_callback, METH_VARARGS, "Set memory read callback"},
	{"set_memory_write_callback", py_set_memory_write_callback, METH_VARARGS, "Set memory write callback"},
	{"set_brk_callback", py_set_brk_callback, METH_VARARGS, "Set BRK instruction callback"},
	{"set_interrupt_callback", py_set_interrupt_callback, METH_VARARGS, "Set interrupt callback"},
	{"set_reset_callback", py_set_reset_callback, METH_VARARGS, "Set reset callback"},

	// Keyboard methods
	{"key_press", py_key_press, METH_VARARGS, "Press a key by KI/KO indices: key_press(ki, ko)"},
	{"key_release", py_key_release, METH_VARARGS, "Release a key by KI/KO indices: key_release(ki, ko)"},
	{"key_press_code", py_key_press_code, METH_VARARGS, "Press a key by kiko code byte: key_press_code(code)"},
	{"key_release_code", py_key_release_code, METH_VARARGS, "Release a key by kiko code byte: key_release_code(code)"},
	{"key_release_all", py_key_release_all, METH_NOARGS, "Release all non-stuck keys"},
	{nullptr, nullptr, 0, nullptr}
};
// Module definition
static PyModuleDef EmulatorModule = {
	PyModuleDef_HEAD_INIT,
	"emulator",
	"Emulator control module",
	-1,
	EmulatorMethods
};

// Module initialization
PyMODINIT_FUNC PyInit_emulator(void) {
	return PyModule_Create(&EmulatorModule);
}

// Python console window implementation
class PythonConsole : public UIWindow {
public:
	PythonConsole() : UIWindow("Python Console") {
		memset(m_inputBuffer, 0, sizeof(m_inputBuffer));
		InitPythonTypes();
		m_output << "Python " << Py_GetVersion() << " Console\n";
		m_output << "Type command and press Enter to evaluate. "
					"The 'emulator' module is imported by default as 'emulator' and 'emu'.\n";
	}

	void RenderCore() override {
		// Reorder: Output at top, Input at bottom
		ImVec2 childSize = ImGui::GetContentRegionAvail();
		childSize.y -= ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

		ImGui::BeginChild("ScrollingRegion", childSize, true, ImGuiWindowFlags_HorizontalScrollbar);
		ImGui::TextUnformatted(m_output.str().c_str());
		if (m_scrollToBottom) {
			ImGui::SetScrollHereY(1.0f);
			m_scrollToBottom = false;
		}
		ImGui::EndChild();

		ImGui::Separator();

		bool reclaim_focus = false;
		ImGui::PushItemWidth(-1);
		if (ImGui::InputText("##input", m_inputBuffer, sizeof(m_inputBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
			[](ImGuiInputTextCallbackData* data) -> int {
				PythonConsole* console = (PythonConsole*)data->UserData;
				return console->TextEditCallback(data);
			}, (void*)this))
		{
			ExecuteCommand();
			reclaim_focus = true;
		}
		ImGui::PopItemWidth();

		ImGui::SetItemDefaultFocus();
		if (reclaim_focus) {
			ImGui::SetKeyboardFocusHere(-1); // Auto-focus back on input
		}
	}

private:
	char m_inputBuffer[4096];
	std::stringstream m_output;
	std::vector<std::string> m_history;
	int m_historyPos = -1;
	bool m_scrollToBottom = true;


	// Python redirection types
	struct ConsoleOutput {
		PyObject_HEAD
			PythonConsole* console;
	};

	static PyObject* ConsoleOutput_write(PyObject* self, PyObject* args) {
		ConsoleOutput* output = (ConsoleOutput*)self;
		PyObject* textObj;
		if (!PyArg_ParseTuple(args, "O", &textObj))
			return nullptr;

		PyObject* str = PyObject_Str(textObj);
		if (!str)
			return nullptr;

		const char* text = PyUnicode_AsUTF8(str);
		if (text) {
			output->console->m_output << text;
			output->console->m_scrollToBottom = true;
		}

		Py_DECREF(str);
		Py_RETURN_NONE;
	}

	static PyObject* ConsoleOutput_flush(PyObject* self, PyObject* args) {
		Py_RETURN_NONE; // No-op, just to satisfy Python's expectations
	}

	static PyMethodDef ConsoleOutput_methods[];
	static PyTypeObject ConsoleOutputType;
	static void InitPythonTypes() {
		static bool initialized = false;
		if (!initialized) {
			ConsoleOutputType.tp_name = "PythonConsole.ConsoleOutput";
			ConsoleOutputType.tp_basicsize = sizeof(ConsoleOutput);
			ConsoleOutputType.tp_flags = Py_TPFLAGS_DEFAULT;
			ConsoleOutputType.tp_methods = ConsoleOutput_methods;
			ConsoleOutputType.tp_new = PyType_GenericNew;
			PyType_Ready(&ConsoleOutputType);
			initialized = true;
		}
	}

	void ExecuteCommand() {
		if (strlen(m_inputBuffer) > 0) {
			// Add to history
			m_history.push_back(m_inputBuffer);

			std::string cmd = m_inputBuffer;
			if (cmd == "clear") {
				m_output.str("");
				m_output.clear();
			} else {
				// Output the command
				m_output << ">>> " << m_inputBuffer << "\n";

				// Redirect Python stdout/stderr to our output stream
				PyObject* sysModule = PyImport_ImportModule("sys");
				if (sysModule) {
					// Backup original streams
					PyObject* oldStdout = PyObject_GetAttrString(sysModule, "stdout");
					PyObject* oldStderr = PyObject_GetAttrString(sysModule, "stderr");

					// Create redirection objects
					ConsoleOutput* stdoutObj = PyObject_New(ConsoleOutput, &ConsoleOutputType);
					stdoutObj->console = this;
					ConsoleOutput* stderrObj = PyObject_New(ConsoleOutput, &ConsoleOutputType);
					stderrObj->console = this;

					// Replace standard streams
					PyObject_SetAttrString(sysModule, "stdout", (PyObject*)stdoutObj);
					PyObject_SetAttrString(sysModule, "stderr", (PyObject*)stderrObj);

					if (cmd == "help") {
						m_output << "CasioEmu Python Console Built-in Help\n";
						m_output << "-------------------------------------\n";
						m_output << "The 'emulator' module is imported by default as 'emu'.\n";
						m_output << "Available API functions:\n";

						m_output << "\n--- Memory & Registers ---\n";
						m_output << "  emu.read_memory(address: int) -> int\n";
						m_output << "  emu.write_memory(address: int, value: int)\n";
						m_output << "  emu.read_register(reg_name: str) -> int\n";

						m_output << "\n--- Emulator Control ---\n";
						m_output << "  emu.pause()\n";
						m_output << "  emu.resume()\n";
						m_output << "  emu.raise_interrupt(interrupt_index: int)\n";
						m_output << "  emu.get_solar_voltage() -> float\n";
						m_output << "  emu.get_battery_voltage() -> float\n";

						m_output << "\n--- Keyboard ---\n";
						m_output << "  emu.key_press(ki: int, ko: int)\n";
						m_output << "  emu.key_release(ki: int, ko: int)\n";
						m_output << "  emu.key_press_code(code: int)\n";
						m_output << "  emu.key_release_code(code: int)\n";
						m_output << "  emu.key_release_all()\n";

						m_output << "\n--- Hooks ---\n";
						m_output << "  emu.set_instruction_callback(cb(pc_before, pc_after))  # cb returns object with 'should_break' bool\n";
						m_output << "  emu.set_call_callback(cb(pc, lr))\n";
						m_output << "  emu.set_return_callback(cb(pc, lr))\n";
						m_output << "  emu.set_memory_read_callback(cb(offset, value))  # cb returns object with 'handled' bool\n";
						m_output << "  emu.set_memory_write_callback(cb(offset, value)) # cb returns object with 'handled' bool\n";
						m_output << "  emu.set_brk_callback(cb(index))                  # cb returns object with 'handled' bool\n";
						m_output << "  emu.set_interrupt_callback(cb(index))            # cb returns object with 'handled' bool\n";
						m_output << "  emu.set_reset_callback(cb())\n";

						m_output << "\nSpecial UI commands:\n";
						m_output << "  clear - Clear the console screen\n";
						m_output << "  help  - Show this help message\n";
					} else {
						// Execute Python code
						PyRun_SimpleString(m_inputBuffer);
					}

					// Restore original streams
					PyObject_SetAttrString(sysModule, "stdout", oldStdout);
					PyObject_SetAttrString(sysModule, "stderr", oldStderr);

					// Cleanup
					Py_XDECREF(oldStdout);
					Py_XDECREF(oldStderr);
					Py_XDECREF(stdoutObj);
					Py_XDECREF(stderrObj);
					Py_DECREF(sysModule);
				}
			}
			// Clear input and scroll to bottom
			memset(m_inputBuffer, 0, sizeof(m_inputBuffer));
			m_historyPos = -1;
			m_scrollToBottom = true;
		}
	}

	int TextEditCallback(ImGuiInputTextCallbackData* data) {
		switch (data->EventFlag) {
		case ImGuiInputTextFlags_CallbackHistory: {
			const int prev_history_pos = m_historyPos;
			if (data->EventKey == ImGuiKey_UpArrow) {
				if (m_historyPos == -1)
					m_historyPos = m_history.empty() ? -1 : m_history.size() - 1;
				else if (m_historyPos > 0)
					m_historyPos--;
			}
			else if (data->EventKey == ImGuiKey_DownArrow) {
				if (m_historyPos != -1) {
					if (++m_historyPos >= m_history.size())
						m_historyPos = -1;
				}
			}

			if (prev_history_pos != m_historyPos) {
				const std::string history_str = (m_historyPos >= 0) ? m_history[m_historyPos] : "";
				data->DeleteChars(0, data->BufTextLen);
				data->InsertChars(0, history_str.c_str());
			}
		}
		}
		return 0;
	}
};
PyMethodDef PythonConsole::ConsoleOutput_methods[] = {
	{"write", PythonConsole::ConsoleOutput_write, METH_VARARGS, "Write output to console"},
	{"flush", PythonConsole::ConsoleOutput_flush, METH_NOARGS, "Flush output to console"},
	{nullptr, nullptr, 0, nullptr}
};

PyTypeObject PythonConsole::ConsoleOutputType = {
	PyVarObject_HEAD_INIT(nullptr, 0)
};

// Plugin entry point
extern "C" __declspec(dllexport) void fPluginLoad(PluginApi* api) {
	if (!api)
		return;

	g_api = api;
	PLUGINASSERTSTL(api);

	if (!api->RegisterPlugin("python_console", "Python Console", 1))
		return;

	// Get interfaces
	g_cpu = api->QueryInterface<ICPU>();
	g_mmu = api->QueryInterface<IMMU>();
	g_chipset = api->QueryInterface<IChipset>();
	g_emulator = api->QueryInterface<IEmulator>();
	g_hooks = api->QueryInterface<Hooks>();
	g_keyboard = api->QueryInterface<IKeyboard>();
	// Set up hooks
	if (g_hooks) {
		g_hooks->SetupOnInstructionHook(OnInstructionHandler);
		g_hooks->SetupOnCallFunctionHook(OnCallHandler);
		g_hooks->SetupOnFunctionReturnHook(OnReturnHandler);
		g_hooks->SetupOnMemoryReadHook(OnMemoryReadHandler);
		g_hooks->SetupOnMemoryWriteHook(OnMemoryWriteHandler);
		g_hooks->SetupOnBrkHook(OnBrkHandler);
		g_hooks->SetupOnInterruptHook(OnInterruptHandler);
		g_hooks->SetupOnResetHook(OnResetHandler);
	}
	// Initialize Python
	PyImport_AppendInittab("emulator", PyInit_emulator);

	PyStatus status;
	PyConfig config;
	PyConfig_InitPythonConfig(&config);
	status = PyConfig_SetBytesString(&config, &config.program_name, "");
	Py_InitializeFromConfig(&config);

	// Pre-import emulator to make it available immediately in the console context
	PyRun_SimpleString(
		"import emulator\n"
		"emu = emulator\n"
	);

	// Add the console window
	api->AddWindow(new PythonConsole());
}

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH: {
		break;
	}
	case DLL_PROCESS_DETACH:
		Py_Finalize();
		break;
	}
	return TRUE;
}