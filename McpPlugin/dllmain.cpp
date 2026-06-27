#include "pch.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

#include "PluginApi.h"
#include "json.hpp"

#define CPPHTTPLIB_THREAD_POOL_COUNT 8
#include "httplib.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <iomanip>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

namespace {

constexpr const char* kServerName = "casioemu-mcp";
constexpr const char* kServerVersion = "0.2.0";
constexpr const char* kLatestProtocolVersion = "2025-11-25";
constexpr size_t kMaxMemoryTransfer = 64 * 1024;
constexpr size_t kMaxCodeWords = 32 * 1024;
constexpr uint32_t kMaxAddress = 0x00ffffff;

PluginApi* g_api = nullptr;
IDebugger* g_debugger = nullptr;
IEmulator* g_emulator = nullptr;
IChipset* g_chipset = nullptr;
IKeyboard* g_keyboard = nullptr;
Hooks* g_hooks = nullptr;

std::atomic_bool g_running{true};

struct EventQueue {
    std::mutex Mutex;
    std::condition_variable Cv;
    std::deque<std::string> Messages;
    bool Connected = true;
};

std::mutex g_sessionsMutex;
std::unordered_map<std::string, std::shared_ptr<EventQueue>> g_legacySessions;
std::unordered_map<std::string, std::shared_ptr<EventQueue>> g_httpSessions;

json RpcError(const json& id, int code, const std::string& message, json data = nullptr) {
    json error = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message},
        }},
    };
    if (!data.is_null())
        error["error"]["data"] = std::move(data);
    return error;
}

json RpcResult(const json& id, json result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", std::move(result)},
    };
}

json ToolResult(json value, bool isError = false) {
    json result = {
        {"content", json::array({
            {
                {"type", "text"},
                {"text", value.dump(2)},
            },
        })},
        {"structuredContent", value},
    };
    if (isError)
        result["isError"] = true;
    return result;
}

json ToolError(const std::string& message) {
    return ToolResult({{"error", message}}, true);
}

std::string NewSessionId() {
    static std::mutex randomMutex;
    static std::mt19937_64 random{std::random_device{}()};
    std::lock_guard lock(randomMutex);
    std::ostringstream stream;
    stream << std::hex << std::setfill('0')
           << std::setw(16) << random()
           << std::setw(16) << random();
    return stream.str();
}

bool IsAllowedOrigin(const httplib::Request& req) {
    if (!req.has_header("Origin"))
        return true;
    const auto origin = req.get_header_value("Origin");
    return origin == "null"
        || origin.starts_with("http://127.0.0.1")
        || origin.starts_with("https://127.0.0.1")
        || origin.starts_with("http://localhost")
        || origin.starts_with("https://localhost");
}

bool CheckOrigin(const httplib::Request& req, httplib::Response& res) {
    if (IsAllowedOrigin(req))
        return true;
    res.status = 403;
    res.set_content("Forbidden origin", "text/plain");
    return false;
}

template <typename T>
bool GetInteger(
    const json& args,
    const char* name,
    T& value,
    std::string& error,
    uint64_t minimum = 0,
    uint64_t maximum = std::numeric_limits<T>::max()) {
    auto it = args.find(name);
    if (it == args.end() || (!it->is_number_integer() && !it->is_number_unsigned())) {
        error = std::string("Missing or invalid integer argument: ") + name;
        return false;
    }
    int64_t signedValue = 0;
    if (it->is_number_unsigned()) {
        const auto unsignedValue = it->get<uint64_t>();
        if (unsignedValue > maximum) {
            error = std::string("Argument out of range: ") + name;
            return false;
        }
        value = static_cast<T>(unsignedValue);
        return true;
    }
    signedValue = it->get<int64_t>();
    if (signedValue < 0
        || static_cast<uint64_t>(signedValue) < minimum
        || static_cast<uint64_t>(signedValue) > maximum) {
        error = std::string("Argument out of range: ") + name;
        return false;
    }
    value = static_cast<T>(signedValue);
    return true;
}

bool GetString(const json& args, const char* name, std::string& value, std::string& error) {
    auto it = args.find(name);
    if (it == args.end() || !it->is_string()) {
        error = std::string("Missing or invalid string argument: ") + name;
        return false;
    }
    value = it->get<std::string>();
    return true;
}

bool GetBool(const json& args, const char* name, bool& value, std::string& error) {
    auto it = args.find(name);
    if (it == args.end() || !it->is_boolean()) {
        error = std::string("Missing or invalid boolean argument: ") + name;
        return false;
    }
    value = it->get<bool>();
    return true;
}

bool GetBytes(const json& args, const char* name, std::vector<uint8_t>& bytes, std::string& error) {
    auto it = args.find(name);
    if (it == args.end() || !it->is_array()) {
        error = std::string("Missing or invalid byte array argument: ") + name;
        return false;
    }
    if (it->size() > kMaxMemoryTransfer) {
        error = "Byte array exceeds the 65536-byte transfer limit";
        return false;
    }
    bytes.clear();
    bytes.reserve(it->size());
    for (const auto& item : *it) {
        if ((!item.is_number_integer() && !item.is_number_unsigned())) {
            error = "Byte array must contain integers";
            return false;
        }
        const auto value = item.get<int64_t>();
        if (value < 0 || value > 255) {
            error = "Byte values must be between 0 and 255";
            return false;
        }
        bytes.push_back(static_cast<uint8_t>(value));
    }
    return true;
}

json EmptyObjectSchema() {
    return {
        {"type", "object"},
        {"properties", json::object()},
        {"additionalProperties", false},
    };
}

json ObjectSchema(json properties, json required = json::array()) {
    json schema = {
        {"type", "object"},
        {"properties", std::move(properties)},
        {"additionalProperties", false},
    };
    if (!required.empty())
        schema["required"] = std::move(required);
    return schema;
}

