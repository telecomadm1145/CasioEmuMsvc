#include "ModelInfo.h"

#include "../../McpPlugin/json.hpp"
#include "tinyxml2/tinyxml2.h"

#include <SDL.h>
#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace casioemu {
	namespace {
		using json = nlohmann::json;

		struct SvgRect {
			double x{};
			double y{};
			double w{};
			double h{};
		};

		std::string ToUpperAscii(std::string value) {
			for (char& ch : value) {
				if (ch >= 'a' && ch <= 'z')
					ch = static_cast<char>(ch - 'a' + 'A');
			}
			return value;
		}

		bool FileExists(const std::filesystem::path& path) {
			std::error_code ec;
			return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
		}

		std::string ReadTextFile(const std::filesystem::path& path) {
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
				throw std::runtime_error("Cannot open " + path.string());
			return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
		}

		int RoundToInt(double value) {
			if (!std::isfinite(value))
				return 0;
			return static_cast<int>(std::lround(value));
		}

		Rect ToRect(const SvgRect& rect) {
			return {RoundToInt(rect.x), RoundToInt(rect.y), std::max(1, RoundToInt(rect.w)), std::max(1, RoundToInt(rect.h))};
		}

		SvgRect ScaleRect(const SvgRect& rect, double sx, double sy) {
			return {rect.x * sx, rect.y * sy, rect.w * sx, rect.h * sy};
		}

		void ScaleButtonRects(std::vector<ButtonInfo>& buttons, double sx, double sy) {
			for (auto& button : buttons) {
				SvgRect rect{
					static_cast<double>(button.rect.x),
					static_cast<double>(button.rect.y),
					static_cast<double>(button.rect.w),
					static_cast<double>(button.rect.h)};
				button.rect = ToRect(ScaleRect(rect, sx, sy));
			}
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

		double ToDouble(const std::string& value, double fallback = 0.0) {
			if (value.empty())
				return fallback;
			char* end = nullptr;
			const double result = std::strtod(value.c_str(), &end);
			return end == value.c_str() ? fallback : result;
		}

		int ToInt(const std::string& value, int fallback = 0) {
			if (value.empty())
				return fallback;
			char* end = nullptr;
			const long result = std::strtol(value.c_str(), &end, 0);
			return end == value.c_str() ? fallback : static_cast<int>(result);
		}

		bool HasClassToken(const tinyxml2::XMLElement* element, const char* token) {
			const char* classes = element ? element->Attribute("class") : nullptr;
			if (!classes || !token)
				return false;
			std::istringstream stream(classes);
			std::string item;
			while (stream >> item) {
				if (item == token)
					return true;
			}
			return false;
		}

		const tinyxml2::XMLElement* FindElementById(const tinyxml2::XMLNode* node, const char* id) {
			if (!node || !id)
				return nullptr;
			for (auto* element = node->FirstChildElement(); element; element = element->NextSiblingElement()) {
				const char* value = element->Attribute("id");
				if (value && std::string(value) == id)
					return element;
				if (auto* found = FindElementById(element, id))
					return found;
			}
			return nullptr;
		}

		const tinyxml2::XMLElement* FindElementByClass(const tinyxml2::XMLNode* node, const char* class_name) {
			if (!node || !class_name)
				return nullptr;
			for (auto* element = node->FirstChildElement(); element; element = element->NextSiblingElement()) {
				if (HasClassToken(element, class_name))
					return element;
				if (auto* found = FindElementByClass(element, class_name))
					return found;
			}
			return nullptr;
		}

		bool ParseRectFromAttrs(const tinyxml2::XMLElement* element, SvgRect& rect) {
			if (!element)
				return false;
			const char* x = element->Attribute("x");
			const char* y = element->Attribute("y");
			const char* w = element->Attribute("width");
			const char* h = element->Attribute("height");
			if (!w || !h)
				return false;
			rect.x = ToDouble(x ? x : "");
			rect.y = ToDouble(y ? y : "");
			rect.w = ToDouble(w);
			rect.h = ToDouble(h);
			return rect.w > 0 && rect.h > 0;
		}

		bool ParseViewBoxValue(const char* view_box, SvgRect& rect) {
			if (!view_box)
				return false;
			const std::string value = view_box;
			const std::regex number_re(R"([-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?)");
			std::vector<double> nums;
			for (std::sregex_iterator it(value.begin(), value.end(), number_re), end; it != end; ++it)
				nums.push_back(ToDouble((*it)[0].str()));
			if (nums.size() != 4)
				return false;
			rect = {nums[0], nums[1], nums[2], nums[3]};
			return rect.w > 0 && rect.h > 0;
		}

		bool ParseViewBox(const tinyxml2::XMLElement* element, SvgRect& rect) {
			return ParseViewBoxValue(element ? element->Attribute("viewBox") : nullptr, rect) || ParseRectFromAttrs(element, rect);
		}

		std::string SerializeXmlNode(const tinyxml2::XMLNode* node) {
			if (!node)
				return {};
			tinyxml2::XMLPrinter printer(nullptr, true);
			node->Accept(&printer);
			return printer.CStr() ? printer.CStr() : "";
		}

		std::string SerializeXmlChildren(const tinyxml2::XMLElement* element) {
			std::string result;
			if (!element)
				return result;
			for (auto* child = element->FirstChild(); child; child = child->NextSibling())
				result += SerializeXmlNode(child);
			return result;
		}

		std::string CollectDefsBlocks(const tinyxml2::XMLNode* node) {
			std::string result;
			if (!node)
				return result;
			for (auto* element = node->FirstChildElement(); element; element = element->NextSiblingElement()) {
				if (std::string(element->Name()) == "defs")
					result += SerializeXmlNode(element);
				result += CollectDefsBlocks(element);
			}
			return result;
		}

		bool ParseTranslate(const std::string& transform, double& x, double& y) {
			const std::regex translate_re(R"(translate\s*\(\s*([-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?)\s*(?:,|\s)\s*([-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?)?)", std::regex::icase);
			std::smatch match;
			if (!std::regex_search(transform, match, translate_re))
				return false;
			x = ToDouble(match[1].str());
			y = match[2].matched ? ToDouble(match[2].str()) : 0.0;
			return true;
		}

		const char* Attr(const tinyxml2::XMLElement* element, const char* name) {
			return element ? element->Attribute(name) : nullptr;
		}

		bool IsElementNamed(const tinyxml2::XMLElement* element, const char* name) {
			return element && element->Name() && ToUpperAscii(element->Name()) == ToUpperAscii(name);
		}

		std::string JoinTransforms(const std::vector<std::string>& transforms) {
			std::string result;
			for (const auto& transform : transforms) {
				if (transform.empty())
					continue;
				if (!result.empty())
					result += ' ';
				result += transform;
			}
			return result;
		}

		std::string WrapTransformed(const std::string& content, const std::vector<std::string>& transforms) {
			const std::string transform = JoinTransforms(transforms);
			if (transform.empty())
				return "<g>" + content + "</g>";
			return "<g transform=\"" + transform + "\">" + content + "</g>";
		}

		std::string ExpandUseElement(const tinyxml2::XMLDocument& document, const tinyxml2::XMLElement* use_element) {
			if (!IsElementNamed(use_element, "use"))
				return SerializeXmlNode(use_element);

			const char* href_attr = Attr(use_element, "href");
			if (!href_attr)
				href_attr = Attr(use_element, "xlink:href");
			if (!href_attr || href_attr[0] != '#')
				return SerializeXmlNode(use_element);

			const auto* target = FindElementById(&document, href_attr + 1);
			if (!target)
				return SerializeXmlNode(use_element);

			std::vector<std::string> transforms;
			if (const char* transform = Attr(use_element, "transform"))
				transforms.push_back(transform);
			const double x = ToDouble(Attr(use_element, "x") ? Attr(use_element, "x") : "");
			const double y = ToDouble(Attr(use_element, "y") ? Attr(use_element, "y") : "");
			if (x != 0.0 || y != 0.0)
				transforms.push_back("translate(" + std::to_string(x) + " " + std::to_string(y) + ")");

			if (IsElementNamed(target, "symbol")) {
				SvgRect view_box{};
				ParseViewBox(target, view_box);
				const char* width_attr = Attr(use_element, "width");
				const char* height_attr = Attr(use_element, "height");
				if (view_box.w > 0 && view_box.h > 0 && width_attr && height_attr) {
					transforms.push_back("scale(" + std::to_string(ToDouble(width_attr, view_box.w) / view_box.w) + " " +
						std::to_string(ToDouble(height_attr, view_box.h) / view_box.h) + ")");
					transforms.push_back("translate(" + std::to_string(-view_box.x) + " " + std::to_string(-view_box.y) + ")");
				}
				return WrapTransformed(SerializeXmlChildren(target), transforms);
			}

			return WrapTransformed(SerializeXmlNode(target), transforms);
		}

		std::string SerializeRenderableElement(const tinyxml2::XMLDocument& document, const tinyxml2::XMLElement* element) {
			if (IsElementNamed(element, "use"))
				return ExpandUseElement(document, element);
			return SerializeXmlNode(element);
		}

		std::string BuildStandaloneSvg(const SvgRect& view_box, const std::string& defs, const std::string& content, int width = 0, int height = 0) {
			std::ostringstream stream;
			stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" ";
			if (width > 0 && height > 0)
				stream << "width=\"" << width << "\" height=\"" << height << "\" ";
			stream << "viewBox=\"" << view_box.x << ' ' << view_box.y << ' ' << view_box.w << ' ' << view_box.h << "\">"
				<< "<style>*{fill:#fff!important;stroke:none!important;opacity:1!important;fill-opacity:1!important;stroke-opacity:0!important;}</style>"
				<< defs << content << "</svg>";
			return stream.str();
		}

		bool RasterAlphaBounds(const std::string& svg, int width, int height, const SvgRect& view_box, SvgRect& bounds) {
			if (svg.empty() || width <= 0 || height <= 0 || view_box.w <= 0 || view_box.h <= 0)
				return false;
			SDL_RWops* rw = SDL_RWFromConstMem(svg.data(), static_cast<int>(svg.size()));
			if (!rw)
				return false;
			SDL_Surface* loaded = IMG_LoadSizedSVG_RW(rw, width, height);
			SDL_RWclose(rw);
			if (!loaded) {
				SDL_Log("[ModelInfo][Warn] IMG_LoadSizedSVG_RW failed while measuring SVG: %s", IMG_GetError());
				return false;
			}
			SDL_Surface* surface = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
			SDL_FreeSurface(loaded);
			if (!surface)
				return false;

			int min_x = surface->w;
			int min_y = surface->h;
			int max_x = -1;
			int max_y = -1;
			for (int y = 0; y < surface->h; ++y) {
				const auto* row = reinterpret_cast<const uint8_t*>(surface->pixels) + y * surface->pitch;
				for (int x = 0; x < surface->w; ++x) {
					Uint8 r = 0, g = 0, b = 0, a = 0;
					const Uint32 pixel = *reinterpret_cast<const Uint32*>(row + x * surface->format->BytesPerPixel);
					SDL_GetRGBA(pixel, surface->format, &r, &g, &b, &a);
					if (a <= 12)
						continue;
					min_x = std::min(min_x, x);
					min_y = std::min(min_y, y);
					max_x = std::max(max_x, x);
					max_y = std::max(max_y, y);
				}
			}
			SDL_FreeSurface(surface);
			if (max_x < min_x || max_y < min_y)
				return false;

			bounds.x = view_box.x + static_cast<double>(min_x) * view_box.w / width;
			bounds.y = view_box.y + static_cast<double>(min_y) * view_box.h / height;
			bounds.w = static_cast<double>(max_x - min_x + 1) * view_box.w / width;
			bounds.h = static_cast<double>(max_y - min_y + 1) * view_box.h / height;
			return bounds.w > 0 && bounds.h > 0;
		}

		bool ParseViewBox(const std::string& svg, SvgRect& rect) {
			tinyxml2::XMLDocument document;
			if (document.Parse(svg.data(), svg.size()) != tinyxml2::XML_SUCCESS)
				return false;
			const auto* root = document.RootElement();
			if (root && ParseViewBox(root, rect))
				return true;
			const auto* image = root ? root->FirstChildElement("image") : nullptr;
			if (ParseRectFromAttrs(image, rect))
				return true;
			return false;
		}

		bool ReadRasterSize(const std::filesystem::path& path, SvgRect& rect) {
			SDL_Surface* surface = IMG_Load(path.string().c_str());
			if (!surface)
				return false;
			rect = {0, 0, static_cast<double>(surface->w), static_cast<double>(surface->h)};
			SDL_FreeSurface(surface);
			return rect.w > 0 && rect.h > 0;
		}

		bool ReadInterfaceSize(const std::filesystem::path& path, SvgRect& rect) {
			const std::string ext = ToUpperAscii(path.extension().string());
			if (ext == ".SVG") {
				try {
					return ParseViewBox(ReadTextFile(path), rect);
				}
				catch (...) {
					return false;
				}
			}
			return ReadRasterSize(path, rect);
		}

		bool ParseScreenSlot(const tinyxml2::XMLDocument& document, SvgRect& rect) {
			return ParseRectFromAttrs(FindElementById(&document, "screenSlot"), rect);
		}

		bool ParseScale(const std::string& transform, double& sx, double& sy) {
			const std::regex scale_re(R"(scale\s*\(\s*([-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?)\s*(?:(?:,|\s)\s*([-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?))?)", std::regex::icase);
			std::smatch match;
			if (!std::regex_search(transform, match, scale_re))
				return false;
			sx = ToDouble(match[1].str(), 1.0);
			sy = match[2].matched ? ToDouble(match[2].str(), sx) : sx;
			return true;
		}

		struct BoardDisplayInfo {
			double x{};
			double y{};
			double sx{1.0};
			double sy{1.0};
		};

		BoardDisplayInfo ParseBoardDisplayInfo(const tinyxml2::XMLDocument& document) {
			BoardDisplayInfo info{};
			const auto* element = FindElementByClass(&document, "board-display");
			if (!element)
				return info;
			const std::string transform = element->Attribute("transform") ? element->Attribute("transform") : "";
			ParseTranslate(transform, info.x, info.y);
			ParseScale(transform, info.sx, info.sy);
			return info;
		}

		const tinyxml2::XMLElement* FindStatusFrame(const tinyxml2::XMLDocument& document) {
			if (auto* element = FindElementByClass(&document, "board-status"))
				return element;
			return FindElementById(&document, "status");
		}

		const tinyxml2::XMLElement* FindStatusContainer(const tinyxml2::XMLElement* status_frame) {
			if (!status_frame)
				return nullptr;
			if (auto* element = FindElementById(status_frame, "status"))
				return element;
			return status_frame;
		}

		std::vector<const tinyxml2::XMLElement*> ExtractDirectChildElements(const tinyxml2::XMLElement* container) {
			std::vector<const tinyxml2::XMLElement*> children;
			if (!container)
				return children;
			for (auto* child = container->FirstChildElement(); child; child = child->NextSiblingElement()) {
				const std::string name = child->Name() ? child->Name() : "";
				if (ToUpperAscii(name) == "DEFS" || ToUpperAscii(name) == "STYLE")
					continue;
				children.push_back(child);
			}
			return children;
		}

		std::vector<std::string> CollectAncestorTransforms(const tinyxml2::XMLElement* ancestor, const tinyxml2::XMLElement* element) {
			std::vector<std::string> transforms;
			for (auto* node = element ? element->Parent() : nullptr; node && node != ancestor; node = node->Parent()) {
				const auto* parent_element = node->ToElement();
				if (!parent_element)
					continue;
				if (const char* transform = parent_element->Attribute("transform"))
					transforms.push_back(transform);
			}
			std::reverse(transforms.begin(), transforms.end());
			return transforms;
		}

		std::string BuildStatusSvgShape(const tinyxml2::XMLDocument& document, const tinyxml2::XMLElement* status_frame, const tinyxml2::XMLElement* child, const std::string& defs) {
			SvgRect frame_rect{};
			ParseViewBox(status_frame, frame_rect);
			if (frame_rect.w <= 0 || frame_rect.h <= 0)
				return {};

			std::string shape;
			const std::string child_name = child && child->Name() ? child->Name() : "";
			if (child_name == "svg") {
				SvgRect child_rect{};
				ParseRectFromAttrs(child, child_rect);
				SvgRect child_view_box{};
				ParseViewBoxValue(child->Attribute("viewBox"), child_view_box);
				if (child_rect.w > 0 && child_rect.h > 0 && child_view_box.w > 0 && child_view_box.h > 0) {
					const double sx = child_rect.w / child_view_box.w;
					const double sy = child_rect.h / child_view_box.h;
					shape = "<g transform=\"translate(" + std::to_string(child_rect.x) + " " + std::to_string(child_rect.y) +
						") scale(" + std::to_string(sx) + " " + std::to_string(sy) +
						") translate(" + std::to_string(-child_view_box.x) + " " + std::to_string(-child_view_box.y) + ")\">" +
						SerializeXmlChildren(child) + "</g>";
				}
				else {
					shape = SerializeXmlChildren(child);
				}
			}
			else {
				shape = SerializeRenderableElement(document, child);
			}

			shape = WrapTransformed(shape, CollectAncestorTransforms(status_frame, child));
			return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" +
				std::to_string(frame_rect.x) + " " + std::to_string(frame_rect.y) + " " +
				std::to_string(frame_rect.w) + " " + std::to_string(frame_rect.h) +
				"\"><g fill=\"#000000\">" + defs + shape + "</g></svg>";
		}

		void AddStatusSprites(ModelInfo& model, const tinyxml2::XMLDocument& document, const std::map<std::string, int>& configured_status_sprites) {
			if (configured_status_sprites.empty())
				throw std::runtime_error("config.json must specify status_sprites for WebCalcEmu SVG/PNG model inference.");
			const auto* status_frame = FindStatusFrame(document);
			if (!status_frame)
				throw std::runtime_error("The board SVG does not contain a status frame for configured status_sprites.");
			SvgRect status_rect{};
			if (!ParseRectFromAttrs(status_frame, status_rect))
				ParseViewBox(status_frame, status_rect);
			if (status_rect.w <= 0 || status_rect.h <= 0)
				throw std::runtime_error("The board SVG status frame does not expose a usable size.");
			const auto display = ParseBoardDisplayInfo(document);
			const Rect dest = ToRect({
				display.x + status_rect.x * display.sx,
				display.y + status_rect.y * display.sy,
				status_rect.w * display.sx,
				status_rect.h * display.sy});

			const std::string defs = CollectDefsBlocks(status_frame);
			auto children = ExtractDirectChildElements(FindStatusContainer(status_frame));
			model.status_sprites = configured_status_sprites;
			for (const auto& [name, index] : configured_status_sprites) {
				if (index < 0 || static_cast<size_t>(index) >= children.size())
					throw std::runtime_error("status_sprites index is out of range for " + name + ".");
				SpriteInfo sprite{};
				sprite.src = {0, 0, std::max(1, RoundToInt(status_rect.w)), std::max(1, RoundToInt(status_rect.h))};
				sprite.dest = dest;
				sprite.svg_shape = BuildStatusSvgShape(document, status_frame, children[static_cast<size_t>(index)], defs);
				model.sprites[name] = std::move(sprite);
			}
		}

		int MaskToIndex(int mask) {
			if (mask <= 0)
				return -1;
			for (int ix = 0; ix < 8; ++ix) {
				if (mask == (1 << ix))
					return ix;
			}
			return -1;
		}

		std::filesystem::path ResolveModelPath(const std::filesystem::path& model_path, const std::string& file_name) {
			if (file_name.empty())
				return {};
			std::filesystem::path path(file_name);
			if (path.is_absolute())
				return path;
			return model_path / path;
		}

		std::filesystem::path ResolveBoardSvg(const std::filesystem::path& model_path, std::string* board_key, const std::string& requested_board) {
			if (requested_board.empty())
				throw std::runtime_error("config.json must specify board_path for WebCalcEmu SVG/PNG model inference.");
			auto requested_path = ResolveModelPath(model_path, requested_board);
			if (FileExists(requested_path)) {
				if (board_key)
					*board_key = ToUpperAscii(requested_path.stem().string());
				return requested_path;
			}
			throw std::runtime_error("The board SVG referenced by config.json was not found: " + requested_board);
		}

		void CollectButtonElements(const tinyxml2::XMLNode* node, std::vector<const tinyxml2::XMLElement*>& elements) {
			if (!node)
				return;
			for (auto* element = node->FirstChildElement(); element; element = element->NextSiblingElement()) {
				if (element->Attribute("data-ki") && element->Attribute("data-ko"))
					elements.push_back(element);
				CollectButtonElements(element, elements);
			}
		}

		std::vector<ButtonInfo> ParseButtons(const tinyxml2::XMLDocument& document, const SvgRect& board_rect) {
			std::vector<ButtonInfo> buttons;
			const auto defs = CollectDefsBlocks(&document);
			std::vector<const tinyxml2::XMLElement*> elements;
			CollectButtonElements(&document, elements);

			const double longest_side = std::max(board_rect.w, board_rect.h);
			double scan_scale = longest_side > 0 ? std::min(2.0, 2048.0 / longest_side) : 1.0;
			if (scan_scale < 1.0)
				scan_scale = 1.0;
			const int scan_width = std::max(1, RoundToInt(board_rect.w * scan_scale));
			const int scan_height = std::max(1, RoundToInt(board_rect.h * scan_scale));

			for (const auto* element : elements) {
				const char* ki_attr = element->Attribute("data-ki");
				const char* ko_attr = element->Attribute("data-ko");
				if (!ki_attr || !ko_attr)
					continue;

				const std::string shape = SerializeRenderableElement(document, element);
				const std::string measure_svg = BuildStandaloneSvg(board_rect, defs, shape, scan_width, scan_height);
				SvgRect svg_rect{};
				if (!RasterAlphaBounds(measure_svg, scan_width, scan_height, board_rect, svg_rect))
					continue;

				const int ki_mask = ToInt(ki_attr);
				const int ko_mask = ToInt(ko_attr);
				ButtonInfo button{};
				button.rect = ToRect(svg_rect);
				button.svg_shape = shape;
				button.svg_defs = defs;
				if (ki_mask == 0 && ko_mask == 0) {
					button.kiko = 0xff;
					button.keyname = "Escape";
				}
				else {
					const int ki = MaskToIndex(ki_mask);
					const int ko = MaskToIndex(ko_mask);
					if (ki < 0 || ko < 0)
						continue;
					button.kiko = (ko << 4) | ki;
				}
				buttons.push_back(button);
			}
			return buttons;
		}

		ModelInfo InferWebCalcModelInfo(
			const std::filesystem::path& model_path,
			unsigned short hardware_id,
			const std::string& requested_board = {},
			const std::string& requested_interface = {},
			const std::string& requested_rom = {},
			const std::string& requested_flash = {},
			int display_w = 0,
			int display_h = 0,
			double screen_scale_y = 0.0,
			const std::map<std::string, int>& configured_status_sprites = {}) {
			std::string board_key;
			const auto board_path = ResolveBoardSvg(model_path, &board_key, requested_board);
			if (board_path.empty())
				throw std::runtime_error("No supported WebCalcEmu board SVG was found.");

			const std::string svg = ReadTextFile(board_path);
			tinyxml2::XMLDocument board_document;
			if (board_document.Parse(svg.data(), svg.size()) != tinyxml2::XML_SUCCESS)
				throw std::runtime_error("The board SVG is not valid XML.");
			SvgRect board_rect{};
			if (!ParseViewBox(svg, board_rect))
				throw std::runtime_error("The board SVG does not expose a usable viewBox or image size.");

			SvgRect screen_slot{};
			if (!ParseScreenSlot(board_document, screen_slot) || screen_slot.w <= 0 || screen_slot.h <= 0)
				throw std::runtime_error("The board SVG does not contain a usable screen-slot.");

			ModelInfo model{};
			model.hardware_id = hardware_id;
			model.board_path = requested_board;
			model.interface_path = requested_interface;
			model.rom_path = requested_rom;
			model.flash_path = requested_flash;
			model.screen_width = display_w;
			model.screen_height = display_h;
			model.screen_scale_y = screen_scale_y;
			model.buttons = ParseButtons(board_document, board_rect);

			if (model.interface_path.empty())
				throw std::runtime_error("config.json must specify interface_path for WebCalcEmu SVG/PNG model inference.");
			if (!FileExists(ResolveModelPath(model_path, model.interface_path)))
				throw std::runtime_error("The interface image referenced by config.json was not found: " + model.interface_path);
			if (model.rom_path.empty())
				throw std::runtime_error("config.json must specify rom_path for WebCalcEmu SVG/PNG model inference.");
			if (!FileExists(ResolveModelPath(model_path, model.rom_path)))
				throw std::runtime_error("The ROM image referenced by config.json was not found: " + model.rom_path);
			if (!model.flash_path.empty() && !FileExists(ResolveModelPath(model_path, model.flash_path)))
				throw std::runtime_error("The flash image referenced by config.json was not found: " + model.flash_path);

			SvgRect interface_src = board_rect;
			if (!ReadInterfaceSize(ResolveModelPath(model_path, model.interface_path), interface_src))
				throw std::runtime_error("The interface image referenced by config.json does not expose a usable size: " + model.interface_path);

			const Rect interface_dest = ToRect(board_rect);
			model.sprites["rsd_interface"] = {ToRect(interface_src), interface_dest};

			if (display_w <= 0 || display_h <= 0 || screen_scale_y <= 0)
				throw std::runtime_error("config.json must specify positive screen_width, screen_height, and screen_scale_y.");
			screen_slot.h = screen_slot.w * static_cast<double>(display_h) / static_cast<double>(display_w) * screen_scale_y;
			model.sprites["rsd_pixel"] = {{0, 0, display_w, display_h}, ToRect(screen_slot)};
			AddStatusSprites(model, board_document, configured_status_sprites);
			return model;
		}

		std::string ReadJsonString(const json& value, std::initializer_list<const char*> names) {
			for (const char* name : names) {
				if (value.contains(name) && value.at(name).is_string())
					return value.at(name).get<std::string>();
			}
			if (value.contains("webcalc") && value.at("webcalc").is_object()) {
				const auto& webcalc = value.at("webcalc");
				for (const char* name : names) {
					if (webcalc.contains(name) && webcalc.at(name).is_string())
						return webcalc.at(name).get<std::string>();
				}
			}
			return {};
		}

		const json* FindJsonMember(const json& value, std::initializer_list<const char*> names) {
			for (const char* name : names) {
				if (value.contains(name))
					return &value.at(name);
			}
			if (value.contains("webcalc") && value.at("webcalc").is_object()) {
				const auto& webcalc = value.at("webcalc");
				for (const char* name : names) {
					if (webcalc.contains(name))
						return &webcalc.at(name);
				}
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
			const json* item = FindJsonMember(value, {"hardware_id", "hardware"});
			if (!item)
				return false;
			if (!item->is_number_integer())
				throw std::runtime_error("hardware_id must be a numeric HardwareId enum value.");
			hardware_id = item->get<unsigned short>();
			return true;
		}

		std::map<std::string, int> ParseStatusSpriteMap(const json& item) {
			std::map<std::string, int> result;
			if (item.is_object()) {
				for (auto it = item.begin(); it != item.end(); ++it) {
					if (it.value().is_number_integer()) {
						result[it.key()] = it.value().get<int>();
					}
					else if (it.value().is_object() && it.value().contains("index")) {
						result[it.key()] = it.value().at("index").get<int>();
					}
					else {
						throw std::runtime_error("config.json status_sprites values must be integer indexes.");
					}
				}
			}
			else if (item.is_array()) {
				for (size_t ix = 0; ix < item.size(); ++ix) {
					const auto& entry = item.at(ix);
					if (entry.is_string()) {
						result[entry.get<std::string>()] = static_cast<int>(ix);
					}
					else if (entry.is_object() && entry.contains("name")) {
						const int index = entry.contains("index") ? entry.at("index").get<int>() : static_cast<int>(ix);
						result[entry.at("name").get<std::string>()] = index;
					}
					else {
						throw std::runtime_error("config.json status_sprites array entries must be strings or objects with name/index.");
					}
				}
			}
			else {
				throw std::runtime_error("config.json field status_sprites must be an object or array.");
			}
			return result;
		}

		std::map<std::string, int> ReadStatusSpriteMap(const json& value) {
			const json* item = FindJsonMember(value, {"status_sprites", "status_sprite_map", "status_map"});
			if (!item)
				return {};
			return ParseStatusSpriteMap(*item);
		}

		void ApplyWebCalcButtonOverrides(const json& buttons, ModelInfo& model) {
			if (!buttons.is_array())
				throw std::runtime_error("config.json field buttons must be an array.");
			for (size_t ix = 0; ix < buttons.size(); ++ix) {
				const auto& entry = buttons.at(ix);
				if (!entry.is_object())
					throw std::runtime_error("config.json WebCalc SVG button entries must be objects.");
				const size_t button_index = entry.contains("index") ? entry.at("index").get<size_t>() : ix;
				if (button_index >= model.buttons.size())
					throw std::runtime_error("config.json WebCalc SVG button index is out of range.");
				auto& button = model.buttons[button_index];
				if (entry.contains("keyname"))
					button.keyname = entry.at("keyname").get<std::string>();
				if (entry.contains("kiko"))
					button.kiko = entry.at("kiko").get<int>();
			}
		}

		std::map<std::string, int> RequireStatusSpriteMap(const json& value) {
			const json& item = RequireJsonMember(value, {"status_sprites", "status_sprite_map", "status_map"}, "status_sprites");
			auto result = ParseStatusSpriteMap(item);
			if (result.empty())
				throw std::runtime_error("config.json status_sprites must not be empty.");
			return result;
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

		void RequireWebCalcFields(const json& value) {
			RequireJsonString(value, {"board_path", "board_svg", "webcalc_board", "board"}, "board_path");
			RequireJsonInt(value, {"screen_width"}, "screen_width");
			RequireJsonInt(value, {"screen_height"}, "screen_height");
			RequireJsonNumber(value, {"screen_scale_y"}, "screen_scale_y");
			RequireStatusSpriteMap(value);
			const json& extra = RequireJsonMember(value, {"extra"}, "extra");
			if (!extra.is_object() || !extra.contains("webcalc_board") || !extra.contains("webcalc_screen_slot"))
				throw std::runtime_error("config.json extra must specify webcalc_board and webcalc_screen_slot.");
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
			if (const auto board_path = ReadJsonString(value, {"board_path", "board_svg", "webcalc_board", "board"}); !board_path.empty())
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
			if (auto status_sprites = ReadStatusSpriteMap(value); !status_sprites.empty())
				model.status_sprites = std::move(status_sprites);

			if (value.contains("buttons")) {
				if (!model.board_path.empty()) {
					if (!model.buttons.empty())
						ApplyWebCalcButtonOverrides(value.at("buttons"), model);
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
			if (value.contains("sprites")) {
				model.sprites.clear();
				for (auto it = value.at("sprites").begin(); it != value.at("sprites").end(); ++it)
					model.sprites[it.key()] = SpriteFromJson(it.value());
			}
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
		if (!model.status_sprites.empty())
			value["status_sprites"] = model.status_sprites;
		value["extra"] = model.extra;

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
			value["buttons"] = json::array();
			for (size_t ix = 0; ix < model.buttons.size(); ++ix) {
				const auto& button = model.buttons[ix];
				value["buttons"].push_back({
					{"index", ix},
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

		bool IsBinaryCacheUsable(const std::filesystem::path& bin_path, const std::filesystem::path& json_path) {
			if (!FileExists(bin_path))
				return false;
			if (!FileExists(json_path))
				return true;
			std::error_code ec;
			const auto bin_time = std::filesystem::last_write_time(bin_path, ec);
			if (ec)
				return false;
			const auto json_time = std::filesystem::last_write_time(json_path, ec);
			if (ec)
				return false;
			return bin_time >= json_time;
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
			if (IsBinaryCacheUsable(bin_path, json_path)) {
				std::ifstream stream(bin_path, std::ios::binary);
				if (!stream)
					throw std::runtime_error("Cannot open config.bin.");
				model.Read(stream);
				bool cache_missing_webcalc_fields = false;
				if (FileExists(json_path) && (model.board_path.empty() || model.screen_width <= 0 || model.screen_height <= 0 || model.screen_scale_y <= 0)) {
					try {
						std::ifstream json_stream(json_path);
						const json value = json::parse(json_stream);
						cache_missing_webcalc_fields = !ReadJsonString(value, {"board_path", "board_svg", "webcalc_board", "board"}).empty();
					}
					catch (...) {
						cache_missing_webcalc_fields = false;
					}
				}
				if (!cache_missing_webcalc_fields) {
					if (loaded_from)
						*loaded_from = MODEL_CONFIG_BIN;
					return true;
				}
			}

			if (FileExists(json_path)) {
				std::ifstream stream(json_path);
				if (!stream)
					throw std::runtime_error("Cannot open config.json.");
				json value = json::parse(stream);
				RequireBaseModelFields(value);
				const std::string requested_board = ReadJsonString(value, {"board_path", "board_svg", "webcalc_board", "board"});
				const std::string requested_interface = ReadJsonString(value, {"interface_path", "face_path", "face"});
				const std::string requested_rom = ReadJsonString(value, {"rom_path", "core_path", "rom"});
				const std::string requested_flash = ReadJsonString(value, {"flash_path", "flash"});
				if (!requested_board.empty() || !value.contains("buttons") || !value.contains("sprites")) {
					RequireWebCalcFields(value);
					unsigned short hardware_id{};
					ReadHardwareId(value, hardware_id);
					const auto requested_status_sprites = RequireStatusSpriteMap(value);
					const int display_w = RequireJsonInt(value, {"screen_width"}, "screen_width");
					const int display_h = RequireJsonInt(value, {"screen_height"}, "screen_height");
					const double screen_scale_y = RequireJsonNumber(value, {"screen_scale_y"}, "screen_scale_y");
					model = InferWebCalcModelInfo(model_path, hardware_id, requested_board, requested_interface, requested_rom, requested_flash, display_w, display_h, screen_scale_y, requested_status_sprites);
					ApplyModelInfoJsonValue(value, model, false);
				}
				else {
					ApplyModelInfoJsonValue(value, model, true);
				}
				if (loaded_from)
					*loaded_from = MODEL_CONFIG_JSON;
				SaveModelInfoBinCache(model_path, model);
				return true;
			}

			if (FileExists(bin_path)) {
				std::ifstream stream(bin_path, std::ios::binary);
				if (!stream)
					throw std::runtime_error("Cannot open config.bin.");
				model.Read(stream);
				if (loaded_from)
					*loaded_from = MODEL_CONFIG_BIN;
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
