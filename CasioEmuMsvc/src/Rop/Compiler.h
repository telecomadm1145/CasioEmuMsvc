#pragma once
#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace lc::details {
	using namespace std;

	// ==========================
	// Utility
	// ==========================

	static string ltrim(string s) {
		s.erase(s.begin(), find_if(s.begin(), s.end(),
							   [](unsigned char c) { return !isspace(c); }));
		return s;
	}

	static string rtrim(string s) {
		s.erase(find_if(s.rbegin(), s.rend(),
					[](unsigned char c) { return !isspace(c); })
					.base(),
			s.end());
		return s;
	}

	static string trim(string s) {
		return rtrim(ltrim(std::move(s)));
	}

	static string lower(string s) {
		transform(s.begin(), s.end(), s.begin(),
			[](unsigned char c) { return char(tolower(c)); });
		return s;
	}

	static bool startsWith(const string& s, const string& p) {
		return s.rfind(p, 0) == 0;
	}

	static bool endsWith(const string& s, const string& p) {
		return s.size() >= p.size() &&
			   equal(p.rbegin(), p.rend(), s.rbegin());
	}

	static vector<string> split(const string& s, char ch) {
		vector<string> r;
		string cur;
		stringstream ss(s);
		while (getline(ss, cur, ch))
			r.push_back(cur);
		return r;
	}

	static string removeInlineComment(string line) {
		size_t p = line.find('#');
		if (p != string::npos)
			line.resize(p);
		return rtrim(line);
	}

	// 模拟 Python 的 canonicalize，剥离非字母/数字前后的空格（同时安全跳过字符串内容）
	static string canonicalize(string st) {
		st = trim(std::move(st));
		string out;
		bool in_quote = false;
		for (size_t i = 0; i < st.size(); ++i) {
			if (st[i] == '"') {
				in_quote = !in_quote;
				out.push_back('"');
			}
			else if (!in_quote && st[i] == ' ') {
				bool left_alnum = (out.size() > 0) && isalnum((unsigned char)out.back());
				size_t j = i + 1;
				while (j < st.size() && st[j] == ' ')
					++j;
				bool right_alnum = (j < st.size()) && isalnum((unsigned char)st[j]);

				if (left_alnum && right_alnum) {
					out.push_back(' ');
				}
				i = j - 1; // skip extra spaces
			}
			else {
				out.push_back(st[i]);
			}
		}
		return out;
	}

	static long long parseInteger(string s, int base = 0) {
		s = trim(s);
		size_t idx = 0;
		long long v = stoll(s, &idx, base);
		if (idx != s.size()) {
			throw runtime_error("invalid integer: " + s);
		}
		return v;
	}

	static string hexWord(int v, int width = 4) {
		stringstream ss;
		ss << "0x" << hex << nouppercase << setw(width)
		   << setfill('0') << v;
		return ss.str();
	}

	static string byteToKey(int byte, const vector<int>& npress, const vector<string>& symbolrepr) {
		if (byte == 0)
			return "<NUL>";

		string sym = symbolrepr.empty() ? "" : symbolrepr[byte];
		if (sym == "@" || sym.empty()) {
			char buf[16];
			snprintf(buf, sizeof(buf), "<%02x>", byte);
			return buf;
		}

		int offset = 0;
		int current_byte = byte;
		while (current_byte > 0 && npress[current_byte] >= 100) {
			current_byte--;
			offset++;
		}

		string typesym = current_byte > 0 ? symbolrepr[current_byte] : "NUL";

		if (offset == 0) {
			return sym;
		}
		else {
			return "<" + sym + ":" + typesym + "+" + to_string(offset) + ">";
		}
	}

	// ==========================
	// Diagnostics
	// ==========================

	struct Diagnostic {
		string file;
		int line = 0;

		[[noreturn]] void error(const string& msg) const {
			stringstream ss;
			if (!file.empty())
				ss << file << ":";
			if (line > 0)
				ss << line << ": ";
			ss << msg;
			throw runtime_error(ss.str());
		}

		void warn(const string& msg) const {
			if (!file.empty())
				cerr << file << ":";
			if (line > 0)
				cerr << line << ": ";
			cerr << "warning: " << msg << "\n";
		}
	};

	// ==========================
	// Lexer
	// ==========================

	enum class TokenKind {
		Identifier,
		Number,
		String,
		Symbol,
		End
	};

	struct Token {
		TokenKind kind = TokenKind::End;
		string text;
		size_t pos = 0;
	};

	class Lexer {
		string src;
		size_t pos = 0;

	public:
		explicit Lexer(string s) : src(std::move(s)) {}

		Token next() {
			skipSpace();
			if (pos >= src.size())
				return {TokenKind::End, "", pos};

			size_t start = pos;
			char c = src[pos];

			if (isalpha((unsigned char)c) || c == '_' || c == '.' || c == '[' || c == ']') {
				string out;
				while (pos < src.size()) {
					char x = src[pos];
					if (isalnum((unsigned char)x) ||
						x == '_' || x == '.' || x == '[' || x == ']' ||
						x == ',' || x == '-' || x == '+' || x == '>') {
						out.push_back(x);
						++pos;
					}
					else {
						break;
					}
				}
				return {TokenKind::Identifier, out, start};
			}

			if (isdigit((unsigned char)c)) {
				string out;
				while (pos < src.size()) {
					char x = src[pos];
					if (isxdigit((unsigned char)x) || x == 'x' || x == 'X') {
						out.push_back(x);
						++pos;
					}
					else {
						break;
					}
				}
				return {TokenKind::Number, out, start};
			}

			if (c == '"') {
				++pos;
				string out;
				while (pos < src.size() && src[pos] != '"') {
					if (src[pos] == '\\' && pos + 1 < src.size()) {
						char e = src[++pos];
						switch (e) {
						case 'n':
							out.push_back('\n');
							break;
						case 't':
							out.push_back('\t');
							break;
						case '"':
							out.push_back('"');
							break;
						case '\\':
							out.push_back('\\');
							break;
						default:
							out.push_back(e);
							break;
						}
						++pos;
					}
					else {
						out.push_back(src[pos++]);
					}
				}
				if (pos < src.size() && src[pos] == '"')
					++pos;
				return {TokenKind::String, out, start};
			}

			++pos;
			return {TokenKind::Symbol, string(1, c), start};
		}

	private:
		void skipSpace() {
			while (pos < src.size() && isspace((unsigned char)src[pos]))
				++pos;
		}
	};

	// ==========================
	// Command DB & AST
	// ==========================

	struct BuiltinCommand {
		int address = 0;
		vector<string> tags;
	};

	class CommandDatabase {
		unordered_map<string, BuiltinCommand> commands;
		unordered_map<string, int> dataLabels;

	public:
		void addCommand(int address, string name, vector<string> tags = {}) {
			name = lower(canonicalize(name)); // 确保存入的 Key 被正确格式化
			if (name.empty())
				throw runtime_error("empty command");

			for (string p : {"0x", "call", "goto", "adr_of"}) {
				if (startsWith(name, p))
					throw runtime_error("illegal command prefix: " + name);
			}

			for (auto& [old, cmd] : commands) {
				if (old == name || cmd.address == address) {
					throw runtime_error("duplicated command: " + name);
				}
			}
			commands[name] = BuiltinCommand{address, std::move(tags)};
		}

		bool hasCommand(const string& name) const { return commands.count(lower(name)); }

		const BuiltinCommand& getCommand(const string& name) const {
			auto it = commands.find(lower(name));
			if (it == commands.end())
				throw runtime_error("unknown command: " + name);
			return it->second;
		}

		void addDataLabel(string name, int address) { dataLabels[lower(canonicalize(name))] = address; }
		bool hasDataLabel(const string& name) const { return dataLabels.count(lower(name)); }

		int getDataLabel(const string& name) const {
			auto it = dataLabels.find(lower(name));
			if (it == dataLabels.end())
				throw runtime_error("unknown data label: " + name);
			return it->second;
		}
	};

	struct EmptyStmt {};
	struct LabelStmt {
		string name;
	};
	struct HexLiteralStmt {
		string text;
	};
	struct RawHexStmt {
		string hex;
	};
	struct CallStmt {
		string target;
	};
	struct GotoStmt {
		string label;
	};
	struct AdrOfStmt {
		int offset = 0;
		string label;
	};
	struct AssignmentStmt {
		string reg;
		string valueText;
	};
	struct OrgStmt {
		string expr;
	};
	struct BackupIsStmt {
		string value;
	};
	struct AddrCopyIsStmt {
		string value;
	};
	struct MacroStmt {
		string name;
	};
	struct AdrArithStmt {
		int leftOffset = 0;
		string leftLabel;
		int rightOffset = 0;
		string rightLabel;
	};
	struct PrLengthStmt {};
	struct RemainingLengthStmt {};
	struct LBytesStmt {};
	struct StrStoreStmt {
		string name;
		string text;
	};
	struct StrEmitStmt {
		string text;
	};
	struct StrUseStmt {
		string name;
	};
	struct FreeformStmt {
		string text;
	};

	using Stmt = variant<
		EmptyStmt, LabelStmt, HexLiteralStmt, RawHexStmt, CallStmt, GotoStmt,
		AdrOfStmt, AssignmentStmt, OrgStmt, BackupIsStmt, AddrCopyIsStmt,
		MacroStmt, AdrArithStmt, PrLengthStmt, RemainingLengthStmt,
		LBytesStmt, StrStoreStmt, StrEmitStmt, StrUseStmt, FreeformStmt>;

	// ==========================
	// Parser
	// ==========================

	class Parser {
		Diagnostic diag;
		const CommandDatabase* db; // 添加 db 指针感知内置小挂件

	public:
		explicit Parser(const CommandDatabase* db = nullptr, Diagnostic d = {}) : diag(std::move(d)), db(db) {}

		Stmt parseLine(string line) {
			line = removeInlineComment(std::move(line));
			line = trim(line);

			if (line.empty())
				return EmptyStmt{};

			string low = lower(line);

			// 如果已经存在于 Commands 中，则直接作为 Call 执行。
			// 这是为了防止 `[er0]=er2,rt` 被误判为 Assignment 赋值从而崩溃。
			if (db && db->hasCommand(low)) {
				return CallStmt{low};
			}

			if (endsWith(line, ":")) {
				return LabelStmt{lower(trim(line.substr(0, line.size() - 1)))};
			}

			if (startsWith(low, "0x"))
				return HexLiteralStmt{low};
			if (startsWith(low, "hex") && low.find("hex_") == string::npos)
				return RawHexStmt{trim(line.substr(3))};
			if (startsWith(low, "call"))
				return CallStmt{lower(trim(line.substr(4)))};
			if (startsWith(low, "goto"))
				return GotoStmt{lower(trim(line.substr(4)))};
			if (startsWith(low, "adr_of"))
				return parseAdrOf(trim(low.substr(6)));
			if (startsWith(low, "org"))
				return OrgStmt{trim(line.substr(3))};
			if (startsWith(low, "backup is "))
				return BackupIsStmt{trim(line.substr(10))};
			if (startsWith(low, "addrcopy is "))
				return AddrCopyIsStmt{trim(line.substr(12))};

			if (startsWith(low, "loop880") || startsWith(low, "loop580") ||
				startsWith(low, "backup580") || startsWith(low, "backup880")) {
				return MacroStmt{low};
			}

			if (startsWith(low, "adr_arith"))
				return parseAdrArith(low);
			if (startsWith(low, "pr_length"))
				return PrLengthStmt{};
			if (startsWith(low, "remaining_length"))
				return RemainingLengthStmt{};
			if (startsWith(low, "l_bytes"))
				return LBytesStmt{};

			if (startsWith(low, "str")) {
				return parseStringStmt(line);
			}

			size_t eq = line.find('=');
			if (eq != string::npos) {
				string reg = lower(trim(line.substr(0, eq)));
				string value = trim(line.substr(eq + 1));
				return AssignmentStmt{reg, lower(value)};
			}

			return FreeformStmt{low};
		}

	private:
		AdrOfStmt parseAdrOf(const string& body) {
			if (body.empty())
				diag.error("adr_of requires label");

			int offset = 0;
			string label;

			if (body[0] == '[') {
				size_t rb = body.find(']');
				if (rb == string::npos)
					diag.error("adr_of has unmatched '['");

				string inside = trim(body.substr(1, rb - 1));
				label = trim(body.substr(rb + 1));

				size_t arrow = inside.find("->");
				if (arrow != string::npos) {
					int a = stoi(trim(inside.substr(0, arrow)), nullptr, 16);
					int b = stoi(trim(inside.substr(arrow + 2)), nullptr, 16);
					offset = b > a ? a - b : b - a;
				}
				else {
					offset = int(parseInteger(inside, 0));
				}
			}
			else {
				label = trim(body);
			}

			if (label.empty())
				diag.error("adr_of requires label");
			return AdrOfStmt{offset, label};
		}

		static pair<int, string> parseAdrPart(string part) {
			part = trim(part);
			if (startsWith(part, "adr_arith"))
				part = trim(part.substr(9));

			int offset = 0;
			string label;
			size_t lb = part.find('[');
			size_t rb = part.find(']');

			if (lb != string::npos && rb != string::npos && rb > lb) {
				string off = trim(part.substr(lb + 1, rb - lb - 1));
				offset = int(parseInteger(off, 0));
				label = trim(part.substr(rb + 1));
			}
			else {
				label = trim(part);
			}

			if (label.empty())
				throw runtime_error("adr_arith part missing label");
			return {offset, label};
		}

		AdrArithStmt parseAdrArith(const string& line) {
			int lastMinus = int(line.size()) - 1;
			while (lastMinus > 0) {
				if (line[lastMinus] == '-' && line.find("adr_arith", lastMinus) != string::npos) {
					break;
				}
				--lastMinus;
			}

			if (lastMinus <= 0)
				diag.error("wrong adr_arith syntax");

			string left = trim(line.substr(9, lastMinus - 9));
			string right = trim(line.substr(lastMinus + 1));

			auto [lo, ll] = parseAdrPart(left);
			auto [ro, rl] = parseAdrPart(right);

			return AdrArithStmt{lo, ll, ro, rl};
		}

		Stmt parseStringStmt(const string& line) {
			string content = trim(line.substr(3));
			size_t q = content.find('"');
			if (q != string::npos) {
				string name = trim(content.substr(0, q));
				Lexer lx(content.substr(q));
				Token t = lx.next();

				if (t.kind != TokenKind::String)
					diag.error("invalid str string literal");

				if (name.empty())
					return StrEmitStmt{t.text};
				else
					return StrStoreStmt{name, t.text};
			}

			if (!content.empty())
				return StrUseStmt{content};
			diag.error("wrong str syntax");
		}
	};

	// ==========================
	// Compiler State
	// ==========================

	struct AdrOfFixup {
		int pos;
		int offset;
		string label;
	};
	struct AdrArithFixup {
		int pos;
		int leftOffset;
		string leftLabel;
		int rightOffset;
		string rightLabel;
	};

	struct CompilerState {
		vector<int> result;
		unordered_map<string, int> labels;

		vector<AdrOfFixup> adrOfFixups;
		vector<AdrArithFixup> adrArithFixups;
		vector<int> prLengthFixups;
		vector<int> remainingFixups;

		optional<int> home;
		optional<int> orgAddress;

		string backup;
		string addrCopy;
		unordered_map<string, string> stringVars;

		int previousSectionLength = 0;
		int remainingBase = 0;

		void resetForSection(int prevLen) {
			result.clear();
			labels.clear();
			adrOfFixups.clear();
			adrArithFixups.clear();
			prLengthFixups.clear();
			remainingFixups.clear();
			home.reset();
			orgAddress.reset();
			backup.clear();
			addrCopy.clear();
			stringVars.clear();
			previousSectionLength = prevLen;
			remainingBase = 0;
		}
	};

	// ==========================
	// Compiler
	// ==========================

	class Compiler {
		CommandDatabase& db;
		CompilerState st;
		Parser parser;

		int maxCallAdr = 0xfffff;
		vector<int> npress;
		unordered_map<unsigned char, vector<int>> charMap;

	public:
		explicit Compiler(CommandDatabase& db, vector<int> npressWeights = vector<int>(256, 1))
			: db(db), parser(&db), npress(std::move(npressWeights)) {
			for (int i = 0; i < 256; ++i)
				charMap[(unsigned char)i] = {i};
		}

		CompilerState& state() { return st; }
		void setPreviousSectionLength(int n) { st.previousSectionLength = n; }
		void setCharMap(unordered_map<unsigned char, vector<int>> m) { charMap = std::move(m); }

		void compileStatement(const Stmt& stmt) {
			std::visit([this](auto&& x) { emit(x); }, stmt);
		}

		void compileLine(const string& line) {
			Stmt stmt = parser.parseLine(line);
			compileStatement(stmt);
		}

		void compileLines(const vector<string>& lines) {
			bool blockComment = false;
			for (string line : lines) {
				string t = trim(line);
				if (startsWith(t, "/*")) {
					blockComment = true;
					continue;
				}
				if (t.find("*/") != string::npos) {
					blockComment = false;
					continue;
				}
				if (blockComment)
					continue;

				string noComment = removeInlineComment(line);
				if (trim(noComment).empty())
					continue;

				string canon = canonicalize(noComment);

				if (canon.find(';') != string::npos) {
					for (auto& part : split(canon, ';'))
						compileLine(part);
				}
				else {
					compileLine(canon);
				}
			}
		}

		void finish() {
			resolveAdrArith();
			resolvePrLength();
			resolveRemaining();
		}

		void resolveAdrOf(int home) {
			for (auto& f : st.adrOfFixups) {
				auto it = st.labels.find(f.label);
				if (it == st.labels.end())
					throw runtime_error("unknown label in adr_of: " + f.label);

				int target = home + it->second + f.offset;
				if (f.pos + 1 >= int(st.result.size()))
					throw runtime_error("adr_of fixup out of range");
				if (st.result[f.pos] != 0 || st.result[f.pos + 1] != 0)
					throw runtime_error("adr_of target already patched");

				st.result[f.pos] = target & 0xff;
				st.result[f.pos + 1] = (target >> 8) & 0xff;
			}
		}

	private:
		void emit(const EmptyStmt&) {}

		void emit(const LabelStmt& x) {
			if (st.labels.count(x.name))
				throw runtime_error("duplicated label: " + x.name);
			st.labels[x.name] = int(st.result.size());
		}

		void emit(const HexLiteralStmt& x) { emitHexLiteral(x.text); }

		void emit(const RawHexStmt& x) {
			string s = x.hex;
			s.erase(remove_if(s.begin(), s.end(), ::isspace), s.end());
			if (s.size() % 2)
				throw runtime_error("invalid hex byte length");

			for (size_t i = 0; i < s.size(); i += 2) {
				int b = stoi(s.substr(i, 2), nullptr, 16);
				st.result.push_back(b);
			}
		}

		void emit(const CallStmt& x) {
			int adr = 0;
			try {
				adr = stoi(x.target, nullptr, 16);
			}
			catch (...) {
				const auto& cmd = db.getCommand(x.target);
				adr = cmd.address;
				for (auto& tag : cmd.tags) {
					if (startsWith(tag, "warning"))
						cerr << tag << "\n";
				}
			}
			if (adr < 0 || adr > maxCallAdr)
				throw runtime_error("invalid call address");
			adr = optimizeAdrForNpress(adr);
			emitHexLiteral(hexWord(adr + 0x30300000, 8));
		}

		void emit(const GotoStmt& x) {
			compileLine("er6 = adr_of [-2] " + x.label);
			compileLine("call sp=er6,pop er8");
		}

		void emit(const AdrOfStmt& x) {
			st.adrOfFixups.push_back(AdrOfFixup{int(st.result.size()), x.offset, x.label});
			st.result.push_back(0);
			st.result.push_back(0);
		}

		void emit(const AssignmentStmt& x) {
			compileLine("call pop " + x.reg);
			size_t old = st.result.size();

			string value = x.valueText;
			replace(value.begin(), value.end(), ',', ';');

			if (value.find(';') != string::npos) {
				for (auto& p : split(value, ';'))
					compileLine(p);
			}
			else {
				compileLine(value);
			}

			int got = int(st.result.size() - old);
			int need = sizeofRegister(x.reg);
			if (got != need) {
				throw runtime_error("register assignment size mismatch: " + x.reg + ", need " + to_string(need) + ", got " + to_string(got));
			}
		}

		void emit(const OrgStmt& x) {
			int hx = int(parseInteger(x.expr, 0));
			st.orgAddress = hx;
			int home = hx - int(st.result.size());
			if (st.home.has_value() && st.home.value() != home)
				throw runtime_error("inconsistent org/home");
			st.home = home;
		}

		void emit(const BackupIsStmt& x) { st.backup = x.value; }
		void emit(const AddrCopyIsStmt& x) { st.addrCopy = x.value; }
		void emit(const MacroStmt& x) { expandMacro(x.name); }

		void emit(const AdrArithStmt& x) {
			st.adrArithFixups.push_back(AdrArithFixup{int(st.result.size()), x.leftOffset, x.leftLabel, x.rightOffset, x.rightLabel});
			st.result.push_back(0);
		}

		void emit(const PrLengthStmt&) {
			st.prLengthFixups.push_back(int(st.result.size()));
			st.result.push_back(0);
			st.result.push_back(0);
		}

		void emit(const RemainingLengthStmt&) {
			st.remainingBase = int(st.result.size());
			st.remainingFixups.push_back(int(st.result.size()));
			st.result.push_back(0);
			st.result.push_back(0);
		}

		void emit(const LBytesStmt&) {
			int n = st.previousSectionLength;
			st.result.push_back(n & 0xff);
			st.result.push_back((n >> 8) & 0xff);
		}

		void emit(const StrStoreStmt& x) { st.stringVars[x.name] = x.text; }

		void emit(const StrEmitStmt& x) {
			auto bytes = encodeString(x.text);
			st.result.insert(st.result.end(), bytes.begin(), bytes.end());
		}

		void emit(const StrUseStmt& x) {
			auto it = st.stringVars.find(x.name);
			if (it == st.stringVars.end())
				throw runtime_error("unknown string variable: " + x.name);
			auto bytes = encodeString(it->second);
			st.result.insert(st.result.end(), bytes.begin(), bytes.end());
		}

		void emit(const FreeformStmt& x) {
			string line = lower(trim(x.text));
			if (db.hasDataLabel(line)) {
				emitHexLiteral(hexWord(db.getDataLabel(line), 4));
				return;
			}

			size_t plus = line.find('+');
			if (plus != string::npos) {
				string name = trim(line.substr(0, plus));
				string off = trim(line.substr(plus + 1));
				if (db.hasDataLabel(name)) {
					int adr = db.getDataLabel(name) + int(parseInteger(off, 0));
					emitHexLiteral(hexWord(adr, 4));
					return;
				}
			}

			if (db.hasCommand(line)) {
				emit(CallStmt{line});
				return;
			}
			throw runtime_error("wrong syntax or unknown command: " + line);
		}

		int getNpress(int b) const { return npress[b & 0xff]; }
		int getNpressAdr(int adr) const { return getNpress(adr & 0xff) + getNpress((adr >> 8) & 0xff); }
		int optimizeAdrForNpress(int adr) const {
			int alt = adr ^ 1;
			return getNpressAdr(alt) < getNpressAdr(adr) ? alt : adr;
		}

		int sizeofRegister(const string& r) const {
			if (r.empty())
				throw runtime_error("empty register");
			switch (r[0]) {
			case 'r':
				return 1;
			case 'e':
				return 2;
			case 'x':
				return 4;
			case 'q':
				return 8;
			default:
				throw runtime_error("Unknown register size or invalid assignment target (must be r,e,x,q): " + r);
			}
		}

		void emitHexLiteral(string line) {
			line = lower(trim(line));
			if (line.find('+') != string::npos) {
				auto p = split(line, '+');
				if (p.size() != 2)
					throw runtime_error("bad hex + syntax");
				long long v = parseInteger(p[0], 16) + parseInteger(p[1], 10);
				int n = int(p[0].size()) / 2 - 1;
				for (int i = 0; i < n; ++i) {
					st.result.push_back(v & 0xff);
					v >>= 8;
				}
				return;
			}

			size_t minus = line.find('-', 2);
			if (minus != string::npos) {
				string a = line.substr(0, minus);
				string b = line.substr(minus + 1);
				long long v = parseInteger(a, 16) - parseInteger(b, 10);
				int n = int(a.size()) / 2 - 1;
				for (int i = 0; i < n; ++i) {
					st.result.push_back(v & 0xff);
					v >>= 8;
				}
				return;
			}

			if (line.size() % 2)
				throw runtime_error("invalid hex literal length");
			long long v = parseInteger(line, 16);
			int n = int(line.size()) / 2 - 1;
			for (int i = 0; i < n; ++i) {
				st.result.push_back(v & 0xff);
				v >>= 8;
			}
		}

		vector<int> encodeString(const string& s) {
			vector<int> out;
			for (unsigned char c : s) {
				auto it = charMap.find(c);
				if (it == charMap.end())
					throw runtime_error("character not in conversion table");
				for (int b : it->second)
					out.push_back(b & 0xff);
			}
			return out;
		}

		void expandMacro(const string& name) {
			vector<string> lines;
			if (startsWith(name, "loop880")) {
				lines = {"set_segment:", "setlr", "di,rt", "call pop xr0", "adr_of length", "0x0001",
					"[er0]=er2,rt", "loop:", "call pop qr0", "adr_of " + st.addrCopy, st.backup,
					"pr_length", "adr_of [-2] " + st.addrCopy, "hex e6 4d", "length:",
					"remaining_length", "0x0000", "call sp=er6,pop er8"};
			}
			else if (startsWith(name, "loop580")) {
				lines = {"set_segment:", "setlr", "di,rt", "call pop xr0", "adr_of length", "0x0001",
					"[er0]=er2,rt", "call pop qr0", "pr_length", st.backup, "adr_of " + st.addrCopy,
					"adr_of [-2] " + st.addrCopy, "0x8932", "length:", "remaining_length", "0x0000", "sp=er6,pop er8"};
			}
			else if (startsWith(name, "backup580")) {
				lines = {"backup:", "call pop xr0", st.backup, "adr_of " + st.addrCopy, "call 0x09450", "pr_length"};
			}
			else if (startsWith(name, "backup880")) {
				lines = {"backup:", "call pop xr0", st.backup, "adr_of " + st.addrCopy, "call 0x14de8", "pr_length", "0x0000"};
			}
			else {
				throw runtime_error("unknown macro: " + name);
			}
			compileLines(lines);
		}

		void resolveAdrArith() {
			for (auto& f : st.adrArithFixups) {
				if (!st.labels.count(f.leftLabel))
					throw runtime_error("unknown label: " + f.leftLabel);
				if (!st.labels.count(f.rightLabel))
					throw runtime_error("unknown label: " + f.rightLabel);
				int l = st.labels[f.leftLabel] + f.leftOffset;
				int r = st.labels[f.rightLabel] + f.rightOffset;
				st.result[f.pos] = (l - r) & 0xff;
			}
		}

		void resolvePrLength() {
			int len = int(st.result.size());
			for (int pos : st.prLengthFixups) {
				st.result[pos] = len & 0xff;
				st.result[pos + 1] = (len >> 8) & 0xff;
			}
		}

		void resolveRemaining() {
			int len = int(st.result.size());
			for (int pos : st.remainingFixups) {
				int rem = len - st.remainingBase;
				st.result[pos] = rem & 0xff;
				st.result[pos + 1] = (rem >> 8) & 0xff;
			}
		}
	};

	// ==========================
	// Source preprocessing
	// ==========================

	class SourcePreprocessor {
	public:
		static vector<vector<string>> splitSections(const vector<string>& src) {
			vector<vector<string>> sections;
			vector<string> cur;

			for (auto line : src) {
				if (trim(line) == "!===") {
					if (!cur.empty()) {
						sections.push_back(cur);
						cur.clear();
					}
				}
				else {
					cur.push_back(line);
				}
			}
			if (!cur.empty())
				sections.push_back(cur);
			if (sections.empty())
				sections.push_back(src);
			return sections;
		}

		static vector<string> processSetupLoop(const vector<string>& program) {
			vector<string> modified;
			bool setup_loop_detected = false;

			for (string line : program) {
				string cleanLine = removeInlineComment(line);
				string lowerLine = lower(trim(cleanLine));

				if (startsWith(lowerLine, "setup_loop")) {
					setup_loop_detected = true;
					auto parts = split(cleanLine, ',');
					if (parts.size() < 2 || parts.size() > 3) {
						throw runtime_error("Invalid setup_loop directive: " + line);
					}

					auto firstPartWords = split(trim(parts[0]), ' ');
					string src = firstPartWords.size() > 1 ? firstPartWords[1] : "";
					string src_backup = trim(parts[1]);
					string label = (parts.size() == 3 && trim(parts[2]) != "none") ? trim(parts[2]) : "home";

					modified.push_back("restore:");
					modified.push_back("    setlr");
					modified.push_back("    DI,RT");
					modified.push_back("    xr0 = adr_of length, 0x01, 0x00");
					modified.push_back("    [er0] = er2,rt");
					modified.push_back("    qr0 = pr_length, " + src_backup + ", " + src + ", 0x0000");
					modified.push_back("    0x8932");
					modified.push_back("length:");
					modified.push_back("    0x0800");
					modified.push_back("    0x0000");
					modified.push_back("set_sp:");
					modified.push_back("    er6 = adr_of [-2] " + label);
					modified.push_back("    sp = er6,pop er8");
				}
				else {
					modified.push_back(line);
				}
			}
			return setup_loop_detected ? modified : program;
		}
	};

	// ==========================
	// Output emitter
	// ==========================

	class OutputEmitter {
	public:
		static void emitHex(const vector<int>& bytes) {
			for (size_t i = 0; i < bytes.size(); ++i) {
				if (i)
					cout << ' ';
				cout << uppercase << hex << setw(2) << setfill('0') << (bytes[i] & 0xff);
			}
			cout << dec << "\n";
		}

		static void emitKey(const vector<int>& bytes, const vector<int>& npress, const vector<string>& symbolrepr) {
			for (size_t i = 0; i < bytes.size(); ++i) {
				if (i)
					cout << ' ';
				cout << byteToKey(bytes[i] & 0xff, npress, symbolrepr);
			}
			cout << dec << "\n";
		}
	};

	// ==========================
	// Multi-section driver
	// ==========================

	struct CompileOptions {
		string target = "none";
		string format = "hex";
		int overflowInitialSp = 0x8154;
	};

	class Driver {
		CommandDatabase& db;
		vector<int> npress;
		vector<string> symbolrepr;

		int getNpressAdr(int adr) const {
			return npress[adr & 0xff] + npress[(adr >> 8) & 0xff];
		}

	public:
		explicit Driver(CommandDatabase& db) : db(db), npress(256, 1), symbolrepr(256, "") {
			for (int i = 0; i < 256; ++i) {
				symbolrepr[i] = "<" + hexWord(i, 2).substr(2) + ">";
			}
		}

		void setNpress(const vector<int>& customNpress) {
			if (customNpress.size() == 256)
				npress = customNpress;
		}

		void setSymbolrepr(const vector<string>& customSymbolrepr) {
			if (customSymbolrepr.size() == 256)
				symbolrepr = customSymbolrepr;
		}

		void compileProgram(const vector<string>& source, const CompileOptions& opt) {
			auto sections = SourcePreprocessor::splitSections(source);
			int previousLen = 0;

			for (size_t i = 0; i < sections.size(); ++i) {
				vector<string> modified_program = SourcePreprocessor::processSetupLoop(sections[i]);

				Compiler cc(db, npress);
				cc.setPreviousSectionLength(previousLen);
				cc.compileLines(modified_program);
				cc.finish();

				auto& st = cc.state();
				int home = 0;
				int home2 = 0;

				if (opt.target == "none" || opt.target == "overflow") {
					if (opt.target == "overflow" && st.result.size() > 100) {
						throw runtime_error("Program too long for overflow (max 100 bytes)");
					}

					if (st.home.has_value()) {
						home = *st.home;
					}
					else {
						home = opt.overflowInitialSp;
						if (st.labels.count("home")) {
							home -= st.labels["home"];
						}
						if (home + st.result.size() > 0x8E00) {
							cerr << "Warning: Program length after home > available space\n";
						}

						int min_home = home;
						while (min_home >= 0x8154 + 200)
							min_home -= 100;
						while (home + st.result.size() <= 0x8E00)
							home += 100;

						int best_home = min_home;
						int best_score = 9999999;
						int best_home_neg = -min_home;

						for (int h = min_home; h < home; h += 100) {
							int score = 0;
							for (auto& fixup : st.adrOfFixups) {
								int target_adr = h + st.labels[fixup.label] + fixup.offset;
								if (getNpressAdr(target_adr) >= 100)
									score += 1;
							}
							if (score < best_score || (score == best_score && (-h) < best_home_neg)) {
								best_score = score;
								best_home_neg = -h;
								best_home = h;
							}
						}
						home = best_home;
					}
				}
				else if (opt.target == "loader") {
					if (st.home.has_value()) {
						home = *st.home;
					}
					else {
						home = 0x85b0 - int(st.result.size());
						int entry = home + (st.labels.count("home") ? st.labels["home"] : 0) - 2;

						vector<int> loader_tail = {0x6a, 0x4f, 0, 0, entry & 255, (entry >> 8) & 255, 0x68, 0x4f, 0, 0};
						st.result.insert(st.result.end(), loader_tail.begin(), loader_tail.end());

						while (home + st.result.size() < 0x85d7) {
							st.result.push_back(0);
						}
						st.result.insert(st.result.end(), {0xff, 0xae, 0x85});

						home2 = 0;
						if (home - home2 < 0x8501)
							throw runtime_error("Program too long for loader mode");

						while (getNpressAdr(home - home2) >= 100) {
							home2 += 1;
						}
					}
				}
				else {
					throw runtime_error("Internal error: Unknown target");
				}

				cc.resolveAdrOf(home);

				cout << "\n=== Section " << (i + 1) << "/" << sections.size() << " ===\n";
				if (i > 0) {
					cout << "Previous section length: 0x" << hex << uppercase << previousLen << dec
						 << " (" << previousLen << " bytes)\n";
				}

				for (auto const& [label, offset] : st.labels) {
					cout << "The " << label << " code is being redirected to address 0x"
						 << hex << uppercase << (home + offset) << dec << ".\n";
				}

				cout << "Length (HEX): 0x" << hex << uppercase << st.result.size()
					 << dec << " bytes | (DEC): " << st.result.size() << " bytes\n";

				if (opt.target == "overflow") {
					vector<int> hackstring(100);
					string pattern = "1234567890";
					for (int k = 0; k < 100; ++k)
						hackstring[k] = pattern[k % 10];

					for (size_t k = 0; k < st.result.size(); ++k) {
						hackstring[(home + k - 0x8154) % 100] = st.result[k];
					}

					if (opt.format == "hex") {
						OutputEmitter::emitHex(hackstring);
					}
					else if (opt.format == "key") {
						OutputEmitter::emitKey(hackstring, npress, symbolrepr);
					}
				}
				else if (opt.target == "none") {
					if (opt.format == "hex") {
						int addr_val = st.orgAddress.has_value() ? (*st.orgAddress + st.result.size()) : (home + st.result.size());
						cout << "=====0x" << hex << uppercase << home << " -> 0x" << addr_val << dec << "=====\n";
						OutputEmitter::emitHex(st.result);
						cout << "======================\n";
					}
					else if (opt.format == "key") {
						cout << "0x" << hex << home << dec << ": ";
						OutputEmitter::emitKey(st.result, npress, symbolrepr);
					}
				}
				else if (opt.target == "loader") {
					if (opt.format == "key") {
						cout << "Address to load: "
							 << byteToKey((home - home2) & 255, npress, symbolrepr) << " "
							 << byteToKey((home - home2) >> 8, npress, symbolrepr) << "\n";

						vector<int> final_result(home2, 0);
						final_result.insert(final_result.end(), st.result.begin(), st.result.end());

						OutputEmitter::emitKey(final_result, npress, symbolrepr);
					}
					else {
						OutputEmitter::emitHex(st.result);
					}
				}

				previousLen = int(st.result.size());

				if (i < sections.size() - 1) {
					cout << "\n============================================================\n";
				}
			}
		}
	};

} // namespace lc::details

namespace lc {
	using Parser = details::Parser;
	using CommandDatabase = details::CommandDatabase;
	using Compiler = details::Compiler;
	using CompilerState = details::CompilerState;
	using Diagnostic = details::Diagnostic;
	using CompileOptions = details::CompileOptions;
	using Driver = details::Driver;
} // namespace lc