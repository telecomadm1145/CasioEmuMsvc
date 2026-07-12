#include "SvgBoardModelLoader.h"
#include "ModelResourceStore.h"

#include "tinyxml2/tinyxml2.h"

#include <SDL.h>
#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace casioemu {
	namespace {
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

		struct BoardGeometryMapper {
			SvgRect source{};
			Rect dest{};
			double sx{1.0};
			double sy{1.0};

			BoardGeometryMapper(const SvgRect& source_rect, const Rect& dest_rect)
				: source(source_rect), dest(dest_rect) {
				if (source.w > 0 && source.h > 0) {
					sx = static_cast<double>(dest.w) / source.w;
					sy = static_cast<double>(dest.h) / source.h;
				}
			}

			SvgRect Map(const SvgRect& rect) const {
				return {
					static_cast<double>(dest.x) + (rect.x - source.x) * sx,
					static_cast<double>(dest.y) + (rect.y - source.y) * sy,
					rect.w * sx,
					rect.h * sy};
			}

			Rect Map(const Rect& rect) const {
				return ToRect(Map(SvgRect{
					static_cast<double>(rect.x),
					static_cast<double>(rect.y),
					static_cast<double>(rect.w),
					static_cast<double>(rect.h)}));
			}

			std::string WrapShape(const std::string& shape) const {
				if (shape.empty())
					return {};
				if (dest.x == 0 && dest.y == 0 && source.x == 0.0 && source.y == 0.0 && sx == 1.0 && sy == 1.0)
					return shape;
				return "<g transform=\"translate(" + std::to_string(dest.x) + " " + std::to_string(dest.y) +
					") scale(" + std::to_string(sx) + " " + std::to_string(sy) +
					") translate(" + std::to_string(-source.x) + " " + std::to_string(-source.y) + ")\">" +
					shape + "</g>";
			}
		};

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

		void AddStatusSprites(ModelInfo& model, const tinyxml2::XMLDocument& document, const BoardGeometryMapper& mapper, const std::map<std::string, int>& configured_status_indexes) {
			if (configured_status_indexes.empty())
				throw std::runtime_error("config.json sprites must include status sprite indexes for board SVG/PNG model inference.");
			const auto* status_frame = FindStatusFrame(document);
			if (!status_frame)
				throw std::runtime_error("The board SVG does not contain a status frame for configured status sprites.");
			SvgRect status_rect{};
			if (!ParseRectFromAttrs(status_frame, status_rect))
				ParseViewBox(status_frame, status_rect);
			if (status_rect.w <= 0 || status_rect.h <= 0)
				throw std::runtime_error("The board SVG status frame does not expose a usable size.");
			const auto display = ParseBoardDisplayInfo(document);
			const Rect dest = mapper.Map(ToRect({
				display.x + status_rect.x * display.sx,
				display.y + status_rect.y * display.sy,
				status_rect.w * display.sx,
				status_rect.h * display.sy}));

			const std::string defs = CollectDefsBlocks(status_frame);
			auto children = ExtractDirectChildElements(FindStatusContainer(status_frame));
			model.status_sprite_indexes = configured_status_indexes;
			for (const auto& [name, index] : configured_status_indexes) {
				if (index < 0 || static_cast<size_t>(index) >= children.size())
					throw std::runtime_error("status sprite index is out of range for " + name + ".");
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
				throw std::runtime_error("config.json must specify board_path for board SVG/PNG model inference.");
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

		std::vector<ButtonInfo> ParseButtons(const tinyxml2::XMLDocument& document, const SvgRect& board_rect, const BoardGeometryMapper& mapper) {
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
				button.rect = mapper.Map(ToRect(svg_rect));
				button.svg_shape = mapper.WrapShape(shape);
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

		ModelInfo LoadSvgBoardModelInfoFromData(
			const std::string& svg,
			const std::function<bool(const std::string&)>& resource_exists,
			unsigned short hardware_id,
			const std::string& requested_board,
			const std::string& requested_interface,
			const std::string& requested_rom,
			const std::string& requested_flash,
			const Rect& interface_src_rect,
			const Rect& interface_dest_rect,
			int display_w,
			int display_h,
			double screen_scale_y,
			const std::map<std::string, int>& configured_status_indexes) {
			tinyxml2::XMLDocument board_document;
			if (board_document.Parse(svg.data(), svg.size()) != tinyxml2::XML_SUCCESS)
				throw std::runtime_error("The board SVG is not valid XML.");
			SvgRect board_rect{};
			if (!ParseViewBox(svg, board_rect))
				throw std::runtime_error("The board SVG does not expose a usable viewBox or image size.");
			if (interface_dest_rect.w <= 0 || interface_dest_rect.h <= 0)
				throw std::runtime_error("config.json must specify sprites.rsd_interface.dest with positive width and height.");
			if (interface_dest_rect.x != 0 || interface_dest_rect.y != 0)
				throw std::runtime_error("config.json sprites.rsd_interface.dest x and y must be zero.");
			const BoardGeometryMapper mapper(board_rect, interface_dest_rect);

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
			model.buttons = ParseButtons(board_document, board_rect, mapper);

			if (model.interface_path.empty())
				throw std::runtime_error("config.json must specify interface_path for board SVG/PNG model inference.");
			if (!resource_exists(model.interface_path))
				throw std::runtime_error("The interface image referenced by config.json was not found: " + model.interface_path);
			if (model.rom_path.empty())
				throw std::runtime_error("config.json must specify rom_path for board SVG/PNG model inference.");
			if (!resource_exists(model.rom_path))
				throw std::runtime_error("The ROM image referenced by config.json was not found: " + model.rom_path);
			if (!model.flash_path.empty() && !resource_exists(model.flash_path))
				throw std::runtime_error("The flash image referenced by config.json was not found: " + model.flash_path);

			if (interface_src_rect.w <= 0 || interface_src_rect.h <= 0)
				throw std::runtime_error("config.json must specify sprites.rsd_interface.src with positive width and height.");
			SvgRect interface_src{
				static_cast<double>(interface_src_rect.x),
				static_cast<double>(interface_src_rect.y),
				static_cast<double>(interface_src_rect.w),
				static_cast<double>(interface_src_rect.h)};
			model.sprites["rsd_interface"] = {ToRect(interface_src), interface_dest_rect};

			if (display_w <= 0 || display_h <= 0 || screen_scale_y <= 0)
				throw std::runtime_error("config.json must specify positive screen_width, screen_height, and screen_scale_y.");
			screen_slot.h = screen_slot.w * static_cast<double>(display_h) / static_cast<double>(display_w) * screen_scale_y;
			model.sprites["rsd_pixel"] = {{0, 0, display_w, display_h}, mapper.Map(ToRect(screen_slot))};
			AddStatusSprites(model, board_document, mapper, configured_status_indexes);
			return model;
		}
	}

	ModelInfo LoadSvgBoardModelInfo(
		const std::filesystem::path& model_path,
		unsigned short hardware_id,
		const std::string& requested_board,
		const std::string& requested_interface,
		const std::string& requested_rom,
		const std::string& requested_flash,
		const Rect& interface_src_rect,
		const Rect& interface_dest_rect,
		int display_w,
		int display_h,
		double screen_scale_y,
		const std::map<std::string, int>& configured_status_indexes) {
		const auto board_path = ResolveBoardSvg(model_path, nullptr, requested_board);
		if (board_path.empty())
			throw std::runtime_error("No supported board SVG was found.");
		const std::string svg = ReadTextFile(board_path);
		return LoadSvgBoardModelInfoFromData(svg,
			[&](const std::string& name) { return FileExists(ResolveModelPath(model_path, name)); },
			hardware_id, requested_board, requested_interface, requested_rom, requested_flash,
			interface_src_rect, interface_dest_rect, display_w, display_h, screen_scale_y, configured_status_indexes);
	}

	ModelInfo LoadSvgBoardModelInfo(
		const ModelResourceStore& resources,
		unsigned short hardware_id,
		const std::string& requested_board,
		const std::string& requested_interface,
		const std::string& requested_rom,
		const std::string& requested_flash,
		const Rect& interface_src_rect,
		const Rect& interface_dest_rect,
		int display_w,
		int display_h,
		double screen_scale_y,
		const std::map<std::string, int>& configured_status_indexes) {
		if (requested_board.empty() || !resources.Exists(requested_board))
			throw std::runtime_error("The board SVG referenced by config.json was not found: " + requested_board);
		const auto board_data = resources.Read(requested_board);
		const std::string svg(board_data.begin(), board_data.end());
		return LoadSvgBoardModelInfoFromData(svg,
			[&](const std::string& name) { return resources.Exists(name); },
			hardware_id, requested_board, requested_interface, requested_rom, requested_flash,
			interface_src_rect, interface_dest_rect, display_w, display_h, screen_scale_y, configured_status_indexes);
	}
}
