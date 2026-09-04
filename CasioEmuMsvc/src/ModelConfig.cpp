#include "ModelConfig.h"

#include "Chipset/Eps6800Display.h"
#include "SvgBoardModelLoader.h"
#include "../../McpPlugin/json.hpp"

#include <SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace casioemu {
	static_assert(FindHardwareDescriptor(HW_EPS6800)->eps_status_size == EPS6800_STATUS_SIZE);
	static_assert(FindHardwareDescriptor(HW_EPS9500)->eps_status_size == EPS9500_STATUS_SIZE);
	static_assert(FindHardwareDescriptor(HW_EPS6800_W192)->eps_status_size == EPS6800_W192_STATUS_SIZE);

	namespace {
		using json = nlohmann::json;

		bool FileExists(const std::filesystem::path& path) {
			std::error_code ec;
			return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
		}

		std::filesystem::path ResolveModelFilePath(const std::filesystem::path& model_path, const std::string& file_name) {
			std::filesystem::path path(file_name);
			if (path.is_absolute())
				return path;
			return model_path / path;
		}

		json RectToJson(const Rect& rect) {
			return json{{"x", rect.x}, {"y", rect.y}, {"w", rect.w}, {"h", rect.h}};
		}

		Rect RectFromJson(const json& value) {
			for (const char* field : {"x", "y", "w", "h"}) {
				if (!value.contains(field))
					throw std::runtime_error(std::string("config.json rect must specify ") + field + ".");
			}
			Rect rect{};
			rect.x = value.at("x").get<int>();
			rect.y = value.at("y").get<int>();
			rect.w = value.at("w").get<int>();
			rect.h = value.at("h").get<int>();
			return rect;
		}

		json SpriteToJson(const SpriteInfo& sprite) {
			json value{{"src", RectToJson(sprite.src)}, {"dest", RectToJson(sprite.dest)}};
			if (!sprite.svg_shape.empty())
				value["svg_shape"] = sprite.svg_shape;
			if (!sprite.svg_defs.empty())
				value["svg_defs"] = sprite.svg_defs;
			return value;
		}

		SpriteInfo SpriteFromJson(const json& value) {
			SpriteInfo sprite{};
			if (!value.contains("src") || !value.contains("dest"))
				throw std::runtime_error("config.json sprite entries must specify src and dest.");
			sprite.src = RectFromJson(value.at("src"));
			sprite.dest = RectFromJson(value.at("dest"));
			if (value.contains("svg_shape"))
				sprite.svg_shape = value.at("svg_shape").get<std::string>();
			if (value.contains("svg_defs"))
				sprite.svg_defs = value.at("svg_defs").get<std::string>();
			return sprite;
		}

		std::string ReadJsonString(const json& value, std::initializer_list<const char*> names) {
			for (const char* name : names) {
				if (value.contains(name) && value.at(name).is_string())
					return value.at(name).get<std::string>();
			}
			return {};
		}

		const json* FindJsonMember(const json& value, std::initializer_list<const char*> names) {
			for (const char* name : names) {
				if (value.contains(name))
					return &value.at(name);
			}
			return nullptr;
		}

		const json& RequireJsonMember(const json& value, std::initializer_list<const char*> names, const char* field_name) {
			if (const json* item = FindJsonMember(value, names))
				return *item;
			throw std::runtime_error(std::string("config.json must specify ") + field_name + ".");
		}

		std::string RequireJsonString(const json& value, std::initializer_list<const char*> names, const char* field_name) {
			const json& item = RequireJsonMember(value, names, field_name);
			if (!item.is_string())
				throw std::runtime_error(std::string("config.json field ") + field_name + " must be a string.");
			return item.get<std::string>();
		}

		int RequireJsonInt(const json& value, std::initializer_list<const char*> names, const char* field_name) {
			const json& item = RequireJsonMember(value, names, field_name);
			if (!item.is_number_integer())
				throw std::runtime_error(std::string("config.json field ") + field_name + " must be an integer.");
			return item.get<int>();
		}

		double RequireJsonNumber(const json& value, std::initializer_list<const char*> names, const char* field_name) {
			const json& item = RequireJsonMember(value, names, field_name);
			if (!item.is_number())
				throw std::runtime_error(std::string("config.json field ") + field_name + " must be a number.");
			return item.get<double>();
		}

		bool ReadHardwareId(const json& value, unsigned short& hardware_id) {
			const json* item = FindJsonMember(value, {"hardware_id"});
			if (!item)
				return false;
			if (!item->is_number_integer())
				throw std::runtime_error("hardware_id must be a numeric HardwareId enum value.");
			hardware_id = item->get<unsigned short>();
			return true;
		}

		std::map<std::string, int> ReadBoardStatusSpriteMap(const json& value) {
			const json& sprites = RequireJsonMember(value, {"sprites"}, "sprites");
			if (!sprites.is_object())
				throw std::runtime_error("config.json field sprites must be an object.");
			std::map<std::string, int> result;
			for (auto it = sprites.begin(); it != sprites.end(); ++it) {
				if (it.key() == "rsd_interface")
					continue;
				const auto& sprite = it.value();
				if (!sprite.is_object() || !sprite.contains("index") || !sprite.at("index").is_number_integer())
					throw std::runtime_error("config.json board status sprite entries must be objects with integer index.");
				result[it.key()] = sprite.at("index").get<int>();
			}
			if (result.empty())
				throw std::runtime_error("config.json sprites must include at least one status sprite index.");
			return result;
		}

		std::vector<StatusIndicatorInfo> ReadStatusIndicators(const json& value) {
			std::vector<StatusIndicatorInfo> result;
			if (!value.contains("status"))
				return result;
			const auto& status = value.at("status");
			if (!status.is_array())
				throw std::runtime_error("config.json field status must be an array.");
			std::set<std::string> names;
			std::set<std::pair<int, int>> bits;
			for (const auto& item : status) {
				if (!item.is_object() || !item.contains("sprite") || !item.at("sprite").is_string() ||
					!item.contains("byte") || !item.at("byte").is_number_integer() ||
					!item.contains("bit") || !item.at("bit").is_number_integer())
					throw std::runtime_error("config.json status entries must specify string sprite and integer byte/bit.");
				const std::string name = item.at("sprite").get<std::string>();
				const int byte_offset = item.at("byte").get<int>();
				const int bit = item.at("bit").get<int>();
				if (name.empty() || !names.insert(name).second)
					throw std::runtime_error("config.json status sprite names must be non-empty and unique.");
				if (byte_offset < 0 || byte_offset > 0xffff || bit < 0 || bit > 7)
					throw std::runtime_error("config.json status byte/bit is out of range.");
				if (!bits.emplace(byte_offset, bit).second)
					throw std::runtime_error("config.json assigns more than one status sprite to the same LCD bit.");
				result.push_back({name, static_cast<unsigned short>(byte_offset), static_cast<unsigned char>(bit)});
			}
			return result;
		}

		void ApplyBoardButtonOverrides(const json& buttons, ModelInfo& model) {
			if (!buttons.is_array())
				throw std::runtime_error("config.json field buttons must be an array.");
			std::set<int> board_kikos;
			for (const auto& button : model.buttons) {
				if (!board_kikos.insert(button.kiko).second)
					throw std::runtime_error("board SVG contains duplicate button kiko values.");
			}
			std::set<int> configured_kikos;
			for (const auto& entry : buttons) {
				if (!entry.is_object())
					throw std::runtime_error("config.json board button entries must be objects.");
				if (!entry.contains("kiko") || !entry.at("kiko").is_number_integer() ||
					!entry.contains("keyname") || !entry.at("keyname").is_string())
					throw std::runtime_error("config.json board button entries must specify integer kiko and string keyname.");
				const int kiko = entry.at("kiko").get<int>();
				if (!configured_kikos.insert(kiko).second)
					throw std::runtime_error("config.json board button kiko values must be unique.");
				auto button = std::find_if(model.buttons.begin(), model.buttons.end(),
					[kiko](const ButtonInfo& item) { return item.kiko == kiko; });
				if (button == model.buttons.end())
					throw std::runtime_error("config.json board button kiko was not found in board SVG.");
				button->keyname = entry.at("keyname").get<std::string>();
			}
		}

		SpriteInfo RequireBoardInterfaceSprite(const json& value) {
			const json& sprites = RequireJsonMember(value, {"sprites"}, "sprites");
			if (!sprites.is_object() || !sprites.contains("rsd_interface"))
				throw std::runtime_error("config.json must specify sprites.rsd_interface.");
			const auto& interface_sprite = sprites.at("rsd_interface");
			if (!interface_sprite.is_object() || !interface_sprite.contains("src"))
				throw std::runtime_error("config.json must specify sprites.rsd_interface.src.");
			if (!interface_sprite.contains("dest"))
				throw std::runtime_error("config.json must specify sprites.rsd_interface.dest.");
			SpriteInfo sprite{};
			sprite.src = RectFromJson(interface_sprite.at("src"));
			sprite.dest = RectFromJson(interface_sprite.at("dest"));
			if (sprite.src.w <= 0 || sprite.src.h <= 0)
				throw std::runtime_error("config.json sprites.rsd_interface.src must have positive width and height.");
			if (sprite.dest.w <= 0 || sprite.dest.h <= 0)
				throw std::runtime_error("config.json sprites.rsd_interface.dest must have positive width and height.");
			if (sprite.dest.x != 0 || sprite.dest.y != 0)
				throw std::runtime_error("config.json sprites.rsd_interface.dest x and y must be zero.");
			return sprite;
		}

		void RequireBaseModelFields(const json& value) {
			static constexpr const char* required_fields[] = {
				"model_name", "hardware_id", "csr_mask", "real_hardware", "pd_value", "interface_path",
				"rom_path", "flash_path", "ink_color", "enable_new_screen", "is_sample_rom", "legacy_ko",
				"u16_mode", "ml620_mirroring"};
			for (const char* field : required_fields) {
				if (!value.contains(field))
					throw std::runtime_error(std::string("config.json must specify ") + field + ".");
			}
			if (!value.contains("large_model") && !value.contains("LARGE_model"))
				throw std::runtime_error("config.json must specify large_model.");
			const auto& color = value.at("ink_color");
			if (!color.is_object() || !color.contains("r") || !color.contains("g") || !color.contains("b"))
				throw std::runtime_error("config.json must specify ink_color.r, ink_color.g, and ink_color.b.");
		}

		void RequireBoardModelFields(const json& value) {
			RequireJsonString(value, {"board_path"}, "board_path");
			RequireJsonInt(value, {"screen_width"}, "screen_width");
			RequireJsonInt(value, {"screen_height"}, "screen_height");
			RequireJsonNumber(value, {"screen_scale_y"}, "screen_scale_y");
			RequireBoardInterfaceSprite(value);
			ReadBoardStatusSpriteMap(value);
		}

		void RequireSpriteModelFields(const json& value) {
			if (!value.contains("buttons") || !value.at("buttons").is_array())
				throw std::runtime_error("config.json must specify a buttons array for non-board models.");
			if (!value.contains("sprites") || !value.at("sprites").is_object())
				throw std::runtime_error("config.json must specify a sprites object for non-board models.");
		}

		void ApplyModelInfoJsonValue(const json& value, ModelInfo& model, bool reset) {
			if (reset) {
				model = {};
			}
			if (value.contains("model_name"))
				model.model_name = value.at("model_name").get<std::string>();
			unsigned short hardware_id{};
			if (ReadHardwareId(value, hardware_id)) {
				model.hardware_id = hardware_id;
			}
			if (value.contains("csr_mask"))
				model.csr_mask = value.at("csr_mask").get<unsigned short>();
			if (value.contains("real_hardware"))
				model.real_hardware = value.at("real_hardware").get<bool>();
			if (value.contains("pd_value"))
				model.pd_value = value.at("pd_value").get<unsigned char>();
			if (value.contains("interface_path"))
				model.interface_path = value.at("interface_path").get<std::string>();
			if (const auto board_path = ReadJsonString(value, {"board_path"}); !board_path.empty())
				model.board_path = board_path;
			if (value.contains("rom_path"))
				model.rom_path = value.at("rom_path").get<std::string>();
			if (value.contains("flash_path"))
				model.flash_path = value.at("flash_path").get<std::string>();
			if (value.contains("ink_color")) {
				const auto& color = value.at("ink_color");
				model.ink_color = {color.at("r").get<int>(), color.at("g").get<int>(), color.at("b").get<int>()};
			}
			if (value.contains("enable_new_screen"))
				model.enable_new_screen = value.at("enable_new_screen").get<bool>();
			if (value.contains("is_sample_rom"))
				model.is_sample_rom = value.at("is_sample_rom").get<bool>();
			if (value.contains("legacy_ko"))
				model.legacy_ko = value.at("legacy_ko").get<bool>();
			if (value.contains("u16_mode"))
				model.u16_mode = value.at("u16_mode").get<bool>();
			if (value.contains("large_model"))
				model.LARGE_model = value.at("large_model").get<bool>();
			if (value.contains("LARGE_model"))
				model.LARGE_model = value.at("LARGE_model").get<bool>();
			if (value.contains("ml620_mirroring"))
				model.ml620_mirroring = value.at("ml620_mirroring").get<bool>();
			if (const json* item = FindJsonMember(value, {"screen_width"}))
				model.screen_width = item->get<int>();
			if (const json* item = FindJsonMember(value, {"screen_height"}))
				model.screen_height = item->get<int>();
			if (const json* item = FindJsonMember(value, {"screen_scale_y"}))
				model.screen_scale_y = item->get<double>();
			if (value.contains("extra")) {
				auto extra = value.at("extra").get<std::map<std::string, std::string>>();
				if (reset) {
					model.extra = std::move(extra);
				}
				else {
					for (auto& [key, item] : extra)
						model.extra[key] = std::move(item);
				}
			}
			if (value.contains("status")) {
				auto configured = ReadStatusIndicators(value);
				if (!model.status_indicators.empty() && !model.board_path.empty()) {
					if (configured.size() != model.status_indicators.size())
						throw std::runtime_error("config.json status does not match board SVG status definitions.");
					for (size_t i = 0; i < configured.size(); ++i) {
						const auto& lhs = configured[i];
						const auto& rhs = model.status_indicators[i];
						if (lhs.sprite_name != rhs.sprite_name || lhs.byte_offset != rhs.byte_offset || lhs.bit != rhs.bit)
							throw std::runtime_error("config.json status does not match board SVG status definitions.");
					}
				}
				else {
					model.status_indicators = std::move(configured);
				}
			}
			if (value.contains("buttons")) {
				if (!model.board_path.empty()) {
					if (!model.buttons.empty())
						ApplyBoardButtonOverrides(value.at("buttons"), model);
				}
				else {
					model.buttons.clear();
					for (const auto& item : value.at("buttons")) {
						ButtonInfo button{};
						if (!item.contains("rect") || !item.contains("kiko") || !item.contains("keyname"))
							throw std::runtime_error("config.json button entries must specify rect, kiko, and keyname.");
						button.rect = RectFromJson(item.at("rect"));
						button.kiko = item.at("kiko").get<int>();
						button.keyname = item.at("keyname").get<std::string>();
						if (item.contains("svg_shape"))
							button.svg_shape = item.at("svg_shape").get<std::string>();
						if (item.contains("svg_defs"))
							button.svg_defs = item.at("svg_defs").get<std::string>();
						model.buttons.push_back(button);
					}
				}
			}
			if (value.contains("sprites") && model.board_path.empty()) {
				model.sprites.clear();
				for (auto it = value.at("sprites").begin(); it != value.at("sprites").end(); ++it)
					model.sprites[it.key()] = SpriteFromJson(it.value());
			}
		}

		void ValidateStatusIndicators(const ModelInfo& model) {
			const auto* descriptor = FindHardwareDescriptor(model.hardware_id);
			if (!descriptor || descriptor->eps_status_size == 0)
				return;
			for (const auto& indicator : model.status_indicators) {
				if (indicator.byte_offset >= descriptor->eps_status_size)
					throw std::runtime_error(std::string(descriptor->display_name) + " status byte is outside the LCD status area for sprite " + indicator.sprite_name + ".");
			}
		}

		bool RequiresEpsStatusMetadata(unsigned short hardware_id) {
			const auto* descriptor = FindHardwareDescriptor(hardware_id);
			return descriptor && descriptor->eps_status_size != 0;
		}
	}

	void WriteModelInfoJson(std::ostream& os, const ModelInfo& model) {
		json value;
		value["format"] = "CasioEmuMsvc.ModelInfo";
		value["version"] = 1;
		value["model_name"] = model.model_name;
		value["hardware_id"] = model.hardware_id;
		value["csr_mask"] = model.csr_mask;
		value["real_hardware"] = model.real_hardware;
		value["pd_value"] = model.pd_value;
		value["interface_path"] = model.interface_path;
		if (!model.board_path.empty())
			value["board_path"] = model.board_path;
		value["rom_path"] = model.rom_path;
		value["flash_path"] = model.flash_path;
		value["ink_color"] = {{"r", model.ink_color.r}, {"g", model.ink_color.g}, {"b", model.ink_color.b}};
		value["enable_new_screen"] = model.enable_new_screen;
		value["is_sample_rom"] = model.is_sample_rom;
		value["legacy_ko"] = model.legacy_ko;
		value["u16_mode"] = model.u16_mode;
		value["large_model"] = model.LARGE_model;
		value["ml620_mirroring"] = model.ml620_mirroring;
		if (!model.board_path.empty()) {
			value["screen_width"] = model.screen_width;
			value["screen_height"] = model.screen_height;
			value["screen_scale_y"] = model.screen_scale_y;
		}
		if (!model.extra.empty())
			value["extra"] = model.extra;
		if (!model.status_indicators.empty()) {
			value["status"] = json::array();
			for (const auto& indicator : model.status_indicators) {
				value["status"].push_back({
					{"sprite", indicator.sprite_name},
					{"byte", indicator.byte_offset},
					{"bit", indicator.bit},
				});
			}
		}

		if (model.board_path.empty()) {
			value["buttons"] = json::array();
			for (const auto& button : model.buttons) {
				json item{
					{"rect", RectToJson(button.rect)},
					{"kiko", button.kiko},
					{"keyname", button.keyname},
				};
				if (!button.svg_shape.empty())
					item["svg_shape"] = button.svg_shape;
				if (!button.svg_defs.empty())
					item["svg_defs"] = button.svg_defs;
				value["buttons"].push_back(item);
			}

			value["sprites"] = json::object();
			for (const auto& [name, sprite] : model.sprites)
				value["sprites"][name] = SpriteToJson(sprite);
		}
		else {
			if (auto it = model.sprites.find("rsd_interface"); it != model.sprites.end()) {
				value["sprites"] = json::object();
				value["sprites"]["rsd_interface"] = SpriteToJson(it->second);
			}
			for (const auto& [name, index] : model.status_sprite_indexes) {
				value["sprites"][name] = {{"index", index}};
			}
			value["buttons"] = json::array();
			for (const auto& button : model.buttons) {
				value["buttons"].push_back({
					{"keyname", button.keyname},
					{"kiko", button.kiko},
				});
			}
		}

		os << value.dump(2) << '\n';
	}

	void ReadModelInfoJson(std::istream& is, ModelInfo& model) {
		json value = json::parse(is);
		RequireBaseModelFields(value);
		ApplyModelInfoJsonValue(value, model, true);
	}

	void SaveModelInfoBinCache(const std::filesystem::path& model_path, const ModelInfo& model) {
		try {
			std::ofstream stream(model_path / MODEL_CONFIG_BIN, std::ios::binary);
			if (!stream) {
				SDL_Log("[ModelInfo][Warn] Cannot open config.bin for writing: %s", model_path.string().c_str());
				return;
			}
			model.Write(stream);
		}
		catch (const std::exception& ex) {
			SDL_Log("[ModelInfo][Warn] Failed to write config.bin cache for %s: %s", model_path.string().c_str(), ex.what());
		}
	}

	bool LoadModelInfoFromFolder(
		const std::filesystem::path& model_path,
		ModelInfo& model,
		std::string* loaded_from,
		std::string* error) {
		try {
			const auto json_path = model_path / MODEL_CONFIG_JSON;
			const auto bin_path = model_path / MODEL_CONFIG_BIN;
			const bool has_json = FileExists(json_path);
			bool has_fresh_bin = FileExists(bin_path) &&
				(!has_json || std::filesystem::last_write_time(bin_path) >= std::filesystem::last_write_time(json_path));
			if (has_fresh_bin && has_json) {
				std::ifstream stream(json_path);
				if (!stream)
					throw std::runtime_error("Cannot open config.json.");
				json value = json::parse(stream);
				const std::string requested_board = ReadJsonString(value, {"board_path"});
				if (!requested_board.empty()) {
					const auto board_path = ResolveModelFilePath(model_path, requested_board);
					if (FileExists(board_path))
						has_fresh_bin = std::filesystem::last_write_time(bin_path) >= std::filesystem::last_write_time(board_path);
				}
			}
			if (has_fresh_bin) {
				std::ifstream stream(bin_path, std::ios::binary);
				if (!stream)
					throw std::runtime_error("Cannot open config.bin.");
				model.Read(stream);
				ValidateStatusIndicators(model);
				if (loaded_from)
					*loaded_from = MODEL_CONFIG_BIN;
				return true;
			}

			if (has_json) {
				std::ifstream stream(json_path);
				if (!stream)
					throw std::runtime_error("Cannot open config.json.");
				json value = json::parse(stream);
				RequireBaseModelFields(value);
				const std::string requested_board = ReadJsonString(value, {"board_path"});
				const std::string requested_interface = ReadJsonString(value, {"interface_path"});
				const std::string requested_rom = ReadJsonString(value, {"rom_path"});
				const std::string requested_flash = ReadJsonString(value, {"flash_path"});
				if (!requested_board.empty()) {
					RequireBoardModelFields(value);
					unsigned short hardware_id{};
					ReadHardwareId(value, hardware_id);
					const auto requested_status_indexes = ReadBoardStatusSpriteMap(value);
					const int display_w = RequireJsonInt(value, {"screen_width"}, "screen_width");
					const int display_h = RequireJsonInt(value, {"screen_height"}, "screen_height");
					const double screen_scale_y = RequireJsonNumber(value, {"screen_scale_y"}, "screen_scale_y");
					const SpriteInfo interface_sprite = RequireBoardInterfaceSprite(value);
					model = LoadSvgBoardModelInfo(model_path, hardware_id, requested_board, requested_interface, requested_rom, requested_flash, interface_sprite.src, interface_sprite.dest, display_w, display_h, screen_scale_y, requested_status_indexes);
					ApplyModelInfoJsonValue(value, model, false);
					if (RequiresEpsStatusMetadata(hardware_id) && model.status_indicators.size() != requested_status_indexes.size())
						throw std::runtime_error("EPS board SVG must define data-status-byte and data-status-bit for every status sprite.");
				}
				else {
					RequireSpriteModelFields(value);
					ApplyModelInfoJsonValue(value, model, true);
				}
				ValidateStatusIndicators(model);
				if (loaded_from)
					*loaded_from = MODEL_CONFIG_JSON;
				SaveModelInfoBinCache(model_path, model);
				return true;
			}

			throw std::runtime_error("config.json/config.bin was not found.");
		}
		catch (const std::exception& ex) {
			if (error)
				*error = ex.what();
			return false;
		}
	}

	bool LoadModelInfoFromResourceStore(
		const ModelResourceStore& resources,
		ModelInfo& model,
		std::string* error) {
		try {
			if (!resources.Exists(MODEL_CONFIG_JSON))
				throw std::runtime_error("config.json was not found.");
			const auto data = resources.Read(MODEL_CONFIG_JSON);
			json value = json::parse(data.begin(), data.end());
			RequireBaseModelFields(value);
			const std::string requested_board = ReadJsonString(value, {"board_path"});
			const std::string requested_interface = ReadJsonString(value, {"interface_path"});
			const std::string requested_rom = ReadJsonString(value, {"rom_path"});
			const std::string requested_flash = ReadJsonString(value, {"flash_path"});
			if (!requested_board.empty()) {
				RequireBoardModelFields(value);
				unsigned short hardware_id{};
				ReadHardwareId(value, hardware_id);
				const auto requested_status_indexes = ReadBoardStatusSpriteMap(value);
				const int display_w = RequireJsonInt(value, {"screen_width"}, "screen_width");
				const int display_h = RequireJsonInt(value, {"screen_height"}, "screen_height");
				const double screen_scale_y = RequireJsonNumber(value, {"screen_scale_y"}, "screen_scale_y");
				const SpriteInfo interface_sprite = RequireBoardInterfaceSprite(value);
				model = LoadSvgBoardModelInfo(resources, hardware_id, requested_board, requested_interface, requested_rom,
					requested_flash, interface_sprite.src, interface_sprite.dest, display_w, display_h, screen_scale_y,
					requested_status_indexes);
				ApplyModelInfoJsonValue(value, model, false);
				if (RequiresEpsStatusMetadata(hardware_id) && model.status_indicators.size() != requested_status_indexes.size())
					throw std::runtime_error("EPS board SVG must define data-status-byte and data-status-bit for every status sprite.");
			}
			else {
				RequireSpriteModelFields(value);
				ApplyModelInfoJsonValue(value, model, true);
			}
			ValidateStatusIndicators(model);
			return true;
		}
		catch (const std::exception& ex) {
			if (error) *error = ex.what();
			return false;
		}
	}

	void SaveModelInfoJson(const std::filesystem::path& model_path, const ModelInfo& model) {
		std::filesystem::create_directories(model_path);
		std::ofstream stream(model_path / MODEL_CONFIG_JSON);
		if (!stream)
			throw std::runtime_error("Cannot open config.json for writing.");
		WriteModelInfoJson(stream, model);
		stream.close();
		SaveModelInfoBinCache(model_path, model);
	}
}
