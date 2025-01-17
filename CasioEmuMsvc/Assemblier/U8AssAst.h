#pragma once
#include <vector>
#include "ScriptContext.h"
#include "ScriptIr.h"
#include "ScriptLexer.h"
#include "ScriptVariant.h"

namespace AST {
	class Statement {
	public:
		Statement() = default;
		Statement(const Statement&) = delete;
		Statement(Statement&&) = delete;
		virtual ~Statement() = default;
		virtual void Emit(ir::Emitter& em) {
			throw std::exception("Invalid operation.");
		}
		virtual bool IsConst(ScriptContext& ctx) {
			return false;
		}
	};
	class Program {
	public:
		virtual ~Program() {}
		void Emit(ir::Emitter& e) {
			auto init_command = e.EmitOpI1(ir::Opcode::OP_PushN, 0); // 初始化栈

			// 插入所有程序内的语句
			for (auto stat : statements_)
				stat->Emit(e);

			init_command.GetOperand() = static_cast<unsigned char>(e.LocalVariables.size()); // 由于已经插入了所有命令，现在可以获取本地变量的数量，并正确初始化栈
			e.EmitOp(ir::Opcode::OP_Brk);													 // 终止程序运行，如果执行到末尾
		}
		std::vector<Statement*> statements_;
	};

	class Expression : public Statement {
	public:
		virtual bool IsLeftValue() {
			return false;
		}
		virtual Variant Eval(ScriptContext& ctx) {
			throw std::runtime_error("Invalid operation.");
		}
		virtual std::string GetVariableName() {
			throw std::runtime_error("Invalid operation.");
		}
		virtual void EmitSet(ir::Emitter& em, Expression* tgt) {
			throw std::runtime_error("Invalid operation.");
		}
	};
	class FunctionExpression : public Expression {
	public:
		std::vector<std::string> Params;
		std::vector<Statement*> Statements;
		FunctionExpression(std::vector<std::string> Params, std::vector<Statement*> Statements)
			: Params(Params), Statements(Statements) {}
		void Emit(ir::Emitter& em) override {
			auto beg = em.Bytes.size();							 // 记录 Lambda 函数体开始
			auto jump_across = em.EmitOp(ir::Opcode::OP_Jmp, 0); // 跳过 Lambda 函数体

			// 隔离 Emitter
			ir::Emitter em2{};
			em2.ctx = em.ctx;
			em2.Arguments = Params;
			em2.Strings = em.Strings;
			em2.Bytes = em.Bytes;

			auto func_start = em.Bytes.size();						   // 记录函数的开始
			auto init_command = em2.EmitOpI1(ir::Opcode::OP_PushN, 0); // 初始化栈

			// 按序插入所有语句
			for (auto exp : Statements) {
				exp->Emit(em2);
			}

			// 拷贝新插入的指令
			em.Bytes = em2.Bytes;
			em.Strings = em2.Strings;

			init_command.GetOperand() = static_cast<unsigned char>(em2.LocalVariables.size()); // 获取新的栈的本地变量数量，使其正确初始化
			em.EmitOp(ir::Opcode::OP_RetNull);												   // 以防 CtrlFlow 中有路径没有返回，插入额外的返回指令，抛弃任何可能的数据
			auto end = em.Bytes.size();														   // Lambda 函数的结尾
			jump_across.GetOperand() = (int)(end - beg) - 5;								   // 计算需要跳过的距离，并减去 Jmp imm4 指令的长度(5)
			em.EmitOp(ir::Opcode::OP_PushFuncPtr, (int)func_start);							   // 发射一条指令，将上述 Lambda 函数作为值推入栈
		}
	};
	class VariantRefExpression : public Expression {
	public:
		VariantRefExpression(std::string varname) : VariantName(varname) {
		}
		bool IsConst(ScriptContext& ctx) override {
			if (ctx.InternalConstants.find(VariantName) != ctx.InternalConstants.end()) {
				return true;
			}
			return false;
		}
		std::string VariantName;
		Variant Eval(ScriptContext& ctx) override {
			return ctx.LookupGlobal(VariantName);
		}
		bool IsLeftValue() override {
			return true;
		}
		std::string GetVariableName() {
			return VariantName;
		}
		void Emit(ir::Emitter& em) override {
			em.EmitOpPushVar(VariantName); // 插入获取变量的指令
		}
		void EmitSet(ir::Emitter& em, Expression* tgt) override {
			if (tgt != 0)
				tgt->Emit(em);				// 插入目标语句，如果可能
			em.EmitOpStoreVar(VariantName); // 插入存储变量的指令
		}
	};
	class GlobalVariantRefExpression : public Expression {
	public:
		GlobalVariantRefExpression(std::string varname) : VariantName(varname) {
		}
		bool IsConst(ScriptContext& ctx) override {
			if (ctx.InternalConstants.find(VariantName) != ctx.InternalConstants.end()) {
				return true;
			}
			return false;
		}
		std::string VariantName;
		Variant Eval(ScriptContext& ctx) override {
			return ctx.LookupGlobal(VariantName);
		}
		bool IsLeftValue() override {
			return true;
		}
		void Set(ScriptContext& ctx, Variant v) {
			ctx.SetGlobalVar(VariantName, v);
		}
		void Emit(ir::Emitter& em) override {
			if (em.ctx != nullptr)
				em.ctx->GlobalVars[VariantName];				  // 保留为全局变量，使后续 Emit 流程 插入正确 Scope 的指令
			em.EmitOp(ir::Opcode::OP_PushGlobalVar, VariantName); // 插入读取变量的指令
		}
		void EmitSet(ir::Emitter& em, Expression* tgt) override {
			if (em.ctx != nullptr)
				em.ctx->GlobalVars[VariantName]; // 保留为全局变量，使后续 Emit 流程 插入正确 Scope 的指令
			if (tgt != 0)
				tgt->Emit(em);									   // 插入目标语句，如果可能
			em.EmitOp(ir::Opcode::OP_StoreGlobalVar, VariantName); // 插入存储变量的指令
		}
	};
	class StringExpression : public Expression {
	public:
		std::string str;
		bool IsConst(ScriptContext& ctx) override {
			return true;
		}
		// 通过 Expression 继承
		Variant Eval(ScriptContext& ctx) override {
			return { ctx.gc, str.data() };
		}

