#include "compiler.h"

#include <windows.h>

//
// StringStorage
//

uint32_t str_storage_append(StringStorage* storage, String string) {
	profile_scope_start(__func__);

	if (storage->count == storage->capacity) {
		uint32_t new_capacity = max(4, storage->capacity + storage->capacity / 2);
		String* new_array = allocator_alloc_array(storage->allocator, String, new_capacity);

		if (storage->count > 0) {
			assert(storage->strings);
			array_copy(new_array, storage->strings, storage->count);
			allocator_release(storage->allocator, storage->strings);
		} else {
			assert(storage->strings == NULL);
		}

		storage->strings = new_array;
		storage->capacity = new_capacity;
	}

	uint32_t index = storage->count;
	storage->strings[storage->count] = string;
	storage->count += 1;

	profile_scope_end();
	return index;
}

void str_storage_release(StringStorage* storage) {
	if (storage->strings) {
		allocator_release(storage->allocator, storage->strings);
	}

	*storage = (StringStorage) {};
}

//
// FunctionCompiler
//

static ControlFlowStmt* _alloc_control_flow_stmt(FunctionCompiler* compiler) {
	if (compiler->free_control_flow_stmt) {
		ControlFlowStmt* stmt = compiler->free_control_flow_stmt;
		compiler->free_control_flow_stmt = stmt->next;
		stmt->next = NULL;
		return stmt;
	}

	ControlFlowStmt* stmt = heap_alloc(ControlFlowStmt);
	stmt->next = NULL;

	size_t arg_count = compiler->function->proto.parameter_count;

	stmt->var_values = heap_alloc_array(InstrIndex, compiler->var_count);
	stmt->arg_values = heap_alloc_array(InstrIndex, arg_count);
	return stmt;
}

static void _free_control_flow_stmt(FunctionCompiler* compiler, ControlFlowStmt* stmt) {
	assert(stmt != NULL);

	ControlFlowStmt* last = stmt;
	for (; last->next != NULL; last = last->next) {}

	last->next = compiler->free_control_flow_stmt;
	compiler->free_control_flow_stmt = stmt;
}

static void _free_all_control_flow_stmts(FunctionCompiler* compiler) {
	ControlFlowStmt* stmt = compiler->free_control_flow_stmt;
	while (stmt) {
		ControlFlowStmt* next = stmt->next;

		heap_release(stmt->var_values);
		heap_release(stmt->arg_values);
		heap_release(stmt);
		stmt = next;
	}
}

static LoopSwitchState* _get_current_loop_state(FunctionCompiler* compiler) {
	LoopSwitchState* state = compiler->loop_switch_state;

	while (state) {
		assert(state->node);
		switch (state->node->kind) {
		case AST_NODE_WHILE_LOOP:
		case AST_NODE_FOR_LOOP:
			return state;
		case AST_NODE_SWITCH:
			panic("Nearest loop is actually a switch statement. This is not allowed");
		default:
			panic("`node` in `LoopSwitchState` can only be either a loop (for, while) or a switch");
		}

		state = state->parent;
	}

	return NULL;
}

static void _restore_loop_switch_state(FunctionCompiler* compiler) {
	assert(compiler->loop_switch_state);
	if (compiler->loop_switch_state->control_flow_stmts) {
		_free_control_flow_stmt(compiler, compiler->loop_switch_state->control_flow_stmts);
	}

	compiler->loop_switch_state = compiler->loop_switch_state->parent;
}

typedef struct {
	InstrIndex initial_region;
	InstrIndex final_region;
} CompiledBlockRegions;

static InstrIndex _compile_expr(FunctionCompiler* compiler, Expr* expr);
static InstrIndex _compile_bin_expr(FunctionCompiler* compiler, Expr* expr);
static InstrIndex _compile_expr_to_bool(FunctionCompiler* compiler, Expr* expr);

// Compiles a single ast node, and places in in the `*region_instr_index`. If the ast node produces
// new regions, `region_instr_index` is modified to point to a new desird region, where the control
// should continue.
static void _compile_single_node(FunctionCompiler* compiler,
		AstNode* node,
		InstrIndex* region_instr_index);

static CompiledBlockRegions _compile_scope(FunctionCompiler* compiler, Scope* scope);
static void _reset_variables_in_scope(FunctionCompiler* compiler, const Scope* scope);

static void _compile_statement(FunctionCompiler* compiler, AstNode* node);

typedef struct {
	// An offset known at compile time
	uint32_t offset;

	// An instruction that computes the base address
	InstrIndex base;
} AddressExpr;

static InstrIndex _compile_address_expr(FunctionCompiler* compiler, AddressExpr addr_expr);
static AddressExpr _compile_address_of(FunctionCompiler* compiler, Expr* expr);

static InstrIndex _compile_int_cast(FunctionCompiler* compiler,
		const Type* int_type,
		const Type* target_type,
		InstrIndex value_instr) {
	if (int_type->kind == TYPE_ARRAY && target_type->kind == TYPE_POINTER) {
		return value_instr;
	}

	assert(type_kind_is_int(int_type->kind)
			|| int_type->kind == TYPE_ENUM
			|| int_type->kind == TYPE_POINTER);

	assert(type_kind_is_int(target_type->kind)
			|| target_type->kind == TYPE_ENUM
			|| target_type->kind == TYPE_POINTER);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	TypeLayout int_layout = type_get_layout(compiler->type_context, int_type);
	TypeLayout target_layout = type_get_layout(compiler->type_context, target_type);

	bool is_signed = !has_flag(int_type->kind, (TypeKind)TYPE_FLAG_UNSIGNED);
	return instr_new_cast(instr_buffer,
			instr_allocator,
			value_instr,
			int_layout.size,
			target_layout.size,
			is_signed);
}

static void _compile_compound_literal_init(FunctionCompiler* compiler,
		CompoundLiteral* literal,
		InstrIndex addr_instr_index) {
	profile_scope_start(__func__);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;
	const TypeContext* type_context = compiler->type_context;

	Type* type = literal->type;
	TypeLayout layout = type_get_layout(type_context, type);

	InstrIndex mem_zero_index = instr_buffer_append(instr_buffer, instr_allocator);
	Instr* mem_zero = instr_buffer_at(instr_buffer, mem_zero_index);
	mem_zero->kind = INSTR_MEM_ZERO_FIXED;
	mem_zero->mem_zero_fixed.dst = addr_instr_index;
	mem_zero->mem_zero_fixed.size = layout.size;
	mem_zero->mem_zero_fixed.io_state = compiler->io_state;

	compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, mem_zero_index);

	CompoundLiteralEntry* entries = literal->entries;
	size_t entry_count = literal->entry_count;
	for (size_t i = 0; i < entry_count; i += 1) {
		const CompoundLiteralEntry* entry = &entries[i];

		size_t offset = 0;
		Type slot_type;

		switch (entry->kind) {
		case COMPOUND_LITERAL_VALUE: {
			assert(type->kind == TYPE_STRUCT || type->kind == TYPE_UNION);
			const Struct* compound_type = type_extract_compound(type);
			const size_t* field_offsets = compound_type->field_offsets;

			assert(entry->not_designated.index < compound_type->field_count);
			offset = field_offsets[entry->not_designated.index];

			slot_type = compound_type->fields[entry->not_designated.index].type;
			break;
		}
		case COMPOUND_LITERAL_FIELD_INIT: {
			assert(type->kind == TYPE_STRUCT || type->kind == TYPE_UNION);
			const Struct* compound_type = type_extract_compound(type);
			const size_t* field_offsets = compound_type->field_offsets;

			StructFieldNamespaceEntry field_entry =
				compound_type->field_namespace->entries[entry->field.index];

			assert(field_entry.struct_def == compound_type);
			offset = field_offsets[field_entry.field_index];

			slot_type = compound_type->fields[field_entry.field_index].type;
			break;
		}
		case COMPOUND_LITERAL_ARRAY_ELEMENT_INIT:
			panic("todo");
		}

		TypeLayout slot_type_layout = type_get_layout(type_context, &slot_type);
		if (slot_type.kind == TYPE_STRUCT
				|| slot_type.kind == TYPE_UNION
				|| slot_type.kind == TYPE_ARRAY) {

			InstrIndex slot_address = _compile_address_expr(
					compiler,
					(AddressExpr) { .base = addr_instr_index, .offset = (uint32_t)offset });

			InstrIndex value_address = _compile_address_expr(compiler,
					_compile_address_of(compiler, entry->value));

			InstrIndex mem_copy_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* mem_copy = instr_buffer_at(instr_buffer, mem_copy_index);
			mem_copy->kind = INSTR_MEM_COPY_FIXED;
			mem_copy->mem_copy_fixed.src = value_address;
			mem_copy->mem_copy_fixed.dst = slot_address;
			mem_copy->mem_copy_fixed.size = slot_type_layout.size;
			mem_copy->mem_copy_fixed.io_state = compiler->io_state;

			compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, mem_copy_index);
		} else {
			InstrIndex value_instr = _compile_expr(compiler, entry->value);

			InstrIndex slot_address = _compile_address_expr(
					compiler,
					(AddressExpr) { .base = addr_instr_index, .offset = (uint32_t)offset });

			InstrIndex store_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* store_instr = instr_buffer_at(instr_buffer, store_index);
			store_instr->ptr_store.ptr = slot_address;
			store_instr->ptr_store.value = value_instr;
			store_instr->ptr_store.io_state = compiler->io_state;

			compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, store_index);

			switch (slot_type_layout.size) {
			case 1:
				store_instr->kind = INSTR_PTR_STORE_8;
				break;
			case 2:
				store_instr->kind = INSTR_PTR_STORE_16;
				break;
			case 4:
				store_instr->kind = INSTR_PTR_STORE_32;
				break;
			case 8:
				store_instr->kind = INSTR_PTR_STORE_64;
				break;
			default:
				panic("Unsupported element size");
			}
		}
	}

	profile_scope_end();
}

// Generates a sequence of instructions that compute the address of an array element
static InstrIndex _compile_address_of_array_element(FunctionCompiler* compiler, Expr* expr) {
	profile_scope_start(__func__);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	Type array_type;
	Type index_type;

	expr_get_type(expr->array_index.array, &array_type);
	expr_get_type(expr->array_index.index, &index_type);

	InstrIndex array = INVALID_INSTR_INDEX;

	if (array_type.kind == TYPE_POINTER) {
		array = _compile_expr(compiler, expr->array_index.array);
	} else {
		array = _compile_address_expr(compiler,
				_compile_address_of(compiler, expr->array_index.array));
	}

	InstrIndex index = _compile_expr(compiler, expr->array_index.index);

	const TypeContext* type_context = compiler->type_context;
	if (type_get_layout(type_context, &index_type).size != type_context->pointer_type_layout.size) {
		index = instr_new_unsigned_cast(instr_buffer,
				instr_allocator,
				index,
				type_get_layout(type_context, &index_type).size,
				type_context->pointer_type_layout.size);
	}

	Type* element_type = type_extract_pointer_base_type(&array_type);
	TypeLayout element_layout = type_get_layout(type_context, element_type);

	assert(element_layout.size > 0);
	assert(is_power_of_2(element_layout.size));

	size_t shift_count = count_trailing_zeros(element_layout.size);
	InstrIndex scaled_index = instr_new_logical_shift_left_by(instr_buffer,
			instr_allocator,
			index,
			8,
			(uint8_t)shift_count);

	InstrIndex add_instr_index = instr_buffer_append(instr_buffer, instr_allocator);
	Instr* add_instr = instr_buffer_at(instr_buffer, add_instr_index);
	add_instr->bin_op.kind = INSTR_BIN_ADD;
	add_instr->bin_op.left = array;
	add_instr->bin_op.right = scaled_index;
	add_instr->kind = INSTR_BIN_OP_64;

	profile_scope_end();
	return add_instr_index;
}

static const Struct* _resolve_compound_type(Expr* expr) {
	assert(expr->kind == EXPR_DIRECT_FIELD_ACCESS || expr->kind == EXPR_INDIRECT_FIELD_ACCESS);

	Type target_type;
	expr_get_type(expr->field_access.target, &target_type);

	const Type* compound_type = NULL;
	if (expr->kind == EXPR_DIRECT_FIELD_ACCESS) {
		compound_type = &target_type;
	} else if (expr->kind == EXPR_INDIRECT_FIELD_ACCESS) {
		assert(target_type.kind == TYPE_POINTER);
		compound_type = target_type.pointer_base_type;
	}

	if (compound_type->kind == TYPE_STRUCT) {
		return compound_type->struct_def;
	} else if (compound_type->kind == TYPE_UNION) {
		return compound_type->union_def;
	}

	panic("Not a compound type");
	return NULL;
}

static AddressExpr _compile_address_of(FunctionCompiler* compiler, Expr* expr) {
	profile_scope_start(__func__);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;
	const TypeContext* type_context = compiler->type_context;

	switch (expr->kind) {
	case EXPR_DIRECT_FIELD_ACCESS: {
		const Struct* compound_type = _resolve_compound_type(expr);
		const size_t* field_offsets = compound_type->field_offsets;

		StructFieldNamespaceEntry entry =
			compound_type->field_namespace->entries[expr->field_access.field_index];

		assert(entry.struct_def == compound_type);

		size_t field_offset = field_offsets[entry.field_index];

		AddressExpr target_addr = _compile_address_of(compiler, expr->field_access.target);

		profile_scope_end();
		return (AddressExpr) {
			.base = target_addr.base,
			.offset = target_addr.offset + (uint32_t)field_offset,
		};
	}
	case EXPR_INDIRECT_FIELD_ACCESS: {
		AddressExpr addr_expr = {};

		const Struct* compound_type = _resolve_compound_type(expr);
		const size_t* field_offsets = compound_type->field_offsets;

		StructFieldNamespaceEntry entry =
			compound_type->field_namespace->entries[expr->field_access.field_index];

		assert(entry.struct_def == compound_type);

		size_t field_offset = field_offsets[entry.field_index];

		Type expr_type;
		expr_get_type(expr->field_access.target, &expr_type);

		if (expr_type.kind == TYPE_POINTER) {
			addr_expr.base = _compile_expr(compiler, expr->field_access.target);
			addr_expr.offset = field_offset;
			profile_scope_end();
			return addr_expr;
		} else {
			unreachable();
		}
		break;
	}
	case EXPR_VARIABLE_REFERENCE: {
		const Variable* var = compiler->vars[expr->variable_ref.var->id];

		InstrIndex value_instr = compiler->var_values[expr->variable_ref.var->id];
		assert(value_instr.value != INVALID_INSTR_INDEX.value);

		if (var->type.kind == TYPE_POINTER) {
			profile_scope_end();
			return (AddressExpr) {
				.base = value_instr,
				.offset = 0,
			};
		}

		assert(var->type.kind == TYPE_STRUCT
				|| var->type.kind == TYPE_UNION
				|| var->type.kind == TYPE_ARRAY);

		InstrIndex stack_addr_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* stack_addr = instr_buffer_at(instr_buffer, stack_addr_index);
		stack_addr->kind = INSTR_STACK_ADDR;
		stack_addr->stack_addr.stack_alloc = value_instr;
		
		profile_scope_end();
		return (AddressExpr) {
			.base = stack_addr_index,
			.offset = 0,
		};
	}
	case EXPR_FUNCTION_PARAM: {
		size_t arg_index = expr->function_param.param_index;

		Type type = compiler->function->proto.parameters[arg_index].type;
		if (type.kind == TYPE_POINTER || type.kind == TYPE_ARRAY) {
			profile_scope_end();
			return (AddressExpr) {
				.base = compiler->arg_states[arg_index],
				.offset = 0,
			};
		}

		if (type.kind == TYPE_STRUCT || type.kind == TYPE_UNION) {
			AddressExpr address_expr;
			if (type_get_layout(compiler->type_context, &type).size > 8) {
				address_expr = (AddressExpr) {
					.base = compiler->arg_states[arg_index],
					.offset = 0,
				};
			} else {
				InstrIndex stack_addr_index = instr_buffer_append(instr_buffer, instr_allocator);
				Instr* stack_addr = instr_buffer_at(instr_buffer, stack_addr_index);
				stack_addr->kind = INSTR_STACK_ADDR;
				stack_addr->stack_addr.stack_alloc = compiler->arg_states[arg_index];

				address_expr = (AddressExpr) {
					.base = stack_addr_index,
					.offset = 0,
				};
			}
			
			profile_scope_end();
			return address_expr;
		}

		unreachable();
	}
	case EXPR_ARRAY_INDEX: {
		InstrIndex element_addr = _compile_address_of_array_element(compiler, expr);

		profile_scope_end();
		return (AddressExpr) {
			.base = element_addr,
			.offset = 0,
		};
	}
	case EXPR_COMPOUND_LITERAL: {
		Type* type = expr->compound_literal.type;
		TypeLayout layout = type_get_layout(compiler->type_context, type);

		InstrIndex alloc_instr_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* alloc_instr = instr_buffer_at(instr_buffer, alloc_instr_index);
		alloc_instr->kind = INSTR_STACK_ALLOC;
		alloc_instr->stack_alloc.size = layout.size;
		alloc_instr->stack_alloc.alignment = layout.alignment;

		InstrIndex stack_addr_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* stack_addr = instr_buffer_at(instr_buffer, stack_addr_index);
		stack_addr->kind = INSTR_STACK_ADDR;
		stack_addr->stack_addr.stack_alloc = alloc_instr_index;

		_compile_compound_literal_init(compiler, &expr->compound_literal, stack_addr_index);

		profile_scope_end();
		return (AddressExpr) { .base = stack_addr_index, .offset = 0 };
	}
	case EXPR_CALL: {
		Type result_type;
		expr_get_type(expr, &result_type);

		InstrIndex call_instr = _compile_expr(compiler, expr);

		AddressExpr address_expr = {};

		size_t return_type_size = type_get_layout(compiler->type_context, &result_type).size;

		TypeLayout pointer_type_layout = compiler->type_context->pointer_type_layout;
		if (return_type_size <= pointer_type_layout.size) {
			// We have a struct value that can fit in a register, thus it won't be return through
			// the stack.
			//
			// To keep things simple, immediately drop the returned value onto the stack.
			InstrIndex stack_alloc_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* stack_alloc = instr_buffer_at(instr_buffer, stack_alloc_index);
			stack_alloc->kind = INSTR_STACK_ALLOC;
			stack_alloc->stack_alloc.size = pointer_type_layout.size;
			stack_alloc->stack_alloc.alignment = pointer_type_layout.alignment;

			InstrIndex stack_addr_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* stack_addr = instr_buffer_at(instr_buffer, stack_addr_index);
			stack_addr->kind = INSTR_STACK_ADDR;
			stack_addr->stack_addr.stack_alloc = stack_alloc_index;

			InstrIndex store_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* store = instr_buffer_at(instr_buffer, store_index);
			store->kind = INSTR_PTR_STORE_64;
			store->ptr_store.ptr = stack_addr_index;
			store->ptr_store.value = call_instr;
			store->ptr_store.io_state = compiler->io_state;

			compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, store_index);

			address_expr = (AddressExpr) {
				.base = stack_addr_index,
				.offset = 0,
			};
		} else {
			InstrIndex stack_addr_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* stack_addr = instr_buffer_at(instr_buffer, stack_addr_index);
			stack_addr->kind = INSTR_STACK_ADDR;
			stack_addr->stack_addr.stack_alloc = call_instr;

			address_expr = (AddressExpr) {
				.base = stack_addr_index,
				.offset = 0,
			};
		}
		
		profile_scope_end();
		return address_expr;
	}
	case EXPR_UNARY:
		switch (expr->unary.op) {
		case UNARY_OP_DEREFERENCE:
			AddressExpr address = _compile_address_of(compiler, expr->unary.operand);
			profile_scope_end();
			return address;
		default:
			unreachable();
		}
		break;
	default: {
		Type expr_type;
		expr_get_type(expr, &expr_type);

		assert(expr_type.kind == TYPE_POINTER);

		AddressExpr addr_expr = {
			.base = _compile_expr(compiler, expr),
			.offset = 0,
		};

		profile_scope_end();
		return addr_expr;
	}
	}

	unreachable();
	return (AddressExpr) {};
}

