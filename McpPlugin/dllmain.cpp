// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

#include "PluginApi.h"
#include "json.hpp" // nlohmann/json

#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include "httplib.h"

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <set>

using json = nlohmann::json;

PluginApi* g_api = nullptr;
ICPU* g_cpu = nullptr;
IMMU* g_mmu = nullptr;
IChipset* g_chipset = nullptr;
IEmulator* g_emu = nullptr;
IKeyboard* g_kbd = nullptr;
Hooks* g_hooks = nullptr;

bool g_mcp_running = true;

// SSE State
std::mutex mcp_q_mtx;
std::condition_variable mcp_q_cv;
std::deque<std::string> mcp_tx_queue;

// Breakpoint State
std::mutex bp_mtx;
std::set<uint32_t> breakpoints;

json handle_mcp_request(const std::string& bodyStr) {
    json req = json::parse(bodyStr);
    std::string method = req.value("method", "");
    
    if (method == "initialize") {
        return {
            {"jsonrpc", "2.0"},
            {"id", req["id"]},
            {"result", {
                {"protocolVersion", "2024-11-05"},
                {"capabilities", {
                    {"tools", json::object()}
                }},
                {"serverInfo", {
                    {"name", "casioemu-mcp-sse"},
                    {"version", "2.0.0"}
                }}
            }}
        };
    }
    else if (method == "tools/list") {
        return {
            {"jsonrpc", "2.0"},
            {"id", req["id"]},
            {"result", {
                {"tools", json::array({
                    {
                        {"name", "get_status"},
                        {"description", "Get emulator runtime info, PC, CPS, voltage, model name."},
                        {"inputSchema", { {"type", "object"}, {"properties", json::object()} }}
                    },
                    {
                        {"name", "pause"},
                        {"description", "Pause the main execution loop."},
                        {"inputSchema", { {"type", "object"}, {"properties", json::object()} }}
                    },
                    {
                        {"name", "resume"},
                        {"description", "Resume execution."},
                        {"inputSchema", { {"type", "object"}, {"properties", json::object()} }}
                    },
                    {
                        {"name", "read_register"},
                        {"description", "Read a CPU register (e.g., PC, R0..R15, LR)"},
                        {"inputSchema", {
                            {"type", "object"},
                            {"properties", { {"name", {{"type", "string"}, {"description", "Register name"}}} }},
                            {"required", json::array({"name"})}
                        }}
                    },
                    {
                        {"name", "write_register"},
                        {"description", "Write a 16-bit value to a CPU register"},
                        {"inputSchema", {
                            {"type", "object"},
                            {"properties", { 
                                {"name", {{"type", "string"}}},
                                {"value", {{"type", "integer"}}} 
                            }},
                            {"required", json::array({"name", "value"})}
                        }}
                    },
                    {
                        {"name", "read_memory"},
                        {"description", "Read an array of bytes from memory space."},
                        {"inputSchema", {
                            {"type", "object"},
                            {"properties", { 
                                {"address", {{"type", "integer"}}},
                                {"size", {{"type", "integer"}}} 
                            }},
                            {"required", json::array({"address", "size"})}
                        }}
                    },
                    {
                        {"name", "write_memory"},
                        {"description", "Write an array of bytes to memory space."},
                        {"inputSchema", {
                            {"type", "object"},
                            {"properties", { 
                                {"address", {{"type", "integer"}}},
                                {"bytes", {{"type", "array"}, {"items", {{"type", "integer"}}}}} 
                            }},
                            {"required", json::array({"address", "bytes"})}
                        }}
                    },
                    {
                        {"name", "read_code"},
                        {"description", "Read code words (16-bit) from the ROM/Flash space."},
                        {"inputSchema", {
                            {"type", "object"},
                            {"properties", { 
                                {"address", {{"type", "integer"}}},
                                {"count", {{"type", "integer"}, {"description", "Number of 16-bit words"}}} 
                            }},
                            {"required", json::array({"address", "count"})}
                        }}
                    },
                    {
                        {"name", "write_code"},
                        {"description", "Write an array of bytes to ROM/Flash code space."},
                        {"inputSchema", {
                            {"type", "object"},
                            {"properties", { 
                                {"address", {{"type", "integer"}}},
                                {"bytes", {{"type", "array"}, {"items", {{"type", "integer"}}}}} 
                            }},
                            {"required", json::array({"address", "bytes"})}
                        }}
                    },
                    {
                        {"name", "raise_interrupt"},
                        {"description", "Raise a CPU interrupt manually via chipset maskables."},
                        {"inputSchema", {
                            {"type", "object"},
                            {"properties", { 
                                {"index", {{"type", "integer"}}} 
                            }},
                            {"required", json::array({"index"})}
                        }}
                    },
                    {
                        {"name", "kbd_key"},
                        {"description", "Press or release a keyboard button based on KI/KO matrix index."},
                        {"inputSchema", {
                            {"type", "object"},
                            {"properties", { 
                                {"ki", {{"type", "integer"}}},
                                {"ko", {{"type", "integer"}}},
                                {"pressed", {{"type", "boolean"}}} 
                            }},
                            {"required", json::array({"ki", "ko", "pressed"})}
                        }}
                    },
                    {
                        {"name", "kbd_release_all"},
                        {"description", "Release all pressed keyboard buttons."},
                        {"inputSchema", { {"type", "object"}, {"properties", json::object()} }}
                    },
                    {
                        {"name", "set_breakpoint"},
                        {"description", "Set an execution breakpoint at a specific PC address."},
                        {"inputSchema", {
                            {"type", "object"},
                            {"properties", { 
                                {"pc", {{"type", "integer"}}} 
                            }},
                            {"required", json::array({"pc"})}
                        }}
                    },
                    {
                        {"name", "clear_breakpoint"},
                        {"description", "Clear a breakpoint from a specific PC address."},
                        {"inputSchema", {
                            {"type", "object"},
                            {"properties", { 
                                {"pc", {{"type", "integer"}}} 
                            }},
                            {"required", json::array({"pc"})}
                        }}
                    }
                })}
            }}
        };
    }
    else if (method == "tools/call") {
        std::string name = req["params"].value("name", "");
        json args = req["params"].value("arguments", json::object());
        json result = json::object();
        
        if (name == "get_status") {
            if (g_emu) {
                //result["model_name"] = g_emu->GetRunningModelName();
                //result["rom_path"] = g_emu->GetRunningRomPath();
                result["paused"] = g_emu->IsPaused();
                result["cps"] = g_emu->GetCyclesPerSecond();
                if (g_emu->SolarPanelVoltage()) result["solar_voltage"] = *g_emu->SolarPanelVoltage();
                if (g_emu->BatteryVoltage()) result["battery_voltage"] = *g_emu->BatteryVoltage();
            } else {
                result["error"] = "Emulator interface not ready";
            }
            if (g_chipset) {
                result["run_status"] = (int)g_chipset->GetStatus();
            }
            if (g_cpu) {
                uint16_t* pc = g_cpu->Register("PC");
                if (pc) result["PC"] = *pc;
            }
        }
        else if (name == "pause") {
            if (g_emu) g_emu->Pause();
            result["success"] = true;
        }
        else if (name == "resume") {
            if (g_emu) g_emu->Resume();
            result["success"] = true;
        }
        else if (name == "read_register") {
            std::string reg = args.value("name", "");
            if (g_cpu) {
                uint16_t* ptr = g_cpu->Register(reg.c_str());
                if (ptr) result["value"] = *ptr;
                else result["error"] = "Register not found";
            }
        }
        else if (name == "write_register") {
            std::string reg = args.value("name", "");
            uint16_t val = args.value("value", 0);
            if (g_cpu) {
                uint16_t* ptr = g_cpu->Register(reg.c_str());
                if (ptr) { *ptr = val; result["success"] = true; }
                else result["error"] = "Register not found";
            }
        }
        else if (name == "read_memory") {
            int addr = args.value("address", 0);
            int size = args.value("size", 1);
            if (g_mmu) {
                json data = json::array();
                for (int i=0; i<size; i++) data.push_back(g_mmu->ReadData(addr + i));
                result["bytes"] = data;
            }
        }
        else if (name == "write_memory") {
            int addr = args.value("address", 0);
            if (args.contains("bytes") && args["bytes"].is_array() && g_mmu) {
                int i = 0;
                for (auto& b : args["bytes"]) {
                    g_mmu->WriteData(addr + i, b.get<uint8_t>());
                    i++;
                }
                result["success"] = true;
                result["written"] = i;
            }
        }
        else if (name == "read_code") {
            int addr = args.value("address", 0);
            int count = args.value("count", 1);
            if (g_mmu) {
                json words = json::array();
                for (int i=0; i<count; i++) words.push_back(g_mmu->ReadCode(addr + i*2)); 
                result["words"] = words;
            }
        }
        else if (name == "write_code") {
            int addr = args.value("address", 0);
            if (args.contains("bytes") && args["bytes"].is_array() && g_mmu) {
                int i = 0;
                for (auto& b : args["bytes"]) {
                    g_mmu->WriteCode(addr + i, b.get<uint8_t>());
                    i++;
                }
                result["success"] = true;
                result["written"] = i;
            }
        }
        else if (name == "raise_interrupt") {
            int idx = args.value("index", 0);
            if (g_chipset) {
                g_chipset->RaiseInterrupt(idx);
                result["success"] = true;
            }
        }
        else if (name == "kbd_key") {
            int ki = args.value("ki", 0);
            int ko = args.value("ko", 0);
            bool pressed = args.value("pressed", false);
            if (g_kbd) { g_kbd->Key(ki, ko, pressed); result["success"] = true; }
            else result["error"] = "Keyboard interface unavailable";
        }
        else if (name == "kbd_release_all") {
            if (g_kbd) { g_kbd->ReleaseAll(); result["success"] = true; }
            else result["error"] = "Keyboard interface unavailable";
        }
        else if (name == "set_breakpoint") {
            uint32_t pc = args.value("pc", 0);
            {
                std::lock_guard<std::mutex> lk(bp_mtx);
                breakpoints.insert(pc);
            }
            result["success"] = true;
        }
        else if (name == "clear_breakpoint") {
            uint32_t pc = args.value("pc", 0);
            {
                std::lock_guard<std::mutex> lk(bp_mtx);
                breakpoints.erase(pc);
            }
            result["success"] = true;
        }
        else {
            result["error"] = "Unknown tool called";
        }
        
        json resp;
        resp["jsonrpc"] = "2.0";
        resp["id"] = req["id"];
        resp["result"]["content"] = json::array();
        
        json text_content = json::object();
        text_content["type"] = "text";
        text_content["text"] = result.dump(4);
        
        resp["result"]["content"].push_back(text_content);
        return resp;
    }
    
    return json::object();
}