		StringExpression(const std::string& str)
			: str(str) {
		}
		void Emit(ir::Emitter& em) override {
			em.EmitOp(ir::Opcode::OP_PushStr, str); // 插入字符串
		}
	};
	class NumberExpression : public Expression {
	public:
		NumberExpression(Variant v) : var(v) {}
		Variant var;
		bool IsConst(ScriptContext& ctx) override {
			return true;
		}
		// 通过 Expression 继承
		Variant Eval(ScriptContext&) override {
			return var;
		}
		void Emit(ir::Emitter& em) override {
			// 根据类型，插入适合的指令
			if (var.Type == Variant::DataType::Int) { // I4
				if (var.Int == 0) {
					em.EmitOp(ir::Opcode::OP_PushI4_0);
				}
				else if (var.Int == 1) {
					em.EmitOp(ir::Opcode::OP_PushI4_1);
				}
				else
					em.EmitOp(ir::Opcode::OP_PushI4, var.Int);
			}
			else if (var.Type == Variant::DataType::Long) { // I8
				if (var.Long == 0) {
					em.EmitOp(ir::Opcode::OP_PushI4_0);
				}
				else if (var.Long == 1) {
					em.EmitOp(ir::Opcode::OP_PushI4_1);
				}
				else
					em.EmitOp(ir::Opcode::OP_PushI4, var.Long);
			}
			else if (var.Type == Variant::DataType::Float) {
				em.EmitOp(ir::Opcode::OP_PushFP4, var.Float);
			}
			else if (var.Type == Variant::DataType::Double) {
				em.EmitOp(ir::Opcode::OP_PushFP8, var.Double);
			}
			else if (var.Type == Variant::DataType::Null) {
				em.EmitOp(ir::Opcode::OP_PushNull);
			}
			else if (var.Type == Variant::DataType::String) {
				em.EmitOp(ir::Opcode::OP_PushStr, var.GetString());
			}
			else {
				throw std::runtime_error("err");
			}
		}
	};
	enum class UnOp {
		Nop,
		Not,
		Bnot,
		Negative,
		Positive,
		Increase,
		Decrease,
		PostfixIncrease,
		PostfixDecrease,
	};
	enum class BinOp {
		Nop,
		Add,
		Sub,
		Mul,
		Div,
		Mov,
		Member,
		Index,

		Greater,
		Lesser,
		IsEqual,
		GreaterOrEqual,
		LesserOrEqual,
		NotEqual,

		Or,
		And,
		Band,
		Bor,
		Xor,

		Range,