static InstrIndex _compile_address_expr(FunctionCompiler* compiler, AddressExpr addr_expr) {
	if (addr_expr.offset == 0) {
		return addr_expr.base;
	}

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	InstrIndex offset_const = instr_new_int_const(instr_buffer,
			instr_allocator,
			addr_expr.offset,
			compiler->type_context->pointer_type_layout.size);

	InstrIndex add_instr_index = instr_buffer_append(instr_buffer, instr_allocator);
	Instr* add_instr = instr_buffer_at(instr_buffer, add_instr_index);
	add_instr->kind = INSTR_BIN_OP_64;
	add_instr->bin_op.kind = INSTR_BIN_ADD;
	add_instr->bin_op.left = addr_expr.base;
	add_instr->bin_op.right = offset_const;
	return add_instr_index;
}

static void _compile_assignment_of_compound_types(FunctionCompiler* compiler,
		Expr* target,
		AddressExpr value_address) {

	profile_scope_start(__func__);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	Type target_type;
	expr_get_type(target, &target_type);

	assert(target_type.kind == TYPE_STRUCT || target_type.kind == TYPE_UNION);

	InstrIndex src_instr = _compile_address_expr(compiler, value_address);

	InstrIndex dst_address_instr = _compile_address_expr(compiler,
			_compile_address_of(compiler, target));

	InstrIndex mem_copy_index = instr_buffer_append(instr_buffer, instr_allocator);
	Instr* mem_copy = instr_buffer_at(instr_buffer, mem_copy_index);
	mem_copy->kind = INSTR_MEM_COPY_FIXED;
	mem_copy->mem_copy_fixed.src = src_instr;
	mem_copy->mem_copy_fixed.dst = dst_address_instr;
	mem_copy->mem_copy_fixed.size =
		type_get_layout(compiler->type_context, &target_type).size;
	mem_copy->mem_copy_fixed.io_state = compiler->io_state;
	
	compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, mem_copy_index);

	profile_scope_end();
}

static void _compile_assignment(FunctionCompiler* compiler,
		Expr* target,
		InstrIndex value_instr) {
	profile_scope_start(__func__);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	Type target_type;
	expr_get_type(target, &target_type);

	if (target->kind == EXPR_VARIABLE_REFERENCE) {
		const Variable* variable = target->variable_ref.var;

		if (target_type.kind == TYPE_STRUCT || target_type.kind == TYPE_UNION) {
			InstrIndex dst_address = _compile_address_expr(compiler,
					_compile_address_of(compiler, target));

			InstrIndex mem_copy_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* mem_copy = instr_buffer_at(instr_buffer, mem_copy_index);
			mem_copy->kind = INSTR_MEM_COPY_FIXED;
			mem_copy->mem_copy_fixed.src = value_instr;
			mem_copy->mem_copy_fixed.dst = dst_address;
			mem_copy->mem_copy_fixed.size =
				type_get_layout(compiler->type_context, &target_type).size;
			mem_copy->mem_copy_fixed.io_state = compiler->io_state;
			
			compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, mem_copy_index);
		} else {
			compiler->var_values[variable->id] = value_instr;
		}
	} else if (target->kind == EXPR_FUNCTION_PARAM) {
		size_t arg_index = target->function_param.param_index;
		compiler->arg_states[arg_index] = value_instr;
	} else if (target->kind == EXPR_UNARY) {
		InstrIndex operand_instr = _compile_expr(compiler, target->unary.operand);
		Type operand_type;
		expr_get_type(target->unary.operand, &operand_type);

		switch (target->unary.op) {
		case UNARY_OP_DEREFERENCE: {
			Type* element_type = type_extract_pointer_base_type(&operand_type);
			TypeLayout element_layout = type_get_layout(compiler->type_context, element_type);

			InstrIndex instr_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* instr = instr_buffer_at(instr_buffer, instr_index);

			instr->ptr_store.ptr = operand_instr;
			instr->ptr_store.value = value_instr;
			instr->ptr_store.io_state = compiler->io_state;

			compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, instr_index);

			switch (element_layout.size) {
			case 1:
				instr->kind = INSTR_PTR_STORE_8;
				break;
			case 2:
				instr->kind = INSTR_PTR_STORE_16;
				break;
			case 4:
				instr->kind = INSTR_PTR_STORE_32;
				break;
			case 8:
				instr->kind = INSTR_PTR_STORE_64;
				break;
			default:
				panic("Only up to 8 byte sizes are supported for dereferencing");
			}

			break;
		}
		default:
			unreachable();
		}
	} else if (target->kind == EXPR_ARRAY_INDEX) {
		Type array_type;
		expr_get_type(target->array_index.array, &array_type);

		Type* element_type = type_extract_pointer_base_type(&array_type);
		TypeLayout element_layout = type_get_layout(compiler->type_context, element_type);

		InstrIndex element_addr = _compile_address_of_array_element(compiler, target);

		InstrIndex store_instr_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* store_instr = instr_buffer_at(instr_buffer, store_instr_index);
		store_instr->ptr_store.ptr = element_addr;
		store_instr->ptr_store.value = value_instr;
		store_instr->ptr_store.io_state = compiler->io_state;

		compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, store_instr_index);

		switch (element_layout.size) {
		case 1:
			store_instr->kind = INSTR_PTR_STORE_8;
			break;
		case 2:
			store_instr->kind = INSTR_PTR_STORE_16;
			break;
		case 4:
			store_instr->kind = INSTR_PTR_STORE_32;
			break;
		case 8:
			store_instr->kind = INSTR_PTR_STORE_64;
			break;
		default:
			panic("Unsupported element size");
		}
	} else if (target->kind == EXPR_INDIRECT_FIELD_ACCESS
			|| target->kind == EXPR_DIRECT_FIELD_ACCESS) {
		Type field_type;
		expr_get_type(target, &field_type);

		TypeLayout field_type_layout = type_get_layout(compiler->type_context, &field_type);

		InstrIndex field_address = _compile_address_expr(
				compiler,
				_compile_address_of(compiler, target));

		InstrIndex store_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* store_instr = instr_buffer_at(instr_buffer, store_index);
		store_instr->ptr_store.ptr = field_address;
		store_instr->ptr_store.value = value_instr;
		store_instr->ptr_store.io_state = compiler->io_state;

		compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, store_index);

		switch (field_type_layout.size) {
		case 1:
			store_instr->kind = INSTR_PTR_STORE_8;
			break;
		case 2:
			store_instr->kind = INSTR_PTR_STORE_16;
			break;
		case 4:
			store_instr->kind = INSTR_PTR_STORE_32;
			break;
		case 8:
			store_instr->kind = INSTR_PTR_STORE_64;
			break;
		default:
			panic("Unsupported element size");
		}
	} else {
		panic("Assignment to this expression kind is not supported");
	}

	profile_scope_end();
}

// Compiles a binary expression without casting compare operations to an interger
static InstrIndex _compile_bin_expr(FunctionCompiler* compiler, Expr* expr) {
	profile_scope_start(__func__);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	if (expr->binary.op == BIN_OP_ASSIGNMENT) {
		Expr* target = expr->binary.left;
		Expr* value = expr->binary.right;

		Type value_type;
		expr_get_type(value, &value_type);
		Type target_type;
		expr_get_type(target, &target_type);

		if (target_type.kind == TYPE_STRUCT || target_type.kind == TYPE_UNION) {
			assert(type_equal(&value_type, &target_type));

			AddressExpr value_address = _compile_address_of(compiler, value);

			_compile_assignment_of_compound_types(compiler, target, value_address);
			profile_scope_end();
			return INVALID_INSTR_INDEX;
		}

		InstrIndex value_instr = _compile_expr(compiler, value);
		if (type_equal(&value_type, &target_type)) {
			// Nothing to do
		} else if (value_type.kind == TYPE_POINTER && target_type.kind == TYPE_POINTER) {
			// Nothing to do
		} else if (type_kind_is_int(value_type.kind) && type_kind_is_int(target_type.kind)) {
			value_instr = _compile_int_cast(compiler, &value_type, &target_type, value_instr);
		} else {
			panic("todo");
		}

		_compile_assignment(compiler, target, value_instr);
		profile_scope_end();
		return value_instr;
	}

	Type left_type;
	Type right_type;

	expr_get_type(expr->binary.left, &left_type);
	expr_get_type(expr->binary.right, &right_type);

	bool left_is_pointer_like = type_kind_is_pointer_like(left_type.kind);
	bool right_is_pointer_like = type_kind_is_pointer_like(right_type.kind);

	InstrIndex left = _compile_expr(compiler, expr->binary.left);
	InstrIndex right = _compile_expr(compiler, expr->binary.right);

	Type common_type = {
		.kind = expr->binary.common_type_kind,
		.pointer_base_type = expr->binary.pointer_base_type
	};

	const TypeContext* type_context = compiler->type_context;

	// TODO: Don't scale int constants during compare operations.

	// NOTE: In case we are doing pointer arithmetics here,
	//       and one of the operands is an interger, we need
	//       to scale that integer by the byte size of base pointer type.
	//
	//       Since during pointer arithmetics those integer constants
	//       encode an offset by a number of array elements and not bytes.
	if (left_is_pointer_like && type_kind_is_int(right_type.kind)) {
		Type* base_type = type_extract_pointer_base_type(&left_type);
		TypeLayout value_layout = type_get_layout(type_context, base_type);

		size_t value_size = type_get_layout(type_context, &right_type).size;
		if (value_layout.size != type_context->pointer_type_layout.size) {

			// promote the right operand to match the pointer size
			right = instr_new_unsigned_cast(instr_buffer,
					instr_allocator,
					right,
					value_size,
					type_context->pointer_type_layout.size);

			value_size = type_context->pointer_type_layout.size;
		}

		size_t shift_count = count_trailing_zeros(value_layout.size);
		right = instr_new_logical_shift_left_by(instr_buffer,
				instr_allocator,
				right,
				value_size,
				(uint8_t)shift_count);
	} else if (right_is_pointer_like && type_kind_is_int(left_type.kind)) {
		Type* base_type = type_extract_pointer_base_type(&right_type);
		TypeLayout value_layout = type_get_layout(type_context, base_type);

		size_t value_size = type_get_layout(type_context, &left_type).size;
		if (value_layout.size != type_context->pointer_type_layout.size) {

			// promote the left operand to match the pointer size
			left = instr_new_unsigned_cast(instr_buffer,
					instr_allocator,
					left,
					value_size,
					type_context->pointer_type_layout.size);

			value_size = type_context->pointer_type_layout.size;
		}

		size_t shift_count = count_trailing_zeros(value_layout.size);
		left = instr_new_logical_shift_left_by(instr_buffer,
				instr_allocator,
				left,
				value_size,
				(uint8_t)shift_count);
	} else {
		left = _compile_int_cast(compiler, &left_type, &common_type, left);
		right = _compile_int_cast(compiler, &right_type, &common_type, right);
	}

	InstrIndex instr_index = instr_buffer_append(instr_buffer, instr_allocator);
	Instr* instr = instr_buffer_at(instr_buffer, instr_index);

	// 0 -> 8-bits
	// 1 -> 16-bits
	// 2 -> 32-bits
	// 3 -> 64-bits
	bool is_unsigned = has_flag(common_type.kind, (TypeKind)TYPE_FLAG_UNSIGNED);
	TypeLayout common_type_layout = type_get_layout(compiler->type_context, &common_type);
	size_t result_bit_size_index = count_trailing_zeros(common_type_layout.size);

	switch (expr->binary.op) {
	case BIN_OP_ADD:
	case BIN_OP_ASSIGNMENT_BY_SUM:
		instr->bin_op.kind = INSTR_BIN_ADD;
		break;
	case BIN_OP_SUB:
	case BIN_OP_ASSIGNMENT_BY_DIFFERENCE:
		instr->bin_op.kind = INSTR_BIN_SUB;
		break;
	case BIN_OP_MUL:
	case BIN_OP_ASSIGNMENT_BY_PRODUCT:
		instr->bin_op.kind = is_unsigned ? INSTR_BIN_UMUL : INSTR_BIN_IMUL;
		break;
	case BIN_OP_DIV:
	case BIN_OP_ASSIGNMENT_BY_QUOTIENT:
		instr->bin_op.kind = is_unsigned ? INSTR_BIN_UDIV : INSTR_BIN_IDIV;
		break;
	case BIN_OP_MOD:
	case BIN_OP_ASSIGNMENT_BY_REMAINDER:
		instr->bin_op.kind = is_unsigned ? INSTR_BIN_UMOD : INSTR_BIN_IMOD;
		break;

	case BIN_OP_LOGICAL_AND:
	case BIN_OP_LOGICAL_OR: {
		InstrIndex op;

		if (expr->binary.op == BIN_OP_LOGICAL_AND) {
			op = instr_buffer_push(instr_buffer, instr_allocator, (Instr) {
				.kind = INSTR_BIN_OP_8 + result_bit_size_index,
				.bin_op = { INSTR_BIN_AND, left, right }
			});
		} else if (expr->binary.op == BIN_OP_LOGICAL_OR) {
			op = instr_buffer_push(instr_buffer, instr_allocator, (Instr) {
				.kind = INSTR_BIN_OP_8 + result_bit_size_index,
				.bin_op = { INSTR_BIN_OR, left, right }
			});
		} else {
			unreachable();
		}

		InstrIndex one_const = instr_new_int_const(instr_buffer,
				instr_allocator,
				1,
				1 << result_bit_size_index);

		*instr = (Instr) {
			.kind = INSTR_COMPARE_8 + result_bit_size_index,
			.compare = {
				.kind = INSTR_CMP_EQUAL,
				.left = op,
				.right = one_const,
			}
		};

		break;
	}
	case BIN_OP_LOGICAL_EQUAL:
		instr->compare.kind = INSTR_CMP_EQUAL;
		break;
	case BIN_OP_LOGICAL_NOT_EQUAL:
		instr->compare.kind = INSTR_CMP_NOT_EQUAL;
		break;
	case BIN_OP_LOGICAL_LESS:
		instr->compare.kind = INSTR_CMP_LESS;
		break;
	case BIN_OP_LOGICAL_LESS_OR_EQUAL:
		instr->compare.kind = INSTR_CMP_LESS_OR_EQUAL;
		break;
	case BIN_OP_LOGICAL_GREATER:
		instr->compare.kind = INSTR_CMP_GREATER;
		break;
	case BIN_OP_LOGICAL_GREATER_OR_EQUAL:
		instr->compare.kind = INSTR_CMP_GREATER_OR_EQUAL;
		break;

	case BIN_OP_BITWISE_AND:
	case BIN_OP_ASSIGNMENT_BY_BITWISE_AND:
		instr->bin_op.kind = INSTR_BIN_AND;
		break;
	case BIN_OP_BITWISE_OR:
	case BIN_OP_ASSIGNMENT_BY_BITWISE_OR:
		instr->bin_op.kind = INSTR_BIN_OR;
		break;
	case BIN_OP_BITWISE_XOR:
	case BIN_OP_ASSIGNMENT_BY_BITWISE_XOR:
		instr->bin_op.kind = INSTR_BIN_XOR;
		break;
	case BIN_OP_BITWISE_SHIFT_LEFT:
	case BIN_OP_ASSIGNMENT_BY_BITWISE_SHIFT_LEFT:
		instr->bin_op.kind = INSTR_BIN_SHIFT_LEFT;
		break;
	case BIN_OP_BITWISE_SHIFT_RIGHT:
	case BIN_OP_ASSIGNMENT_BY_BITWISE_SHIFT_RIGHT:
		instr->bin_op.kind = INSTR_BIN_SHIFT_RIGHT;
		break;

	case BIN_OP_ASSIGNMENT:
		panic("Assignment is handled in a different path");
	}

	assert_msg(instr->kind != INSTR_NO_OP,
			"Binary operation was not handled, "
			"and thus haven't produced a valid instruction");

	if (bin_op_is_compare(expr->binary.op)) {
		instr->kind = INSTR_COMPARE_8 + result_bit_size_index;
		instr->compare.left = left;
		instr->compare.right = right;
	} else if (expr->binary.op == BIN_OP_LOGICAL_AND || expr->binary.op == BIN_OP_LOGICAL_OR) {
		// Already handled
	} else {
		instr->kind = INSTR_BIN_OP_8 + result_bit_size_index;
		instr->bin_op.left = left;
		instr->bin_op.right = right;
	}

	if (bin_op_is_assignment(expr->binary.op)) {
		_compile_assignment(compiler, expr->binary.left, instr_index);
	}

	profile_scope_end();
	return instr_index;
}