json ToolDefinitions() {
    return json::array({
        {
            {"name", "get_status"},
            {"description", "Get emulator model, ROM, run state, PC, clock rate, voltages, and recording state."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "pause"},
            {"description", "Pause CPU execution."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "resume"},
            {"description", "Resume CPU execution."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "reset"},
            {"description", "Reset the emulated chipset while preserving the prior pause state."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "step_into"},
            {"description", "Execute one instruction. The emulator must be paused."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "step_over"},
            {"description", "Step over the current call. The emulator must be paused."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "step_out"},
            {"description", "Continue until the current function returns. The emulator must be paused."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "list_registers"},
            {"description", "List all CPU registers with values and bit widths."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "read_register"},
            {"description", "Read a CPU register by name. Register names are case-insensitive."},
            {"inputSchema", ObjectSchema({
                {"name", {{"type", "string"}}},
            }, {"name"})},
        },
        {
            {"name", "write_register"},
            {"description", "Write a CPU register. Full segmented PC and LR values are accepted."},
            {"inputSchema", ObjectSchema({
                {"name", {{"type", "string"}}},
                {"value", {{"type", "integer"}, {"minimum", 0}}},
            }, {"name", "value"})},
        },
        {
            {"name", "read_memory"},
            {"description", "Read bytes through the MMU data address space."},
            {"inputSchema", ObjectSchema({
                {"address", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}}},
                {"size", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxMemoryTransfer}}},
            }, {"address", "size"})},
        },
        {
            {"name", "write_memory"},
            {"description", "Write bytes through the MMU data address space."},
            {"inputSchema", ObjectSchema({
                {"address", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}}},
                {"bytes", {{"type", "array"}, {"maxItems", kMaxMemoryTransfer}, {"items", {{"type", "integer"}, {"minimum", 0}, {"maximum", 255}}}}},
            }, {"address", "bytes"})},
        },
        {
            {"name", "read_code"},
            {"description", "Read 16-bit instruction words from code space."},
            {"inputSchema", ObjectSchema({
                {"address", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}}},
                {"count", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxCodeWords}}},
            }, {"address", "count"})},
        },
        {
            {"name", "write_code"},
            {"description", "Patch bytes in the loaded ROM image."},
            {"inputSchema", ObjectSchema({
                {"address", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}}},
                {"bytes", {{"type", "array"}, {"maxItems", kMaxMemoryTransfer}, {"items", {{"type", "integer"}, {"minimum", 0}, {"maximum", 255}}}}},
            }, {"address", "bytes"})},
        },
        {
            {"name", "disassemble"},
            {"description", "Return disassembly lines generated by the same decoder and labels as the Code window."},
            {"inputSchema", ObjectSchema({
                {"address", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}}},
                {"count", {{"type", "integer"}, {"minimum", 1}, {"maximum", 4096}}},
            }, {"address", "count"})},
        },
        {
            {"name", "list_execution_breakpoints"},
            {"description", "List execution breakpoints shared with the Code debugger window."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "add_execution_breakpoint"},
            {"description", "Add an execution breakpoint shared with the Code debugger window."},
            {"inputSchema", ObjectSchema({
                {"address", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}}},
            }, {"address"})},
        },
        {
            {"name", "remove_execution_breakpoint"},
            {"description", "Remove an execution breakpoint."},
            {"inputSchema", ObjectSchema({
                {"address", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}}},
            }, {"address"})},
        },
        {
            {"name", "clear_execution_breakpoints"},
            {"description", "Remove all execution breakpoints."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "list_memory_breakpoints"},
            {"description", "List memory read/write monitors and breakpoints shared with the Breakpoints window."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "list_memory_breakpoint_hits"},
            {"description", "List recorded PCs, link registers, and call stacks for one non-breaking memory monitor."},
            {"inputSchema", ObjectSchema({
                {"address", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}}},
                {"write", {{"type", "boolean"}}},
            }, {"address", "write"})},
        },
        {
            {"name", "add_memory_breakpoint"},
            {"description", "Monitor reads or writes to one address, optionally pausing when hit."},
            {"inputSchema", ObjectSchema({
                {"address", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}}},
                {"write", {{"type", "boolean"}}},
                {"break_when_hit", {{"type", "boolean"}, {"default", true}}},
            }, {"address", "write"})},
        },
        {
            {"name", "remove_memory_breakpoint"},
            {"description", "Remove a memory read/write breakpoint."},
            {"inputSchema", ObjectSchema({
                {"address", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}}},
                {"write", {{"type", "boolean"}}},
            }, {"address", "write"})},
        },
        {
            {"name", "clear_memory_breakpoints"},
            {"description", "Remove all memory breakpoints and access monitors."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "get_backtrace"},
            {"description", "Get the textual CPU backtrace."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "get_stack_frames"},
            {"description", "Get structured stack frames with symbols, PC, LR, SP, ER0, and ER2."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "list_labels"},
            {"description", "Search labels loaded by the debugger."},
            {"inputSchema", ObjectSchema({
                {"query", {{"type", "string"}, {"default", ""}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10000}, {"default", 256}}},
            })},
        },
        {
            {"name", "list_variables"},
            {"description", "Read calculator variables using the same BCD decoding as the Variables window."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "list_snapshots"},
            {"description", "List snapshot nodes shared with the Snapshot window."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "save_snapshot"},
            {"description", "Save the current emulator state as a snapshot node."},
            {"inputSchema", ObjectSchema({
                {"parent_id", {{"type", "integer"}, {"minimum", 0}, {"default", 0}}},
                {"label", {{"type", "string"}, {"default", "MCP Snapshot"}}},
            })},
        },
        {
            {"name", "load_snapshot"},
            {"description", "Restore one snapshot node."},
            {"inputSchema", ObjectSchema({
                {"id", {{"type", "integer"}, {"minimum", 1}}},
            }, {"id"})},
        },
        {
            {"name", "delete_snapshot"},
            {"description", "Delete a snapshot and its descendants."},
            {"inputSchema", ObjectSchema({
                {"id", {{"type", "integer"}, {"minimum", 1}}},
            }, {"id"})},
        },
        {
            {"name", "export_snapshots"},
            {"description", "Export all snapshots, one node, or a subtree to a .snapshot file."},
            {"inputSchema", ObjectSchema({
                {"path", {{"type", "string"}}},
                {"id", {{"type", "integer"}, {"minimum", 0}, {"default", 0}}},
                {"subtree", {{"type", "boolean"}, {"default", false}}},
            }, {"path"})},
        },
        {
            {"name", "import_snapshots"},
            {"description", "Import and merge snapshots from a .snapshot file."},
            {"inputSchema", ObjectSchema({
                {"path", {{"type", "string"}}},
            }, {"path"})},
        },
        {
            {"name", "list_address_locks"},
            {"description", "List watched and fixed memory addresses from the Addrs window."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "set_address_lock"},
            {"description", "Add or update an address watch/fixed-value override."},
            {"inputSchema", ObjectSchema({
                {"address", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}}},
                {"value", {{"type", "integer"}, {"minimum", 0}, {"maximum", 255}}},
                {"locked", {{"type", "boolean"}, {"default", true}}},
            }, {"address", "value"})},
        },
        {
            {"name", "remove_address_lock"},
            {"description", "Remove an address from the Addrs window."},
            {"inputSchema", ObjectSchema({
                {"address", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}}},
            }, {"address"})},
        },
        {
            {"name", "clear_address_locks"},
            {"description", "Clear all address watches and fixed-value overrides."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "start_call_recording"},
            {"description", "Start function-call recording with optional caller and callee filters."},
            {"inputSchema", ObjectSchema({
                {"caller", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}}},
                {"callee", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}}},
            })},
        },
        {
            {"name", "stop_call_recording"},
            {"description", "Stop function-call recording."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "clear_call_recording"},
            {"description", "Clear recorded function calls."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "list_function_calls"},
            {"description", "List recorded calls, optionally restricted to one function address."},
            {"inputSchema", ObjectSchema({
                {"function", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxAddress}, {"default", 0}}},
            })},
        },
        {
            {"name", "set_cycles_per_second"},
            {"description", "Set the emulated CPU clock rate."},
            {"inputSchema", ObjectSchema({
                {"cps", {{"type", "integer"}, {"minimum", 1}, {"maximum", 268435456}}},
            }, {"cps"})},
        },
        {
            {"name", "set_pd_value"},
            {"description", "Set the model PD register value used by the Hardware window."},
            {"inputSchema", ObjectSchema({
                {"value", {{"type", "integer"}, {"minimum", 0}, {"maximum", 255}}},
            }, {"value"})},
        },
        {
            {"name", "get_display_settings"},
            {"description", "Get display, residual, fading, buffer, and audio settings from the Hardware debugger window."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "get_qr_code"},
            {"description", "Get the currently displayed calculator QR payload, QR version, and capture revision."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "set_display_settings"},
            {"description", "Update any subset of display, residual, fading, buffer, and audio settings."},
            {"inputSchema", ObjectSchema({
                {"flashing_threshold", {{"type", "integer"}, {"minimum", 0}, {"maximum", 63}}},
                {"flashing_brightness", {{"type", "number"}, {"minimum", 1.0}, {"maximum", 8.0}}},
                {"buffer_select", {{"type", "integer"}, {"minimum", 0}, {"maximum", 2}}},
                {"fading_enabled", {{"type", "boolean"}}},
                {"fading_coefficient", {{"type", "number"}}},
                {"residual_enabled", {{"type", "boolean"}}},
                {"residual_alpha_scale", {{"type", "number"}}},
                {"audio_enabled", {{"type", "boolean"}}},
            })},
        },
        {
            {"name", "raise_interrupt"},
            {"description", "Raise a maskable interrupt."},
            {"inputSchema", ObjectSchema({
                {"index", {{"type", "integer"}, {"minimum", 0}, {"maximum", 255}}},
            }, {"index"})},
        },
        {
            {"name", "keyboard_key"},
            {"description", "Press or release a key by KI/KO matrix coordinates."},
            {"inputSchema", ObjectSchema({
                {"ki", {{"type", "integer"}, {"minimum", 0}, {"maximum", 7}}},
                {"ko", {{"type", "integer"}, {"minimum", 0}, {"maximum", 15}}},
                {"pressed", {{"type", "boolean"}}},
            }, {"ki", "ko", "pressed"})},
        },
        {
            {"name", "keyboard_code"},
            {"description", "Press or release a key by raw ModelInfo kiko code."},
            {"inputSchema", ObjectSchema({
                {"code", {{"type", "integer"}, {"minimum", 0}, {"maximum", 255}}},
                {"pressed", {{"type", "boolean"}}},
            }, {"code", "pressed"})},
        },
        {
            {"name", "keyboard_release_all"},
            {"description", "Release all non-stuck calculator keys."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "request_screenshot"},
            {"description", "Request the same screenshot action as the Hardware debugger window."},
            {"inputSchema", EmptyObjectSchema()},
        },
        {
            {"name", "set_recording"},
            {"description", "Start or stop screen recording."},
            {"inputSchema", ObjectSchema({
                {"start", {{"type", "boolean"}}},
            }, {"start"})},
        },
        {
            {"name", "hot_reload_rom"},
            {"description", "Reload the configured ROM file and reset the chipset."},
            {"inputSchema", EmptyObjectSchema()},
        },
    });
}

