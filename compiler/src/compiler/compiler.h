#ifndef COMPILER_H
#define COMPILER_H

#include "parser/ast.h"
#include "code_gen/instr.h"
#include "code_gen/code_gen.h"
#include "code_gen/abi.h"

//
// StringStorage
//

typedef struct {
	Allocator allocator;

	String* strings;
	uint32_t count;
	uint32_t capacity;
} StringStorage;

uint32_t str_storage_append(StringStorage* storage, String string);
void str_storage_release(StringStorage* storage);

inline StringArray str_storage_to_array(StringStorage* storage) {
	return (StringArray) { .values = storage->strings, .count = storage->count };
}

//
// FunctionCompiler
//

typedef struct ControlFlowStmt ControlFlowStmt;
typedef struct LoopSwitchState LoopSwitchState;

typedef enum {
	CONTROL_FLOW_BREAK,
	CONTROL_FLOW_CONTINUE,
} ControlFlowKind;

struct ControlFlowStmt {
	ControlFlowKind kind;

	// A region where this `break` or `continue` statement appears
	InstrIndex region;
	
	// Var and arg values at the time of reaching the `break` or `continue` statement.
	InstrIndex* var_values;
	InstrIndex* arg_values;

	ControlFlowStmt* next;
};

// This is ment to keep track of the nearest loop or switch statement to the current compiler
// location in the ast. Together with the nearest loop or switch, this struct also keeps track of
// the control flow statements (`break`s and `continue`s).
//
// NOTE: Here by the "compiler location in the ast" is ment, the ast node the compiler is currently
//       processing.
//
// Important considerations:
// * `break` can break from both loops and switch statement, whichever is the nearest one.
// * `continue`, however, only applies to loops. That means, when inside a switch statement a
//   `continue` will apply to the outer loop.
struct LoopSwitchState {
	// The parent loop/switch
	LoopSwitchState* parent;

	// Control flow statement for the this loop or switch.
	ControlFlowStmt* control_flow_stmts;

	AstNode* node;
};

typedef struct {
	Function* function;

	Arena* allocator;

	Arena* instr_allocator;
	Arena* temp_allocator;
	InstrBuffer instr_buffer;

	InstrIndex io_state;

	// The total number of variables in the function.
	//
	// Invariant `var_count == function->var_count`.
	size_t var_count;
	// The size is `var_count`. Index using the variable id.
	//
	// This array stores variables accessible in the scope of the node the compiler is currently
	// processing.
	//
	// * Each element is only assigned when the varialbe definition is encountered, otherwise it
	//   stays as `NULL`.
	// * Whenever the compiler leaves a scope, all the variables defined in that scope are reset
	//   back to `NULL` together with their instruction values.
	const Variable** vars;
	// The size is `var_count`.
	// Index using the variable id.
	//
	// Elements are assigned in the same way as for `vars`.
	const Scope** var_parent_scopes;
	// The size is `var_count`.
	// Index using the variable id.
	//
	// Elements are assigned in the same way as for `vars`. If the variable is not accessible,
	// stores `INVALID_INSTR_INDEX`.
	InstrIndex* var_values;
	InstrIndex* arg_states;

	const TypeContext* type_context;

	StringStorage* str_storage;
	SymbolMap* symbol_map;

	LoopSwitchState* loop_switch_state;
	ControlFlowStmt* free_control_flow_stmt;

	// An array internal to the compiler, which is used to defer filling of the
	// `function_call_signatures`. The array is allocated using the `temp_allocator`.
	//
	// Capacity is `function_call_count`
	Call** function_calls;

	// Number of calls currently stored in `function_calls`.
	size_t function_call_count;

	// Signatures used to tell the backend how to call functions
	//
	// Size is `function->function_call_count`
	AbiSignature* function_call_signatures;
} FunctionCompiler;

typedef struct {
	InstrBuffer instr_buffer;
	InstrIndex start_region;

	StringArray string_consts;
	AbiSignature* function_call_signatures;
	size_t function_call_signature_count;
} CompiledFunction;

CompiledFunction function_compiler_compile(FunctionCompiler* compiler);
void compiler_resolve_default_func_refs(SymbolMap* map);

void compiler_create_function_import_symbol(const Function* function, Symbol* out_symbol);
void compiler_collect_imported_symbols(const AST* ast, SymbolMap* imported_symbols);

AbiSignature function_prototype_to_abi_signature(const TypeContext* type_context,
		const FunctionPrototype* proto,
		Allocator allocator);

#endif