static InstrIndex _compile_unary_expr(FunctionCompiler* compiler, Expr* expr) {
	profile_scope_start(__func__);

	assert(expr->kind == EXPR_UNARY);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	Type operand_type;
	expr_get_type(expr->unary.operand, &operand_type);

	TypeLayout operand_type_layout = type_get_layout(compiler->type_context, &operand_type);
	size_t result_bit_size_index = count_trailing_zeros(operand_type_layout.size);

	UnaryOpKind op = expr->unary.op;
	switch (expr->unary.op) {
	case UNARY_OP_DEREFERENCE: {
		InstrIndex operand_instr = _compile_expr(compiler, expr->unary.operand);
		const Type* base_type = NULL;
		if (operand_type.kind == TYPE_POINTER) {
			base_type = operand_type.pointer_base_type;
		} else if (operand_type.kind == TYPE_ARRAY) {
			base_type = operand_type.array.element_type;
		} else {
			panic("todo: report error");
		}

		InstrIndex instr_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* instr = instr_buffer_at(instr_buffer, instr_index);

		instr->ptr_load.ptr = operand_instr;
		instr->ptr_load.io_state = compiler->io_state;

		compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, instr_index);

		TypeLayout layout = type_get_layout(compiler->type_context, base_type);
		switch (layout.size) {
		case 1:
			instr->kind = INSTR_PTR_LOAD_8;
			break;
		case 2:
			instr->kind = INSTR_PTR_LOAD_16;
			break;
		case 4:
			instr->kind = INSTR_PTR_LOAD_32;
			break;
		case 8:
			instr->kind = INSTR_PTR_LOAD_64;
			break;
		default:
			panic("Only up to 8 byte sizes are supported for dereferencing");
		}

		profile_scope_end();
		return instr_index;
	}
	case UNARY_OP_PRE_INCREMENT:
	case UNARY_OP_POST_INCREMENT:
	case UNARY_OP_PRE_DECREMENT:
	case UNARY_OP_POST_DECREMENT: {
		InstrIndex operand_instr = _compile_expr(compiler, expr->unary.operand);

		bool is_increment = op == UNARY_OP_PRE_INCREMENT  || op == UNARY_OP_POST_INCREMENT;
		bool is_pre_op = op == UNARY_OP_PRE_INCREMENT || op == UNARY_OP_PRE_DECREMENT;

		assert(type_kind_is_int(operand_type.kind) || operand_type.kind == TYPE_POINTER);
		assert(operand_type_layout.size <= 8);

		InstrIndex step_const = INVALID_INSTR_INDEX;

		if (operand_type.kind == TYPE_POINTER) {
			TypeLayout element_type_layout = type_get_layout(
					compiler->type_context,
					operand_type.pointer_base_type);

			step_const = instr_new_int_const(instr_buffer,
					instr_allocator,
					element_type_layout.size,
					operand_type_layout.size);
		} else {
			step_const = instr_new_int_const(instr_buffer,
					instr_allocator,
					1, operand_type_layout.size);
		}


		InstrIndex bin_op_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* bin_op = instr_buffer_at(instr_buffer, bin_op_index);
		bin_op->kind = INSTR_BIN_OP_8 + result_bit_size_index;
		bin_op->bin_op.left = operand_instr;
		bin_op->bin_op.right = step_const;

		if (is_increment) {
			bin_op->bin_op.kind = INSTR_BIN_ADD;
		} else {
			bin_op->bin_op.kind = INSTR_BIN_SUB;
		}

		_compile_assignment(compiler, expr->unary.operand, bin_op_index);

		if (is_pre_op) {
			profile_scope_end();
			return bin_op_index;
		} else {
			profile_scope_end();
			return operand_instr;
		}
	}
	case UNARY_OP_NEGATE: {
		InstrIndex operand_instr = _compile_expr(compiler, expr->unary.operand);

		TypeLayout layout = type_get_layout(compiler->type_context, &operand_type);
		assert_msg(layout.size <= 8, "Only up to 8 byte sizes are supported for dereferencing");

		InstrIndex instr_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* instr = instr_buffer_at(instr_buffer, instr_index);
		instr->kind = INSTR_NEGATE_8 + result_bit_size_index;
		instr->negate.operand = operand_instr;

		profile_scope_end();
		return instr_index;
	}
	case UNARY_OP_PLUS: {
		InstrIndex operand_instr = _compile_expr(compiler, expr->unary.operand);
		// Nothing to do here
		profile_scope_end();
		return operand_instr;
	}
	case UNARY_OP_BITWISE_NOT: {
		InstrIndex operand_instr = _compile_expr(compiler, expr->unary.operand);

		InstrIndex instr_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* instr = instr_buffer_at(instr_buffer, instr_index);
		instr->kind = INSTR_BITWISE_NOT_8 + result_bit_size_index;
		instr->bitwise_not.operand = operand_instr;

		profile_scope_end();
		return instr_index;
	}
	case UNARY_OP_LOGICAL_NOT: {
		InstrIndex operand_instr = _compile_expr_to_bool(compiler, expr->unary.operand);

		InstrIndex instr_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* instr = instr_buffer_at(instr_buffer, instr_index);
		instr->kind = INSTR_NOT;
		instr->not.operand = operand_instr;

		profile_scope_end();
		return instr_index;
	}
	case UNARY_OP_ADDRESS: {
		Expr* operand = expr->unary.operand;

		InstrIndex address_instr;

		switch (operand->kind) {
		case EXPR_DIRECT_FIELD_ACCESS:
		case EXPR_INDIRECT_FIELD_ACCESS:
		case EXPR_ARRAY_INDEX:
		case EXPR_VARIABLE_REFERENCE: {
			address_instr = _compile_address_expr(compiler, _compile_address_of(compiler, operand));
			break;
		}
		default:
			panic("Taking an address is not supported for this expression kind");
		}

		profile_scope_end();
		return address_instr;
	}
	}

	unreachable();
	return INVALID_INSTR_INDEX;
}

// Compiles an expression.
//
// Doesn't implicitely cast bool expression to `TYPE_INT`.
static InstrIndex _compile_expr_without_implicit_casts(FunctionCompiler* compiler, Expr* expr) {
	profile_scope_start(__func__);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;
	const TypeContext* type_context = compiler->type_context;

	switch (expr->kind) {
	case EXPR_CALL: {
		assert(expr->call.args.count <= UINT16_MAX);

		Expr* callable = expr->call.callable;

		Type callable_type;
		expr_get_type(callable, &callable_type);

		assert(callable_type.kind == TYPE_POINTER);
		assert(callable_type.pointer_base_type->kind == TYPE_FUNCTION);

		InstrInputs arg_inputs = instr_allocate_inputs_array(instr_buffer, expr->call.args.count);
		for (uint16_t i = 0; i < arg_inputs.count; i += 1) {
			Expr* arg = expr->call.args.exprs[i];

			Type arg_type;
			expr_get_type(arg, &arg_type);

			InstrIndex arg_instr = _compile_expr(compiler, arg);

			TypeLayout arg_type_layout = type_get_layout(compiler->type_context, &arg_type);
			bool is_compound_type = arg_type.kind == TYPE_STRUCT || arg_type.kind == TYPE_UNION;
			if (is_compound_type && arg_type_layout.size <= 8) {
				InstrIndex stack_addr_index = instr_buffer_append(instr_buffer, instr_allocator);
				Instr* stack_addr = instr_buffer_at(instr_buffer, stack_addr_index);
				stack_addr->kind = INSTR_STACK_ADDR;
				stack_addr->stack_addr.stack_alloc = arg_instr;

				InstrIndex load_index = instr_buffer_append(instr_buffer, instr_allocator);
				Instr* load = instr_buffer_at(instr_buffer, load_index);
				load->kind = INSTR_PTR_LOAD_64;
				load->ptr_load.ptr = stack_addr_index;
				load->ptr_load.io_state = compiler->io_state;

				compiler->io_state = instr_new_io_state(instr_buffer,
						instr_allocator,
						load_index);

				arg_instr = load_index;
			} else if (type_kind_is_int(arg_type.kind)) {
				const FunctionPrototype* proto = callable_type.pointer_base_type->function;
				if (i >= proto->parameter_count) {
					assert(proto->has_va_args);
					if (has_flag(arg_type.kind, (TypeKind)TYPE_FLAG_UNSIGNED)) {
						arg_instr = instr_new_unsigned_cast(instr_buffer,
								instr_allocator,
								arg_instr,
								arg_type_layout.size,
								type_context->pointer_type_layout.size);
					} else {
						arg_instr = instr_new_signed_cast(instr_buffer,
								instr_allocator,
								arg_instr,
								arg_type_layout.size,
								type_context->pointer_type_layout.size);
					}
				} else {
					arg_instr = _compile_int_cast(compiler,
							&arg_type,
							&proto->parameters[i].type,
							arg_instr);
				}
			}

			instr_buffer->inputs_buffer[arg_inputs.start + i] = arg_instr;
		}

		// Save the call, to later create the corresponding `AbiSignature`
		assert(compiler->function_call_count <= UINT16_MAX);
		assert(compiler->function_call_count < compiler->function->function_call_count);
		uint16_t callee_signature_index = (uint16_t)compiler->function_call_count;
		compiler->function_calls[compiler->function_call_count] = &expr->call;
		compiler->function_call_count += 1;

		bool is_indirect_call = false;
		if (callable->kind != EXPR_FUNCTION_REFERENCE) {
			is_indirect_call = true;
		} else {
			Function* func = callable->function_ref.func;
			if (func->decl_spec && func->decl_spec->kind == DECL_SPEC_DLL_IMPORT) {
				is_indirect_call = true;
			}
		}

		InstrIndex call_instr_index = INVALID_INSTR_INDEX;

		if (is_indirect_call) {
			InstrIndex load_func_addr_index = _compile_expr(compiler, callable);

			call_instr_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* call_instr = instr_buffer_at(instr_buffer, call_instr_index);

			call_instr->kind = INSTR_CALL_INDIRECT;
			call_instr->call_indirect.args = arg_inputs;
			call_instr->call_indirect.io_state = compiler->io_state;
			call_instr->call_indirect.function_addr = load_func_addr_index;
			call_instr->call_indirect.signature_index = callee_signature_index;
		} else {
			// First resolve the function symbol id
			assert(callable->kind == EXPR_FUNCTION_REFERENCE);

			Symbol symbol = {};
			compiler_create_function_import_symbol(callable->function_ref.func, &symbol);

			SymbolKey key = symbol_key_from_symbol(&symbol);
			SymbolId function_symbol_id = symbol_map_find(compiler->symbol_map, key);
			assert(function_symbol_id != SYMBOL_ID_INVALID);

			// Now compile the call
			call_instr_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* call_instr = instr_buffer_at(instr_buffer, call_instr_index);

			call_instr->kind = INSTR_CALL_DIRECT;
			call_instr->call_direct.args = arg_inputs;
			call_instr->call_direct.io_state = compiler->io_state;
			call_instr->call_direct.function_index = function_symbol_id;
			call_instr->call_direct.signature_index = callee_signature_index;
		}

		compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, call_instr_index);
		profile_scope_end();
		return call_instr_index;
	}
	case EXPR_BINARY: {
		InstrIndex instr_index = _compile_bin_expr(compiler, expr);
		profile_scope_end();
		return instr_index;
	}
	case EXPR_FUNCTION_REFERENCE: {
		const Function* func = expr->function_ref.func;

		Symbol symbol = {};
		compiler_create_function_import_symbol(func, &symbol);

		SymbolKey key = symbol_key_from_symbol(&symbol);
		SymbolId func_symbol_id = symbol_map_find(compiler->symbol_map, key);

		// There is a pass that runs before the compiler and collects all the imported symbols
		// into the `symbol_map`. So the symbol is guaranted to appear in the symbol map.
		assert(func_symbol_id != SYMBOL_ID_INVALID);

		InstrIndex load_func_addr_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* load_func_addr = instr_buffer_at(instr_buffer, load_func_addr_index);
		load_func_addr->kind = symbol.linkage == SYMBOL_LINKAGE_EXTERNAL_DYNAMIC
			? INSTR_LOAD_EXTERNAL_FUNCTION_ADDR
			: INSTR_LOAD_FUNCTION_ADDR;
		load_func_addr->load_function_addr.function_index = func_symbol_id;
		profile_scope_end();
		return load_func_addr_index;
	}
	case EXPR_VARIABLE_REFERENCE: {
		const Variable* var = compiler->vars[expr->variable_ref.var->id];

		InstrIndex value_instr = compiler->var_values[expr->variable_ref.var->id];
		assert(value_instr.value != INVALID_INSTR_INDEX.value);

		if (var->type.kind == TYPE_ARRAY) {
			InstrIndex stack_addr_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* stack_addr = instr_buffer_at(instr_buffer, stack_addr_index);
			stack_addr->kind = INSTR_STACK_ADDR;
			stack_addr->stack_addr.stack_alloc = value_instr;

			value_instr = stack_addr_index;
		} else if (var->type.kind == TYPE_STRUCT || var->type.kind == TYPE_UNION) {
			TypeLayout type_layout = type_get_layout(compiler->type_context, &var->type);

			InstrIndex alloc_index = instr_buffer_push(instr_buffer, instr_allocator, (Instr) {
					.kind = INSTR_STACK_ALLOC,
					.stack_alloc = {
						.size = type_layout.size,
						.alignment = type_layout.alignment,
					}
				});

			InstrIndex src_addr_index = instr_buffer_push(instr_buffer, instr_allocator, (Instr) {
					.kind = INSTR_STACK_ADDR,
					.stack_addr = { value_instr },
				});

			InstrIndex dst_addr_index = instr_buffer_push(instr_buffer, instr_allocator, (Instr) {
					.kind = INSTR_STACK_ADDR,
					.stack_addr = { alloc_index },
				});

			InstrIndex mem_copy_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* mem_copy = instr_buffer_at(instr_buffer, mem_copy_index);
			mem_copy->kind = INSTR_MEM_COPY_FIXED;
			mem_copy->mem_copy_fixed.src = src_addr_index;
			mem_copy->mem_copy_fixed.dst = dst_addr_index;
			mem_copy->mem_copy_fixed.size = type_layout.size;
			mem_copy->mem_copy_fixed.io_state = compiler->io_state;

			compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, mem_copy_index);

			value_instr = alloc_index;
		}

		profile_scope_end();
		return value_instr;
	}
	case EXPR_INTEGER_LITERAL: {
		assert(type_kind_is_int(expr->int_literal.integer_type));

		Type int_type = { .kind = expr->int_literal.integer_type };
		size_t int_size = type_get_layout(compiler->type_context, &int_type).size;
		
		InstrIndex instr_index = instr_new_int_const(instr_buffer,
				instr_allocator,
				expr->int_literal.value,
				int_size);
		profile_scope_end();
		return instr_index;
	}
	case EXPR_STRING_LITERAL: {
		String string = expr->string_literal.full_string;
		uint32_t string_id = str_storage_append(compiler->str_storage, string);

		InstrIndex instr_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* instr = instr_buffer_at(instr_buffer, instr_index);
		instr->kind = INSTR_CONST_STRING;
		instr->const_string.string_id = string_id;
		profile_scope_end();
		return instr_index;
	}
	case EXPR_FUNCTION_PARAM: {
		size_t arg_index = expr->function_param.param_index;
		assert(arg_index < compiler->function->proto.parameter_count);
		profile_scope_end();
		return compiler->arg_states[arg_index];
	}
	case EXPR_UNARY: {
		InstrIndex instr_index = _compile_unary_expr(compiler, expr);
		profile_scope_end();
		return instr_index;
	}
	case EXPR_CHAR_LITERAL: {
		assert(expr->char_literal.value <= 0xff);

		InstrIndex instr_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* instr = instr_buffer_at(instr_buffer, instr_index);

		instr->kind = INSTR_CONST_8;
		instr->const_8.u = (uint8_t)expr->char_literal.value;
		profile_scope_end();
		return instr_index;
	}
	case EXPR_ARRAY_INDEX: {
		Type array_type;
		expr_get_type(expr->array_index.array, &array_type);

		Type* element_type = type_extract_pointer_base_type(&array_type);
		TypeLayout element_layout = type_get_layout(compiler->type_context, element_type);

		InstrIndex element_addr = _compile_address_of_array_element(compiler, expr);

		InstrIndex load_instr_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* load_instr = instr_buffer_at(instr_buffer, load_instr_index);
		load_instr->ptr_load.ptr = element_addr;
		load_instr->ptr_load.io_state = compiler->io_state;

		compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, load_instr_index);

		switch (element_layout.size) {
		case 1:
			load_instr->kind = INSTR_PTR_LOAD_8;
			break;
		case 2:
			load_instr->kind = INSTR_PTR_LOAD_16;
			break;
		case 4:
			load_instr->kind = INSTR_PTR_LOAD_32;
			break;
		case 8:
			load_instr->kind = INSTR_PTR_LOAD_64;
			break;
		default:
			panic("Unsupported element size");
		}

		profile_scope_end();
		return load_instr_index;
	}
	case EXPR_INDIRECT_FIELD_ACCESS:
	case EXPR_DIRECT_FIELD_ACCESS: {
		Type field_type;
		expr_get_type(expr, &field_type);

		TypeLayout field_type_layout = type_get_layout(compiler->type_context, &field_type);

		InstrIndex field_address = _compile_address_expr(compiler,
				_compile_address_of(compiler, expr));

		if (field_type.kind == TYPE_STRUCT || field_type.kind == TYPE_UNION) {
			InstrIndex alloc_index = instr_buffer_push(instr_buffer, instr_allocator, (Instr) {
					.kind = INSTR_STACK_ALLOC,
					.stack_alloc = {
						.size = field_type_layout.size,
						.alignment = field_type_layout.alignment,
					}
				});

			InstrIndex stack_addr_index = instr_buffer_push(instr_buffer, instr_allocator, (Instr) {
					.kind = INSTR_STACK_ADDR,
					.stack_addr = { alloc_index },
				});

			InstrIndex mem_copy_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* mem_copy = instr_buffer_at(instr_buffer, mem_copy_index);
			mem_copy->kind = INSTR_MEM_COPY_FIXED;
			mem_copy->mem_copy_fixed.src = field_address;
			mem_copy->mem_copy_fixed.dst = stack_addr_index;
			mem_copy->mem_copy_fixed.size = field_type_layout.size;
			mem_copy->mem_copy_fixed.io_state = compiler->io_state;

			compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, mem_copy_index);

			profile_scope_end();
			return alloc_index;
		}

		InstrIndex load_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* load_instr = instr_buffer_at(instr_buffer, load_index);
		load_instr->ptr_load.ptr = field_address;
		load_instr->ptr_load.io_state = compiler->io_state;

		compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, load_index);

		switch (field_type_layout.size) {
		case 1:
			load_instr->kind = INSTR_PTR_LOAD_8;
			break;
		case 2:
			load_instr->kind = INSTR_PTR_LOAD_16;
			break;
		case 4:
			load_instr->kind = INSTR_PTR_LOAD_32;
			break;
		case 8:
			load_instr->kind = INSTR_PTR_LOAD_64;
			break;
		default:
			panic("Unsupported element size");
		}

		profile_scope_end();
		return load_index;
	}
	case EXPR_ENUM_CONSTANT: {
		assert(expr->enum_constant.variant_index < INT32_MAX);
		InstrIndex instr_index = instr_new_int_const(instr_buffer,
				instr_allocator,
				expr->enum_constant.variant_index,
				4);
		profile_scope_end();
		return instr_index;
	}
	case EXPR_CAST: {
		Type value_type;
		expr_get_type(expr->cast.expr, &value_type);

		InstrIndex value = _compile_expr(compiler, expr->cast.expr);
		value = _compile_int_cast(compiler, &value_type, expr->cast.target_type, value);
		profile_scope_end();
		return value;
	}
	case EXPR_SIZE_OF_EXPR: {
		Type type;
		expr_get_type(expr->size_of_expr.expr, &type);

		TypeLayout type_layout = type_get_layout(compiler->type_context, &type);

		size_t type_size = type_layout.size;

		Expr* target_expr = expr->size_of_expr.expr;
		if (target_expr->kind == EXPR_FUNCTION_PARAM && type.kind == TYPE_ARRAY) {
			type_size = compiler->type_context->pointer_type_layout.size;
		}

		InstrIndex size_const = instr_new_int_const(instr_buffer,
				instr_allocator,
				type_size,
				compiler->type_context->pointer_type_layout.size);

		profile_scope_end();
		return size_const;
	}
	case EXPR_SIZE_OF_TYPE: {
		TypeLayout type_layout = type_get_layout(compiler->type_context, expr->size_of_type.type);

		InstrIndex size_const = instr_new_int_const(instr_buffer,
				instr_allocator,
				type_layout.size,
				compiler->type_context->pointer_type_layout.size);

		profile_scope_end();
		return size_const;
	}
	case EXPR_COMPOUND_LITERAL: {
		Type* literal_type = expr->compound_literal.type;
		TypeLayout layout = type_get_layout(compiler->type_context, literal_type);

		InstrIndex stack_alloc_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* stack_alloc = instr_buffer_at(instr_buffer, stack_alloc_index);
		stack_alloc->kind = INSTR_STACK_ALLOC;
		stack_alloc->stack_alloc.size = (uint32_t)layout.size;
		stack_alloc->stack_alloc.alignment = (uint32_t)layout.alignment;

		InstrIndex stack_addr_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* stack_addr = instr_buffer_at(instr_buffer, stack_addr_index);
		stack_addr->kind = INSTR_STACK_ADDR;
		stack_addr->stack_addr.stack_alloc = stack_alloc_index;

		_compile_compound_literal_init(compiler, &expr->compound_literal, stack_addr_index);

		profile_scope_end();
		return stack_alloc_index;
	}
	}

	unreachable();
	profile_scope_end();
	return (InstrIndex) {};
}