json CallTool(const std::string& name, const json& args) {
    if (!g_debugger)
        return ToolError("Debugger interface is unavailable");

    std::string error;

    if (name == "get_status") {
        json result = {
            {"model_name", g_emulator ? g_emulator->GetRunningModelName() : ""},
            {"rom_path", g_emulator ? g_emulator->GetRunningRomPath() : ""},
            {"paused", g_emulator && g_emulator->IsPaused()},
            {"program_counter", g_debugger->GetProgramCounter()},
            {"cycles_per_second", g_emulator ? g_emulator->GetCyclesPerSecond() : 0},
            {"run_status", g_chipset ? static_cast<int>(g_chipset->GetStatus()) : -1},
        };
        if (g_emulator && g_emulator->SolarPanelVoltage())
            result["solar_voltage"] = *g_emulator->SolarPanelVoltage();
        if (g_emulator && g_emulator->BatteryVoltage())
            result["battery_voltage"] = *g_emulator->BatteryVoltage();
        return ToolResult(std::move(result));
    }
    if (name == "pause") {
        g_debugger->Pause();
        return ToolResult({{"success", true}, {"paused", true}});
    }
    if (name == "resume") {
        g_debugger->Resume();
        return ToolResult({{"success", true}, {"paused", false}});
    }
    if (name == "reset") {
        g_debugger->Reset();
        return ToolResult({{"success", true}});
    }
    if (name == "step_into" || name == "step_over" || name == "step_out") {
        bool success = name == "step_into"
            ? g_debugger->StepInto()
            : name == "step_over"
                ? g_debugger->StepOver()
                : g_debugger->StepOut();
        return success
            ? ToolResult({{"success", true}})
            : ToolError("The requested step operation is unavailable; pause the emulator and ensure the debugger UI is initialized");
    }
    if (name == "list_registers") {
        json registers = json::array();
        for (const auto& reg : g_debugger->GetRegisters())
            registers.push_back({
                {"name", reg.Name},
                {"value", reg.Value},
                {"hex", "0x" + [&] {
                    std::ostringstream stream;
                    stream << std::hex << std::uppercase << reg.Value;
                    return stream.str();
                }()},
                {"bit_width", reg.BitWidth},
            });
        return ToolResult({{"registers", std::move(registers)}});
    }
    if (name == "read_register") {
        std::string registerName;
        if (!GetString(args, "name", registerName, error))
            return ToolError(error);
        uint32_t value = 0;
        uint32_t bitWidth = 0;
        if (!g_debugger->ReadRegister(registerName.c_str(), value, bitWidth))
            return ToolError("Register not found: " + registerName);
        return ToolResult({
            {"name", registerName},
            {"value", value},
            {"bit_width", bitWidth},
        });
    }
    if (name == "write_register") {
        std::string registerName;
        uint32_t value = 0;
        if (!GetString(args, "name", registerName, error)
            || !GetInteger(args, "value", value, error, 0, kMaxAddress))
            return ToolError(error);
        if (!g_debugger->WriteRegister(registerName.c_str(), value))
            return ToolError("Register is unknown or read-only on this CPU model: " + registerName);
        return ToolResult({{"success", true}});
    }
    if (name == "read_memory") {
        uint32_t address = 0;
        size_t size = 0;
        if (!GetInteger(args, "address", address, error, 0, kMaxAddress)
            || !GetInteger(args, "size", size, error, 0, kMaxMemoryTransfer))
            return ToolError(error);
        return ToolResult({
            {"address", address},
            {"bytes", g_debugger->ReadMemory(address, size)},
        });
    }
    if (name == "write_memory") {
        uint32_t address = 0;
        std::vector<uint8_t> bytes;
        if (!GetInteger(args, "address", address, error, 0, kMaxAddress)
            || !GetBytes(args, "bytes", bytes, error))
            return ToolError(error);
        g_debugger->WriteMemory(address, bytes);
        return ToolResult({{"success", true}, {"written", bytes.size()}});
    }
    if (name == "read_code") {
        uint32_t address = 0;
        size_t count = 0;
        if (!GetInteger(args, "address", address, error, 0, kMaxAddress)
            || !GetInteger(args, "count", count, error, 0, kMaxCodeWords))
            return ToolError(error);
        return ToolResult({
            {"address", address},
            {"words", g_debugger->ReadCode(address, count)},
        });
    }
    if (name == "write_code") {
        uint32_t address = 0;
        std::vector<uint8_t> bytes;
        if (!GetInteger(args, "address", address, error, 0, kMaxAddress)
            || !GetBytes(args, "bytes", bytes, error))
            return ToolError(error);
        g_debugger->WriteCode(address, bytes);
        return ToolResult({{"success", true}, {"written", bytes.size()}});
    }
    if (name == "disassemble") {
        uint32_t address = 0;
        size_t count = 0;
        if (!GetInteger(args, "address", address, error, 0, kMaxAddress)
            || !GetInteger(args, "count", count, error, 1, 4096))
            return ToolError(error);
        json lines = json::array();
        for (const auto& line : g_debugger->GetDisassembly(address, count))
            lines.push_back({
                {"address", line.Address},
                {"text", line.Text},
                {"is_label", line.IsLabel},
                {"cross_reference", line.CrossReference},
            });
        return ToolResult({{"lines", std::move(lines)}});
    }
    if (name == "list_execution_breakpoints") {
        return ToolResult({{"breakpoints", g_debugger->GetExecutionBreakpoints()}});
    }
    if (name == "add_execution_breakpoint" || name == "remove_execution_breakpoint") {
        uint32_t address = 0;
        if (!GetInteger(args, "address", address, error, 0, kMaxAddress))
            return ToolError(error);
        const bool success = name == "add_execution_breakpoint"
            ? g_debugger->AddExecutionBreakpoint(address)
            : g_debugger->RemoveExecutionBreakpoint(address);
        return success ? ToolResult({{"success", true}}) : ToolError("Code debugger is unavailable");
    }
    if (name == "clear_execution_breakpoints") {
        g_debugger->ClearExecutionBreakpoints();
        return ToolResult({{"success", true}});
    }
    if (name == "list_memory_breakpoints") {
        json breakpoints = json::array();
        for (const auto& bp : g_debugger->GetMemoryBreakpoints())
            breakpoints.push_back({
                {"address", bp.Address},
                {"write", bp.Write},
                {"break_when_hit", bp.BreakWhenHit},
                {"hit_count", bp.HitCount},
            });
        return ToolResult({{"breakpoints", std::move(breakpoints)}});
    }
    if (name == "list_memory_breakpoint_hits") {
        uint32_t address = 0;
        bool write = false;
        if (!GetInteger(args, "address", address, error, 0, kMaxAddress)
            || !GetBool(args, "write", write, error))
            return ToolError(error);
        json hits = json::array();
        for (const auto& hit : g_debugger->GetMemoryBreakpointHits(address, write)) {
            json registers = json::object();
            for (const auto& reg : hit.Registers)
                registers[reg.Name] = reg.Value;
            hits.push_back({
                {"program_counter", hit.ProgramCounter},
                {"link_register", hit.LinkRegister},
                {"stack", hit.Stack},
                {"registers", std::move(registers)},
            });
        }
        return ToolResult({{"hits", std::move(hits)}});
    }
    if (name == "add_memory_breakpoint") {
        uint32_t address = 0;
        bool write = false;
        bool breakWhenHit = args.value("break_when_hit", true);
        if (!GetInteger(args, "address", address, error, 0, kMaxAddress)
            || !GetBool(args, "write", write, error))
            return ToolError(error);
        return g_debugger->AddMemoryBreakpoint(address, write, breakWhenHit)
            ? ToolResult({{"success", true}})
            : ToolError("Memory breakpoint debugger is unavailable");
    }
    if (name == "remove_memory_breakpoint") {
        uint32_t address = 0;
        bool write = false;
        if (!GetInteger(args, "address", address, error, 0, kMaxAddress)
            || !GetBool(args, "write", write, error))
            return ToolError(error);
        return g_debugger->RemoveMemoryBreakpoint(address, write)
            ? ToolResult({{"success", true}})
            : ToolError("Memory breakpoint not found");
    }
    if (name == "clear_memory_breakpoints") {
        g_debugger->ClearMemoryBreakpoints();
        return ToolResult({{"success", true}});
    }
    if (name == "get_backtrace") {
        return ToolResult({{"backtrace", g_debugger->GetBacktrace()}});
    }
    if (name == "get_stack_frames") {
        json frames = json::array();
        for (const auto& frame : g_debugger->GetStackFrames())
            frames.push_back({
                {"program_counter", frame.ProgramCounter},
                {"link_register", frame.LinkRegister},
                {"stack_pointer", frame.StackPointer},
                {"er0", frame.Er0},
                {"er2", frame.Er2},
                {"link_register_pushed", frame.LinkRegisterPushed},
                {"is_jump", frame.IsJump},
                {"symbol", frame.Symbol},
            });
        return ToolResult({{"frames", std::move(frames)}});
    }
    if (name == "list_labels") {
        const std::string query = args.value("query", "");
        size_t limit = 256;
        if (args.contains("limit") && !GetInteger(args, "limit", limit, error, 1, 10000))
            return ToolError(error);
        json labels = json::array();
        for (const auto& label : g_debugger->GetLabels(query.c_str(), limit))
            labels.push_back({{"address", label.Address}, {"name", label.Name}});
        return ToolResult({{"labels", std::move(labels)}});
    }
    if (name == "list_variables") {
        json variables = json::array();
        for (const auto& variable : g_debugger->GetVariables())
            variables.push_back({
                {"name", variable.Name},
                {"real_address", variable.RealAddress},
                {"real_value", variable.RealValue},
                {"real_hex", variable.RealHex},
                {"has_imaginary_part", variable.HasImaginaryPart},
                {"imaginary_address", variable.ImaginaryAddress},
                {"imaginary_value", variable.ImaginaryValue},
                {"imaginary_hex", variable.ImaginaryHex},
            });
        return ToolResult({{"variables", std::move(variables)}});
    }
    if (name == "list_snapshots") {
        json snapshots = json::array();
        for (const auto& snapshot : g_debugger->GetSnapshots())
            snapshots.push_back({
                {"id", snapshot.Id},
                {"parent_id", snapshot.ParentId},
                {"label", snapshot.Label},
                {"timestamp", snapshot.Timestamp},
                {"preview_size", snapshot.PreviewSize},
                {"state_size", snapshot.StateSize},
            });
        return ToolResult({{"snapshots", std::move(snapshots)}});
    }
    if (name == "save_snapshot") {
        uint32_t parentId = args.value("parent_id", 0u);
        const std::string label = args.value("label", "MCP Snapshot");
        const auto id = g_debugger->SaveSnapshot(parentId, label.c_str());
        return id ? ToolResult({{"success", true}, {"id", id}}) : ToolError("Snapshot manager is unavailable");
    }
    if (name == "load_snapshot" || name == "delete_snapshot") {
        uint32_t id = 0;
        if (!GetInteger(args, "id", id, error, 1, std::numeric_limits<uint32_t>::max()))
            return ToolError(error);
        if (name == "delete_snapshot")
            return g_debugger->DeleteSnapshot(id)
                ? ToolResult({{"success", true}})
                : ToolError("Snapshot not found");
        std::string snapshotError;
        return g_debugger->LoadSnapshot(id, snapshotError)
            ? ToolResult({{"success", true}})
            : ToolError(snapshotError);
    }
    if (name == "export_snapshots") {
        std::string path;
        if (!GetString(args, "path", path, error))
            return ToolError(error);
        const uint32_t id = args.value("id", 0u);
        const bool subtree = args.value("subtree", false);
        std::string snapshotError;
        return g_debugger->ExportSnapshots(path.c_str(), id, subtree, snapshotError)
            ? ToolResult({{"success", true}, {"path", path}})
            : ToolError(snapshotError);
    }
    if (name == "import_snapshots") {
        std::string path;
        if (!GetString(args, "path", path, error))
            return ToolError(error);
        std::string snapshotError;
        return g_debugger->ImportSnapshots(path.c_str(), snapshotError)
            ? ToolResult({{"success", true}})
            : ToolError(snapshotError);
    }
    if (name == "list_address_locks") {
        json locks = json::array();
        for (const auto& item : g_debugger->GetAddressLocks())
            locks.push_back({
                {"address", item.Address},
                {"value", item.Value},
                {"locked", item.Locked},
            });
        return ToolResult({{"addresses", std::move(locks)}});
    }
    if (name == "set_address_lock") {
        uint32_t address = 0;
        uint32_t value = 0;
        if (!GetInteger(args, "address", address, error, 0, kMaxAddress)
            || !GetInteger(args, "value", value, error, 0, 255))
            return ToolError(error);
        g_debugger->SetAddressLock(address, static_cast<uint8_t>(value), args.value("locked", true));
        return ToolResult({{"success", true}});
    }
    if (name == "remove_address_lock") {
        uint32_t address = 0;
        if (!GetInteger(args, "address", address, error, 0, kMaxAddress))
            return ToolError(error);
        return g_debugger->RemoveAddressLock(address)
            ? ToolResult({{"success", true}})
            : ToolError("Address watch not found");
    }
    if (name == "clear_address_locks") {
        g_debugger->ClearAddressLocks();
        return ToolResult({{"success", true}});
    }
    if (name == "start_call_recording") {
        const bool filterCaller = args.contains("caller");
        const bool filterCallee = args.contains("callee");
        const uint32_t caller = args.value("caller", 0u);
        const uint32_t callee = args.value("callee", 0u);
        g_debugger->StartCallRecording(filterCaller, caller, filterCallee, callee);
        return ToolResult({{"success", true}});
    }
    if (name == "stop_call_recording") {
        g_debugger->StopCallRecording();
        return ToolResult({{"success", true}});
    }
    if (name == "clear_call_recording") {
        g_debugger->ClearCallRecording();
        return ToolResult({{"success", true}});
    }
    if (name == "list_function_calls") {
        const uint32_t function = args.value("function", 0u);
        json calls = json::array();
        for (const auto& call : g_debugger->GetFunctionCalls(function))
            calls.push_back({
                {"function", call.Function},
                {"caller", call.Caller},
                {"xr0", call.Xr0},
                {"stack", call.Stack},
            });
        return ToolResult({{"calls", std::move(calls)}});
    }
    if (name == "set_cycles_per_second") {
        uint32_t cps = 0;
        if (!GetInteger(args, "cps", cps, error, 1, 268435456))
            return ToolError(error);
        g_debugger->SetCyclesPerSecond(cps);
        return ToolResult({{"success", true}, {"cycles_per_second", cps}});
    }
    if (name == "set_pd_value") {
        uint32_t value = 0;
        if (!GetInteger(args, "value", value, error, 0, 255))
            return ToolError(error);
        g_debugger->SetPdValue(static_cast<uint8_t>(value));
        return ToolResult({{"success", true}});
    }
    if (name == "get_display_settings") {
        const auto settings = g_debugger->GetDisplaySettings();
        return ToolResult({
            {"flashing_threshold", settings.FlashingThreshold},
            {"flashing_brightness", settings.FlashingBrightness},
            {"buffer_select", settings.BufferSelect},
            {"fading_enabled", settings.FadingEnabled},
            {"fading_coefficient", settings.FadingCoefficient},
            {"residual_enabled", settings.ResidualEnabled},
            {"residual_alpha_scale", settings.ResidualAlphaScale},
            {"audio_enabled", settings.AudioEnabled},
        });
    }
    if (name == "get_qr_code") {
        const auto qr = g_debugger->GetQrCode();
        json history = json::array();
        for (const auto& entry : qr.History) {
            history.push_back({
                {"id", entry.Id},
                {"version", entry.Version},
                {"data", entry.Data},
                {"length", entry.Data.size()},
            });
        }
        json realPageLengths = json::array();
        for (const auto length : qr.RealPageLengths)
            realPageLengths.push_back(length);
        return ToolResult({
            {"active", qr.Active},
            {"complete", qr.Complete},
            {"version", qr.Version},
            {"revision", qr.Revision},
            {"data", qr.Data},
            {"length", qr.Data.size()},
            {"real_current_page", qr.RealCurrentPage},
            {"real_total_pages", qr.RealTotalPages},
            {"real_current_page_data", qr.RealCurrentPageData},
            {"real_current_page_length", qr.RealCurrentPageData.size()},
            {"real_page_lengths", std::move(realPageLengths)},
            {"history", history},
            {"history_count", qr.History.size()},
        });
    }
    if (name == "set_display_settings") {
        auto settings = g_debugger->GetDisplaySettings();
        uint32_t integer = 0;
        bool boolean = false;
        if (args.contains("flashing_threshold")) {
            if (!GetInteger(args, "flashing_threshold", integer, error, 0, 63))
                return ToolError(error);
            settings.FlashingThreshold = static_cast<int>(integer);
        }
        if (args.contains("buffer_select")) {
            if (!GetInteger(args, "buffer_select", integer, error, 0, 2))
                return ToolError(error);
            settings.BufferSelect = static_cast<int>(integer);
        }
        if (args.contains("fading_enabled")) {
            if (!GetBool(args, "fading_enabled", boolean, error))
                return ToolError(error);
            settings.FadingEnabled = boolean;
        }
        if (args.contains("residual_enabled")) {
            if (!GetBool(args, "residual_enabled", boolean, error))
                return ToolError(error);
            settings.ResidualEnabled = boolean;
        }
        if (args.contains("audio_enabled")) {
            if (!GetBool(args, "audio_enabled", boolean, error))
                return ToolError(error);
            settings.AudioEnabled = boolean;
        }
        const auto setNumber = [&](const char* key, float& target, float minimum, float maximum) -> bool {
            auto it = args.find(key);
            if (it == args.end())
                return true;
            if (!it->is_number()) {
                error = std::string(key) + " must be a number";
                return false;
            }
            const auto value = it->get<float>();
            if (value < minimum || value > maximum) {
                error = std::string(key) + " is outside the supported range";
                return false;
            }
            target = value;
            return true;
        };
        if (!setNumber("flashing_brightness", settings.FlashingBrightness, 1.0f, 8.0f)
            || !setNumber("fading_coefficient", settings.FadingCoefficient, -1000.0f, 1000.0f)
            || !setNumber("residual_alpha_scale", settings.ResidualAlphaScale, -1000.0f, 1000.0f))
            return ToolError(error);
        g_debugger->SetDisplaySettings(settings);
        return ToolResult({{"success", true}});
    }
    if (name == "raise_interrupt") {
        uint32_t index = 0;
        if (!GetInteger(args, "index", index, error, 0, 255))
            return ToolError(error);
        if (!g_chipset)
            return ToolError("Chipset interface is unavailable");
        g_chipset->RaiseInterrupt(static_cast<int>(index));
        return ToolResult({{"success", true}});
    }
    if (name == "keyboard_key") {
        uint32_t ki = 0;
        uint32_t ko = 0;
        bool pressed = false;
        if (!GetInteger(args, "ki", ki, error, 0, 7)
            || !GetInteger(args, "ko", ko, error, 0, 15)
            || !GetBool(args, "pressed", pressed, error))
            return ToolError(error);
        if (!g_keyboard)
            return ToolError("Keyboard interface is unavailable");
        g_keyboard->Key(static_cast<int>(ki), static_cast<int>(ko), pressed);
        return ToolResult({{"success", true}});
    }
    if (name == "keyboard_code") {
        uint32_t code = 0;
        bool pressed = false;
        if (!GetInteger(args, "code", code, error, 0, 255)
            || !GetBool(args, "pressed", pressed, error))
            return ToolError(error);
        if (!g_keyboard)
            return ToolError("Keyboard interface is unavailable");
        g_keyboard->PressCode(static_cast<uint8_t>(code), pressed);
        return ToolResult({{"success", true}});
    }
    if (name == "keyboard_release_all") {
        if (!g_keyboard)
            return ToolError("Keyboard interface is unavailable");
        g_keyboard->ReleaseAll();
        return ToolResult({{"success", true}});
    }
    if (name == "request_screenshot") {
        g_debugger->RequestScreenshot();
        return ToolResult({{"success", true}});
    }
    if (name == "set_recording") {
        bool start = false;
        if (!GetBool(args, "start", start, error))
            return ToolError(error);
        g_debugger->RequestRecording(start);
        return ToolResult({{"success", true}, {"start", start}});
    }
    if (name == "hot_reload_rom") {
        std::string reloadError;
        return g_debugger->HotReloadRom(reloadError)
            ? ToolResult({{"success", true}})
            : ToolError(reloadError);
    }

    return ToolError("Unknown tool: " + name);
}