void mcp_server_loop() {
    httplib::Server svr;
    
    // Serve SSE endpoints
    svr.Get("/sse", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        
        res.set_content_provider(
            "text/event-stream",
            [](size_t offset, httplib::DataSink &sink) {
                std::string init_msg = "event: endpoint\ndata: /messages\n\n";
                sink.write(init_msg.data(), init_msg.size());
                
                bool client_connected = true;
                while (g_mcp_running && client_connected) {
                    std::unique_lock<std::mutex> lk(mcp_q_mtx);
                    mcp_q_cv.wait_for(lk, std::chrono::milliseconds(200));
                    
                    while (!mcp_tx_queue.empty()) {
                        std::string m = "event: message\ndata: " + mcp_tx_queue.front() + "\n\n";
                        mcp_tx_queue.pop_front();
                        if (!sink.write(m.data(), m.size())) {
                            client_connected = false;
                            break;
                        }
                    }
                }
                return true;
            }
        );
    });
    
    // Handle POST Messages
    svr.Post("/messages", [](const httplib::Request& req, httplib::Response& res) {
        try {
            json resp = handle_mcp_request(req.body);
            if (!resp.empty()) {
                std::lock_guard<std::mutex> lk(mcp_q_mtx);
                mcp_tx_queue.push_back(resp.dump());
                mcp_q_cv.notify_one();
            }
        } catch (...) {
            // Ignore malformed requests
        }
        res.status = 202;
        res.set_content("Accepted", "text/plain");
    });
    
    // Listen to Port 3001 locally for MCP SSE Session!
    svr.listen("127.0.0.1", 3001);
}