// Compiles the expression while casting results of boolean expressions to `TYPE_INT`.
//
// Other expression kinds are returned untouched.
static InstrIndex _compile_expr(FunctionCompiler* compiler, Expr* expr) {
	profile_scope_start(__func__);

	InstrIndex instr_index = _compile_expr_without_implicit_casts(compiler, expr);

	if (expr_is_bool(expr)) {
		Type result_type = {};
		expr_get_type(expr, &result_type);

		assert_msg(result_type.kind == TYPE_INT, "A boolean expression must have a `TYPE_INT`");

		InstrBuffer* instr_buffer = &compiler->instr_buffer;
		Arena* instr_allocator = compiler->instr_allocator;

		InstrIndex convert_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* convert = instr_buffer_at(instr_buffer, convert_index);
		convert->kind = INSTR_BOOL_TO_INT;
		convert->bool_to_int.operand = instr_index;

		instr_index = instr_new_unsigned_cast(instr_buffer,
				instr_allocator,
				convert_index,
				sizeof(char), // FIXME: Don't hardcode?
				type_get_layout(compiler->type_context, &result_type).size);
	}

	profile_scope_end();
	return instr_index;
}

// Compiles the expression while casting non-boolean expressions to a boolean.
// 
// Boolean expressions are returned untouched.
static InstrIndex _compile_expr_to_bool(FunctionCompiler* compiler, Expr* expr) {
	profile_scope_start(__func__);

	InstrIndex instr_index = INVALID_INSTR_INDEX;
	if (expr_is_bool(expr)) {
		instr_index = _compile_expr_without_implicit_casts(compiler, expr);
	} else {
		InstrIndex expr_value = _compile_expr(compiler, expr);

		Type result_type;
		expr_get_type(expr, &result_type);

		TypeLayout result_layout = type_get_layout(compiler->type_context, &result_type);
		size_t result_bit_size_index = count_trailing_zeros(result_layout.size);

		assert_msg(type_kind_is_int(result_type.kind)
				|| result_type.kind == TYPE_POINTER
				|| result_type.kind == TYPE_ARRAY,
			"This expression type is not implicitely convertable to a boolean");

		InstrBuffer* instr_buffer = &compiler->instr_buffer;
		Arena* instr_allocator = compiler->instr_allocator;

		// A cast to boolean is done using a comparison against a zero
		InstrIndex zero = instr_new_int_const(instr_buffer,
				instr_allocator,
				0,
				result_layout.size);

		InstrIndex compare_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* compare = instr_buffer_at(instr_buffer, compare_index);
		compare->kind = INSTR_COMPARE_8 + result_bit_size_index;
		compare->compare.left = expr_value;
		compare->compare.right = zero;
		compare->compare.kind = INSTR_CMP_GREATER;

		instr_index = compare_index;
	}

	assert(instr_index.value != INVALID_INSTR_INDEX.value);

	profile_scope_end();
	return instr_index;
}

static InstrIndex _create_phi_of_2_variants(FunctionCompiler* compiler,
		InstrIndex variant_a,
		InstrIndex region_a,
		InstrIndex variant_b,
		InstrIndex region_b) {
	profile_scope_start(__func__);

	assert(variant_a.value != INVALID_INSTR_INDEX.value);
	assert(region_a.value != INVALID_INSTR_INDEX.value);
	assert(variant_b.value != INVALID_INSTR_INDEX.value);
	assert(region_b.value != INVALID_INSTR_INDEX.value);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	InstrIndex select_a_index = instr_buffer_append(instr_buffer, instr_allocator);
	Instr* select_a = instr_buffer_at(instr_buffer, select_a_index);
	select_a->kind = INSTR_SELECT;
	select_a->select.value = variant_a;
	select_a->select.region = region_a;

	InstrIndex select_b_index = instr_buffer_append(instr_buffer, instr_allocator);
	Instr* select_b = instr_buffer_at(instr_buffer, select_b_index);
	select_b->kind = INSTR_SELECT;
	select_b->select.value = variant_b;
	select_b->select.region = region_b;

	InstrInputs select_inputs_buffer = instr_allocate_inputs_array(instr_buffer, 2);

	InstrIndex* select_inputs = &instr_buffer->inputs_buffer[select_inputs_buffer.start];
	select_inputs[0] = select_a_index;
	select_inputs[1] = select_b_index;

	InstrIndex phi_index = instr_buffer_append(instr_buffer, instr_allocator);
	Instr* phi = instr_buffer_at(instr_buffer, phi_index);
	phi->kind = INSTR_PHI;
	phi->phi.variants = select_inputs_buffer;

	profile_scope_end();
	return phi_index;
}

//
// Loop Compilation
//

static Scope* _loop_body_scope(AstNode* loop) {
	if (loop->kind == AST_NODE_FOR_LOOP) {
		return loop->for_loop.body_scope;
	} else if (loop->kind == AST_NODE_WHILE_LOOP) {
		return loop->while_loop.body_scope;
	} else {
		unreachable();
	}

	return NULL;
}

typedef struct LoopValuesSnapshot LoopValuesSnapshot;
struct LoopValuesSnapshot {
	InstrIndex** entries;
	InstrIndex* regions;
	size_t count;
};

static void _reserve_phis(FunctionCompiler* compiler,
		BitArray filter,
		InstrIndex* original_values,
		InstrIndex* out_phis,
		size_t count) {
	profile_scope_start(__func__);

	assert(filter.bit_count == count);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	for (size_t i = 0; i < count; i += 1) {
		if (bit_array_get(&filter, i)) {
			out_phis[i] = instr_new_empty_phi(instr_buffer, instr_allocator);
		} else {
			out_phis[i] = original_values[i];
		}
	}

	profile_scope_end();
}

static BitArray _reserve_var_phis(FunctionCompiler* compiler,
		Arena* filter_allocator,
		InstrIndex* out_phis) {
	profile_scope_start(__func__);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	BitArray filter = bit_array_alloc_zeros(filter_allocator, compiler->var_count);

	for (size_t i = 0; i < compiler->var_count; i += 1) {
		if (compiler->vars[i] == NULL) {
			out_phis[i] = INVALID_INSTR_INDEX;
			continue;
		}

		TypeKind var_type_kind = compiler->vars[i]->type.kind;
		if (var_type_kind == TYPE_STRUCT
				|| var_type_kind == TYPE_UNION
				|| var_type_kind == TYPE_ARRAY) {
			out_phis[i] = compiler->var_values[i];
			continue;
		}

		bit_array_set(&filter, i, true);
		out_phis[i] = instr_new_empty_phi(instr_buffer, instr_allocator);
	}

	profile_scope_end();
	return filter;
}

static BitArray _reserve_arg_phis(FunctionCompiler* compiler,
		Arena* filter_allocator,
		InstrIndex* out_phis) {
	profile_scope_start(__func__);


	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	size_t arg_count = compiler->function->proto.parameter_count;
	for (size_t phi_index = 0; phi_index < arg_count; phi_index++) {
		out_phis[phi_index] = instr_new_empty_phi(instr_buffer, instr_allocator);
	}

	BitArray filter = bit_array_alloc_ones(filter_allocator, arg_count);

	profile_scope_end();
	return filter;
}

static InstrIndex _deduplicate_snapshot_values(const LoopValuesSnapshot* snapshots,
		size_t snapshot_count,
		size_t value_index) {
	profile_scope_start(__func__);

	InstrIndex deduplicated = INVALID_INSTR_INDEX;
	for (size_t snapshot_index = 0; snapshot_index < snapshot_count; snapshot_index += 1) {
		LoopValuesSnapshot snapshot = snapshots[snapshot_index];
		for (size_t entry_index = 0; entry_index < snapshot.count; entry_index += 1) {
			InstrIndex variant_index = snapshot.entries[entry_index][value_index];
			if (deduplicated.value == INVALID_INSTR_INDEX.value) {
				assert(variant_index.value != INVALID_INSTR_INDEX.value);
				deduplicated = variant_index;
			} else if (variant_index.value != deduplicated.value) {
				profile_scope_end();
				return INVALID_INSTR_INDEX;
			}
		}
	}

	profile_scope_end();
	return deduplicated;
}

// Merges values from all snapshots into a single phi:
// * `phi` is already expected to be prefilled with empty phis (for example, using `_reserve_phis`).
// * `filter` tells which `phis` entries to ignore. If `bit_array_get(&filter, phi_index) == false`,
//   then it is skipped.
// * `deduplicate_all` - in case *all of the variants are the same*, then replace the phi with
//   that variant instead by directly modifing `phis.
// 
// NOTE: The number of snapshots is usually very low.
static void _merge_variants(FunctionCompiler* compiler,
		InstrIndex* phis,
		size_t phi_count,
		const LoopValuesSnapshot* snapshots,
		size_t snapshot_count,
		BitArray filter,
		bool deduplicate_all) {
	profile_scope_start(__func__);
	assert(phi_count == filter.bit_count);

	size_t variant_count = 0;
	for (size_t i = 0; i < snapshot_count; i += 1) {
		variant_count += snapshots[i].count;
	}

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	for (size_t phi_index = 0; phi_index < phi_count; phi_index++) {
		if (!bit_array_get(&filter, phi_index)) {
			continue;
		}

		Instr* phi = instr_buffer_at(instr_buffer, phis[phi_index]);
		assert(phi->kind == INSTR_PHI);
		assert(phi->phi.variants.start == UINT16_MAX);
		assert(phi->phi.variants.count == 0);

		if (deduplicate_all) {
			InstrIndex deduplicated = _deduplicate_snapshot_values(snapshots,
					snapshot_count,
					phi_index);

			if (deduplicated.value != INVALID_INSTR_INDEX.value) {
				phis[phi_index] = deduplicated;
				continue;
			}
		}

		InstrInputs select_inputs_buffer = instr_allocate_inputs_array(instr_buffer, variant_count);
		InstrIndex* select_inputs = &instr_buffer->inputs_buffer[select_inputs_buffer.start];

		size_t variant_index = 0;

		for (size_t snapshot_index = 0; snapshot_index < snapshot_count; snapshot_index += 1) {
			LoopValuesSnapshot snapshot = snapshots[snapshot_index];
			for (size_t entry_index = 0; entry_index < snapshot.count; entry_index += 1) {
				InstrIndex select_index = instr_buffer_append(instr_buffer, instr_allocator);
				Instr* select = instr_buffer_at(instr_buffer, select_index);
				select->kind = INSTR_SELECT;
				select->select.value = snapshot.entries[entry_index][phi_index];
				select->select.region = snapshot.regions[entry_index];

				select_inputs[variant_index] = select_index;
				variant_index += 1;
			}
		}

		assert(variant_index == variant_count);

		phi->kind = INSTR_PHI;
		phi->phi.variants = select_inputs_buffer;
	}

	profile_scope_end();
}

static void _fix_loop_control_jumps(InstrBuffer* instr_buffer,
		ControlFlowStmt* stmts,
		InstrIndex break_target,
		InstrIndex continue_target) {
	profile_scope_start(__func__);

	maybe(break_target.value == INVALID_INSTR_INDEX.value);
	maybe(continue_target.value == INVALID_INSTR_INDEX.value);

	for (ControlFlowStmt* stmt = stmts;
			stmt != NULL;
			stmt = stmt->next) {

		const Instr* region = instr_buffer_at(instr_buffer, stmt->region);
		assert(region->kind == INSTR_REGION);

		Instr* jump = instr_buffer_at(instr_buffer, region->region.last_instr);
		assert(jump->kind == INSTR_JUMP);
		assert(jump->jump.target_region.value == INVALID_INSTR_INDEX.value);

		if (stmt->kind == CONTROL_FLOW_BREAK) {
			assert(break_target.value != INVALID_INSTR_INDEX.value);
			jump->jump.target_region = break_target;
		} else if (stmt->kind == CONTROL_FLOW_CONTINUE) {
			assert(continue_target.value != INVALID_INSTR_INDEX.value);
			jump->jump.target_region = continue_target;
		} else {
			unreachable();
		}
	}

	profile_scope_end();
}