		AddMov,
		SubMov,
		MulMov,
		DivMov,
		BandMov,
		BorMov,
		XorMov,
	};
	class OutNullStatement : public Statement {
	public:
		OutNullStatement(Expression* expr) : expr(expr) {}
		Expression* expr;
		bool IsConst(ScriptContext& ctx) override {
			if (expr == nullptr)
				return true;
			return expr->IsConst(ctx);
		}
		void Emit(ir::Emitter& em) override {
			if (expr != 0) {
				expr->Emit(em);				   // 插入目标语句
				em.EmitOp(ir::Opcode::OP_Pop); // 弹出内容
			}
		}
	};
	class BinaryExpression : public Expression {
	public:
		BinaryExpression(Expression* leftExpression, BinOp op, Expression* rightExpression)
			: leftExpression_(leftExpression), op(op), rightExpression_(rightExpression) {}
		bool IsConst(ScriptContext& ctx) override {
			return leftExpression_->IsConst(ctx) && rightExpression_->IsConst(ctx);
		}
		~BinaryExpression() {
			if (leftExpression_ != 0)
				delete leftExpression_;
			if (rightExpression_ != 0)
				delete rightExpression_;
		}
		Expression* leftExpression_;
		Expression* rightExpression_;
		BinOp op;
		virtual bool IsLeftValue() {
			return op == BinOp::Member;
		}
		virtual void Set(ScriptContext& ctx, Variant v) {
			auto l = leftExpression_->Eval(ctx);
			auto r = rightExpression_->GetVariableName();
			if (l.Type == Variant::DataType::Object) {
				((ScriptObject*)l.Object)->Set(r, v);
			}
		}
		Variant Eval(ScriptContext& ctx) override {
			switch (op) {
			case AST::BinOp::Nop:
				return leftExpression_->Eval(ctx);
				break;
			case AST::BinOp::Add: {
				auto lft = leftExpression_->Eval(ctx);
				auto rht = rightExpression_->Eval(ctx);
				if (lft.Type == Variant::DataType::String || rht.Type == Variant::DataType::String) {
					return { ctx.gc, (lft.ToString() + rht.ToString()).c_str() };
				}
				if (lft.Type == Variant::DataType::Double || rht.Type == Variant::DataType::Double) {
					return Variant{
						script_cast<double>(lft) + script_cast<double>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Float || rht.Type == Variant::DataType::Float) {
					return Variant{
						script_cast<float>(lft) + script_cast<float>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Long || rht.Type == Variant::DataType::Long) {
					return Variant{
						script_cast<long long>(lft) + script_cast<long long>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Int || rht.Type == Variant::DataType::Int) {
					return Variant{
						script_cast<int>(lft) + script_cast<int>(rht)
					};
				}

				throw std::exception("Cannot promote a fit type.");
			} break;
			case AST::BinOp::Sub: {
				auto lft = leftExpression_->Eval(ctx);
				auto rht = rightExpression_->Eval(ctx);
				if (lft.Type == Variant::DataType::Double || rht.Type == Variant::DataType::Double) {
					return Variant{
						script_cast<double>(lft) - script_cast<double>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Float || rht.Type == Variant::DataType::Float) {
					return Variant{
						script_cast<float>(lft) - script_cast<float>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Long || rht.Type == Variant::DataType::Long) {
					return Variant{
						script_cast<long long>(lft) - script_cast<long long>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Int || rht.Type == Variant::DataType::Int) {
					return Variant{
						script_cast<int>(lft) - script_cast<int>(rht)
					};
				}

				throw std::exception("Cannot promote a fit type.");
			} break;
			case AST::BinOp::Mul: {
				auto lft = leftExpression_->Eval(ctx);
				auto rht = rightExpression_->Eval(ctx);
				if (lft.Type == Variant::DataType::Double || rht.Type == Variant::DataType::Double) {
					return Variant{
						script_cast<double>(lft) * script_cast<double>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Float || rht.Type == Variant::DataType::Float) {
					return Variant{
						script_cast<float>(lft) * script_cast<float>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Long || rht.Type == Variant::DataType::Long) {
					return Variant{
						script_cast<long long>(lft) * script_cast<long long>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Int || rht.Type == Variant::DataType::Int) {
					return Variant{
						script_cast<int>(lft) * script_cast<int>(rht)
					};
				}

				throw std::exception("Cannot promote a fit type.");
			} break;
			case AST::BinOp::Div: {
				auto lft = leftExpression_->Eval(ctx);
				auto rht = rightExpression_->Eval(ctx);
				if (lft.Type == Variant::DataType::Double || rht.Type == Variant::DataType::Double) {
					return Variant{
						script_cast<double>(lft) / script_cast<double>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Float || rht.Type == Variant::DataType::Float) {
					return Variant{
						script_cast<float>(lft) / script_cast<float>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Long || rht.Type == Variant::DataType::Long) {
					return Variant{
						script_cast<long long>(lft) / script_cast<long long>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Int || rht.Type == Variant::DataType::Int) {
					return Variant{
						script_cast<int>(lft) / script_cast<int>(rht)
					};
				}

				throw std::exception("Cannot promote a fit type.");
			} break;
			case AST::BinOp::Member: {
				auto l = leftExpression_->Eval(ctx);
				auto r = rightExpression_->GetVariableName();
				switch (l.Type) {
				case Variant::DataType::Object: {
					if (l.Object->GetType() == typeid(ScriptObject)) {
						return ((ScriptObject*)l.Object)->Get(r);
					}
					if (l.Object->GetType() == typeid(ScriptArray)) {
						if (r == "size")
							return (long long)((ScriptArray*)l.Object)->Size();
						throw std::exception("Invalid opreation to array.");
					}
				} break;
				default:
					break;
				}
				throw std::exception("Left is not Object.");
			} break;
			case AST::BinOp::Band: {
				auto lft = leftExpression_->Eval(ctx);
				auto rht = rightExpression_->Eval(ctx);
				if (lft.Type == Variant::DataType::Long || rht.Type == Variant::DataType::Long) {
					return Variant{
						script_cast<long long>(lft) & script_cast<long long>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Int || rht.Type == Variant::DataType::Int) {
					return Variant{
						script_cast<int>(lft) & script_cast<int>(rht)
					};
				}

				throw std::exception("Cannot promote a fit type.");
			} break;
			case AST::BinOp::Bor: {
				auto lft = leftExpression_->Eval(ctx);
				auto rht = rightExpression_->Eval(ctx);
				if (lft.Type == Variant::DataType::Long || rht.Type == Variant::DataType::Long) {
					return Variant{
						script_cast<long long>(lft) | script_cast<long long>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Int || rht.Type == Variant::DataType::Int) {
					return Variant{
						script_cast<int>(lft) | script_cast<int>(rht)
					};
				}

				throw std::exception("Cannot promote a fit type.");
			} break;
			case AST::BinOp::Xor: {
				auto lft = leftExpression_->Eval(ctx);
				auto rht = rightExpression_->Eval(ctx);
				if (lft.Type == Variant::DataType::Long || rht.Type == Variant::DataType::Long) {
					return Variant{
						script_cast<long long>(lft) ^ script_cast<long long>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Int || rht.Type == Variant::DataType::Int) {
					return Variant{
						script_cast<int>(lft) ^ script_cast<int>(rht)
					};
				}

				throw std::exception("Cannot promote a fit type.");
			} break;
			case AST::BinOp::And: {
				auto lft = leftExpression_->Eval(ctx);
				if (!lft)
					return Variant{ 0 };
				auto rht = rightExpression_->Eval(ctx);
				if (rht)
					return Variant{ 1 };
				return Variant{ 0 };
			} break;
			case AST::BinOp::Or: {
				auto lft = leftExpression_->Eval(ctx);
				if (lft)
					return Variant{ 1 };
				auto rht = rightExpression_->Eval(ctx);
				if (rht)
					return Variant{ 1 };
				return Variant{ 0 };
			} break;
			case AST::BinOp::Greater: {
				auto lft = leftExpression_->Eval(ctx);
				auto rht = rightExpression_->Eval(ctx);
				if (lft.Type == Variant::DataType::String || rht.Type == Variant::DataType::String) {
					return lft.ToString() > rht.ToString();
				}
				if (lft.Type == Variant::DataType::Double || rht.Type == Variant::DataType::Double) {
					return Variant{
						script_cast<double>(lft) > script_cast<double>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Float || rht.Type == Variant::DataType::Float) {
					return Variant{
						script_cast<float>(lft) > script_cast<float>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Long || rht.Type == Variant::DataType::Long) {
					return Variant{
						script_cast<long long>(lft) > script_cast<long long>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Int || rht.Type == Variant::DataType::Int) {
					return Variant{
						script_cast<int>(lft) > script_cast<int>(rht)
					};
				}

				throw std::exception("Cannot promote a fit type.");
			} break;
			case AST::BinOp::Lesser: {
				auto lft = leftExpression_->Eval(ctx);
				auto rht = rightExpression_->Eval(ctx);
				if (lft.Type == Variant::DataType::String || rht.Type == Variant::DataType::String) {
					return lft.ToString() < rht.ToString();
				}
				if (lft.Type == Variant::DataType::Double || rht.Type == Variant::DataType::Double) {
					return Variant{
						script_cast<double>(lft) < script_cast<double>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Float || rht.Type == Variant::DataType::Float) {
					return Variant{
						script_cast<float>(lft) < script_cast<float>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Long || rht.Type == Variant::DataType::Long) {
					return Variant{
						script_cast<long long>(lft) < script_cast<long long>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Int || rht.Type == Variant::DataType::Int) {
					return Variant{
						script_cast<int>(lft) < script_cast<int>(rht)
					};
				}

				throw std::exception("Cannot promote a fit type.");
			} break;
			case AST::BinOp::GreaterOrEqual: {
				auto lft = leftExpression_->Eval(ctx);
				auto rht = rightExpression_->Eval(ctx);
				if (lft.Type == Variant::DataType::String || rht.Type == Variant::DataType::String) {
					return lft.ToString() >= rht.ToString();
				}
				if (lft.Type == Variant::DataType::Double || rht.Type == Variant::DataType::Double) {
					return Variant{
						script_cast<double>(lft) >= script_cast<double>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Float || rht.Type == Variant::DataType::Float) {
					return Variant{
						script_cast<float>(lft) >= script_cast<float>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Long || rht.Type == Variant::DataType::Long) {
					return Variant{
						script_cast<long long>(lft) >= script_cast<long long>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Int || rht.Type == Variant::DataType::Int) {
					return Variant{
						script_cast<int>(lft) >= script_cast<int>(rht)
					};
				}

				throw std::exception("Cannot promote a fit type.");
			} break;
			case AST::BinOp::LesserOrEqual: {
				auto lft = leftExpression_->Eval(ctx);
				auto rht = rightExpression_->Eval(ctx);
				if (lft.Type == Variant::DataType::String || rht.Type == Variant::DataType::String) {
					return lft.ToString() <= rht.ToString();
				}
				if (lft.Type == Variant::DataType::Double || rht.Type == Variant::DataType::Double) {
					return Variant{
						script_cast<double>(lft) <= script_cast<double>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Float || rht.Type == Variant::DataType::Float) {
					return Variant{
						script_cast<float>(lft) <= script_cast<float>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Long || rht.Type == Variant::DataType::Long) {
					return Variant{
						script_cast<long long>(lft) <= script_cast<long long>(rht)
					};
				}
				if (lft.Type == Variant::DataType::Int || rht.Type == Variant::DataType::Int) {
					return Variant{
						script_cast<int>(lft) <= script_cast<int>(rht)
					};
				}

				throw std::exception("Cannot promote a fit type.");
			} break;
			case AST::BinOp::IsEqual: {
				auto lft = leftExpression_->Eval(ctx);
				auto rht = rightExpression_->Eval(ctx);
				return lft == rht;
			} break;
			case AST::BinOp::NotEqual: {
				auto lft = leftExpression_->Eval(ctx);
				auto rht = rightExpression_->Eval(ctx);
				return lft != rht;
			} break;
			case AST::BinOp::Range: {
				auto lft = leftExpression_->Eval(ctx);
				auto rht = rightExpression_->Eval(ctx);
				if (lft.Type == Variant::DataType::Long || rht.Type == Variant::DataType::Long) {
					Variant v{};
					v.Type = Variant::DataType::Object;
					auto arr = new ScriptArray(ctx.gc);
					if (rht.Long < lft.Long)
						std::swap(rht.Long, lft.Long);
					arr->Variants.resize(rht.Long - lft.Long);
					size_t p = 0;
					for (long long i = lft.Long; i < rht.Long; i++) {
						arr->Set(p++, i);
					}
					v.Object = arr;
					return v;
				}
				if (lft.Type == Variant::DataType::Int || rht.Type == Variant::DataType::Int) {
					Variant v{};
					v.Type = Variant::DataType::Object;
					auto arr = new ScriptArray(ctx.gc);
					if (rht.Int < lft.Int)
						std::swap(rht.Int, lft.Int);
					arr->Variants.resize(rht.Int - lft.Int);
					size_t p = 0;
					for (int i = lft.Int; i < rht.Long; i++) {
						arr->Set(p++, i);
					}
					v.Object = arr;
					return v;
				}
				throw std::exception("Cannot promote a fit type.");
			} break;
			default:
				throw std::exception("Invalid operation.");
				break;
			}
			return {};
		}
		void EmitSet(ir::Emitter& em, Expression* expr) override {
			switch (op) {
			case AST::BinOp::Member: {
				leftExpression_->Emit(em);
				expr->Emit(em);
				auto r = rightExpression_->GetVariableName();
				em.EmitOp(ir::Opcode::OP_SetProp, r);
				return;
			}
			case AST::BinOp::Index: {
				leftExpression_->Emit(em);
				expr->Emit(em);
				rightExpression_->Emit(em);
				em.EmitOp(ir::Opcode::OP_SetIndex);
				return;
			}
			default:
				throw std::runtime_error("Invalid Operation.");
			}
		}
		void Emit(ir::Emitter& em) override {
			switch (op) {
			case AST::BinOp::Mov:
				leftExpression_->EmitSet(em, rightExpression_);
				return;
			case AST::BinOp::Member: {
				leftExpression_->Emit(em);
				auto r = rightExpression_->GetVariableName();
				em.EmitOp(ir::Opcode::OP_GetProp, r);
				return;
			}
			}
			leftExpression_->Emit(em);
			rightExpression_->Emit(em);
			switch (op) {
			case AST::BinOp::Nop:
				em.EmitOpI1(ir::Opcode::OP_Popn, 2);
				break;
			case AST::BinOp::Add:
				em.EmitOp(ir::Opcode::OP_Add);
				break;
			case AST::BinOp::Sub:
				em.EmitOp(ir::Opcode::OP_Sub);
				break;
			case AST::BinOp::Mul:
				em.EmitOp(ir::Opcode::OP_Mul);
				break;
			case AST::BinOp::Div:
				em.EmitOp(ir::Opcode::OP_Div);
				break;
			case AST::BinOp::AddMov:
				em.EmitOp(ir::Opcode::OP_Add);
				leftExpression_->EmitSet(em, 0);
				break;
			case AST::BinOp::SubMov:
				em.EmitOp(ir::Opcode::OP_Sub);
				leftExpression_->EmitSet(em, 0);
				break;
			case AST::BinOp::MulMov:
				em.EmitOp(ir::Opcode::OP_Mul);
				leftExpression_->EmitSet(em, 0);
				break;
			case AST::BinOp::DivMov:
				em.EmitOp(ir::Opcode::OP_Div);
				leftExpression_->EmitSet(em, 0);
				break;
			case AST::BinOp::Greater:
				em.EmitOp(ir::Opcode::OP_Gt);
				break;
			case AST::BinOp::Lesser:
				em.EmitOp(ir::Opcode::OP_Lt);
				break;
			case AST::BinOp::IsEqual:
				em.EmitOp(ir::Opcode::OP_Equ);
				break;
			case AST::BinOp::GreaterOrEqual:
				em.EmitOp(ir::Opcode::OP_Ge);
				break;
			case AST::BinOp::LesserOrEqual:
				em.EmitOp(ir::Opcode::OP_Le);
				break;
			case AST::BinOp::NotEqual:
				em.EmitOp(ir::Opcode::OP_Neq);
				break;
			case AST::BinOp::Or:
				em.EmitOp(ir::Opcode::OP_Or);
				break;
			case AST::BinOp::And:
				em.EmitOp(ir::Opcode::OP_And);
				break;
			case AST::BinOp::Band:
				em.EmitOp(ir::Opcode::OP_Band);
				break;
			case AST::BinOp::Bor:
				em.EmitOp(ir::Opcode::OP_Bor);
				break;
			case AST::BinOp::Xor:
				em.EmitOp(ir::Opcode::OP_Xor);
				break;
			case AST::BinOp::BandMov:
				em.EmitOp(ir::Opcode::OP_Band);
				leftExpression_->EmitSet(em, 0);
				break;
			case AST::BinOp::BorMov:
				em.EmitOp(ir::Opcode::OP_Bor);
				leftExpression_->EmitSet(em, 0);
				break;
			case AST::BinOp::XorMov:
				em.EmitOp(ir::Opcode::OP_Xor);
				leftExpression_->EmitSet(em, 0);
				break;
			case AST::BinOp::Index:
				em.EmitOp(ir::Opcode::OP_GetIndex);
				break;
			case AST::BinOp::Range:
				throw std::runtime_error("Range haven't been suppported.");
			default:
				throw std::runtime_error("op haven't been supported.");
				break;
			}
		}
	};
	class TernaryExpression : public Expression {
	public:
		TernaryExpression(Expression* condition, Expression* onTrue, Expression* onFalse) : condition(condition), onTrue(onTrue), onFalse(onFalse) {
		}
		~TernaryExpression() {
			if (condition != 0)
				delete condition;
			if (onTrue != 0)
				delete onTrue;
			if (onFalse != 0)
				delete onFalse;
		}
		Expression* condition;
		Expression* onTrue;
		Expression* onFalse;
		Variant Eval(ScriptContext& ctx) override {
			if (condition->Eval(ctx)) {
				return onTrue->Eval(ctx);
			}
			else {
				return onFalse->Eval(ctx);
			}
		}
		void Emit(ir::Emitter& em) override {
			condition->Emit(em);
			auto beg = em.Bytes.size();
			// je [elseBranch]
			em.EmitOp(ir::Opcode::OP_Jnz, 0);
			onTrue->Emit(em);
			auto el = em.Bytes.size();
			if (onFalse != 0) {
				// jmp end
				em.EmitOp(ir::Opcode::OP_Jmp, 0);
				onFalse->Emit(em);
				auto ed = em.Bytes.size();
				em.Modify(em.Bytes.begin() + el + 1, (int)(ed - el - 5));
				em.Modify(em.Bytes.begin() + beg + 1, (int)(el - beg));
			}
			else {
				em.Modify(em.Bytes.begin() + beg + 1, (int)(el - beg - 5));
			}
		}
	};
	class CallExpression : public Expression {
	public:
		CallExpression(Expression* method, std::vector<Expression*> args) : method(method), arguments(args) {
		}
		Expression* method;
		std::vector<Expression*> arguments;
		Variant Eval(ScriptContext& ctx) override {
			auto left = method->Eval(ctx);
			if (left.Type == Variant::DataType::Null)
				throw std::exception("Call on a null object.");
			auto vars = std::vector<Variant>();
			for (auto exp : arguments) {
				vars.push_back(exp->Eval(ctx));
			}
			if (left.Type == Variant::DataType::InternMethod) {
				return left.InternMethod(ctx, vars);
			}
			throw std::exception("Left is not Callable.");
			return {};
		}
		virtual void Emit(ir::Emitter& em) {
			for (auto arg : arguments) {
				arg->Emit(em);
			}
			method->Emit(em);
			em.EmitOpI1(ir::Opcode::OP_Call, static_cast<unsigned char>(arguments.size()));
		}
		~CallExpression() {
			if (method != 0)
				delete method;
			for (auto exp : arguments) {
				if (exp != 0)
					delete exp;
			}
		}
	};
	class UnaryExpression : public Expression {
	public:
		UnaryExpression(Expression* left, UnOp op) : left(left), op(op) {
		}
		Expression* left;
		UnOp op;

		~UnaryExpression() {
			if (left != 0)
				delete left;
		}
		bool IsConst(ScriptContext& ctx) override {
			if (op == AST::UnOp::Increase || op == AST::UnOp::Decrease) {
				return false;
			}
			return left->IsConst(ctx);
		}
		// 通过 Expression 继承
		Variant Eval(ScriptContext& ctx) override {
			switch (op) {
			case AST::UnOp::Nop:
				return left->Eval(ctx);
			case AST::UnOp::Not: {
				return !left->Eval(ctx);
			} break;
			case AST::UnOp::Bnot: {
				auto v = left->Eval(ctx);
				if (v.Type == Variant::DataType::Int) {
					return ~(v.Int);
				}
				if (v.Type == Variant::DataType::Long) {
					return ~(v.Long);
				}
				throw std::exception("Bad input.");
			} break;
			case AST::UnOp::Negative: {
				auto v = left->Eval(ctx);
				if (v.Type == Variant::DataType::Double) {
					return -(v.Double);
				}
				if (v.Type == Variant::DataType::Float) {
					return -(v.Float);
				}
				if (v.Type == Variant::DataType::Int) {
					return -(v.Int);
				}
				if (v.Type == Variant::DataType::Long) {
					return -(v.Long);
				}
				throw std::exception("Bad input.");
			} break;
			case AST::UnOp::Positive: {
				auto v = left->Eval(ctx);
				if (v.Type == Variant::DataType::Double) {
					return v.Double;
				}
				if (v.Type == Variant::DataType::Float) {
					return v.Float;
				}
				if (v.Type == Variant::DataType::Int) {
					return v.Int;
				}
				if (v.Type == Variant::DataType::Long) {
					return v.Long;
				}
				throw std::exception("Bad input.");
			} break;
			default:
				break;
			}
			return {};
		}

		void Emit(ir::Emitter& em) override {
			left->Emit(em);
			switch (op) {
			case AST::UnOp::Nop:
				break;
			case AST::UnOp::Not:
				em.EmitOp(ir::Opcode::OP_Not);
				break;
			case AST::UnOp::Bnot:
				em.EmitOp(ir::Opcode::OP_Bnot);
				break;
			case AST::UnOp::Negative:
				em.EmitOp(ir::Opcode::OP_Neg);
				break;
			case AST::UnOp::Positive:
				break;
			case AST::UnOp::Increase: {
				em.EmitOp(ir::Opcode::OP_Inc);
				left->EmitSet(em, 0);
			} break;
			case AST::UnOp::Decrease: {
				em.EmitOp(ir::Opcode::OP_Dec);
				left->EmitSet(em, 0);
			} break;
			case AST::UnOp::PostfixIncrease: {
				em.EmitOp(ir::Opcode::OP_Dup);
				em.EmitOp(ir::Opcode::OP_Inc);
				left->EmitSet(em, 0);
				em.EmitOp(ir::Opcode::OP_Pop);
			} break;
			case AST::UnOp::PostfixDecrease: {
				em.EmitOp(ir::Opcode::OP_Dup);
				em.EmitOp(ir::Opcode::OP_Dec);
				left->EmitSet(em, 0);
				em.EmitOp(ir::Opcode::OP_Pop);
			} break;
			default:
				break;
			}
		}
	};
	class StatementBlock : public Statement {
	public:
		StatementBlock(std::vector<Statement*> expression)
			: expressions(expression) {}

		void Emit(ir::Emitter& em) override {
			for (auto exp : expressions)
				exp->Emit(em);
		}
		std::vector<Statement*> expressions;
	};
	class ReturnStatement : public Statement {
	public:
		ReturnStatement(Expression* expression)
			: expression_(expression) {}

		void Emit(ir::Emitter& em) override {
			if (expression_ != 0) {
				expression_->Emit(em);
				em.EmitOp(ir::Opcode::OP_Ret);
			}
			else
				em.EmitOp(ir::Opcode::OP_RetNull);
		}
		Expression* expression_;
	};
	class BreakpointStatement : public Statement {
	public:
		void Emit(ir::Emitter& em) override {
			em.EmitOp(ir::Opcode::OP_Err);
		}
	};
	class IfStatement : public Statement {
	public:
		IfStatement(Expression* condition, Statement* thenStatement, Statement* elseStatement)
			: condition_(condition), thenStatement_(thenStatement), elseStatement_(elseStatement) {}

		void Emit(ir::Emitter& em) override {
			condition_->Emit(em);
			auto beg = em.Bytes.size();
			// je [elseBranch]
			em.EmitOp(ir::Opcode::OP_Jnz, 0);
			thenStatement_->Emit(em);
			auto el = em.Bytes.size();
			if (elseStatement_ != 0) {
				// jmp end
				em.EmitOp(ir::Opcode::OP_Jmp, 0);
				elseStatement_->Emit(em);
				auto ed = em.Bytes.size();
				em.Modify(em.Bytes.begin() + el + 1, (int)(ed - el - 5));
				em.Modify(em.Bytes.begin() + beg + 1, (int)(el - beg));
			}
			else {
				em.Modify(em.Bytes.begin() + beg + 1, (int)(el - beg - 5));
			}
		}
		Expression* condition_;
		Statement* thenStatement_;
		Statement* elseStatement_;
	};
	class WhileStatement : public Statement {
	public:
		WhileStatement(Expression* condition, Statement* bodyStatement)
			: condition_(condition), Statements(bodyStatement) {}
		bool IsConst(ScriptContext& ctx) override {
			return condition_->IsConst(ctx) && Statements->IsConst(ctx);
		}
		void Emit(ir::Emitter& em) override {
			auto beg = em.Bytes.size();
			condition_->Emit(em);
			auto branch = em.Bytes.size();
			em.EmitOp(ir::Opcode::OP_Jnz, 0);
			auto binds = em.LateBinds;
			Statements->Emit(em);
			auto end = em.Bytes.size();
			em.EmitOp(ir::Opcode::OP_Jmp, -(int)(end - beg) - 5);
			auto end2 = em.Bytes.size();
			em.Modify(em.Bytes.begin() + branch + 1, (int)(end2 - branch) - 5);
			em.EvalLateBinds(end2, beg);
			em.LateBinds = binds;
		}
		Expression* condition_;
		Statement* Statements;
	};
	class ForStatement : public Statement {
	public:
		ForStatement(Expression* startExpression, Expression* endExpression, Expression* stepExpression, Statement* bodyStatement)
			: startExpression_(startExpression), endExpression_(endExpression), stepExpression_(stepExpression), bodyStatement_(bodyStatement) {}

		void Emit(ir::Emitter& em) override {
			startExpression_->Emit(em);
			auto beg = em.Bytes.size();
			endExpression_->Emit(em);
			auto branch = em.Bytes.size();
			em.EmitOp(ir::Opcode::OP_Jnz, 0);
			auto binds = em.LateBinds;
			if (bodyStatement_ != 0)
				bodyStatement_->Emit(em);
			stepExpression_->Emit(em);
			em.EmitOp(ir::Opcode::OP_Pop);
			auto end = em.Bytes.size();
			em.EmitOp(ir::Opcode::OP_Jmp, -(int)(end - beg) - 5);
			auto end2 = em.Bytes.size();
			em.Modify(em.Bytes.begin() + branch + 1, (int)(end2 - branch) - 5);
			em.EvalLateBinds(end2, beg);
			em.LateBinds = binds;
		}

		Expression* startExpression_;
		Expression* endExpression_;
		Expression* stepExpression_;
		Statement* bodyStatement_;
	};
	class ThrowStatement : public Statement {
	public:
		ThrowStatement(Expression* expression)
			: expression_(expression) {}
		void Emit(ir::Emitter& em) override {
			expression_->Emit(em);
			em.EmitOp(ir::Opcode::OP_Throw);
		}
		Expression* expression_;
	};
	class BreakStatement : public Statement {
	public:
		BreakStatement() = default;
		void Emit(ir::Emitter& em) override {
			em.EmitOpLate(ir::Opcode::OP_Jmp, ir::Emitter::LateBindPointType::Break);
		}
	};
	class ContinueStatement : public Statement {
	public:
		ContinueStatement() = default;
		void Emit(ir::Emitter& em) override {
			em.EmitOpLate(ir::Opcode::OP_Jmp, ir::Emitter::LateBindPointType::Continue);
		}
	};
	class RangeForStatement : public Statement {
	public:
		RangeForStatement(std::string var, Expression* rangeExpression, Statement* bodyStatement)
			: varname(var), rangeExpression(rangeExpression), bodyStatement_(bodyStatement) {}
		std::string varname;
		Expression* rangeExpression;
		Statement* bodyStatement_;
	};
	class AssignmentStatement : public Statement {
	public:
		enum Scope {
			Global,
			Local,
		} scope{};
		std::vector<std::pair<std::string, Expression*>> initials;
		void Emit(ir::Emitter& em) override {
			for (auto& [name, init] : initials) {
				if (scope == Scope::Global) {
					if (em.ctx != nullptr)
						em.ctx->GlobalVars[name];
					if (init != 0)
						init->Emit(em);
					else
						em.EmitOp(ir::Opcode::OP_PushNull);
					em.EmitOp(ir::Opcode::OP_StoreGlobalVar, name);
				}
				else {
					if (init != 0)
						init->Emit(em);
					else
						em.EmitOp(ir::Opcode::OP_PushNull);
					em.EmitOpStoreVar(name);
				}
			}
			em.EmitOpI1(ir::Opcode::OP_Popn, static_cast<unsigned char>(initials.size()));
		}
	};
}
/// <summary>
/// 这是一个主体是递归下降的解析器
/// </summary>
class Parser {
public:
	Parser(std::vector<Lexer::Token> tokens) : tokens_(tokens), position_(0) {}

	AST::Program* parse() {
		AST::Program* program = new AST::Program();

		while (position_ < tokens_.size()) {
			AST::Statement* statement = parseStatement();
			if (statement != nullptr) {
				program->statements_.push_back(statement);
			}
		}

		return program;
	}
	size_t GetPos() const {
		return position_;
	}

private:
	std::vector<Lexer::Token> tokens_;
	size_t position_;

	AST::Statement* parseStatement() {
		auto stat = parseStatement_In();
		match(";");
		return stat;
	}
	AST::Statement* parseStatement_In() {
		switch (tokens_[position_++].type) {
		case Lexer::TokenType::ReturnKeyword:
			return new AST::ReturnStatement(parseExpression());
		case Lexer::TokenType::DebugbreakKeyword:
			return new AST::BreakpointStatement();
		case Lexer::TokenType::VarKeyword: {
			auto res = new AST::AssignmentStatement();
			res->scope = AST::AssignmentStatement::Scope::Global;
			while (match(Lexer::TokenType::Identifier)) {
				auto name = std::string(tokens_[position_ - 1].lexeme);
				AST::Expression* expr = 0;
				if (match(Lexer::TokenType::Operator, "=")) {
					expr = parseExpression();
				}
				res->initials.push_back({ name, expr });
				if (!match(Lexer::TokenType::Delimiter, ","))
					break;
			}
			return res;
		}
		case Lexer::TokenType::LetKeyword: {
			auto res = new AST::AssignmentStatement();
			res->scope = AST::AssignmentStatement::Scope::Local;
			while (match(Lexer::TokenType::Identifier)) {
				auto name = std::string(tokens_[position_ - 1].lexeme);
				AST::Expression* expr = 0;
				if (match(Lexer::TokenType::Operator, "=")) {
					expr = parseExpression();
				}
				res->initials.push_back({ name, expr });
				if (!match(Lexer::TokenType::Delimiter, ","))
					break;
			}
			return res;
		}
		case Lexer::TokenType::BreakKeyword:
			return new AST::BreakStatement();
		case Lexer::TokenType::ContinueKeyword:
			return new AST::ContinueStatement();
		case Lexer::TokenType::ThrowKeyword:
			return new AST::ThrowStatement(parseExpression());
		case Lexer::TokenType::IfKeyword: {
			AST::Expression* condition = parseExpression();
			if (condition != nullptr) {
				AST::Statement* thenStatement = parseStatement();
				if (thenStatement != nullptr) {
					AST::Statement* elseStatement = nullptr;
					if (match(Lexer::TokenType::ElseKeyword)) {
						elseStatement = parseStatement();
					}
					return new AST::IfStatement(condition, thenStatement, elseStatement);
				}
			}
			break;
		}
		case Lexer::TokenType::WhileKeyword: {
			auto openb = match("(");
			auto cond = parseExpression();
			if (openb)
				expect(")");
			return new AST::WhileStatement(cond, parseStatement());
		}
		case Lexer::TokenType::ForKeyword: {
			auto openb = match("(");
			auto a = parseExpression();
			match(";");
			auto b = parseExpression();
			match(";");
			auto c = parseExpression();
			if (openb)
				expect(")");
			return new AST::ForStatement(a, b, c, parseStatement());
		}
		case Lexer::TokenType::ForeachKeyword: {
			auto openb = match("(");
			auto var = tokens_[position_].lexeme;
			position_++;
			match(":");
			auto c = parseExpression();
			if (openb)
				expect(")");
			return new AST::RangeForStatement((std::string)var, c, parseStatement());
		}
		default:
			if (match("{")) {
				std::vector<AST::Statement*> statements;
				while (!match("}")) {
					auto old_pos = position_;
					auto stat = parseStatement();
					if (old_pos == position_)
						throw std::exception("Unexpected character.");
					if (stat != nullptr)
						statements.push_back(stat);
				}
				return new AST::StatementBlock(statements);
			}
			position_--; // 撤销解析
			break;
		}
		auto expr = parseExpression();
		return new AST::OutNullStatement(expr);
	}

	AST::Expression* parseExpression() {
		return parseBinaryExpression();
	}
	AST::Expression* parseTernaryExpression() {
		AST::Expression* condition = parsePrimaryExpression();
		if (match("?")) {
			AST::Expression* trueExpression = parseExpression();
			if (match(":")) {
				AST::Expression* falseExpression = parseExpression();
				return new AST::TernaryExpression(condition, trueExpression, falseExpression);
			}
			else {
				return trueExpression;
			}
		}
		return condition;
	}
	AST::UnOp prefixUnopConvert(std::string_view sv) {
		static const std::unordered_map<std::string_view, AST::UnOp> prefixOpMap = {
			{ "+", AST::UnOp::Positive },
			{ "-", AST::UnOp::Negative },
			{ "!", AST::UnOp::Not },
			{ "~", AST::UnOp::Bnot },
			{ "++", AST::UnOp::Increase },
			{ "--", AST::UnOp::Decrease }
		};

		auto it = prefixOpMap.find(sv);
		if (it != prefixOpMap.end())
			return it->second;

		return AST::UnOp::Nop;
	}

	AST::UnOp postfixUnopConvert(std::string_view sv) {
		static const std::unordered_map<std::string_view, AST::UnOp> postfixOpMap = {
			{ "++", AST::UnOp::PostfixIncrease },
			{ "--", AST::UnOp::PostfixDecrease }
		};

		auto it = postfixOpMap.find(sv);
		if (it != postfixOpMap.end())
			return it->second;

		return AST::UnOp::Nop;
	}

	AST::Expression* parsePrimaryExpression() {
		AST::Expression* exp = 0;
		auto op = prefixUnopConvert(tokens_[position_].lexeme);
		if (op != AST::UnOp::Nop) {
			position_++;
			return new AST::UnaryExpression(parsePrimaryExpression(), op);
		}
		if (match(Lexer::TokenType::FunctionKeyword)) {
			expect("(");
			std::vector<std::string> parameters;
			std::vector<AST::Statement*> statements;
			while (!match(")")) {
				if (match(Lexer::TokenType::Identifier)) {
					parameters.push_back(std::string(tokens_[position_ - 1].lexeme));
				}
				match(",");
			}
			expect("{");
			while (!match("}")) {
				AST::Statement* statement = parseStatement();
				statements.push_back(statement);
			}
			exp = new AST::FunctionExpression(parameters, statements);
		}
		else if (match(Lexer::TokenType::Identifier)) {
			std::string_view identifier = tokens_[position_ - 1].lexeme;
			if (identifier == "var") {
				position_++;
				identifier = tokens_[position_ - 1].lexeme;
				exp = new AST::GlobalVariantRefExpression(std::string(identifier));
			}
			else {
				exp = new AST::VariantRefExpression(std::string(identifier));
			}
		}
		else if (match(Lexer::TokenType::FloatLiteral)) {
			double value = std::stod(tokens_[position_ - 1].lexeme.data());
			Variant v{};
			v.Type = Variant::DataType::Double;
			v.Double = value;
			exp = new AST::NumberExpression(v);
		}
		else if (match(Lexer::TokenType::IntegerLiteral)) {
			long long value = std::stoll(tokens_[position_ - 1].lexeme.data());
			Variant v{};
			if (std::abs(value) <= INT_MAX) {
				v.Type = Variant::DataType::Int;
				v.Int = static_cast<int>(value);
			}
			else {
				v.Type = Variant::DataType::Long;
				v.Long = value;
			}
			exp = new AST::NumberExpression(v);
		}
		else if (match(Lexer::TokenType::StringLiteral)) {
			std::string sv = (std::string)tokens_[position_ - 1].lexeme;
			exp = new AST::StringExpression(sv);
		}
		else if (match("(")) {
			exp = parseExpression();
			expect(")");
		}
		else if (match(";")) {
			return nullptr;
		}
		else {
			throw std::exception("bad input.");
		}
		while (position_ < tokens_.size()) {
			op = postfixUnopConvert(tokens_[position_].lexeme);
			if (op != AST::UnOp::Nop) {
				position_++;
				exp = new AST::UnaryExpression(exp, op);
			}
			else {
				return exp;
			}
		}
		throw std::exception("Unexpected EOF.");
	}
	int getOperatorPrecedence(AST::BinOp op) {
		switch (op) {
		case AST::BinOp::Add:
		case AST::BinOp::Sub:
			return 98;
		case AST::BinOp::Mul:
		case AST::BinOp::Div:
			return 99;
		case AST::BinOp::Index:
		case AST::BinOp::Mov:
		case AST::BinOp::AddMov:
		case AST::BinOp::BandMov:
		case AST::BinOp::BorMov:
		case AST::BinOp::DivMov:
		case AST::BinOp::MulMov:
		case AST::BinOp::XorMov:
		case AST::BinOp::SubMov:
			return 5;
		case AST::BinOp::Greater:
		case AST::BinOp::Lesser:
		case AST::BinOp::GreaterOrEqual:
		case AST::BinOp::LesserOrEqual:
		case AST::BinOp::IsEqual:
		case AST::BinOp::NotEqual:
			return 10;
		case AST::BinOp::Band:
		case AST::BinOp::Bor:
		case AST::BinOp::Xor:
			return 97;
		case AST::BinOp::And:
		case AST::BinOp::Or:
			return 9;
		case AST::BinOp::Member:
			return 200;
		case AST::BinOp::Range:
			return 300;
		default:
			return 1;
		}
	}
	AST::BinOp opConvert(std::string_view sv) {
		static const std::unordered_map<std::string_view, AST::BinOp> opMap = {
			{ "+", AST::BinOp::Add },
			{ "-", AST::BinOp::Sub },
			{ "*", AST::BinOp::Mul },
			{ "/", AST::BinOp::Div },
			{ ".", AST::BinOp::Member },
			{ "!=", AST::BinOp::NotEqual },
			{ "==", AST::BinOp::IsEqual },
			{ ">", AST::BinOp::Greater },
			{ "<", AST::BinOp::Lesser },
			{ ">=", AST::BinOp::GreaterOrEqual },
			{ "<=", AST::BinOp::LesserOrEqual },
			{ "&", AST::BinOp::Band },
			{ "|", AST::BinOp::Bor },
			{ "&&", AST::BinOp::And },
			{ "||", AST::BinOp::Or },
			{ "^", AST::BinOp::Xor },
			{ "@", AST::BinOp::Range },
			{ "+=", AST::BinOp::AddMov },
			{ "-=", AST::BinOp::SubMov },
			{ "*=", AST::BinOp::MulMov },
			{ "/=", AST::BinOp::DivMov },
			{ "&=", AST::BinOp::BandMov },
			{ "|=", AST::BinOp::BorMov },
			{ "^=", AST::BinOp::XorMov }
		};

		auto it = opMap.find(sv);
		if (it != opMap.end()) {
			return it->second;
		}

		return AST::BinOp::Nop;
	}
	AST::Expression* parseBinaryExpression(int precedence = 0) {
		AST::Expression* left = parseTernaryExpression();

		while (position_ < tokens_.size()) {
			if (match("(")) {
				std::vector<AST::Expression*> expressions{};
				while (true) {
					if (match(")")) {
						left = new AST::CallExpression{ left, expressions };
						break;
					}
					match(",");
					expressions.push_back(parseExpression());
				}
			}
			else if (match("[")) {
				if (getOperatorPrecedence(AST::BinOp::Index) <= precedence) {
					position_--;
					break;
				}
				auto index = parseExpression();
				expect("]");
				left = new AST::BinaryExpression{ left, AST::BinOp::Index, index };
			}
			else if (match("=")) {
				if (getOperatorPrecedence(AST::BinOp::Mov) <= precedence) {
					position_--;
					break;
				}
				auto right = parseExpression();
				if (right == nullptr) {
					throw std::exception("Expect expression on right.");
				}
				left = new AST::BinaryExpression{ left, AST::BinOp::Mov, right };
			}
			else {
				Lexer::Token operatorToken = tokens_[position_];
				auto binop = opConvert(operatorToken.lexeme);
				if (binop == AST::BinOp::Nop)
					break;
				int operatorPrecedence = getOperatorPrecedence(binop);
				if (operatorPrecedence <= precedence)
					break;
				position_++;
				AST::Expression* right = parseBinaryExpression(operatorPrecedence);
				left = new AST::BinaryExpression(left, binop, right);
			}
		}

		return left;
	}
	bool match(Lexer::TokenType type) {
		if (position_ < tokens_.size() && tokens_[position_].type == type) {
			position_++;
			return true;
		}

		return false;
	}

	bool match(Lexer::TokenType type, std::string_view lexeme) {
		if (position_ < tokens_.size() && tokens_[position_].type == type && tokens_[position_].lexeme == lexeme) {
			position_++;
			return true;
		}

		return false;
	}
	bool match(std::string_view lexeme) {
		if (position_ < tokens_.size() && is_id(tokens_[position_].type) && tokens_[position_].lexeme == lexeme) {
			position_++;
			return true;
		}

		return false;
	}
	bool is_id(Lexer::TokenType tk) {
		using Lexer::TokenType::Operator, Lexer::TokenType::Identifier, Lexer::TokenType::Delimiter;
		return tk == Operator || tk == Identifier || tk == Delimiter;
	}
	void expect(Lexer::TokenType type, std::string_view lexeme) {
		if (position_ < tokens_.size() && tokens_[position_].type == type && tokens_[position_].lexeme == lexeme) {
			position_++;
			return;
		}
		auto s = std::format("Expect \"{}\" however got a \"{}\".", lexeme, tokens_[position_].lexeme);
		throw std::exception(s.c_str());
	}
	void expect(std::string_view lexeme) {
		if (position_ < tokens_.size() && is_id(tokens_[position_].type) && tokens_[position_].lexeme == lexeme) {
			position_++;
			return;
		}
		auto s = std::format("Expect \"{}\" however got a \"{}\".", lexeme, tokens_[position_].lexeme);
		throw std::exception(s.c_str());
	}
};