json HandleRequest(const json& request, std::string* initializedSession = nullptr) {
    if (!request.is_object())
        return RpcError(nullptr, -32600, "Invalid Request");

    const auto id = request.contains("id") ? request["id"] : json(nullptr);
    const auto methodIt = request.find("method");
    if (methodIt == request.end() || !methodIt->is_string())
        return RpcError(id, -32600, "Invalid Request");
    const auto method = methodIt->get<std::string>();

    if (method == "initialize") {
        std::string sessionId = NewSessionId();
        if (initializedSession)
            *initializedSession = sessionId;
        {
            std::lock_guard lock(g_sessionsMutex);
            g_httpSessions[sessionId] = std::make_shared<EventQueue>();
        }
        return RpcResult(id, {
            {"protocolVersion", kLatestProtocolVersion},
            {"capabilities", {
                {"tools", {
                    {"listChanged", false},
                }},
                {"logging", json::object()},
            }},
            {"serverInfo", {
                {"name", kServerName},
                {"version", kServerVersion},
            }},
            {"instructions", "Control and inspect the CasioEmuMsvc debugger. Mutating tools affect the running emulator immediately."},
        });
    }
    if (method == "ping")
        return RpcResult(id, json::object());
    if (method == "tools/list")
        return RpcResult(id, {{"tools", ToolDefinitions()}});
    if (method == "tools/call") {
        const auto params = request.value("params", json::object());
        if (!params.is_object())
            return RpcError(id, -32602, "Invalid params");
        const auto name = params.value("name", "");
        const auto args = params.value("arguments", json::object());
        if (name.empty() || !args.is_object())
            return RpcError(id, -32602, "Invalid params");
        try {
            return RpcResult(id, CallTool(name, args));
        }
        catch (const std::exception& exception) {
            return RpcResult(id, ToolError(exception.what()));
        }
        catch (...) {
            return RpcResult(id, ToolError("Unknown debugger failure"));
        }
    }

    return RpcError(id, -32601, "Method not found");
}