// Compiles a while or a for loop.
//
// A loop in compiled form looks like this:
// 
// `pre_loop_region`:
//   here we assign the initial values to all the variables
//   then jump to `condition_region`
//
// `condition_region`:
//   here we check the condition
//   then jump to either `body_region` or `post_loop_region`
// 
// `body_region`:
//   the actual body of the loop
//   then jump back to `condition_region`
//
// `post_loop_region`:
//   the region after the loop, where the program executes further.
//
// --- Phi Nodes ---
//
// On each iteration of the loop we need to decide which value for each variable to use:
// 1. The initial value
// 2. The one from the last iteration
//
// This functions creates a phi node for each variable. And these phi nodes select an initial value
// from `pre_loop_region` or the value of the last iteration from `body_region`.
//
// NOTE: Phi nodes are created for all variables and function arguments, since we don't really know
//       which variables will be modified inside the loop, without doing extra `Ast` traversals.
//
// * `node`           - loop node
// * `init_stmt`      - statement that defines the initial state of a for loop (irrelevant for while
//                      loops)
// * `condition_expr` - the loop condition
// * `body`           - loop body
// * `advance_expr`   - an expression, that advances the state forward after each iteration
//                      (relevant only for `for loop`s)
//
// FIXME: For `for` loops should reset variables defined in the `loop scope` (by the init statement)
static InstrIndex _compile_loop(FunctionCompiler* compiler,
		InstrIndex current_region,
		AstNode* node,
		AstNode* init_stmt,
		Expr* condition_expr,
		Expr* advance_expr) {
	profile_scope_start(__func__);

	if (node->kind == AST_NODE_WHILE_LOOP) {
		assert(node->while_loop.condition_kind == WHILE_LOOP_PRE_CONDITION);

		assert(init_stmt == NULL);
		assert(condition_expr != NULL);
		assert(advance_expr == NULL);
	}

	ArenaRegion temp = arena_begin_temp(compiler->temp_allocator);

	LoopSwitchState current_loop_switch_state = (LoopSwitchState) {
		.parent = compiler->loop_switch_state,
		.control_flow_stmts = NULL,
		.node = node,
		.break_count = 0,
		.break_capacity = node->while_loop.break_count,
		.break_var_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex*,
				node->while_loop.break_count),
		.break_arg_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex*,
				node->while_loop.break_count),
		.break_regions = arena_alloc_array(compiler->temp_allocator,
				InstrIndex,
				node->while_loop.break_count),
		.continue_count = 0,
		.continue_capacity = node->while_loop.continue_count,
		.continue_var_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex*,
				node->while_loop.continue_count),
		.continue_arg_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex*,
				node->while_loop.continue_count),
		.continue_regions = arena_alloc_array(compiler->temp_allocator,
				InstrIndex,
				node->while_loop.continue_count),
	};

	compiler->loop_switch_state = &current_loop_switch_state;

	size_t arg_count = compiler->function->proto.parameter_count;
	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	Instr* region_instr = instr_buffer_at(instr_buffer, current_region);

	InstrIndex pre_loop_region_index;

	// Jump to `pre_loop_region`
	{
		InstrIndex jump_to_pre_loop = instr_new_jump(instr_buffer,
				instr_allocator,
				INVALID_INSTR_INDEX,
				&compiler->io_state);

		pre_loop_region_index = instr_new_region(instr_buffer, instr_allocator);
		instr_set_jump_target(instr_buffer, jump_to_pre_loop, pre_loop_region_index);

		region_instr->region.last_instr = jump_to_pre_loop;
	}

	// Compile the `init_stmt`. So that any variables defined by it, get initialized and later get
	// replaced with a phi.
	if (init_stmt) {
		_compile_statement(compiler, init_stmt);
	}

	// Arrays of original var & arg values before the loop
	InstrIndex* original_var_values = compiler->var_values;
	InstrIndex* original_arg_values = compiler->arg_states;

	InstrIndex* var_phis = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			compiler->var_count);
	InstrIndex* arg_phis = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			arg_count);

	// Replace current variables and arguments with phis
	BitArray var_filter = _reserve_var_phis(compiler, compiler->temp_allocator, var_phis);
	BitArray arg_filter = _reserve_arg_phis(compiler, compiler->temp_allocator, arg_phis);

	// Copy arrays with the replaced phis, for the inner loop body to modify them
	InstrIndex* var_values_for_body = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			compiler->var_count);
	InstrIndex* arg_values_for_body = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			arg_count);

	array_copy(var_values_for_body, var_phis, compiler->var_count);
	array_copy(arg_values_for_body, arg_phis, arg_count);

	compiler->var_values = var_values_for_body;
	compiler->arg_states = arg_values_for_body;

	// Compile the condition

	InstrIndex branch_index = INVALID_INSTR_INDEX;
	InstrIndex condition_region = INVALID_INSTR_INDEX;
	InstrIndex pre_loop_to_body_jump = INVALID_INSTR_INDEX;
	if (condition_expr) {
		InstrIndex jump_to_condition = instr_new_jump(instr_buffer,
				instr_allocator,
				INVALID_INSTR_INDEX,
				&compiler->io_state);

		condition_region = instr_new_region(instr_buffer, instr_allocator);
		instr_set_jump_target(instr_buffer, jump_to_condition, condition_region);
		instr_region_set_last(instr_buffer, pre_loop_region_index, jump_to_condition);

		branch_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* branch = instr_buffer_at(instr_buffer, branch_index);
		branch->kind = INSTR_BRANCH;
		branch->branch.condition = _compile_expr_to_bool(compiler, condition_expr);
		branch->branch.io_state = compiler->io_state;

		compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, INVALID_INSTR_INDEX);

		instr_region_set_last(instr_buffer, condition_region, branch_index);
	} else {
		// Now, that this loop doesn't have a `condition_expr`, there is also no `condition_region`,
		// which means we can directly jump to the body of the loop.
		//
		// Here jump target is `INVALID_INSTR_INDEX`, since we haven't yet compiled the body, and
		// thus don't know its `initial_region`
		pre_loop_to_body_jump = instr_new_jump(instr_buffer,
				instr_allocator,
				INVALID_INSTR_INDEX,
				&compiler->io_state);

		instr_region_set_last(instr_buffer, pre_loop_region_index, pre_loop_to_body_jump);
	}

	// Compile the body
	Scope* body_scope = _loop_body_scope(node);
	CompiledBlockRegions body_block = _compile_scope(compiler, body_scope);

	// Compile `advance_expr` right at the end of the body.
	if (!instr_region_finished(instr_buffer, body_block.final_region)) {
		// If the loop body already ends with a control instruction, whether it's break, cotinue or
		// a return, the `advance_expr` won't be rechable any more.
		if (node->kind == AST_NODE_FOR_LOOP && advance_expr) {
			_compile_expr(compiler, advance_expr);
		}
	}

	{
		LoopValuesSnapshot var_snapshots[] = {
			(LoopValuesSnapshot) {
				.entries = &original_var_values,
				.regions = &pre_loop_region_index,
				.count = 1,
			},
			(LoopValuesSnapshot) {
				.entries = &compiler->var_values,
				.regions = &body_block.final_region,
				.count = 1,
			},
			(LoopValuesSnapshot) {
				.entries = current_loop_switch_state.break_var_values,
				.regions = current_loop_switch_state.break_regions,
				.count = current_loop_switch_state.break_count,
			},
			(LoopValuesSnapshot) {
				.entries = current_loop_switch_state.continue_var_values,
				.regions = current_loop_switch_state.continue_regions,
				.count = current_loop_switch_state.continue_count,
			},
		};

		_merge_variants(compiler,
				var_phis,
				compiler->var_count,
				var_snapshots,
				array_size(var_snapshots),
				var_filter,
				false);

		LoopValuesSnapshot arg_snapshots[] = {
			(LoopValuesSnapshot) {
				.entries = &original_arg_values,
				.regions = &pre_loop_region_index,
				.count = 1,
			},
			(LoopValuesSnapshot) {
				.entries = &compiler->arg_states,
				.regions = &body_block.final_region,
				.count = 1,
			},
			(LoopValuesSnapshot) {
				.entries = current_loop_switch_state.break_arg_values,
				.regions = current_loop_switch_state.break_regions,
				.count = current_loop_switch_state.break_count,
			},
			(LoopValuesSnapshot) {
				.entries = current_loop_switch_state.continue_arg_values,
				.regions = current_loop_switch_state.continue_regions,
				.count = current_loop_switch_state.continue_count,
			},
		};

		_merge_variants(compiler,
				arg_phis,
				arg_count,
				arg_snapshots,
				array_size(arg_snapshots),
				arg_filter,
				false);
	}


	array_copy(original_var_values, var_phis, compiler->var_count);
	array_copy(original_arg_values, arg_phis, arg_count);

	compiler->var_values = original_var_values;
	compiler->arg_states = original_arg_values;

	// Jump back to the start of the loop
	if (!instr_region_finished(instr_buffer, body_block.final_region)) {
		// NOTE: In case this loop doesn't have a `condition_expr`, jump directly to the first
		//       region of the body.
		InstrIndex post_loop_jump_target = condition_expr
			? condition_region
			: body_block.initial_region;

		assert(post_loop_jump_target.value != INVALID_INSTR_INDEX.value);

		InstrIndex post_loop_jump = instr_new_jump(instr_buffer,
				instr_allocator,
				post_loop_jump_target,
				&compiler->io_state);

		instr_region_set_last(instr_buffer, body_block.final_region, post_loop_jump);
	}

	if (!condition_expr) {
		assert(pre_loop_to_body_jump.value != INVALID_INSTR_INDEX.value);

		instr_set_jump_target(instr_buffer, pre_loop_to_body_jump, body_block.initial_region);
	}

	InstrIndex post_loop_region_index = instr_new_region(instr_buffer, instr_allocator);

	// Now fix the jump targets, of the branch instruction. If there is actually a `condition_expr`.
	if (condition_expr) {
		assert(branch_index.value != INVALID_INSTR_INDEX.value);

		Instr* branch = instr_buffer_at(instr_buffer, branch_index);
		branch->branch.true_region = body_block.initial_region;
		branch->branch.false_region = post_loop_region_index;
	}

	compiler->var_values = original_var_values;
	compiler->arg_states = original_arg_values;

	// Now fix the jumps inserted by `break` and `continue` statements.
	_fix_loop_control_jumps(instr_buffer,
			compiler->loop_switch_state->control_flow_stmts,
			post_loop_region_index, 
			condition_region);

	arena_end_temp(temp);

	_restore_loop_switch_state(compiler);

	profile_scope_end();
	return post_loop_region_index;
}

static InstrIndex _compile_for_loop(FunctionCompiler* compiler,
		InstrIndex current_region,
		AstNode* node) {
	profile_scope_start(__func__);
	assert(node->kind == AST_NODE_FOR_LOOP);

	ArenaRegion temp = arena_begin_temp(compiler->temp_allocator);

	// 1. Setup loop state
	LoopSwitchState current_loop_switch_state = (LoopSwitchState) {
		.parent = compiler->loop_switch_state,
		.control_flow_stmts = NULL,
		.node = node,
		.break_count = 0,
		.break_capacity = node->for_loop.break_count,
		.break_var_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex*,
				node->for_loop.break_count),
		.break_arg_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex*,
				node->for_loop.break_count),
		.break_regions = arena_alloc_array(compiler->temp_allocator,
				InstrIndex,
				node->for_loop.break_count),
		.continue_count = 0,
		.continue_capacity = node->for_loop.continue_count,
		.continue_var_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex*,
				node->for_loop.continue_count),
		.continue_arg_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex*,
				node->for_loop.continue_count),
		.continue_regions = arena_alloc_array(compiler->temp_allocator,
				InstrIndex,
				node->for_loop.continue_count),
	};

	compiler->loop_switch_state = &current_loop_switch_state;

	// 2. Setup loop header
	size_t arg_count = compiler->function->proto.parameter_count;
	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	Instr* region_instr = instr_buffer_at(instr_buffer, current_region);

	InstrIndex pre_loop_region_index;

	// Jump to `pre_loop_region`
	{
		InstrIndex jump_to_pre_loop = instr_new_jump(instr_buffer,
				instr_allocator,
				INVALID_INSTR_INDEX,
				&compiler->io_state);

		pre_loop_region_index = instr_new_region(instr_buffer, instr_allocator);
		instr_set_jump_target(instr_buffer, jump_to_pre_loop, pre_loop_region_index);

		region_instr->region.last_instr = jump_to_pre_loop;
	}

	// Compile the `init_stmt`. So that any variables defined by it, get initialized and later get
	// replaced with a phi.
	if (node->for_loop.init_stmt) {
		_compile_statement(compiler, node->for_loop.init_stmt);
	}

	// 3. Original var & arg values
	InstrIndex* original_var_values = compiler->var_values;
	InstrIndex* original_arg_values = compiler->arg_states;

	InstrIndex* var_phis = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			compiler->var_count);
	InstrIndex* arg_phis = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			arg_count);

	// 4. Replace current variables and arguments with phis
	BitArray var_filter = _reserve_var_phis(compiler, compiler->temp_allocator, var_phis);
	BitArray arg_filter = _reserve_arg_phis(compiler, compiler->temp_allocator, arg_phis);

	// 5. Set the created phis as current variable values
	InstrIndex* var_values_for_body = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			compiler->var_count);
	InstrIndex* arg_values_for_body = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			arg_count);

	array_copy(var_values_for_body, var_phis, compiler->var_count);
	array_copy(arg_values_for_body, arg_phis, arg_count);

	compiler->var_values = var_values_for_body;
	compiler->arg_states = arg_values_for_body;

	// 6. Compile the condition
	InstrIndex branch_index = INVALID_INSTR_INDEX;
	InstrIndex condition_region = INVALID_INSTR_INDEX;
	InstrIndex pre_loop_to_body_jump = INVALID_INSTR_INDEX;
	if (node->for_loop.condition) {
		InstrIndex jump_to_condition = instr_new_jump(instr_buffer,
				instr_allocator,
				INVALID_INSTR_INDEX,
				&compiler->io_state);

		condition_region = instr_new_region(instr_buffer, instr_allocator);
		instr_set_jump_target(instr_buffer, jump_to_condition, condition_region);
		instr_region_set_last(instr_buffer, pre_loop_region_index, jump_to_condition);

		branch_index = instr_buffer_append(instr_buffer, instr_allocator);
		Instr* branch = instr_buffer_at(instr_buffer, branch_index);
		branch->kind = INSTR_BRANCH;
		branch->branch.condition = _compile_expr_to_bool(compiler, node->for_loop.condition);
		branch->branch.io_state = compiler->io_state;

		compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, INVALID_INSTR_INDEX);

		instr_region_set_last(instr_buffer, condition_region, branch_index);
	} else {
		// Now, that this loop doesn't have a `condition_expr`, there is also no `condition_region`,
		// which means we can directly jump to the body of the loop.
		//
		// Here jump target is `INVALID_INSTR_INDEX`, since we haven't yet compiled the body, and
		// thus don't know its `initial_region`
		pre_loop_to_body_jump = instr_new_jump(instr_buffer,
				instr_allocator,
				INVALID_INSTR_INDEX,
				&compiler->io_state);

		instr_region_set_last(instr_buffer, pre_loop_region_index, pre_loop_to_body_jump);
	}

	// 7. Compile the body
	Scope* body_scope = _loop_body_scope(node);
	CompiledBlockRegions body_block = _compile_scope(compiler, body_scope);

	// 8.1. Merge values from the last iteration and from the blocks ending with `continue`

	{
		size_t var_count = compiler->var_count;

		ArenaRegion temp = arena_begin_temp(compiler->temp_allocator);

		InstrIndex* current_var_values = NULL;
		InstrIndex* current_arg_values = NULL;

		size_t snapshot_count = 1;
		LoopValuesSnapshot var_snapshots[2] = {};
		var_snapshots[0] = (LoopValuesSnapshot) {
			.entries = current_loop_switch_state.continue_var_values,
			.regions = current_loop_switch_state.continue_regions,
			.count = current_loop_switch_state.continue_count,
		};

		LoopValuesSnapshot arg_snapshots[2] = {};
		arg_snapshots[0] = (LoopValuesSnapshot) {
			.entries = current_loop_switch_state.continue_arg_values,
			.regions = current_loop_switch_state.continue_regions,
			.count = current_loop_switch_state.continue_count,
		};

		if (!instr_region_finished(instr_buffer, body_block.final_region)) {
			snapshot_count = 2;

			current_var_values = arena_alloc_array(compiler->temp_allocator,
					InstrIndex,
					var_count);
			current_arg_values = arena_alloc_array(compiler->temp_allocator,
					InstrIndex,
					arg_count);

			array_copy(current_var_values, compiler->var_values, var_count);
			array_copy(current_arg_values, compiler->arg_states, arg_count);

			var_snapshots[1] = (LoopValuesSnapshot) {
				.entries = &current_var_values,
				.regions = &body_block.final_region,
				.count = 1,
			};

			arg_snapshots[1] = (LoopValuesSnapshot) {
				.entries = &current_arg_values,
				.regions = &body_block.final_region,
				.count = 1,
			};
		}

		assert(array_size(var_snapshots) == array_size(arg_snapshots));
		assert(snapshot_count <= array_size(var_snapshots));

		_reserve_phis(compiler, var_filter, compiler->var_values, compiler->var_values, var_count);
		_reserve_phis(compiler, arg_filter, compiler->arg_states, compiler->arg_states, arg_count);

		// Merge
		_merge_variants(compiler,
				compiler->var_values,
				var_count,
				var_snapshots,
				snapshot_count,
				var_filter,
				false);
		_merge_variants(compiler,
				compiler->arg_states,
				arg_count,
				arg_snapshots,
				snapshot_count,
				arg_filter,
				false);

		arena_end_temp(temp);
	}

	// 8.2. Compile the advance expression.
	
	// Blocks that ends with a `continue`, first lead to the `advance_expr`.
	InstrIndex advance_region = INVALID_INSTR_INDEX;
	if (node->for_loop.advance_expr) {
		if (!instr_region_finished(instr_buffer, body_block.final_region)) {
			// If the loop body already ends with a control instruction, whether it's break,
			// continue or a return, the `advance_expr` won't be rechable any more.
			_compile_expr(compiler, node->for_loop.advance_expr);

			advance_region = instr_new_region(instr_buffer, instr_allocator);
			InstrIndex jump_to_advance = instr_new_jump(instr_buffer,
					instr_allocator,
					advance_region,
					&compiler->io_state);
			instr_region_set_last(instr_buffer, body_block.final_region, jump_to_advance);
		}
	}

	// 9. Merge values from before the loop and from the last iteration

	// Since we've merged values from blocks with `continue` before the `advance_expr`, here we only
	// need to merge the onces ending with `break`.
	{
		size_t snapshot_count = 3;
		LoopValuesSnapshot var_snapshots[3] = {};
		var_snapshots[0] = (LoopValuesSnapshot) {
			.entries = &original_var_values,
			.regions = &pre_loop_region_index,
			.count = 1,
		};
		var_snapshots[1] = (LoopValuesSnapshot) {
			.entries = current_loop_switch_state.break_var_values,
			.regions = current_loop_switch_state.break_regions,
			.count = current_loop_switch_state.break_count,
		};

		LoopValuesSnapshot arg_snapshots[3] = {};
		arg_snapshots[0] = (LoopValuesSnapshot) {
			.entries = &original_arg_values,
			.regions = &pre_loop_region_index,
			.count = 1,
		};
		arg_snapshots[1] = (LoopValuesSnapshot) {
			.entries = current_loop_switch_state.break_arg_values,
			.regions = current_loop_switch_state.break_regions,
			.count = current_loop_switch_state.break_count,
		};

		if (advance_region.value != INVALID_INSTR_INDEX.value) {
			snapshot_count = 3;
			var_snapshots[2] = (LoopValuesSnapshot) {
				.entries = &compiler->var_values,
				.regions = &advance_region,
				.count = 1,
			};

			arg_snapshots[2] = (LoopValuesSnapshot) {
				.entries = &compiler->arg_states,
				.regions = &advance_region,
				.count = 1,
			};
		} else if (!instr_region_finished(instr_buffer, body_block.final_region)) {
			snapshot_count = 3;
			var_snapshots[2] = (LoopValuesSnapshot) {
				.entries = &compiler->var_values,
				.regions = &body_block.final_region,
				.count = 1,
			};

			arg_snapshots[2] = (LoopValuesSnapshot) {
				.entries = &compiler->arg_states,
				.regions = &body_block.final_region,
				.count = 1,
			};
		}

		assert(array_size(var_snapshots) == array_size(arg_snapshots));
		assert(snapshot_count <= array_size(var_snapshots));

		_merge_variants(compiler,
				var_phis,
				compiler->var_count,
				var_snapshots,
				snapshot_count,
				var_filter,
				false);
		_merge_variants(compiler,
				arg_phis,
				arg_count,
				arg_snapshots,
				snapshot_count,
				arg_filter,
				false);
	}

	// 10. Set post loop values to phis
	array_copy(original_var_values, var_phis, compiler->var_count);
	array_copy(original_arg_values, arg_phis, arg_count);

	compiler->var_values = original_var_values;
	compiler->arg_states = original_arg_values;

	// 11. Start the next iteration. Jump to the start of the loop.
	if (!instr_region_finished(instr_buffer, advance_region.value == INVALID_INSTR_INDEX.value
					? body_block.final_region
					: advance_region)) {
		InstrIndex post_loop_jump_target = node->for_loop.condition
			? condition_region
			: body_block.initial_region;

		assert(post_loop_jump_target.value != INVALID_INSTR_INDEX.value);

		InstrIndex post_loop_jump = instr_new_jump(instr_buffer,
				instr_allocator,
				post_loop_jump_target,
				&compiler->io_state);

		instr_region_set_last(instr_buffer,
				advance_region.value == INVALID_INSTR_INDEX.value
					? body_block.final_region
					: advance_region,
				post_loop_jump);
	}

	// 12. Fix the jump targets, of the branch instruction
	if (!node->for_loop.condition) {
		assert(pre_loop_to_body_jump.value != INVALID_INSTR_INDEX.value);
		instr_set_jump_target(instr_buffer, pre_loop_to_body_jump, body_block.initial_region);
	}

	InstrIndex post_loop_region_index = instr_new_region(instr_buffer, instr_allocator);

	if (node->for_loop.condition) {
		assert(branch_index.value != INVALID_INSTR_INDEX.value);

		Instr* branch = instr_buffer_at(instr_buffer, branch_index);
		branch->branch.true_region = body_block.initial_region;
		branch->branch.false_region = post_loop_region_index;
	}

	// 13. Fix the jumps inserted by `break` and `continue` statements.
	_fix_loop_control_jumps(instr_buffer,
			compiler->loop_switch_state->control_flow_stmts,
			post_loop_region_index, 
			advance_region);
	
	_reset_variables_in_scope(compiler, node->for_loop.loop_scope);

	arena_end_temp(temp);

	_restore_loop_switch_state(compiler);

	profile_scope_end();
	return post_loop_region_index;
}

