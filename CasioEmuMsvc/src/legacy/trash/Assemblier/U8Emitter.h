#pragma once
#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace u8 {
	using Imm1 = unsigned char;
	using UImm4 = unsigned int;
	using UImm8 = unsigned long long;
	using Imm4 = int;
	using Imm8 = long long;
	// Opcodes datas

	using Opcode = uint16_t;
	// clang-format off
		//           function,                     hints, main mask, operand {size, mask, shift} x2
		// * Arithmetic Instructions

		enum OpcodeHint {
			H_IE = 0x0001, // * Extend Immediate flag for arithmetic instructions.
			H_ST = 0x0002, // * Store flag for load/store/coprocessor instructions.
			H_DW = 0x0004, // * Store a new DSR value.
			H_DS = 0x0008, // * Instruction is a DSR prefix.
			H_IA = 0x0010, // * Increment EA flag for load/store/coprocessor instructions.
			H_TI = 0x0020, // * Instruction takes an external long immediate value.
			H_WB = 0x0040, // * Register Writeback flag for a lot of instructions to make life easier.
			H_PUSHL = 0x010000, // * Push {Register List}
			H_POPL = 0x020000, // * Pop {Register List}
			H_BCC = 0x040000, // * Bcc
			H_EXTBW = 0x080000, // * ExtBW
		};
		struct InstMap {
			const char* name;
			int hint;
			Opcode mask;
			struct {
				// 0 == imm
				// 1-7 == register
				// 9 == sp
				// 10 == bp
				// 11 == fp
				// 12 == psw
				// 13 == ECSR
				// 14 == ELR
				// 15 == EPSW
				// 16 == cc
				// 17 == ea
				// 19 == [erm]
				// 20 == [erm+disp16]
				// 21 == [bp+disp6]
				// 22 == [fp+disp6]
				// 23 == [dadr]
				// 24 == [ea]
				// 25 == [ea+]
				int operand_size;
				Opcode mask;
				int shift;
				bool empty()  const {
					return shift == 0 && mask == 0 && operand_size == 0;
				}
			} operands[2];
		};
	constexpr InstMap lookup_table[] = {
		{"add"        , H_WB                     , 0x8001, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"add"        , H_WB                     , 0x1000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"add"      , H_WB                     , 0xF006, {{2, 0x000E,  8}, {2, 0x000E,  4}}},
		{"add"      , H_WB               | H_IE, 0xE080, {{2, 0x000E,  8}, {0, 0x007F,  0}}},
		{"addc"       , H_WB                     , 0x8006, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"addc"       , H_WB                     , 0x6000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"and"        , H_WB                     , 0x8002, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"and"        , H_WB                     , 0x2000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"sub"        ,                         0, 0x8007, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"sub"        ,                         0, 0x7000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"subc"       ,                         0, 0x8005, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"subc"       ,                         0, 0x5000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"mov"      , H_WB                     , 0xF005, {{2, 0x000E,  8}, {2, 0x000E,  4}}},
		{"mov"      , H_WB               | H_IE, 0xE000, {{2, 0x000E,  8}, {0, 0x007F,  0}}},
		{"mov"        , H_WB                     , 0x8000, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"mov"        , H_WB                     , 0x0000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"or"         , H_WB                     , 0x8003, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"or"         , H_WB                     , 0x3000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"xor"        , H_WB                     , 0x8004, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"xor"        , H_WB                     , 0x4000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"cmp"      ,                         0, 0xF007, {{2, 0x000E,  8}, {2, 0x000E,  4}}},
		{"sub"        , H_WB                     , 0x8008, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"subc"       , H_WB                     , 0x8009, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		// * Shift Instructions
		{"sll"        , H_WB                     , 0x800A, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"sll"        , H_WB                     , 0x900A, {{1, 0x000F,  8}, {0, 0x0007,  4}}},
		{"sllc"       , H_WB                     , 0x800B, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"sllc"       , H_WB                     , 0x900B, {{1, 0x000F,  8}, {0, 0x0007,  4}}},
		{"sra"        , H_WB                     , 0x800E, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"sra"        , H_WB                     , 0x900E, {{1, 0x000F,  8}, {0, 0x0007,  4}}},
		{"srl"        , H_WB                     , 0x800C, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"srl"        , H_WB                     , 0x900C, {{1, 0x000F,  8}, {0, 0x0007,  4}}},
		{"srlc"       , H_WB                     , 0x800D, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"srlc"       , H_WB                     , 0x900D, {{1, 0x000F,  8}, {0, 0x0007,  4}}},

		// * Control Register Access Instructions
		{"add"      ,                         0, 0xE100, {{9}, {0, 0x00FF,  0}}},
		{"mov"       ,                    1 << 8, 0xA00F, {{13,      0,  0}, {1, 0x000F,  4}}},
		{"mov"       ,                    2 << 8, 0xA00D, {{14,      0,  0}, {2, 0x000E,  8}}},
		{"mov"       ,                    3 << 8, 0xA00C, {{15,      0,  0}, {1, 0x000F,  4}}},
		{"mov"       , H_WB            |  4 << 8, 0xA005, {{2, 0x000E,  8}, {14,      0,  0}}},
		{"mov"       , H_WB            |  5 << 8, 0xA01A, {{2, 0x000E,  8}, {9,      0,  0}}},
		{"mov"       ,                    6 << 8, 0xA00B, {{12,      0,  0}, {1, 0x000F,  4}}},
		{"mov"       ,                    7 << 8, 0xE900, {{12,      0,  0}, {0, 0x00FF,  0}}},
		{"mov"       , H_WB            |  8 << 8, 0xA007, {{1, 0x000F,  8}, {13,      0,  0}}},
		{"mov"       , H_WB            |  9 << 8, 0xA004, {{1, 0x000F,  8}, {15,      0,  0}}},
		{"mov"       , H_WB            | 10 << 8, 0xA003, {{1, 0x000F,  8}, {12,      0,  0}}},
		{"mov"       ,                   11 << 8, 0xA10A, {{9,      0,  0}, {2, 0x000E,  4}}},
		// * PUSH/POP Instructions
		{"push"       ,                         0, 0xF05E, {{2, 0x000E,  8}}},
		{"push"       ,                         0, 0xF07E, {{8, 0x0008,  8}}},
		{"push"       ,                         0, 0xF04E, {{1, 0x000F,  8}}},
		{"push"       ,                         0, 0xF06E, {{4, 0x000C,  8}}},
		{"pop"        , H_WB                     , 0xF01E, {{2, 0x000E,  8}}},
		{"pop"       , H_WB                     , 0xF03E, {{8, 0x0008,  8}}},
		{"pop"       , H_WB                     , 0xF00E, {{1, 0x000F,  8}}},
		{"pop"     , H_WB                     , 0xF02E, {{4, 0x000C,  8}}},

		// Just ignore coprocessor rn
		//// * Coprocessor Data Transfer Instructions
		//{&CPU::OP_CR_R       ,                         0, 0xA00E, {{0, 0x000F,  8}, {0, 0x000F,  4}}},
		//{&CPU::OP_CR_EA      ,      2 << 8 |           0, 0xF02D, {{0, 0x000E,  8}}},
		//{&CPU::OP_CR_EA      ,      2 << 8 | H_IA       , 0xF03D, {{0, 0x000E,  8}}},
		//{&CPU::OP_CR_EA      ,      1 << 8 |           0, 0xF00D, {{0, 0x000F,  8}}},
		//{&CPU::OP_CR_EA      ,      1 << 8 | H_IA       , 0xF01D, {{0, 0x000F,  8}}},
		//{&CPU::OP_CR_EA      ,      4 << 8 |           0, 0xF04D, {{0, 0x000C,  8}}},
		//{&CPU::OP_CR_EA      ,      4 << 8 | H_IA       , 0xF05D, {{0, 0x000C,  8}}},
		//{&CPU::OP_CR_EA      ,      8 << 8 |           0, 0xF06D, {{0, 0x0008,  8}}},
		//{&CPU::OP_CR_EA      ,      8 << 8 | H_IA       , 0xF07D, {{0, 0x0008,  8}}},
		//{&CPU::OP_CR_R       ,                      H_ST, 0xA006, {{0, 0x000F,  8}, {0, 0x000F,  4}}},
		//{&CPU::OP_CR_EA      ,      2 << 8 |        H_ST, 0xF0AD, {{0, 0x000E,  8}}},
		//{&CPU::OP_CR_EA      ,      2 << 8 | H_IA | H_ST, 0xF0BD, {{0, 0x000E,  8}}},
		//{&CPU::OP_CR_EA      ,      1 << 8 |        H_ST, 0xF08D, {{0, 0x000F,  8}}},
		//{&CPU::OP_CR_EA      ,      1 << 8 | H_IA | H_ST, 0xF09D, {{0, 0x000F,  8}}},
		//{&CPU::OP_CR_EA      ,      4 << 8 |        H_ST, 0xF0CD, {{0, 0x000C,  8}}},
		//{&CPU::OP_CR_EA      ,      4 << 8 | H_IA | H_ST, 0xF0DD, {{0, 0x000C,  8}}},
		//{&CPU::OP_CR_EA      ,      8 << 8 |        H_ST, 0xF0ED, {{0, 0x0008,  8}}},
		//{&CPU::OP_CR_EA      ,      8 << 8 | H_IA | H_ST, 0xF0FD, {{0, 0x0008,  8}}},
		


		// L ERn,[EA]
		{"l"      , 0                   , 0x9032, {{2, 0x000E,  8}, {24,      0,  0}}},
		// L ERn,[EA+]
		{"l"      , 0 |      H_IA       , 0x9052, {{2, 0x000E,  8}, {25,      0,  0}}},
		// L ERn,[ERm]
		{"l"       , 0                   , 0x9002, {{2, 0x000E,  8}, {19, 0x000E,  4}}},
		// L ERn,[ERm+Disp16]
		{"l"     , 0 |      H_TI       , 0xA008, {{2, 0x000E,  8}, {20, 0x000E,  4}}},
		// L ERn,[BP+Disp6]
		{"l"     , 0 |                0, 0xB000, {{2, 0x000E,  8}, {21, 0x003F,  0}}},
		// L ERn,[FP+Disp16]
		{"l"     , 0 |                0, 0xB040, {{2, 0x000E,  8}, {22, 0x003F,  0}}},
		// L ERn,[Dadr]
		{"l"     , 0 |      H_TI       , 0x9012, {{2, 0x000E,  8}, {23,      0,  0}}},
		// L Rn,[EA]
		{"l"     , 0                   , 0x9030, {{1, 0x000F,  8}, {24,      0,  0}}},
		// L Rn,[EA+]
		{"l"     , 0 |      H_IA       , 0x9050, {{1, 0x000F,  8}, {25,      0,  0}}},
		// L Rn,[ERm]
		{"l"     , 0                   , 0x9000, {{1, 0x000F,  8}, {19, 0x000E,  4}}},
		// L Rn,[Erm+Disp16]
		{"l"     , 0 |      H_TI       , 0x9008, {{1, 0x000F,  8}, {20, 0x000E,  4}}},
		// L Rn,[BP+Disp6]
		{"l"     , 0 |                0, 0xD000, {{1, 0x000F,  8}, {21, 0x003F,  0}}},
		// L Rn,[FP+Disp6]
		{"l"     , 0 |                0, 0xD040, {{1, 0x000F,  8}, {22, 0x003F,  0}}},
		// L Rn,[Dadr]
		{"l"     , 0 |      H_TI       , 0x9010, {{1, 0x000F,  8}, {23,      0,  0}}},
		// L XRn,[EA]
		{"l"     , 0                   , 0x9034, {{4, 0x000C,  8}, {24,      0,  0}}},
		// L XRn,[EA+]
		{"l"     , 0 |      H_IA       , 0x9054, {{4, 0x000C,  8}, {25,      0,  0}}},
		// L QRn,[EA]
		{"l"     , 0                   , 0x9036, {{8, 0x0008,  8}, {24,      0,  0}}},
		// L QRn,[EA+]
		{"l"     , 0 |      H_IA       , 0x9056, {{8, 0x0008,  8}, {25,      0,  0}}},
		//{&CPU::OP_LS_EA      , 0 |             H_ST, 0x9033, {{0, 0x000E,  8}, {0,      0,  0}}},
		//{&CPU::OP_LS_EA      , 0 |      H_IA | H_ST, 0x9053, {{0, 0x000E,  8}, {0,      0,  0}}},
		//{&CPU::OP_LS_R       , 0 |             H_ST, 0x9003, {{0, 0x000E,  8}, {2, 0x000E,  4}}},
		//{&CPU::OP_LS_I_R     , 0 |      H_TI | H_ST, 0xA009, {{0, 0x000E,  8}, {2, 0x000E,  4}}},
		//{&CPU::OP_LS_BP      , 0 |             H_ST, 0xB080, {{0, 0x000E,  8}, {0, 0x003F,  0}}},
		//{&CPU::OP_LS_FP      , 0 |             H_ST, 0xB0C0, {{0, 0x000E,  8}, {0, 0x003F,  0}}},
		//{&CPU::OP_LS_I       , 0 |      H_TI | H_ST, 0x9013, {{0, 0x000E,  8}, {0,      0,  0}}},
		//{&CPU::OP_LS_EA      , 0 |             H_ST, 0x9031, {{0, 0x000F,  8}, {0,      0,  0}}},
		//{&CPU::OP_LS_EA      , 0 |      H_IA | H_ST, 0x9051, {{0, 0x000F,  8}, {0,      0,  0}}},
		//{&CPU::OP_LS_R       , 0 |             H_ST, 0x9001, {{0, 0x000F,  8}, {2, 0x000E,  4}}},
		//{&CPU::OP_LS_I_R     , 0 |      H_TI | H_ST, 0x9009, {{0, 0x000F,  8}, {2, 0x000E,  4}}},
		//{&CPU::OP_LS_BP      , 0 |             H_ST, 0xD080, {{0, 0x000F,  8}, {0, 0x003F,  0}}},
		//{&CPU::OP_LS_FP      , 0 |             H_ST, 0xD0C0, {{0, 0x000F,  8}, {0, 0x003F,  0}}},
		//{&CPU::OP_LS_I       , 0 |      H_TI | H_ST, 0x9011, {{0, 0x000F,  8}, {0,      0,  0}}},
		//{&CPU::OP_LS_EA      , 0 |             H_ST, 0x9035, {{0, 0x000C,  8}, {0,      0,  0}}},
		//{&CPU::OP_LS_EA      , 0 |      H_IA | H_ST, 0x9055, {{0, 0x000C,  8}, {0,      0,  0}}},
		//{&CPU::OP_LS_EA      , 0 |             H_ST, 0x9037, {{0, 0x0008,  8}, {0,      0,  0}}},
		//{&CPU::OP_LS_EA      , 0 |      H_IA | H_ST, 0x9057, {{0, 0x0008,  8}, {0,      0,  0}}},
		
		// * EA Register Data Transfer Instructions
		{"lea"        ,                         0, 0xF00A, {{2, 0x000E,  4}}},
		{"lea"        ,        H_TI              , 0xF00B, {{2, 0x000E,  4}}},
		{"lea"        ,        H_TI              , 0xF00C},
		// * ALU Instructions
		{"daa"        , H_WB                     , 0x801F, {{1, 0x000F,  8}}},
		{"das"        , H_WB                     , 0x803F, {{1, 0x000F,  8}}},
		{"neg"        , H_WB                     , 0x805F, {{1, 0x000F,  8}}},
		// * Bit Access Instructions
		{"sb"     ,                         0, 0xA000, {{0, 0x000F,  8}, {0, 0x0007,  4}}},
		{"sb"     ,        H_TI              , 0xA080, {{0, 0x0007,  4}}},
		{"rb"     ,                         0, 0xA002, {{0, 0x000F,  8}, {0, 0x0007,  4}}},
		{"rb"     ,        H_TI              , 0xA082, {{0, 0x0007,  4}}},
		{"tb"     ,                         0, 0xA001, {{0, 0x000F,  8}, {0, 0x0007,  4}}},
		{"tb"     ,        H_TI              , 0xA081, {{0, 0x0007,  4}}},
		// * PSW Access Instructions
		{"ei"     ,                         0, 0xED08},
		{"di"    ,                         0, 0xEBF7},
		{"sc"     ,                         0, 0xED80},
		{"rc"    ,                         0, 0xEB7F},
		{"cplc"       ,                         0, 0xFECF},
		// * Software Interrupt Instructions
		{"swi"        ,                         0, 0xE500, {{0, 0x00FF,  0}}},
		{"brk"        ,                         0, 0xFFFF},
		{"iceswi",0,0xfeff},
		// * Multiplication and Division Instructions
		{"mul"        , H_WB                     , 0xF004, {{2, 0x000E,  8}, {1, 0x000F,  4}}},
		{"div"        , H_WB                     , 0xF009, {{2, 0x000E,  8}, {1, 0x000F,  4}}},
		// * Miscellaneous Instructions
		{"inc"     ,                         0, 0xFE2F,{{8}}}, // inc [ea]
		{"dec"     ,                         0, 0xFE3F,{{8}}}, // dec [ea]
		{"rt"         ,                         0, 0xFE1F},
		{"rti"        ,                         0, 0xFE0F},
		{"rtice"        ,                         0, 0xFE6F},
		{"rtice"        ,                         0, 0xFE7F},
		{"nop"        ,                         0, 0xFE8F},
		{"edsr"        ,               H_DS       , 0xFE9F},
		{"dsr"        ,               H_DS | H_DW, 0xE300, {{0, 0x00FF,  0}}},
		{"dsr"        ,               H_DS | H_DW, 0x900F, {{1, 0x000F,  4}}}
	};

	// clang-format on

	const char* GetOpCodeAbbr(Opcode op) {
		switch (op) {
		}
		return "";
	}

	struct Token {
		enum Type {
			IDENTIFIER,
			NUMBER,
			COLON,
			LBRACKET,
			RBRACKET,
			PLUS,
			COMMA,
			LBRACE, // 添加大括号支持
			RBRACE,
			EOL,
			INVALID
		} type;
		std::string value;

		Token(Type t, std::string v = "") : type(t), value(v) {}
	};
	constexpr const char* condition_codes[] = {"ge", "lt", "gt", "le", "ges", "lts", "gts", "les",
		"ne", "eq", "nv", "ov", "ps", "ns", "al", "nop"};
	class Tokenizer {
	private:
		std::string input;
		size_t position = 0;

		bool isRegisterStart(char c) {
			c = toupper(c);
			return c == 'R' || c == 'E' || c == 'X' || c == 'Q';
		}

		std::string readWhile(std::function<bool(char)> predicate) {
			std::string result;
			while (position < input.length() && predicate(input[position])) {
				result += input[position++];
			}
			return result;
		}

		void skipWhitespace() {
			while (position < input.length() && std::isspace(input[position])) {
				position++;
			}
		}

	public:
		Tokenizer(const std::string& input) : input(input) {}

		Token nextToken() {
			skipWhitespace();

			if (position >= input.length()) {
				return Token(Token::EOL);
			}

			char current = input[position];

			// Handle special characters
			switch (current) {
			case '[':
				position++;
				return Token(Token::LBRACKET);
			case ']':
				position++;
				return Token(Token::RBRACKET);
			case '{':
				position++;
				return Token(Token::LBRACE);
			case '}':
				position++;
				return Token(Token::RBRACE);
			case ':':
				position++;
				return Token(Token::COLON);
			case '+':
				position++;
				return Token(Token::PLUS);
			case ',':
				position++;
				return Token(Token::COMMA);
			}

			// Handle numbers (decimal and hex)
			if (std::isdigit(current) || (current == '0' && position + 1 < input.length() && input[position + 1] == 'x')) {
				if (current == '0' && position + 1 < input.length() && input[position + 1] == 'x') {
					position += 2;
					std::string hex = readWhile([](char c) { return std::isxdigit(c); });
					return Token(Token::NUMBER, "0x" + hex);
				}
				else {
					std::string num = readWhile([](char c) { return std::isdigit(c); });
					return Token(Token::NUMBER, num);
				}
			}

			if (std::isalpha(current) || current == '_') {
				std::string word = readWhile([](char c) { return std::isalnum(c) || c == '_'; });
				std::transform(word.begin(), word.end(), word.begin(), ::tolower);
				return Token(Token::IDENTIFIER, word);
			}

			position++;
			return Token(Token::INVALID, std::string(1, current));
		}
	};
	class Emitter {
	public:
		std::vector<char> Bytes;
		struct Operand {
            Operand(){

            }
			enum OperandType {
				None,
				Reg,
				EA,
				SP,
				Imm,
				DSR,
				Address,
			} type = OperandType::None;
			int offset{};
			int size{};
		};

	private:
		bool IsEqual(auto l, const Operand r) {
			// 0 == imm
			// 1-7 == register
			// 8 == ea
			// 9 == sp
			// 10 == bp(ER12)
			// 11 == fp(ER14)
			if ((l.operand_size == 0) && r.type == Operand::Imm)
				return true;
			if (l.operand_size >= 1 && l.operand_size <= 8) {
				if (l.operand_size == r.size && r.type == Operand::Reg)
					return true;
			}
			if (l.operand_size == 17 && r.type == Operand::EA)
				return true;
			if (l.operand_size == 9 && r.type == Operand::SP)
				return true;
			if (l.operand_size == 10 && r.type == Operand::Reg && r.size == 2 && r.offset == 12) {
				return true;
			}
			if (l.operand_size == 11 && r.type == Operand::Reg && r.size == 2 && r.offset == 14) {
				return true;
			}
			return false;
		}
		Opcode GenerateSubMask(auto l, const Operand r) {
			if (l.empty())
				return 0;
			if (r.type == Operand::Reg || r.type == Operand::Imm) {
				return (r.offset & l.mask) << l.shift;
			}
			return 0;
		}

	public:
		void EmitOp(const char* opname, Operand a = {}, Operand b = {}, std::optional<int16_t> imm16 = {}) {
			const InstMap* matched = nullptr;
			auto len = strlen(opname);
			if (len == 0)
				return;
			for (const auto& val : lookup_table) {
				if (strcmp(val.name, opname) == 0) {
					if ((val.operands[0].empty() || IsEqual(val.operands[0], a))) {
						if ((val.operands[1].empty() || IsEqual(val.operands[1], b))) {
							// if (((!!(val.hint | H_IE)) == imm16.has_value())) {
							matched = &val;
							break;
							// }
						}
					}
				}
			}
			if (!matched) {
				// Let's do some builtin magics, or just report an error?
				throw std::runtime_error("Instruction not found in the lookup table.");
			}
			else {
				auto opcode = matched->mask | GenerateSubMask(matched->operands[0], a) | GenerateSubMask(matched->operands[1], b);
				Emit((uint16_t)opcode);
				if (imm16.has_value())
					Emit((uint16_t)imm16.value());
			}
		}
		void EmitDsr(Operand op) {
			if (op.type == Operand::DSR)
				Emit((uint16_t)0xFE9F);
			else if (op.type == Operand::Reg)
				Emit((uint16_t)(0x900F | ((op.offset & 0xf) << 4)));
			else if (op.type == Operand::Imm)
				Emit((uint16_t)(0xE300 | (op.offset & 0xff)));
			else
				throw std::runtime_error("Invalid Dsr prefix detected.");
		}
		void Emit(auto imm) {
			Bytes.insert(Bytes.end(), (char*)&imm, (char*)(&imm + 1));
		}

		void Assembly(std::string inst) {
			Tokenizer tokenizer(inst);
			std::vector<Token> tokens;
			Token token = tokenizer.nextToken();

			// Collect all tokens
			while (token.type != Token::EOL) {
				tokens.push_back(token);
				token = tokenizer.nextToken();
			}

			if (tokens.empty())
				return;

			// Parse instruction
			if (tokens[0].type != Token::IDENTIFIER) {
				throw std::runtime_error("Expected instruction at start of line");
			}

			std::string opname = tokens[0].value;

			// 处理特殊指令
			if (opname == "b") {
				// 检查是否是条件分支
				if (tokens.size() >= 3 && tokens[1].type == Token::IDENTIFIER) {
					// Bcc 指令
					auto cond = -1;
					for (size_t i = 0; i < 16; i++) {
						if (strcmp(condition_codes[i], tokens[1].value.c_str() + 1) == 0) {
							cond = i;
							break;
						}
					}
					if (cond != -1) {
						if (tokens[2].type != Token::NUMBER) {
							throw std::runtime_error("Expected immediate value for branch offset");
						}
						int offset = std::stoi(tokens[2].value);
						// 编码 Bcc 指令: 0xC000 | (condition << 8) | (offset & 0xFF)
						Emit((uint16_t)(0xC000 | (cond << 8) | (offset & 0xFF)));
						return;
					}
				}
				// 普通分支指令
				if (tokens.size() >= 2) {
					bool isLong = false;
					Operand target;

					if (tokens[1].type == Token::IDENTIFIER) {
						target.type = Operand::Reg;
						// 解析寄存器编号
						if (tokens[1].value.substr(0, 2) == "er") {
							target.size = 2;
							target.offset = std::stoi(tokens[1].value.substr(2));
							EmitOp("b", target);
						}
						else {
							throw std::runtime_error("Invalid register for branch");
						}
					}
					else if (tokens[1].type == Token::NUMBER) {
						target.type = Operand::Imm;
						target.offset = std::stoi(tokens[1].value);
						EmitOp("b", target);
					}
					return;
				}
			}

			// 处理 EXTBW 指令
			if (opname == "extbw") {
				throw std::runtime_error("TODO");
				Emit((uint16_t)0x810F);
				return;
			}

			// 处理 PUSH/POP 寄存器列表
			if (opname == "push" || opname == "pop") {
				if (tokens.size() >= 3 && tokens[1].type == Token::LBRACE) {
					int regList = 0;
					size_t i = 2;
					while (i < tokens.size() && tokens[i].type != Token::RBRACE) {
						if (tokens[i].type == Token::IDENTIFIER) {
							// 解析寄存器并设置相应的位
							if (tokens[i].value == "ea")
								regList |= 0x01;
							else if (tokens[i].value == "elr" || tokens[i].value == "pc")
								regList |= 0x02;
							else if (tokens[i].value == "epsw" || tokens[i].value == "psw")
								regList |= 0x04;
							else if (tokens[i].value == "lr")
								regList |= 0x08;
						}
						i++;
						if (i < tokens.size() && tokens[i].type == Token::COMMA)
							i++;
					}

					if (opname == "push") {
						Emit((uint16_t)(0xF0CE | (regList << 8)));
					}
					else { // pop
						Emit((uint16_t)(0xF08E | (regList << 8)));
					}
					return;
				}
			}


			// 处理常规指令
			std::vector<Operand> operands;
			Operand current_operand;
			std::optional<int16_t> imm16;
			bool in_brackets = false;
			bool expect_plus = false;

			// Parse operands
			for (size_t i = 1; i < tokens.size(); i++) {
				const Token& t = tokens[i];

				if (t.type == Token::COMMA) {
					if (!in_brackets && current_operand.type != Operand::None) {
						operands.push_back(current_operand);
						current_operand = Operand();
					}
					continue;
				}

				// op类型
				// 0 == imm
				// 1-8 == register
				// 9 == sp
				// 10 == bp
				// 11 == fp
				// 12 == psw
				// 13 == ECSR
				// 14 == ELR
				// 15 == EPSW
				// 16 == cc
				// 17 == ea
				
				// 19 == [erm]
				// 20 == [erm+disp16] (disp16存储到imm16)
				// 21 == [bp+disp6]
				// 22 == [fp+disp6]
				// 23 == [dadr] (存储到imm16)
				// 24 == [ea]
				// 25 == [ea+]
				switch (t.type) {
				case Token::IDENTIFIER: {
					if (t.value == "sp") {
						current_operand.type = Operand::SP;
					}
					else if (t.value == "ea") {
						current_operand.type = Operand::EA;
					}
					else if (t.value[0] == 'r') {
						current_operand.type = Operand::Reg;
						current_operand.size = 1;
						current_operand.offset = std::stoi(t.value.substr(1));
					}
					else if (t.value[0] == 'e') {
						current_operand.type = Operand::Reg;
						current_operand.size = 2;
						current_operand.offset = std::stoi(t.value.substr(2));
					}
					else if (t.value[0] == 'x') {
						current_operand.type = Operand::Reg;
						current_operand.size = 4;
						current_operand.offset = std::stoi(t.value.substr(2));
					}
					else if (t.value[0] == 'q') {
						current_operand.type = Operand::Reg;
						current_operand.size = 8;
						current_operand.offset = std::stoi(t.value.substr(2));
					}
					break;
				}
				case Token::LBRACKET: {
					in_brackets = true;
					current_operand.type = Operand::EA;
					break;
				}
				case Token::RBRACKET: {
					in_brackets = false;
					expect_plus = true;
					break;
				}
				case Token::NUMBER: {
					int value;
					if (t.value.substr(0, 2) == "0x") {
						value = std::stoi(t.value.substr(2), nullptr, 16);
					}
					else {
						value = std::stoi(t.value);
					}

					if (expect_plus) {
						imm16 = value;
						expect_plus = false;
					}
					else {
						current_operand.type = Operand::Imm;
						current_operand.offset = value;
					}
					break;
				}
				case Token::PLUS: {
					if (!expect_plus) {
						throw std::runtime_error("Unexpected '+'");
					}
					break;
				}
				default:
					throw std::runtime_error("Unexpected token in operand");
				}
			}

			if (current_operand.type != Operand::None) {
				operands.push_back(current_operand);
			}

			// Emit the instruction
			switch (operands.size()) {
			case 0:
				EmitOp(opname.c_str());
				break;
			case 1:
				EmitOp(opname.c_str(), operands[0]);
				break;
			case 2:
				EmitOp(opname.c_str(), operands[0], operands[1], imm16);
				break;
			default:
				throw std::runtime_error("Too many operands");
			}
		}
	};
} // namespace u8