void QueueEvent(const std::shared_ptr<EventQueue>& queue, const json& message) {
    if (!queue)
        return;
    {
        std::lock_guard lock(queue->Mutex);
        queue->Messages.push_back(message.dump());
    }
    queue->Cv.notify_all();
}

void BroadcastNotification(const json& notification) {
    std::vector<std::shared_ptr<EventQueue>> queues;
    {
        std::lock_guard lock(g_sessionsMutex);
        for (const auto& [_, queue] : g_legacySessions)
            queues.push_back(queue);
        for (const auto& [_, queue] : g_httpSessions)
            queues.push_back(queue);
    }
    for (const auto& queue : queues)
        QueueEvent(queue, notification);
}

void ServeEventStream(
    const std::shared_ptr<EventQueue>& queue,
    httplib::DataSink& sink,
    const std::string& initialEvent = {}) {
    if (!initialEvent.empty() && !sink.write(initialEvent.data(), initialEvent.size()))
        return;
    while (g_running.load() && sink.is_writable()) {
        std::unique_lock lock(queue->Mutex);
        queue->Cv.wait_for(lock, std::chrono::seconds(10), [&] {
            return !queue->Messages.empty() || !g_running.load();
        });
        if (!g_running.load())
            break;
        if (queue->Messages.empty()) {
            static constexpr char keepAlive[] = ": keepalive\n\n";
            if (!sink.write(keepAlive, sizeof(keepAlive) - 1))
                break;
            continue;
        }
        while (!queue->Messages.empty()) {
            auto payload = "event: message\ndata: " + queue->Messages.front() + "\n\n";
            queue->Messages.pop_front();
            lock.unlock();
            const bool written = sink.write(payload.data(), payload.size());
            lock.lock();
            if (!written)
                return;
        }
    }
}