// Similar to `_compile_loop`.
static InstrIndex _compile_do_while_loop(FunctionCompiler* compiler,
		InstrIndex current_region,
		AstNode* node) {
	profile_scope_start(__func__);
	assert(node->while_loop.condition_kind == WHILE_LOOP_POST_CONDITION);

	ArenaRegion temp = arena_begin_temp(compiler->temp_allocator);

	// Save the previous loop state
	LoopSwitchState current_loop_switch_state = (LoopSwitchState) {
		.parent = compiler->loop_switch_state,
		.control_flow_stmts = NULL,
		.node = node,
		.break_count = 0,
		.break_capacity = node->while_loop.break_count,
		.break_var_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex*,
				node->while_loop.break_count),
		.break_arg_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex*,
				node->while_loop.break_count),
		.break_regions = arena_alloc_array(compiler->temp_allocator,
				InstrIndex,
				node->while_loop.break_count),
		.continue_count = 0,
		.continue_capacity = node->while_loop.continue_count,
		.continue_var_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex*,
				node->while_loop.continue_count),
		.continue_arg_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex*,
				node->while_loop.continue_count),
		.continue_regions = arena_alloc_array(compiler->temp_allocator,
				InstrIndex,
				node->while_loop.continue_count),
	};

	compiler->loop_switch_state = &current_loop_switch_state;

	size_t arg_count = compiler->function->proto.parameter_count;
	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	Instr* region_instr = instr_buffer_at(instr_buffer, current_region);

	// 1. Create preloop region (where the variables get their initial values)
	InstrIndex pre_loop_region;

	InstrIndex jump_to_pre_loop = instr_new_jump(instr_buffer,
			instr_allocator,
			INVALID_INSTR_INDEX,
			&compiler->io_state);

	pre_loop_region = instr_new_region(instr_buffer, instr_allocator);
	instr_set_jump_target(instr_buffer, jump_to_pre_loop, pre_loop_region);

	region_instr->region.last_instr = jump_to_pre_loop;

	// 2. Jump to first iteration
	InstrIndex jump_to_first_iteration = instr_new_jump(instr_buffer,
			instr_allocator,
			INVALID_INSTR_INDEX,
			&compiler->io_state);

	instr_region_set_last(instr_buffer, pre_loop_region, jump_to_first_iteration);

	// 3. Now setup phi node for all variables and function arguments
	InstrIndex* original_var_values = compiler->var_values;
	InstrIndex* original_arg_values = compiler->arg_states;

	InstrIndex* var_phis = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			compiler->var_count);
	InstrIndex* arg_phis = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			arg_count);

	// Replace current variables and arguments with phis
	BitArray var_filter = _reserve_var_phis(compiler, compiler->temp_allocator, var_phis);
	BitArray arg_filter = _reserve_arg_phis(compiler, compiler->temp_allocator, arg_phis);

	// 4. Create copies of variable value arrays
	InstrIndex* var_values_for_body = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			compiler->var_count);
	InstrIndex* arg_values_for_body = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			arg_count);

	array_copy(var_values_for_body, var_phis, compiler->var_count);
	array_copy(arg_values_for_body, arg_phis, arg_count);

	compiler->var_values = var_values_for_body;
	compiler->arg_states = arg_values_for_body;

	// 5. Compile the body
	const Scope* body_scope = _loop_body_scope(node);
	CompiledBlockRegions body_block = _compile_scope(compiler, node->while_loop.body_scope);

	// 6. Link the `jump_to_first_iteration`

	instr_set_jump_target(instr_buffer, jump_to_first_iteration, body_block.initial_region);
	
	// 7. Compile the condition

	// NOTE: Here the condition should use variable values from within the loop body, not the ones
	//       replaced with phis.
	//
	//       Replacement of empty phi variants is done later.

	InstrIndex jump_to_condition;
	bool final_region_finished = instr_region_finished(instr_buffer, body_block.final_region);
	if (!final_region_finished) {
		jump_to_condition = instr_new_jump(instr_buffer,
				instr_allocator,
				INVALID_INSTR_INDEX,
				&compiler->io_state);

		instr_region_set_last(instr_buffer, body_block.final_region, jump_to_condition);
	}

	InstrIndex condition_region = instr_new_region(instr_buffer, instr_allocator);

	if (!final_region_finished) {
		instr_set_jump_target(instr_buffer, jump_to_condition, condition_region);
	}

	InstrIndex branch_index = instr_buffer_append(instr_buffer, instr_allocator);
	Instr* branch = instr_buffer_at(instr_buffer, branch_index);
	branch->kind = INSTR_BRANCH;
	branch->branch.condition = _compile_expr_to_bool(compiler, &node->while_loop.condition);
	branch->branch.io_state = compiler->io_state;

	instr_region_set_last(instr_buffer, condition_region, branch_index);

	compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, INVALID_INSTR_INDEX);

	// 7. Now merge values from before the loop, last iteration and blocks with either a `break` or
	//    a `continue`

	{
		size_t snapshot_count = 3;
		LoopValuesSnapshot var_snapshots[4] = {};
		var_snapshots[0] = (LoopValuesSnapshot) {
			.entries = &original_var_values,
			.regions = &pre_loop_region,
			.count = 1,
		};
		var_snapshots[1] = (LoopValuesSnapshot) {
			.entries = current_loop_switch_state.break_var_values,
			.regions = current_loop_switch_state.break_regions,
			.count = current_loop_switch_state.break_count,
		};
		var_snapshots[2] = (LoopValuesSnapshot) {
			.entries = current_loop_switch_state.continue_var_values,
			.regions = current_loop_switch_state.continue_regions,
			.count = current_loop_switch_state.continue_count,
		};

		LoopValuesSnapshot arg_snapshots[4] = {};
		arg_snapshots[0] = (LoopValuesSnapshot) {
			.entries = &original_arg_values,
			.regions = &pre_loop_region,
			.count = 1,
		};
		arg_snapshots[1] = (LoopValuesSnapshot) {
			.entries = current_loop_switch_state.break_arg_values,
			.regions = current_loop_switch_state.break_regions,
			.count = current_loop_switch_state.break_count,
		};
		arg_snapshots[2] = (LoopValuesSnapshot) {
			.entries = current_loop_switch_state.continue_arg_values,
			.regions = current_loop_switch_state.continue_regions,
			.count = current_loop_switch_state.continue_count,
		};

		if (!final_region_finished) {
			snapshot_count = 4;

			var_snapshots[3] = (LoopValuesSnapshot) {
				.entries = &compiler->var_values,
				.regions = &condition_region,
				.count = 1,
			};

			arg_snapshots[3] = (LoopValuesSnapshot) {
				.entries = &compiler->arg_states,
				.regions = &condition_region,
				.count = 1,
			};
		}

		assert(array_size(var_snapshots) == array_size(arg_snapshots));
		assert(snapshot_count <= array_size(var_snapshots));

		_merge_variants(compiler,
				var_phis,
				compiler->var_count,
				var_snapshots,
				snapshot_count,
				var_filter,
				false);

		_merge_variants(compiler,
				arg_phis,
				arg_count,
				arg_snapshots,
				snapshot_count,
				arg_filter,
				false);
	}

	array_copy(original_var_values, var_phis, compiler->var_count);
	array_copy(original_arg_values, arg_phis, arg_count);

	compiler->var_values = original_var_values;
	compiler->arg_states = original_arg_values;

	// 9. post loop region

	InstrIndex post_loop_region = instr_new_region(instr_buffer, instr_allocator);

	branch->branch.true_region = body_block.initial_region;
	branch->branch.false_region = post_loop_region;

	// Now fix the jumps inserted by `break` and `continue` statements.
	_fix_loop_control_jumps(instr_buffer,
			compiler->loop_switch_state->control_flow_stmts,
			post_loop_region, 
			body_block.initial_region);

	arena_end_temp(temp);

	// Restore the previous loop state
	_restore_loop_switch_state(compiler);

	profile_scope_end();
	return post_loop_region;
}

static InstrIndex _compile_if_statement(FunctionCompiler* compiler,
		AstNode* node,
		InstrIndex region_instr_index) {

	profile_scope_start(__func__);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	// NOTE: How are branches compiled:
	//       
	//       Branches split the flow of the program into two possible paths,
	//       and at the end those two paths need to be merged back into one.
	//       
	//       For each alternative path we create a region, the branch instruction
	//       jumps to one of them, based on the condition.
	//       
	//       To merge these two paths we end both alternative paths with an
	//       unconditional jump to a third region. This third region is where
	//       the program flow continues further after the branch.
	InstrIndex branch_instr_index = instr_buffer_append(instr_buffer, instr_allocator);
	Instr* branch_instr = instr_buffer_at(instr_buffer, branch_instr_index);
	branch_instr->kind = INSTR_BRANCH;
	branch_instr->branch.condition = _compile_expr_to_bool(compiler, &node->if_stmt.condition);
	branch_instr->branch.io_state = compiler->io_state;

	compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, INVALID_INSTR_INDEX);

	InstrIndex post_branch_region_index = instr_new_region(instr_buffer, instr_allocator);

	CompiledBlockRegions true_block;
	CompiledBlockRegions false_block;

	{
		// NOTE: Here is the fun part: placing phi nodes
		//       We have an array where each variable stores it's currently
		//       assigned instruction (value).
		//       
		//       To decide where to place the phi nodes, we need to track
		//       which variables get assigned a new value during the compilation
		//       of each of the branch's alternative paths.
		//       
		//       This is done by creating copies of the original array of variable
		//       values for each branch path, then compiling the paths with the
		//       replaced arrays for variable values. In that way we gather newly
		//       assigned value and later are able to make a placement decision.

		ArenaRegion temp = arena_begin_temp(compiler->temp_allocator);

		InstrIndex* original_var_values = compiler->var_values;
		InstrIndex* original_arg_values = compiler->arg_states;

		// Create copies of variable value arrays
		InstrIndex* var_values_for_true_path = arena_alloc_array(compiler->temp_allocator,
				InstrIndex,
				compiler->var_count);

		arena_alloc_guard(compiler->temp_allocator);

		InstrIndex* var_values_for_false_path = arena_alloc_array(compiler->temp_allocator,
				InstrIndex,
				compiler->var_count);

		arena_alloc_guard(compiler->temp_allocator);

		array_copy(var_values_for_true_path, compiler->var_values, compiler->var_count);
		array_copy(var_values_for_false_path, compiler->var_values, compiler->var_count);

		size_t arg_count = compiler->function->proto.parameter_count;
		InstrIndex* arg_values_for_true_path = arena_alloc_array(compiler->temp_allocator,
				InstrIndex,
				arg_count);

		arena_alloc_guard(compiler->temp_allocator);

		InstrIndex* arg_values_for_false_path = arena_alloc_array(compiler->temp_allocator,
				InstrIndex,
				arg_count);

		arena_alloc_guard(compiler->temp_allocator);

		array_copy(arg_values_for_true_path, compiler->arg_states, arg_count);
		array_copy(arg_values_for_false_path, compiler->arg_states, arg_count);

		compiler->var_values = var_values_for_true_path;
		compiler->arg_states = arg_values_for_true_path;

		true_block = _compile_scope(compiler, node->if_stmt.true_scope);

		if (!instr_region_finished(instr_buffer, true_block.final_region)) {
			Instr* true_region = instr_buffer_at(instr_buffer, true_block.final_region);
			true_region->region.last_instr = instr_new_jump(instr_buffer,
					instr_allocator,
					post_branch_region_index,
					&compiler->io_state);
		} else if (compiler->io_state.value == INVALID_INSTR_INDEX.value) {
			compiler->io_state = instr_new_io_state(instr_buffer,
					instr_allocator,
					INVALID_INSTR_INDEX);
		}

		compiler->var_values = var_values_for_false_path;
		compiler->arg_states = arg_values_for_false_path;

		if (node->if_stmt.false_scope) {
			false_block = _compile_scope(compiler, node->if_stmt.false_scope);
		} else {
			InstrIndex false_region_index = instr_new_region(instr_buffer, instr_allocator);
			false_block.initial_region = false_region_index;
			false_block.final_region = false_region_index;
		}

		if (!instr_region_finished(instr_buffer, false_block.final_region)) {
			Instr* false_region = instr_buffer_at(instr_buffer, false_block.final_region);
			false_region->region.last_instr = instr_new_jump(instr_buffer,
					instr_allocator,
					post_branch_region_index,
					&compiler->io_state);
		} else if (compiler->io_state.value == INVALID_INSTR_INDEX.value) {
			compiler->io_state = instr_new_io_state(instr_buffer,
					instr_allocator,
					INVALID_INSTR_INDEX);
		}

		const Scope* if_parent_scope = node->parent_scope;
		for (size_t i = 0; i < compiler->var_count; i += 1) {
			if (compiler->vars[i] == NULL) {
				continue;
			}

			bool assigned_in_true_path =
				var_values_for_true_path[i].value != original_var_values[i].value;
			bool assigned_in_false_path =
				var_values_for_false_path[i].value != original_var_values[i].value;

			if (!assigned_in_true_path && !assigned_in_false_path) {
				continue;
			}

			InstrIndex phi = _create_phi_of_2_variants(compiler,
					var_values_for_true_path[i],
					true_block.final_region,
					var_values_for_false_path[i],
					false_block.final_region);

			original_var_values[i] = phi;
		}
		
		for (size_t i = 0; i < arg_count; i += 1) {
			bool assigned_in_true_path =
				arg_values_for_true_path[i].value != original_arg_values[i].value;
			bool assigned_in_false_path =
				arg_values_for_false_path[i].value != original_arg_values[i].value;

			if (!assigned_in_true_path && !assigned_in_false_path) {
				continue;
			}

			InstrIndex phi = _create_phi_of_2_variants(compiler,
					arg_values_for_true_path[i],
					true_block.final_region,
					arg_values_for_false_path[i],
					false_block.final_region);

			original_arg_values[i] = phi;
		}

		arena_end_temp(temp);

		// Reset back to the original array of values
		compiler->var_values = original_var_values;
		compiler->arg_states = original_arg_values;
	}

	branch_instr->branch.true_region = true_block.initial_region;
	branch_instr->branch.false_region = false_block.initial_region;

	instr_region_set_last(instr_buffer, region_instr_index, branch_instr_index);

	profile_scope_end();
	return post_branch_region_index;
}

static void _compile_statement(FunctionCompiler* compiler, AstNode* node) {
	profile_scope_start(__func__);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	switch (node->kind) {
	case AST_NODE_VARIABLE: {
		uint32_t var_id = node->variable.id;
		assert(var_id < compiler->var_count);
		assert(node->parent_scope);

		compiler->vars[var_id] = &node->variable;
		compiler->var_parent_scopes[var_id] = node->parent_scope;

		Type variable_type = node->variable.type;
		TypeLayout variable_type_layout = type_get_layout(
				compiler->type_context,
				&variable_type);

		if (node->variable.type.kind == TYPE_STRUCT || node->variable.type.kind == TYPE_UNION) {
			InstrIndex instr_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* instr = instr_buffer_at(instr_buffer, instr_index);
			instr->kind = INSTR_STACK_ALLOC;

			assert(variable_type_layout.size <= UINT32_MAX);
			assert(variable_type_layout.alignment <= UINT32_MAX);

			instr->stack_alloc.size = (uint32_t)variable_type_layout.size;
			instr->stack_alloc.alignment = (uint32_t)variable_type_layout.alignment;

			// Immediately store the new value instruction, since it will be used in the next
			// `_compile_address_of`
			compiler->var_values[var_id] = instr_index;

			if (node->variable.value) {
				Expr assignment_target = { 
					.kind = EXPR_VARIABLE_REFERENCE,
					.variable_ref = &node->variable
				};

				AddressExpr value_address = _compile_address_of(compiler, node->variable.value);

				_compile_assignment_of_compound_types(compiler,
						&assignment_target,
						value_address);
			}
		} else if (node->variable.type.kind == TYPE_ARRAY) {
			assert(variable_type.array.size != NULL);

			InstrIndex instr_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* instr = instr_buffer_at(instr_buffer, instr_index);
			instr->kind = INSTR_STACK_ALLOC;

			assert(variable_type_layout.size <= UINT32_MAX);
			assert(variable_type_layout.alignment <= UINT32_MAX);

			instr->stack_alloc.size = (uint32_t)variable_type_layout.size;
			instr->stack_alloc.alignment = (uint32_t)variable_type_layout.alignment;

			compiler->var_values[var_id] = instr_index;
		} else if (node->variable.value) {
			Type value_type;
			expr_get_type(node->variable.value, &value_type);

			InstrIndex value = _compile_expr(compiler, node->variable.value);

			// insert an implicit cast to the variable type
			compiler->var_values[var_id] = _compile_int_cast(compiler,
					&value_type,
					&node->variable.type,
					value);
		} else {
			assert(variable_type_layout.size <= 8);

			uint8_t bit_count_index = count_trailing_zeros(variable_type_layout.size);
			InstrIndex instr_index = instr_buffer_append(instr_buffer, instr_allocator);
			Instr* instr = instr_buffer_at(instr_buffer, instr_index);
			instr->kind = INSTR_UNINITIALIZED_8 + bit_count_index;

			compiler->var_values[var_id] = instr_index;
		}

		assert(compiler->var_values[var_id].value != INVALID_INSTR_INDEX.value);
		break;
	}
	case AST_NODE_EXPR:
		_compile_expr(compiler, &node->expr);
		break;
	default:
		unreachable();
	}

	profile_scope_end();
}