extern "C" _declspec(dllexport) void fPluginLoad(PluginApi* api) {
	if (api == nullptr)
		return;
	
    g_api = api;
	if (!api->RegisterPlugin("mcp_server", "McpPlugin", 0))
		return;
        
	g_cpu = api->QueryInterface<ICPU>();
	g_mmu = api->QueryInterface<IMMU>();
	g_chipset = api->QueryInterface<IChipset>();
	g_emu = api->QueryInterface<IEmulator>();
    g_kbd = api->QueryInterface<IKeyboard>();
    g_hooks = api->QueryInterface<Hooks>();
    
    if (g_hooks) {
        g_hooks->SetupOnInstructionHook([](InstructionEventArgs& args) {
            bool is_bp = false;
            {
                std::lock_guard<std::mutex> lk(bp_mtx);
                if (breakpoints.count(args.pc_before)) {
                    is_bp = true;
                }
            }
            if (is_bp) {
                if (g_emu) g_emu->Pause();
                
                // Construct MCP Server Event Payload for Notification
                json notif = {
                    {"jsonrpc", "2.0"},
                    {"method", "notifications/message"},
                    {"params", {
                        {"message", "Breakpoint hit at PC: " + std::to_string(args.pc_before)}
                    }}
                };
                
                std::lock_guard<std::mutex> lk(mcp_q_mtx);
                mcp_tx_queue.push_back(notif.dump());
                mcp_q_cv.notify_one();
            }
        });
    }

    std::thread(mcp_server_loop).detach();
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