void ConfigureServer(httplib::Server& server) {
    server.Options(R"(/.*)", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckOrigin(req, res))
            return;
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Accept, MCP-Session-Id, MCP-Protocol-Version");
        res.status = 204;
    });

    server.Post("/mcp", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckOrigin(req, res))
            return;
        try {
            const auto request = json::parse(req.body);
            if (!request.contains("id")) {
                res.status = 202;
                return;
            }
            std::string sessionId;
            auto response = HandleRequest(request, &sessionId);
            if (!sessionId.empty())
                res.set_header("MCP-Session-Id", sessionId);
            res.set_header("MCP-Protocol-Version", kLatestProtocolVersion);
            res.set_content(response.dump(), "application/json");
        }
        catch (const json::parse_error& exception) {
            res.status = 400;
            res.set_content(RpcError(nullptr, -32700, "Parse error", exception.what()).dump(), "application/json");
        }
        catch (const std::exception& exception) {
            res.status = 500;
            res.set_content(RpcError(nullptr, -32603, "Internal error", exception.what()).dump(), "application/json");
        }
    });

    server.Get("/mcp", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckOrigin(req, res))
            return;
        const auto sessionId = req.get_header_value("MCP-Session-Id");
        std::shared_ptr<EventQueue> queue;
        {
            std::lock_guard lock(g_sessionsMutex);
            const auto it = g_httpSessions.find(sessionId);
            if (it != g_httpSessions.end())
                queue = it->second;
        }
        if (!queue) {
            res.status = 404;
            res.set_content("Unknown MCP session", "text/plain");
            return;
        }
        res.set_header("Cache-Control", "no-cache");
        res.set_content_provider(
            "text/event-stream",
            [queue](size_t, httplib::DataSink& sink) {
                ServeEventStream(queue, sink);
                return true;
            });
    });

    server.Get("/sse", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckOrigin(req, res))
            return;
        const auto sessionId = NewSessionId();
        const auto queue = std::make_shared<EventQueue>();
        {
            std::lock_guard lock(g_sessionsMutex);
            g_legacySessions[sessionId] = queue;
        }
        res.set_header("Cache-Control", "no-cache");
        res.set_content_provider(
            "text/event-stream",
            [queue, sessionId](size_t, httplib::DataSink& sink) {
                const auto endpoint =
                    "event: endpoint\ndata: /messages?sessionId=" + sessionId + "\n\n";
                ServeEventStream(queue, sink, endpoint);
                {
                    std::lock_guard lock(g_sessionsMutex);
                    g_legacySessions.erase(sessionId);
                }
                return true;
            });
    });

    server.Post("/messages", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckOrigin(req, res))
            return;
        const auto sessionId = req.get_param_value("sessionId");
        std::shared_ptr<EventQueue> queue;
        {
            std::lock_guard lock(g_sessionsMutex);
            const auto it = g_legacySessions.find(sessionId);
            if (it != g_legacySessions.end())
                queue = it->second;
        }
        if (!queue) {
            res.status = 404;
            res.set_content("Unknown MCP session", "text/plain");
            return;
        }
        try {
            const auto request = json::parse(req.body);
            if (request.contains("id"))
                QueueEvent(queue, HandleRequest(request));
            res.status = 202;
            res.set_content("Accepted", "text/plain");
        }
        catch (const std::exception&) {
            res.status = 400;
            res.set_content("Malformed JSON-RPC message", "text/plain");
        }
    });

    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            json({
                {"status", "ok"},
                {"server", kServerName},
                {"version", kServerVersion},
                {"mcp_endpoint", "http://127.0.0.1:3001/mcp"},
                {"legacy_sse_endpoint", "http://127.0.0.1:3001/sse"},
            }).dump(2),
            "application/json");
    });
}