static void _compile_switch(FunctionCompiler* compiler,
		AstNode* stmt,
		InstrIndex* region_instr_index) {

	profile_scope_start(__func__);
	assert(stmt->kind == AST_NODE_SWITCH);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	LoopSwitchState current_loop_switch_state = (LoopSwitchState) {
		.parent = compiler->loop_switch_state,
		.control_flow_stmts = NULL,
		.node = stmt,
		.break_capacity = stmt->switch_stmt.break_count,
		.break_var_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex*,
				stmt->switch_stmt.break_count),
		.break_arg_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex*,
				stmt->switch_stmt.break_count),
		.break_regions = arena_alloc_array(compiler->temp_allocator,
				InstrIndex,
				stmt->switch_stmt.break_count),
		.continue_count = 0,
		.continue_capacity = 0,
		.continue_var_values = NULL,
		.continue_arg_values = NULL,
		.continue_regions = NULL,
	};

	compiler->loop_switch_state = &current_loop_switch_state;

	Type tested_expr_type;
	expr_get_type(stmt->switch_stmt.expr, &tested_expr_type);

	InstrIndex tested_expr = _compile_expr(compiler, stmt->switch_stmt.expr);

	if (type_kind_is_int(tested_expr_type.kind)) {
		TypeLayout layout = type_get_layout(compiler->type_context, &tested_expr_type);

		bool is_unsigned = has_flag(tested_expr_type.kind, (TypeKind)TYPE_FLAG_UNSIGNED);
		tested_expr = instr_new_cast(instr_buffer,
				instr_allocator,
				tested_expr,
				layout.size,
				compiler->type_context->pointer_type_layout.size,
				!is_unsigned);
	}

	ArenaRegion temp = arena_begin_temp(compiler->temp_allocator);

	InstrIndex initial_region_index = *region_instr_index;
	InstrIndex true_region_index = initial_region_index;
	InstrIndex false_region_index = initial_region_index;

	InstrIndex default_case_region = INVALID_INSTR_INDEX;

	size_t arg_count = compiler->function->proto.parameter_count;
	size_t var_count = compiler->var_count;

	InstrIndex* initial_arg_values = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			arg_count);
	InstrIndex* initial_var_values = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			var_count);

	array_copy(initial_arg_values, compiler->arg_states, arg_count);
	array_copy(initial_var_values, compiler->var_values, var_count);

	AstNode* first_body_node = stmt->switch_stmt.body->nodes.first;
	for (AstNode* child = first_body_node; child != NULL; child = child->next) {
		if (child->kind == AST_NODE_CASE) {
			InstrIndex new_true_region = instr_new_region(instr_buffer, instr_allocator);
			InstrIndex new_false_region = instr_new_region(instr_buffer, instr_allocator);

			bool fallthrough_from_previous_possible = false;
			if (true_region_index.value == false_region_index.value) {
				// `true_region_index` and `false_region_index` are equal it means, this is the
				// first switch case to be compiled and thus a fallthrough from the previous case is
				// not possible, because there isn't one.
			} else {
				// If the previous region hasn't been terminated by a jump or a return, the
				// fallthrough is possible
				fallthrough_from_previous_possible = !instr_region_finished(
						instr_buffer,
						true_region_index);
			}

			// NOTE: Fallthrough
			if (fallthrough_from_previous_possible) {
				InstrIndex jump_to_current = instr_new_jump(instr_buffer,
						instr_allocator,
						new_true_region,
						&compiler->io_state);

				instr_region_set_last(instr_buffer, true_region_index, jump_to_current);
			}

			if (fallthrough_from_previous_possible) {
				for (size_t i = 0; i < var_count; i += 1) {
					if (initial_var_values[i].value == compiler->var_values[i].value) {
						continue;
					}

					InstrIndex phi_index = _create_phi_of_2_variants(compiler,
							initial_var_values[i],
							false_region_index,
							compiler->var_values[i],
							true_region_index);

					compiler->var_values[i] = phi_index;
				}

				for (size_t i = 0; i < arg_count; i += 1) {
					if (initial_arg_values[i].value == compiler->arg_states[i].value) {
						continue;
					}

					InstrIndex phi_index = _create_phi_of_2_variants(compiler,
							initial_arg_values[i],
							false_region_index,
							compiler->arg_states[i],
							true_region_index);

					compiler->arg_states[i] = phi_index;
				}
			} else {
				array_copy(compiler->arg_states, initial_arg_values, arg_count);
				array_copy(compiler->var_values, initial_var_values, var_count);
			}

			bool is_default_case = child->case_stmt.value == NULL;
			if (is_default_case) {
				// NOTE: If the control flow is comming from a different switch case, just jump over
				//       the `default` case part. After going through all of the cases, and if the
				//       tested expression doesn't match any of them, we can safely jump here and
				//       execute the `default` case.
				InstrIndex threaded_jump = instr_new_jump(instr_buffer,
						instr_allocator,
						new_false_region,
						&compiler->io_state);

				assert(!instr_region_finished(instr_buffer, false_region_index));
				instr_region_set_last(instr_buffer, false_region_index, threaded_jump);
			} else {
				Type case_value_type;
				expr_get_type(child->case_stmt.value, &case_value_type);

				InstrIndex case_value_instr = _compile_expr(compiler, child->case_stmt.value);

				if (type_kind_is_int(case_value_type.kind)) {
					TypeLayout layout = type_get_layout(compiler->type_context, &case_value_type);

					bool is_unsigned = has_flag(case_value_type.kind, (TypeKind)TYPE_FLAG_UNSIGNED);
					case_value_instr = instr_new_cast(instr_buffer,
							instr_allocator,
							case_value_instr,
							layout.size,
							compiler->type_context->pointer_type_layout.size,
							!is_unsigned);
				}

				InstrIndex compare_index = instr_buffer_push(instr_buffer,
					instr_allocator,
					(Instr) {
						.kind = INSTR_COMPARE_64,
						.compare = {
							.kind = INSTR_CMP_EQUAL,
							.left = tested_expr,
							.right = case_value_instr,
						}
					});

				assert(compiler->io_state.value != INVALID_INSTR_INDEX.value);

				InstrIndex branch_index = instr_buffer_push(instr_buffer,
					instr_allocator,
					(Instr) {
					.kind = INSTR_BRANCH,
					.branch = {
						.condition = compare_index,
						.true_region = new_true_region,
						.false_region = new_false_region,
						.io_state = compiler->io_state,
					}
				});

				compiler->io_state = instr_new_io_state(instr_buffer,
						instr_allocator,
						INVALID_INSTR_INDEX);

				assert(!instr_region_finished(instr_buffer, false_region_index));
				instr_region_set_last(instr_buffer, false_region_index, branch_index);
			}

			true_region_index = new_true_region;
			false_region_index = new_false_region;

			if (is_default_case) {
				default_case_region = true_region_index;
			}
		} else {
			_compile_single_node(compiler, child, &true_region_index);

			if (child->kind == AST_NODE_RETURN || compiler->io_state.value == INVALID_INSTR_INDEX.value) {
				// NOTE: Return statement compilation consumes the `io_state` and leaves an invalid
				//       one behind, however we will definitely need a valid `io_state` to finish
				//       other switch cases and the post switch statement code. So here, we just
				//       create an empty one. Creating one here, gives us a valid `io_state` to work
				//       with, while keeping us independent from control flow that was terminated by
				//       the return in the previous case.
				assert(compiler->io_state.value == INVALID_INSTR_INDEX.value);
				compiler->io_state = instr_new_io_state(instr_buffer,
						instr_allocator,
						INVALID_INSTR_INDEX);
			}
		}
	}

	_reset_variables_in_scope(compiler, stmt->switch_stmt.body);

	{
		bool fallthrough_possible = false;

		if (true_region_index.value != false_region_index.value) {
			fallthrough_possible = !instr_region_finished(
					instr_buffer,
					true_region_index);
		}

		size_t var_count = compiler->var_count;
		InstrIndex* current_var_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex, 
				var_count);
		InstrIndex* current_arg_values = arena_alloc_array(compiler->temp_allocator,
				InstrIndex,
				arg_count);

		array_copy(current_var_values, compiler->var_values, compiler->var_count);
		array_copy(current_arg_values, compiler->arg_states, arg_count);

		BitArray var_filter = _reserve_var_phis(compiler,
				compiler->temp_allocator,
				compiler->var_values);
		BitArray arg_filter = _reserve_arg_phis(compiler,
				compiler->temp_allocator,
				compiler->arg_states);


		size_t snapshot_count = 2;
		LoopValuesSnapshot var_snapshots[3] = {};
		var_snapshots[0] = (LoopValuesSnapshot) {
			.entries = &initial_var_values,
			.regions = &initial_region_index,
			.count = 1,
		};
		var_snapshots[1] = (LoopValuesSnapshot) {
			.entries = current_loop_switch_state.break_var_values,
			.regions = current_loop_switch_state.break_regions,
			.count = current_loop_switch_state.break_count,
		};

		LoopValuesSnapshot arg_snapshots[3] = {};
		arg_snapshots[0] = (LoopValuesSnapshot) {
			.entries = &initial_arg_values,
			.regions = &initial_region_index,
			.count = 1,
		};
		arg_snapshots[1] = (LoopValuesSnapshot) {
			.entries = current_loop_switch_state.break_arg_values,
			.regions = current_loop_switch_state.break_regions,
			.count = current_loop_switch_state.break_count,
		};

		if (fallthrough_possible) {
			// Also merge var/arg values from the previous case
			snapshot_count = 3;
			var_snapshots[2] = (LoopValuesSnapshot) {
				.entries = &current_var_values,
				.regions = &true_region_index,
				.count = 1,
			};

			arg_snapshots[2] = (LoopValuesSnapshot) {
				.entries = &current_arg_values,
				.regions = &true_region_index,
				.count = 1,
			};
		}

		assert(array_size(var_snapshots) == array_size(arg_snapshots));
		assert(snapshot_count <= array_size(var_snapshots));

		_reserve_phis(compiler, var_filter, compiler->var_values, compiler->var_values, var_count);
		_reserve_phis(compiler, arg_filter, compiler->arg_states, compiler->arg_states, arg_count);

		// Merge
		_merge_variants(compiler,
				compiler->var_values,
				var_count,
				var_snapshots,
				snapshot_count,
				var_filter,
				true);
		_merge_variants(compiler,
				compiler->arg_states,
				arg_count,
				arg_snapshots,
				snapshot_count,
				arg_filter,
				true);
	}

	maybe(true_region_index.value == false_region_index.value);

	InstrIndex post_switch_region_index = instr_new_region(instr_buffer, instr_allocator);

	_fix_loop_control_jumps(instr_buffer,
			compiler->loop_switch_state->control_flow_stmts,
			post_switch_region_index,
			INVALID_INSTR_INDEX);

	if (!instr_region_finished(instr_buffer, true_region_index)) {
		InstrIndex jump = instr_new_jump(instr_buffer,
				instr_allocator,
				post_switch_region_index,
				&compiler->io_state);

		instr_region_set_last(instr_buffer, true_region_index, jump);
	}

	if (!instr_region_finished(instr_buffer, false_region_index)) {
		// If this `switch` has a default case, we jump to there instead, otherwise leave the 
		// `switch` statement by jumping to the `post_switch_region_index`.
		InstrIndex jump = instr_new_jump(instr_buffer,
				instr_allocator,
				default_case_region.value == INVALID_INSTR_INDEX.value
					? post_switch_region_index
					: default_case_region,
				&compiler->io_state);

		instr_region_set_last(instr_buffer, false_region_index, jump);
	}

	*region_instr_index = post_switch_region_index;

	_restore_loop_switch_state(compiler);

	arena_end_temp(temp);
	profile_scope_end();
}

static void _compile_single_node(FunctionCompiler* compiler,
		AstNode* node,
		InstrIndex* region_instr_index) {

	profile_scope_start(__func__);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	Instr* region_instr = instr_buffer_at(instr_buffer, *region_instr_index);

	switch (node->kind) {
	case AST_NODE_VARIABLE:
		_compile_statement(compiler, node);
		break;
	case AST_NODE_IF:
		*region_instr_index = _compile_if_statement(compiler, node, *region_instr_index);
		break;
	case AST_NODE_BLOCK: {
		InstrIndex jump_to_inner_region = instr_new_jump(instr_buffer,
				instr_allocator,
				INVALID_INSTR_INDEX,
				&compiler->io_state);

		CompiledBlockRegions inner_block = _compile_scope(compiler, &node->block);

		Instr* jump_to_inner = instr_buffer_at(instr_buffer, jump_to_inner_region);
		jump_to_inner->jump.target_region = inner_block.initial_region;
		region_instr->region.last_instr = jump_to_inner_region;

		if (!instr_region_finished(instr_buffer, inner_block.final_region)) {
			InstrIndex post_block_region = instr_new_region(instr_buffer, instr_allocator);
			InstrIndex jump_to_post_block_region = instr_new_jump(instr_buffer,
					instr_allocator,
					post_block_region,
					&compiler->io_state);

			Instr* inner_region_instr = instr_buffer_at(instr_buffer, inner_block.final_region);
			inner_region_instr->region.last_instr = jump_to_post_block_region;

			*region_instr_index = post_block_region;
		} else {
			*region_instr_index = inner_block.final_region;
		}

		break;
	}
	case AST_NODE_RETURN: {
		bool should_return_value = compiler->function->proto.return_type.kind != TYPE_VOID;

		if (should_return_value) {
			assert(node->return_stmt.value != NULL);

			InstrIndex value = _compile_expr(compiler, node->return_stmt.value);

			Type return_type = compiler->function->proto.return_type;
			TypeLayout return_type_layout = type_get_layout(compiler->type_context,
					&return_type);

			bool is_compound_type = return_type.kind == TYPE_STRUCT
				                 || return_type.kind == TYPE_UNION;

			if (is_compound_type && return_type_layout.size <= 8) {
				InstrIndex stack_addr_index = instr_buffer_append(instr_buffer, instr_allocator);
				Instr* stack_addr = instr_buffer_at(instr_buffer, stack_addr_index);
				stack_addr->kind = INSTR_STACK_ADDR;
				stack_addr->stack_addr.stack_alloc = value;

				InstrIndex load_index = instr_buffer_append(instr_buffer, instr_allocator);
				Instr* load = instr_buffer_at(instr_buffer, load_index);
				load->kind = INSTR_PTR_LOAD_64;
				load->ptr_load.ptr = stack_addr_index;
				load->ptr_load.io_state = compiler->io_state;

				compiler->io_state = instr_new_io_state(instr_buffer,
						instr_allocator,
						load_index);

				value = load_index;
			}

			region_instr->region.last_instr = instr_new_return_value(instr_buffer,
					instr_allocator,
					value,
					&compiler->io_state);
			compiler->io_state = INVALID_INSTR_INDEX;
		} else {
			assert(node->return_stmt.value == NULL);

			InstrIndex instr_index = instr_new_return(instr_buffer,
					instr_allocator,
					&compiler->io_state);

			region_instr->region.last_instr = instr_index;
		}
		break;
	}
	case AST_NODE_WHILE_LOOP:
		if (node->while_loop.condition_kind == WHILE_LOOP_PRE_CONDITION) {
			*region_instr_index = _compile_loop(compiler,
					*region_instr_index,
					node,
					NULL,
					&node->while_loop.condition,
					NULL);
		} else  if (node->while_loop.condition_kind == WHILE_LOOP_POST_CONDITION) {
			*region_instr_index = _compile_do_while_loop(compiler, *region_instr_index, node);
		} else {
			unreachable();
		}
		break;
	case AST_NODE_FOR_LOOP:
		*region_instr_index = _compile_for_loop(compiler, *region_instr_index, node);
		break;
	case AST_NODE_BREAK:
	case AST_NODE_CONTINUE: {
		LoopSwitchState* state = NULL;
		if (node->kind == AST_NODE_BREAK) {
			state = compiler->loop_switch_state;
			assert_msg(compiler->loop_switch_state,
					"`break` statement appears outside of a loop or a switch");
			assert(compiler->loop_switch_state->node);
		} else if (node->kind == AST_NODE_CONTINUE) {
			state = _get_current_loop_state(compiler);
			assert_msg(state,
					"`break` statement appears outside of a loop");
		}

		assert(state);
		assert(state->node);

		InstrIndex jump = instr_new_jump(instr_buffer,
				instr_allocator,
				INVALID_INSTR_INDEX,
				&compiler->io_state);

		region_instr->region.last_instr = jump;

		ControlFlowStmt* control = _alloc_control_flow_stmt(compiler);
		control->kind = node->kind == AST_NODE_BREAK
			? CONTROL_FLOW_BREAK
			: CONTROL_FLOW_CONTINUE;
		control->region = *region_instr_index;

		size_t arg_count = compiler->function->proto.parameter_count;

		array_copy(control->var_values, compiler->var_values, compiler->var_count);
		array_copy(control->arg_values, compiler->arg_states, arg_count);

		if (node->kind == AST_NODE_BREAK) {
			assert(state->break_count < state->break_capacity);

			state->break_var_values[state->break_count] = control->var_values;
			state->break_arg_values[state->break_count] = control->arg_values;
			state->break_regions[state->break_count] = *region_instr_index;
			state->break_count += 1;
		} else if (node->kind == AST_NODE_CONTINUE) {
			assert(state->continue_count < state->continue_capacity);

			state->continue_var_values[state->continue_count] = control->var_values;
			state->continue_arg_values[state->continue_count] = control->arg_values;
			state->continue_regions[state->continue_count] = *region_instr_index;
			state->continue_count += 1;
		}

		control->next = state->control_flow_stmts;
		state->control_flow_stmts = control;
		break;
	}
	case AST_NODE_EXPR: 
		_compile_statement(compiler, node);
		break;
	case AST_NODE_TYPE_DEF:
	case AST_NODE_STRUCT:
	case AST_NODE_UNION:
	case AST_NODE_ENUM:
		break;
	case AST_NODE_FUNCTION_DEF:
	case AST_NODE_FUNCTION_DECL:
		panic("Function is not allowed here");
	case AST_NODE_SWITCH:
		_compile_switch(compiler, node, region_instr_index);
		break;
	case AST_NODE_CASE:
		unreachable();
	}

	profile_scope_end();
}

static void _reset_variables_in_scope(FunctionCompiler* compiler, const Scope* scope) {
	for (size_t i = 0; i < compiler->var_count; i += 1) {
		if (compiler->vars[i] == NULL) {
			continue;
		}

		const Scope* var_parent_scope = compiler->var_parent_scopes[i];
		assert(var_parent_scope);
		// assert(compiler->var_values[i].value != INVALID_INSTR_INDEX.value);

		if (var_parent_scope->id >= scope->id) {
			compiler->vars[i] = NULL;
			compiler->var_parent_scopes[i] = NULL;
			compiler->var_values[i] = INVALID_INSTR_INDEX;
		}
	}
}

static CompiledBlockRegions _compile_scope(FunctionCompiler* compiler, Scope* scope) {
	profile_scope_start(__func__);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	InstrIndex initial_region = instr_new_region(instr_buffer, instr_allocator);
	InstrIndex region_instr_index = initial_region;

	AstNode* first_node = scope->nodes.first;
	for (AstNode* node = first_node; node != NULL; node = node->next) {
		if (instr_region_finished(instr_buffer, region_instr_index)) {
			break;
		}

		_compile_single_node(compiler, node, &region_instr_index);
	}

	_reset_variables_in_scope(compiler, scope);

	CompiledBlockRegions regions;
	regions.initial_region = initial_region;
	regions.final_region = region_instr_index;

	profile_scope_end();
	return regions;
}

static void _fill_function_call_signatures(FunctionCompiler* compiler) {
	profile_scope_start(__func__);

	for (size_t call_index = 0; call_index < compiler->function_call_count; call_index += 1) {
		Call* call = compiler->function_calls[call_index];

		Type callable_type;
		expr_get_type(call->callable, &callable_type);

		assert(callable_type.kind == TYPE_POINTER);
		assert(callable_type.pointer_base_type->kind == TYPE_FUNCTION);

		const FunctionPrototype* prototype = callable_type.pointer_base_type->function;

		if (!prototype->has_va_args) {
			compiler->function_call_signatures[call_index] = function_prototype_to_abi_signature(
					compiler->type_context,
					prototype,
					arena_allocator_new(compiler->allocator));
		} else {
			ArenaRegion temp = arena_begin_temp(compiler->temp_allocator);

			FunctionPrototype extended_prototype = *prototype;

			FunctionParam* parameters = arena_alloc_array(compiler->temp_allocator,
					FunctionParam,
					call->args.count);

			size_t i = 0;
			for (; i < prototype->parameter_count; i += 1) {
				parameters[i] = prototype->parameters[i];
			}

			for (; i < call->args.count; i += 1) {
				parameters[i] = (FunctionParam) {};
				expr_get_type(call->args.exprs[i], &parameters[i].type);
			}

			extended_prototype.parameters = parameters;
			extended_prototype.parameter_count = call->args.count;

			compiler->function_call_signatures[call_index] = function_prototype_to_abi_signature(
					compiler->type_context,
					&extended_prototype,
					arena_allocator_new(compiler->allocator));

			arena_end_temp(temp);
		}
	}

	profile_scope_end();
}

static InstrIndex _create_arg_load_instr(FunctionCompiler* compiler,
		size_t arg_index,
		size_t size,
		bool is_in_register) {

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	InstrIndex load_arg_index = instr_buffer_append(instr_buffer, instr_allocator);
	Instr* load_arg = instr_buffer_at(instr_buffer, load_arg_index);
	load_arg->load_arg.index = (uint8_t)arg_index;

	if (is_in_register) {
		switch (size) {
		case 1:
			load_arg->kind = INSTR_LOAD_ARG_8;
			break;
		case 2:
			load_arg->kind = INSTR_LOAD_ARG_16;
			break;
		case 4:
			load_arg->kind = INSTR_LOAD_ARG_32;
			break;
		case 8:
			load_arg->kind = INSTR_LOAD_ARG_64;
			break;
		default:
			unreachable();
		}

		return load_arg_index;
	}

	load_arg->kind = INSTR_LOAD_ARG_64;

	InstrIndex stack_addr_index = instr_buffer_append(instr_buffer, instr_allocator);
	Instr* stack_addr = instr_buffer_at(instr_buffer, stack_addr_index);
	stack_addr->kind = INSTR_STACK_ADDR;
	stack_addr->stack_addr.stack_alloc = load_arg_index;

	InstrIndex ptr_load_index = instr_buffer_append(instr_buffer, instr_allocator);
	Instr* ptr_load = instr_buffer_at(instr_buffer, ptr_load_index);
	ptr_load->ptr_load.ptr = stack_addr_index;
	ptr_load->ptr_load.io_state = compiler->io_state;

	switch (size) {
	case 1:
		ptr_load->kind = INSTR_PTR_LOAD_8;
		break;
	case 2:
		ptr_load->kind = INSTR_PTR_LOAD_16;
		break;
	case 4:
		ptr_load->kind = INSTR_PTR_LOAD_32;
		break;
	case 8:
		ptr_load->kind = INSTR_PTR_LOAD_64;
		break;
	default:
		unreachable();
	}

	compiler->io_state = instr_new_io_state(instr_buffer,
			instr_allocator,
			ptr_load_index);

	return ptr_load_index;
}

static void _compile_argument_loads(FunctionCompiler* compiler) {
	profile_scope_start(__func__);

	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	AbiSignature signature = function_prototype_to_abi_signature(compiler->type_context,
			&compiler->function->proto,
			arena_allocator_new(compiler->temp_allocator));
	
	size_t arg_index = 0;
	for (uint32_t param_index = 0; param_index < signature.param_count; param_index += 1) {
		AbiParam abi_param = signature.params[param_index];

		if (abi_param.kind == ABI_PARAM_RETURN_LOCATION) {
			continue;
		}

		assert(abi_param.kind == ABI_PARAM_NORMAL || abi_param.kind == ABI_PARAM_STRUCT);

		const TypeContext* type_context = compiler->type_context;
		const FunctionParam* param = &compiler->function->proto.parameters[arg_index];

		TypeLayout param_type_layout;
		if (param->type.kind == TYPE_ARRAY) {
			param_type_layout = type_context->pointer_type_layout;
		} else {
			param_type_layout = type_get_layout(type_context, &param->type);
		}

		if (param->type.kind == TYPE_STRUCT || param->type.kind == TYPE_UNION) {
			if (param_type_layout.size > 8) {
				InstrIndex load_arg_index = _create_arg_load_instr(compiler,
						arg_index, 8, param_index < 4);

				if (param_index >= 4 && false) {
					InstrIndex ptr_load_index = instr_buffer_append(instr_buffer, instr_allocator);
					Instr* ptr_load = instr_buffer_at(instr_buffer, ptr_load_index);
					ptr_load->kind = INSTR_PTR_LOAD_64;
					ptr_load->ptr_load.ptr = load_arg_index;
					ptr_load->ptr_load.io_state = compiler->io_state;

					compiler->io_state = instr_new_io_state(instr_buffer,
							instr_allocator,
							ptr_load_index);

					load_arg_index = ptr_load_index;
				}

				compiler->arg_states[arg_index] = load_arg_index;
			} else {
				InstrIndex load_arg_index = _create_arg_load_instr(compiler,
						arg_index, 8, param_index < 4);

				InstrIndex stack_alloc_index = instr_buffer_append(instr_buffer, instr_allocator);
				Instr* stack_alloc = instr_buffer_at(instr_buffer, stack_alloc_index);
				stack_alloc->kind = INSTR_STACK_ALLOC;
				stack_alloc->stack_alloc.size = (uint16_t)max(
						param_type_layout.size,
						type_context->pointer_type_layout.size);

				stack_alloc->stack_alloc.alignment = (uint16_t)max(
						param_type_layout.alignment,
						type_context->pointer_type_layout.alignment);

				InstrIndex stack_addr_index = instr_buffer_append(instr_buffer, instr_allocator);
				Instr* stack_addr = instr_buffer_at(instr_buffer, stack_addr_index);
				stack_addr->kind = INSTR_STACK_ADDR;
				stack_addr->stack_addr.stack_alloc = stack_alloc_index;

				InstrIndex ptr_store_index = instr_buffer_append(instr_buffer, instr_allocator);
				Instr* ptr_store = instr_buffer_at(instr_buffer, ptr_store_index);
				ptr_store->kind = INSTR_PTR_STORE_64;
				ptr_store->ptr_store.ptr = stack_addr_index;
				ptr_store->ptr_store.value = load_arg_index;
				ptr_store->ptr_store.io_state = compiler->io_state;

				compiler->io_state = instr_new_io_state(instr_buffer,
						instr_allocator,
						ptr_store_index);

				compiler->arg_states[arg_index] = stack_alloc_index;
			}
		} else {
			InstrIndex load_arg_index = _create_arg_load_instr(compiler,
					arg_index,
					param_type_layout.size,
					param_index < 4);

			compiler->arg_states[arg_index] = load_arg_index;
		}

		arg_index += 1;
	}

	profile_scope_end();
}

CompiledFunction function_compiler_compile(FunctionCompiler* compiler) {
	profile_scope_start(__func__);

	const Scope* body = compiler->function->body;
	assert(body);

	compiler->loop_switch_state = NULL;

	ArenaRegion temp = arena_begin_temp(compiler->temp_allocator);

	// Allocate var states buffer
	compiler->var_count = compiler->function->var_count;
	compiler->vars = arena_alloc_array_zeroed(compiler->temp_allocator,
			const Variable*,
			compiler->var_count);
	compiler->var_values = arena_alloc_array(compiler->temp_allocator,
			InstrIndex,
			compiler->var_count);
	compiler->var_parent_scopes = arena_alloc_array_zeroed(compiler->temp_allocator,
			const Scope*,
			compiler->var_count);

	for (size_t i = 0; i < compiler->var_count; i += 1) {
		compiler->var_values[i] = INVALID_INSTR_INDEX;
	}

	compiler->function_call_count = 0;
	compiler->function_call_signatures = arena_alloc_array(compiler->allocator,
			AbiSignature,
			compiler->function->function_call_count);

	compiler->function_calls = arena_alloc_array(compiler->temp_allocator,
			Call*,
			compiler->function->function_call_count);

	// Allocate`arg_states` buffer
	compiler->arg_states = arena_alloc_array(compiler->allocator,
			InstrIndex,
			compiler->function->proto.parameter_count);

	// Init `InstrBuffer`
	InstrBuffer* instr_buffer = &compiler->instr_buffer;
	Arena* instr_allocator = compiler->instr_allocator;

	instr_buffer_init(instr_buffer, instr_allocator);

	// Create the initial `io_state`
	compiler->io_state = instr_new_io_state(instr_buffer, instr_allocator, INVALID_INSTR_INDEX);

	// Setup initial `INSTR_LOAD_ARG`.
	//
	// NOTE: This has to happen after the initial `io_state` has been set up. Since argument loads
	//       might use memory loads that requires a valid `io_state`.
	_compile_argument_loads(compiler);

	CompiledBlockRegions body_block = _compile_scope(compiler, compiler->function->body);

	assert(compiler->loop_switch_state == NULL);

	// Free the loop control staff, since it is no longer needed
	_free_all_control_flow_stmts(compiler);

	if (compiler->function->proto.return_type.kind == TYPE_VOID) {
		InstrIndex final_region = body_block.final_region;
		if (!instr_region_finished(instr_buffer, final_region)) {
			Instr* region_instr = instr_buffer_at(instr_buffer, final_region);
			assert(region_instr->region.last_instr.value == INVALID_INSTR_INDEX.value);
			assert_msg(compiler->io_state.value != INVALID_INSTR_INDEX.value,
					"The final region of the function is still unfinished, "
					"which means the `io_state` must still be valid, until it "
					"gets consumed by a control instruction");

			InstrIndex final_return_index = instr_new_return(instr_buffer,
					instr_allocator,
					&compiler->io_state);

			region_instr->region.last_instr = final_return_index;
		}
	}

	assert_msg(compiler->io_state.value == INVALID_INSTR_INDEX.value,
			"`compiler->io_state` should have been consumed during the compilation "
			"of the final region in the function body");

	_fill_function_call_signatures(compiler);

	CompiledFunction compiled_function;
	compiled_function.instr_buffer = compiler->instr_buffer;
	compiled_function.start_region = body_block.initial_region;
	compiled_function.function_call_signatures = compiler->function_call_signatures;
	compiled_function.function_call_signature_count = compiler->function_call_count;

	arena_end_temp(temp);
	profile_scope_end();
	return compiled_function;
}

static int _internal_assert(uint64_t predicate) {
	assert(predicate);
	return 0;
}

static int _internal_print_string(const char* string) {
	printf("%s\n", string);
	return 0;
}

static void _internal_panic(const char* message) {
	panic(message);
}

static uint64_t _internal_identity(uint64_t value) {
	return value;
}

static uint64_t _internal_store_u64(uint64_t* out) {
	*out = 16;
	return 0;
}

static FILE* _internal_fopen(const char* path, const char* mode) {
	FILE* f = NULL;
	errno_t error = fopen_s(&f, path, mode);

	if (!f || error == EINVAL) {
		return NULL;
	}

	return f;
}

void compiler_resolve_default_func_refs(SymbolMap* map) {
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("assert"), _internal_assert);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("print_string"), _internal_print_string);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("printf"), printf);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("panic"), _internal_panic);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("identity"), _internal_identity);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("store_u64"), _internal_identity);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("fopen"), _internal_fopen);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("fclose"), fclose);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("fread"), fread);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("fwrite"), fwrite);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("ftell"), ftell);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("fseek"), fseek);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("malloc"), malloc);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("free"), free);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("exit"), exit);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("strlen"), strlen);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("strncmp"), strncmp);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("memcpy"), memcpy);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("memset"), memset);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("memcmp"), memcmp);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("memchr"), memchr);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("tolower"), tolower);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("isspace"), isspace);

	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("VirtualAlloc"), VirtualAlloc);
	symbol_map_insert_dynamically_linked_impl(map, STR_LIT("VirtualFree"), VirtualFree);
}

void compiler_create_function_import_symbol(const Function* function, Symbol* out_symbol) {
	out_symbol->name = function->proto.name;

	if (function->is_inline) {
		assert(function->decl_spec == NULL);
		out_symbol->linkage = SYMBOL_LINKAGE_INTERNAL;
	} else if (function->storage_specifier == STORAGE_SPEC_STATIC) {
		out_symbol->linkage = SYMBOL_LINKAGE_INTERNAL;
	} else if (function->decl_spec && function->decl_spec->kind == DECL_SPEC_DLL_IMPORT) {
		out_symbol->linkage = SYMBOL_LINKAGE_EXTERNAL_DYNAMIC;
	} else {
		out_symbol->linkage = SYMBOL_LINKAGE_EXTERNAL_STATIC;
	}
}

void compiler_collect_imported_symbols(const AST* ast, SymbolMap* imported_symbols) {
	profile_scope_start(__func__);

	for (const AstNode* node = ast->root_nodes.first; node != NULL; node = node->next) {
		if (node->kind != AST_NODE_FUNCTION_DEF && node->kind != AST_NODE_FUNCTION_DECL) {
			continue;
		}

		const Function* function = node->function_def;

		Symbol symbol = {};
		compiler_create_function_import_symbol(function, &symbol);

		SymbolId id = symbol_map_find(imported_symbols, symbol_key_from_symbol(&symbol));

		if (id != SYMBOL_ID_INVALID) {
			// TODO: Verify that the existing symbol is the same as this one
			continue;
		}

		id = symbol_map_insert(imported_symbols, &symbol);

		assert_msg(id != SYMBOL_ID_INVALID,
				"Duplicate function symbol. Did the parser miss the redefinition?");
	}

	profile_scope_end();
}

AbiSignature function_prototype_to_abi_signature(const TypeContext* type_context,
		const FunctionPrototype* proto,
		Allocator allocator) {
	AbiSignature sig = {};

	switch (proto->calling_convention) {
	case FUNC_CALL_CONV_CDECL:
		sig.call_conv = CALL_CONV_CDECL;
		break;
	default:
		unreachable();
	}

	bool has_return_loc = false;

	size_t return_type_size = type_get_layout(type_context, &proto->return_type).size;
	bool returns_compound = proto->return_type.kind == TYPE_STRUCT
		|| proto->return_type.kind == TYPE_UNION;

	if (proto->return_type.kind == TYPE_VOID) {
		sig.returns = NULL;
	} else if (proto->return_type.kind == TYPE_ARRAY) {
		panic("Arrays are not allowed as return types");
	} else if (returns_compound && return_type_size > 8) {
		sig.returns = allocator_alloc(allocator, AbiParam);
		*sig.returns = (AbiParam) {
			.kind = ABI_PARAM_STRUCT,
			.struct_size = (uint32_t)type_get_layout(type_context, &proto->return_type).size,
		};

		has_return_loc = true;
	} else {
		assert(return_type_size <= 8);

		sig.returns = allocator_alloc(allocator, AbiParam);
		*sig.returns = (AbiParam) { .kind = ABI_PARAM_NORMAL };
	}

	sig.param_count = (uint32_t)proto->parameter_count;
	if (has_return_loc) {
		sig.param_count += 1;
	}

	sig.params = allocator_alloc_array(allocator, AbiParam, sig.param_count);

	size_t param_index = 0;
	if (has_return_loc) {
		sig.params[0] = (AbiParam) {
			.kind = ABI_PARAM_RETURN_LOCATION,
		};

		param_index += 1;
	}

	for (size_t i = 0; i < proto->parameter_count; i += 1, param_index += 1) {
		Type param_type = proto->parameters[i].type;

		assert(type_get_layout(type_context, &param_type).size > 0);

		AbiParam abi_param = {};
		if (param_type.kind == TYPE_VOID) {
			unreachable();
		} else if (param_type.kind == TYPE_ARRAY) {
			abi_param = (AbiParam) { .kind = ABI_PARAM_NORMAL };
		} else if (param_type.kind == TYPE_STRUCT || param_type.kind == TYPE_UNION) {
			abi_param = (AbiParam) {
				.kind = ABI_PARAM_STRUCT,
				.struct_size = (uint32_t)type_get_layout(type_context, &param_type).size,
			};
		} else {
			abi_param = (AbiParam) { .kind = ABI_PARAM_NORMAL };
		}

		sig.params[param_index] = abi_param;
	}

	return sig;
}