void ServerLoop() {
    httplib::Server server;
    ConfigureServer(server);
    if (!server.listen("127.0.0.1", 3001)) {
        BroadcastNotification({
            {"jsonrpc", "2.0"},
            {"method", "notifications/message"},
            {"params", {
                {"level", "error"},
                {"logger", kServerName},
                {"data", "Failed to listen on 127.0.0.1:3001"},
            }},
        });
    }
}

} // namespace

extern "C" __declspec(dllexport) void fPluginLoad(PluginApi* api) {
    if (!api)
        return;

    g_api = api;
    if (!api->RegisterPlugin(
            "mcp_server",
            "MCP Debugger Server",
            kServerVersion,
            "CasioEmuMsvc",
            "Exposes the native debugger through Model Context Protocol"))
        return;

    g_debugger = api->QueryInterface<IDebugger>();
    g_emulator = api->QueryInterface<IEmulator>();
    g_chipset = api->QueryInterface<IChipset>();
    g_keyboard = api->QueryInterface<IKeyboard>();
    g_hooks = api->QueryInterface<Hooks>();

    if (g_hooks) {
        g_hooks->SetupOnInstructionHook([](InstructionEventArgs& args) {
            if (!args.should_break)
                return;
            BroadcastNotification({
                {"jsonrpc", "2.0"},
                {"method", "notifications/message"},
                {"params", {
                    {"level", "info"},
                    {"logger", "casioemu.debugger"},
                    {"data", {
                        {"event", "execution_paused"},
                        {"reason", "breakpoint_or_step"},
                        {"pc_before", args.pc_before},
                        {"pc_after", args.pc_after},
                    }},
                }},
            });
        });
    }

    std::thread(ServerLoop).detach();
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH)
        g_running.store(false);
    return TRUE;
}
