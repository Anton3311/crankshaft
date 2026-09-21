#include "ast.h"

TypeLayout type_get_layout(const TypeContext* context, const Type* type) {
	switch (type->kind) {
	case TYPE_VOID:
		return type_layout_new(0, 0);

	case TYPE_CHAR:
	case TYPE_SIGNED_CHAR:
	case TYPE_UNSIGNED_CHAR:
	case TYPE_INT8:
	case TYPE_SIGNED_INT8:
	case TYPE_UNSIGNED_INT8:
		return type_layout_new(1, 1);
	case TYPE_SHORT:
	case TYPE_SIGNED_SHORT:
	case TYPE_UNSIGNED_SHORT:
	case TYPE_INT16:
	case TYPE_SIGNED_INT16:
	case TYPE_UNSIGNED_INT16:
		return type_layout_new(2, 2);

	// NOTE: Enum is implicitely castable to an int.
	// TODO: Return the size of a corresponding int, not simply `TYPE_INT`. The enum can be larger
	//       then `TYPE_INT`.
	case TYPE_ENUM:

	case TYPE_INT:
	case TYPE_SIGNED_INT:
	case TYPE_UNSIGNED_INT:
	case TYPE_LONG:
	case TYPE_SIGNED_LONG:
	case TYPE_UNSIGNED_LONG:
	case TYPE_INT32:
	case TYPE_SIGNED_INT32:
	case TYPE_UNSIGNED_INT32:
		return type_layout_new(4, 4);
	case TYPE_LONG_LONG:
	case TYPE_SIGNED_LONG_LONG:
	case TYPE_UNSIGNED_LONG_LONG:
	case TYPE_INT64:
	case TYPE_SIGNED_INT64:
	case TYPE_UNSIGNED_INT64:
		return type_layout_new(8, 8);

	case TYPE_SIZE_T:
		return context->pointer_type_layout;

	case TYPE_FLOAT:
		return type_layout_new(4, 4);
	case TYPE_DOUBLE:
	case TYPE_LONG_DOUBLE:
		return type_layout_new(8, 8);

	case TYPE_STRUCT:
		return type->struct_def->type_layout;
	case TYPE_UNION:
		return type->union_def->type_layout;

	case TYPE_POINTER:
		return context->pointer_type_layout;
	case TYPE_ARRAY: {
		if (type->array.size) {
			// NOTE: I think that, if the function param has a type of a sized array, that this
			//       function should still return `context->pointer_type_layout`
			assert(type->array.size->kind == EXPR_INTEGER_LITERAL);

			TypeLayout element_type_layout = type_get_layout(context, type->array.element_type);
			size_t size = element_type_layout.size * type->array.size->int_literal.value;
			return type_layout_new(size, element_type_layout.alignment);
		}

		return context->pointer_type_layout;
	}
	case TYPE_FUNCTION:
		return context->pointer_type_layout;
	case TYPE_BOOL:
		unreachable();
	}

	unreachable();
	return (TypeLayout) {};
}

bool type_is_struct(const Type* type, const Struct* struct_def) {
	assert(struct_def->layout_kind == STRUCT_LAYOUT_KIND_STRUCT);

	if (type->kind != TYPE_STRUCT) {
		return false;
	}

	return type->struct_def == struct_def;
}

bool type_is_enum(const Type* type, const Enum* enum_def) {
	if (type->kind != TYPE_ENUM) {
		return false;
	}

	return type->enum_def == enum_def;
}

bool type_is_callable(const Type* type) {
	if (type->kind == TYPE_POINTER && type->pointer_base_type->kind == TYPE_FUNCTION) {
		return true;
	}

	return false;
}

bool type_equal_ignore_qualifiers(const Type* a, const Type* b) {
	TypeKind a_without_signed = a->kind & (TypeKind)(~TYPE_FLAG_SIGNED);
	TypeKind b_without_signed = b->kind & (TypeKind)(~TYPE_FLAG_SIGNED);

	if (a_without_signed != b_without_signed) {
		return false;
	}

	switch (a->kind) {
	case TYPE_STRUCT:
		return a->struct_def == b->struct_def;
	case TYPE_UNION:
		return a->union_def == b->union_def;
	case TYPE_ENUM:
		return a->enum_def == b->enum_def;
	case TYPE_VOID:
	case TYPE_SIZE_T:

	case TYPE_CHAR:
	case TYPE_INT:
	case TYPE_SHORT:
	case TYPE_LONG:
	case TYPE_LONG_LONG:
	case TYPE_INT8:
	case TYPE_INT16:
	case TYPE_INT32:
	case TYPE_INT64:

	case TYPE_SIGNED_CHAR:
	case TYPE_SIGNED_INT:
	case TYPE_SIGNED_SHORT:
	case TYPE_SIGNED_LONG:
	case TYPE_SIGNED_LONG_LONG:
	case TYPE_SIGNED_INT8:
	case TYPE_SIGNED_INT16:
	case TYPE_SIGNED_INT32:
	case TYPE_SIGNED_INT64:


	case TYPE_UNSIGNED_CHAR:
	case TYPE_UNSIGNED_INT:
	case TYPE_UNSIGNED_SHORT:
	case TYPE_UNSIGNED_LONG:
	case TYPE_UNSIGNED_LONG_LONG:
	case TYPE_UNSIGNED_INT8:
	case TYPE_UNSIGNED_INT16:
	case TYPE_UNSIGNED_INT32:
	case TYPE_UNSIGNED_INT64:

	case TYPE_BOOL:

	case TYPE_FLOAT:
	case TYPE_DOUBLE:
	case TYPE_LONG_DOUBLE:
		return true;
	
	case TYPE_POINTER:
		assert(a->pointer_base_type != NULL);
		assert(b->pointer_base_type != NULL);
		return type_equal(a->pointer_base_type, b->pointer_base_type);
	
	case TYPE_ARRAY:
		assert(a->array.size == NULL);
		assert(b->array.size == NULL);
		return type_equal(a->array.element_type, a->array.element_type);
	case TYPE_FUNCTION: {
		const FunctionPrototype* a_proto = a->function;
		const FunctionPrototype* b_proto = b->function;

		if (!type_equal(&a_proto->return_type, &b_proto->return_type)) {
			return false;
		}

		if (a_proto->calling_convention != b_proto->calling_convention) {
			return false;
		}

		if (a_proto->has_va_args != b_proto->has_va_args) {
			return false;
		}

		if (a_proto->parameter_count != b_proto->parameter_count) {
			return false;
		}

		for (size_t i = 0; i < a_proto->parameter_count; i += 1) {
			const Type* a_param_type = &a_proto->parameters[i].type;
			const Type* b_param_type = &b_proto->parameters[i].type;

			if (!type_equal(a_param_type, b_param_type)) {
				return false;
			}
		}

		return true;
	}
	}

	unreachable();
	return false;
}

bool type_equal(const Type* a, const Type* b) {
	if (a->qualifiers != b->qualifiers) {
		return false;
	}

	return type_equal_ignore_qualifiers(a, b);
}

void type_array_to_pointer(const Type* type, Type* out_type) {
	assert(type->kind == TYPE_ARRAY);

	Type* element_type = type->array.element_type;

	out_type->kind = TYPE_POINTER;
	out_type->pointer_base_type = element_type;
}

void type_format(const Type* type, StringBuilder* builder) {
	if (has_flag(type->qualifiers, TYPE_QUALIFIER_CONST)) {
		str_builder_append(builder, STR_LIT("const "));
	}

	if (type->alias_definition) {
		str_builder_append(builder, type->alias_definition->new_name);
	} else {
		switch (type->kind) {
		case TYPE_POINTER:
			type_format(type->pointer_base_type, builder);
			str_builder_append(builder, STR_LIT("*"));
			break;
		case TYPE_ARRAY:
			type_format(type->array.element_type, builder);
			str_builder_append(builder, STR_LIT("[]"));
			break;
		case TYPE_STRUCT:
			str_builder_append(builder, STR_LIT("struct "));
			str_builder_append(builder, type->struct_def->name);
			break;
		case TYPE_UNION:
			str_builder_append(builder, STR_LIT("union "));
			str_builder_append(builder, type->union_def->name);
			break;
		case TYPE_ENUM:
			str_builder_append(builder, STR_LIT("enum "));
			str_builder_append(builder, type->enum_def->name);
			break;
		case TYPE_VOID:
			str_builder_append(builder, STR_LIT("void"));
			break;
		case TYPE_SIZE_T:
			str_builder_append(builder, STR_LIT("size_t"));
			break;

		case TYPE_CHAR:
			str_builder_append(builder, STR_LIT("char"));
			break;
		case TYPE_INT:
			str_builder_append(builder, STR_LIT("int"));
			break;
		case TYPE_SHORT:
			str_builder_append(builder, STR_LIT("short"));
			break;
		case TYPE_LONG:
			str_builder_append(builder, STR_LIT("long"));
			break;
		case TYPE_LONG_LONG:
			str_builder_append(builder, STR_LIT("long long"));
			break;
		case TYPE_INT8:
			str_builder_append(builder, STR_LIT("__int8"));
			break;
		case TYPE_INT16:
			str_builder_append(builder, STR_LIT("__int16"));
			break;
		case TYPE_INT32:
			str_builder_append(builder, STR_LIT("__int32"));
			break;
		case TYPE_INT64:
			str_builder_append(builder, STR_LIT("__int64"));
			break;

		case TYPE_SIGNED_CHAR:
			str_builder_append(builder, STR_LIT("signed char"));
			break;
		case TYPE_SIGNED_INT:
			str_builder_append(builder, STR_LIT("signed int"));
			break;
		case TYPE_SIGNED_SHORT:
			str_builder_append(builder, STR_LIT("signed short"));
			break;
		case TYPE_SIGNED_LONG:
			str_builder_append(builder, STR_LIT("signed long"));
			break;
		case TYPE_SIGNED_LONG_LONG:
			str_builder_append(builder, STR_LIT("signed long long"));
			break;
		case TYPE_SIGNED_INT8:
			str_builder_append(builder, STR_LIT("signed __int8"));
			break;
		case TYPE_SIGNED_INT16:
			str_builder_append(builder, STR_LIT("signed __int16"));
			break;
		case TYPE_SIGNED_INT32:
			str_builder_append(builder, STR_LIT("signed __int32"));
			break;
		case TYPE_SIGNED_INT64:
			str_builder_append(builder, STR_LIT("signed __int64"));
			break;


		case TYPE_UNSIGNED_CHAR:
			str_builder_append(builder, STR_LIT("unsigned char"));
			break;
		case TYPE_UNSIGNED_INT:
			str_builder_append(builder, STR_LIT("unsigned int"));
			break;
		case TYPE_UNSIGNED_SHORT:
			str_builder_append(builder, STR_LIT("unsigned short"));
			break;
		case TYPE_UNSIGNED_LONG:
			str_builder_append(builder, STR_LIT("unsigned long"));
			break;
		case TYPE_UNSIGNED_LONG_LONG:
			str_builder_append(builder, STR_LIT("unsigned long long"));
			break;
		case TYPE_UNSIGNED_INT8:
			str_builder_append(builder, STR_LIT("unsigned __int8"));
			break;
		case TYPE_UNSIGNED_INT16:
			str_builder_append(builder, STR_LIT("unsigned __int16"));
			break;
		case TYPE_UNSIGNED_INT32:
			str_builder_append(builder, STR_LIT("unsigned __int32"));
			break;
		case TYPE_UNSIGNED_INT64:
			str_builder_append(builder, STR_LIT("unsigned __int64"));
			break;

		case TYPE_FLOAT:
			str_builder_append(builder, STR_LIT("float"));
			break;
		case TYPE_DOUBLE:
			str_builder_append(builder, STR_LIT("double"));
			break;
		case TYPE_LONG_DOUBLE:
			str_builder_append(builder, STR_LIT("long double"));
			break;
		case TYPE_FUNCTION: {
			const FunctionPrototype* proto = type->function;
			type_format(&proto->return_type, builder);
			str_builder_append(builder, STR_LIT(" ("));
			str_builder_append(builder,
					function_calling_convetion_to_string(proto->calling_convention));
			str_builder_append(builder, STR_LIT(" *)("));

			for (size_t i = 0; i < proto->parameter_count; i += 1) {
				type_format(&proto->parameters[i].type, builder);

				if (i + 1 != proto->parameter_count) {
					str_builder_append(builder, STR_LIT(", "));
				}
			}

			str_builder_append(builder, STR_LIT(")"));
			break;
		}
		case TYPE_BOOL:
			str_builder_append(builder, STR_LIT("_Bool"));
			break;
		}
	}
}

uint32_t type_get_int_convertion_rank(const Type* type) {
	switch (type->kind) {
	case TYPE_VOID:
		unreachable();

	case TYPE_CHAR:
	case TYPE_SIGNED_CHAR:
	case TYPE_UNSIGNED_CHAR:
	case TYPE_INT8:
	case TYPE_SIGNED_INT8:
	case TYPE_UNSIGNED_INT8:
		return 1;

	case TYPE_SHORT:
	case TYPE_SIGNED_SHORT:
	case TYPE_UNSIGNED_SHORT:
	case TYPE_INT16:
	case TYPE_SIGNED_INT16:
	case TYPE_UNSIGNED_INT16:
		return 2;

	// NOTE: Enums are implicitely convertable to ints, so they have the same convertion ranks as
	//       the corresponding int
	case TYPE_ENUM:

	case TYPE_INT:
	case TYPE_SIGNED_INT:
	case TYPE_UNSIGNED_INT:
	case TYPE_LONG:
	case TYPE_SIGNED_LONG:
	case TYPE_UNSIGNED_LONG:
	case TYPE_INT32:
	case TYPE_SIGNED_INT32:
	case TYPE_UNSIGNED_INT32:
		return 3;

	case TYPE_LONG_LONG:
	case TYPE_SIGNED_LONG_LONG:
	case TYPE_UNSIGNED_LONG_LONG:
	case TYPE_INT64:
	case TYPE_SIGNED_INT64:
	case TYPE_UNSIGNED_INT64:
		return 4;
	
	case TYPE_SIZE_T:
		return 5;

	case TYPE_FLOAT:
	case TYPE_DOUBLE:
	case TYPE_LONG_DOUBLE:
		break;

	case TYPE_STRUCT:
	case TYPE_UNION:
		break;

	case TYPE_POINTER:
	case TYPE_ARRAY:
	case TYPE_FUNCTION:
		break;
	
	case TYPE_BOOL:
		break;
	}

	unreachable_msg("The provided type is not an integer and this doesn't have a convertion rank");
	return 0;
}

String bin_op_kind_to_string(BinOpKind op) {
	switch (op) {
	case BIN_OP_ADD: return STR_LIT("+");
	case BIN_OP_SUB: return STR_LIT("-");
	case BIN_OP_MUL: return STR_LIT("*");
	case BIN_OP_DIV: return STR_LIT("/");
	case BIN_OP_MOD: return STR_LIT("%");

	case BIN_OP_LOGICAL_AND: return STR_LIT("&&");
	case BIN_OP_LOGICAL_OR: return STR_LIT("||");

	case BIN_OP_LOGICAL_EQUAL: return STR_LIT("==");
	case BIN_OP_LOGICAL_NOT_EQUAL: return STR_LIT("!=");
	case BIN_OP_LOGICAL_LESS: return STR_LIT("<");
	case BIN_OP_LOGICAL_GREATER: return STR_LIT(">");
	case BIN_OP_LOGICAL_LESS_OR_EQUAL: return STR_LIT("<=");
	case BIN_OP_LOGICAL_GREATER_OR_EQUAL: return STR_LIT(">=");

	case BIN_OP_BITWISE_AND: return STR_LIT("&");
	case BIN_OP_BITWISE_OR: return STR_LIT("|");
	case BIN_OP_BITWISE_XOR: return STR_LIT("^");
	case BIN_OP_BITWISE_SHIFT_LEFT: return STR_LIT("<<");
	case BIN_OP_BITWISE_SHIFT_RIGHT: return STR_LIT(">>");

	case BIN_OP_ASSIGNMENT: return STR_LIT("=");

	case BIN_OP_ASSIGNMENT_BY_SUM: return STR_LIT("+=");
	case BIN_OP_ASSIGNMENT_BY_DIFFERENCE: return STR_LIT("-=");
	case BIN_OP_ASSIGNMENT_BY_PRODUCT: return STR_LIT("*=");
	case BIN_OP_ASSIGNMENT_BY_QUOTIENT: return STR_LIT("/=");
	case BIN_OP_ASSIGNMENT_BY_REMAINDER: return STR_LIT("%=");

	case BIN_OP_ASSIGNMENT_BY_BITWISE_AND: return STR_LIT("&=");
	case BIN_OP_ASSIGNMENT_BY_BITWISE_OR: return STR_LIT("|=");
	case BIN_OP_ASSIGNMENT_BY_BITWISE_XOR: return STR_LIT("^=");
	case BIN_OP_ASSIGNMENT_BY_BITWISE_SHIFT_LEFT: return STR_LIT("<<=");
	case BIN_OP_ASSIGNMENT_BY_BITWISE_SHIFT_RIGHT: return STR_LIT(">>=");
	}

	unreachable();
	return (String) {};
}

String unary_op_kind_to_string(UnaryOpKind op) {
	switch (op) {
	case UNARY_OP_NEGATE:
		return STR_LIT("-");
	case UNARY_OP_PLUS:
		return STR_LIT("+");
	case UNARY_OP_ADDRESS:
		return STR_LIT("&");
	case UNARY_OP_DEREFERENCE:
		return STR_LIT("*");
	case UNARY_OP_LOGICAL_NOT:
		return STR_LIT("!");
	case UNARY_OP_BITWISE_NOT:
		return STR_LIT("~");
	case UNARY_OP_PRE_INCREMENT:
		return STR_LIT("pre ++");
	case UNARY_OP_POST_INCREMENT:
		return STR_LIT("post ++");
	case UNARY_OP_PRE_DECREMENT:
		return STR_LIT("pre --");
	case UNARY_OP_POST_DECREMENT:
		return STR_LIT("post --");
	}

	unreachable();
	return (String) {};
}

uint32_t bin_op_precedence(BinOpKind op) {
	switch (op) {
	case BIN_OP_ADD:
	case BIN_OP_SUB:
		return 4;

	case BIN_OP_MUL:
	case BIN_OP_DIV:
	case BIN_OP_MOD:
		return 3;

	case BIN_OP_LOGICAL_AND:
		return 11;
	case BIN_OP_LOGICAL_OR:
		return 12;

	case BIN_OP_LOGICAL_EQUAL:
	case BIN_OP_LOGICAL_NOT_EQUAL:
		return 7;

	case BIN_OP_LOGICAL_LESS:
	case BIN_OP_LOGICAL_GREATER:
	case BIN_OP_LOGICAL_LESS_OR_EQUAL:
	case BIN_OP_LOGICAL_GREATER_OR_EQUAL:
		return 6;

	case BIN_OP_BITWISE_SHIFT_LEFT:
	case BIN_OP_BITWISE_SHIFT_RIGHT:
		return 5;

	case BIN_OP_BITWISE_AND:
		return 8;
	case BIN_OP_BITWISE_OR:
		return 10;
	case BIN_OP_BITWISE_XOR:
		return 9;

	case BIN_OP_ASSIGNMENT:
	case BIN_OP_ASSIGNMENT_BY_SUM:
	case BIN_OP_ASSIGNMENT_BY_DIFFERENCE:
	case BIN_OP_ASSIGNMENT_BY_PRODUCT:
	case BIN_OP_ASSIGNMENT_BY_QUOTIENT:
	case BIN_OP_ASSIGNMENT_BY_REMAINDER:

	case BIN_OP_ASSIGNMENT_BY_BITWISE_AND:
	case BIN_OP_ASSIGNMENT_BY_BITWISE_OR:
	case BIN_OP_ASSIGNMENT_BY_BITWISE_XOR:
	case BIN_OP_ASSIGNMENT_BY_BITWISE_SHIFT_LEFT:
	case BIN_OP_ASSIGNMENT_BY_BITWISE_SHIFT_RIGHT:
		return 14;
	}

	unreachable();
	return UINT32_MAX;
}

String function_calling_convetion_to_string(FunctionCallingConvention conv) {
	switch (conv) {
	case FUNC_CALL_CONV_CDECL:
		return STR_LIT("__cdecl");
	}

	unreachable();
	return (String) {};
}

//
// AST
//

void parsed_node_list_append(NodeList* list, AstNode* node) {
	assert(list != NULL);
	assert(node != NULL);
	assert(node->next == NULL);

	if (list->first == NULL) {
		assert(list->last == NULL);
		assert(list->count == 0);

		list->first = node;
		list->last = node;
		list->count = 1;
	} else {
		assert(list->last != NULL);
		list->last->next = node;
		list->last = node;
		list->count += 1;
	}
}

void expr_get_type(Expr* expr, Type* out_type) {
	memset(out_type, 0, sizeof(*out_type));

	switch (expr->kind) {
	case EXPR_CALL: {
		Expr* callable = expr->call.callable;

		Type callable_type;
		expr_get_type(callable, &callable_type);

		assert(type_is_callable(&callable_type));
		const FunctionPrototype* prototype = callable_type.pointer_base_type->function;

		assert_msg(prototype != NULL, "A callable expression is not function");

		*out_type = prototype->return_type;
		return;
	}
	case EXPR_BINARY: {
		if (bin_op_is_compare(expr->binary.op)) {
			out_type->kind = TYPE_INT;
		} else if (expr->binary.op == BIN_OP_LOGICAL_AND) {
			out_type->kind = TYPE_INT;
		} else if (expr->binary.op == BIN_OP_LOGICAL_OR) {
			out_type->kind = TYPE_INT;
		} else {
			out_type->kind = expr->binary.common_type_kind;
			out_type->pointer_base_type = expr->binary.pointer_base_type;
		}

		return;
	}
	case EXPR_UNARY: {
		switch (expr->unary.op) {
		case UNARY_OP_NEGATE:
		case UNARY_OP_PLUS:

		case UNARY_OP_BITWISE_NOT:

		case UNARY_OP_PRE_INCREMENT:
		case UNARY_OP_POST_INCREMENT:

		case UNARY_OP_PRE_DECREMENT:
		case UNARY_OP_POST_DECREMENT:
			expr_get_type(expr->unary.operand, out_type);
			return;
		case UNARY_OP_LOGICAL_NOT:
			out_type->kind = TYPE_INT;
			return;
		case UNARY_OP_DEREFERENCE: {
			Type operand_type;
			expr_get_type(expr->unary.operand, &operand_type);

			assert_msg(type_kind_is_pointer_like(operand_type.kind),
					"Dereferencing a non-pointer like type is not allowed");

			const Type* base_type = type_extract_pointer_base_type(&operand_type);
			*out_type = *base_type;
			return;
		}
		case UNARY_OP_ADDRESS:
			out_type->kind = TYPE_POINTER;
			out_type->pointer_base_type = expr->unary.pointer_base_type;
			return;
		}

		unreachable();
		return;
	}
	case EXPR_FUNCTION_REFERENCE:
		out_type->kind = TYPE_POINTER;
		out_type->pointer_base_type = &expr->function_ref.func->type;
		return;
	case EXPR_VARIABLE_REFERENCE:
		*out_type = expr->variable_ref.var->type;
		return;
	case EXPR_INTEGER_LITERAL: {
		out_type->kind = expr->int_literal.integer_type;
		return;
	}
	case EXPR_STRING_LITERAL:
	   	static Type s_const_char_type;
		s_const_char_type.kind = TYPE_CHAR;
		s_const_char_type.qualifiers = TYPE_QUALIFIER_CONST;

		out_type->kind = TYPE_ARRAY;
		out_type->array.element_type = &s_const_char_type;
		out_type->array.size = expr->string_literal.array_size_expr;
		return;
	case EXPR_CHAR_LITERAL:
		out_type->kind = TYPE_CHAR;
		return;
	case EXPR_ENUM_CONSTANT:
		out_type->kind = TYPE_ENUM;
		out_type->enum_def = (Enum*)expr->enum_constant.enum_def;
		return;
	case EXPR_FUNCTION_PARAM: {
		const Function* func = expr->function_param.function_def;
		assert(expr->function_param.param_index < func->proto.parameter_count);
		*out_type = func->proto.parameters[expr->function_param.param_index].type;
		return;
	}
	case EXPR_ARRAY_INDEX: {
		Type array_type;
		expr_get_type(expr->array_index.array, &array_type);

		Type* element_type = type_extract_pointer_base_type(&array_type);
		*out_type = *element_type;
		return;
	}
	case EXPR_CAST: {
		*out_type = *expr->cast.target_type;
		return;
	}
	case EXPR_INDIRECT_FIELD_ACCESS: {
		Type type;
		expr_get_type(expr->field_access.target, &type);

		assert(type.kind == TYPE_POINTER);

		Type* inner_type = type.pointer_base_type;
		const Struct* compound_type = NULL;
		if (inner_type->kind == TYPE_STRUCT) {
			compound_type = inner_type->struct_def;
		} else if (inner_type->kind == TYPE_UNION) {
			compound_type = inner_type->union_def;
		} else {
			panic("Not a compound type");
		}

		StructFieldNamespaceEntry entry =
			compound_type->field_namespace->entries[expr->field_access.field_index];

		StructField field = entry.struct_def->fields[entry.field_index];
		*out_type = field.type;
		return;
	}
	case EXPR_DIRECT_FIELD_ACCESS: {
		Type type;
		expr_get_type(expr->field_access.target, &type);

		const Struct* compound_type = NULL;
		if (type.kind == TYPE_STRUCT) {
			compound_type = type.struct_def;
		} else if (type.kind == TYPE_UNION) {
			compound_type = type.union_def;
		} else {
			panic("Not a compound type");
		}

		StructFieldNamespaceEntry entry =
			compound_type->field_namespace->entries[expr->field_access.field_index];

		StructField field = entry.struct_def->fields[entry.field_index];
		*out_type = field.type;
		return;
	}
	case EXPR_SIZE_OF_EXPR:
	case EXPR_SIZE_OF_TYPE:
		out_type->kind = TYPE_SIZE_T;
		return;
	case EXPR_COMPOUND_LITERAL:
		assert(expr->compound_literal.type);
		*out_type = *expr->compound_literal.type;
		return;
	}

	unreachable_msg("Failed to get expr type");
	return;
}

bool expr_is_bool(Expr* expr) {
	if (expr->kind == EXPR_BINARY) {
		return bin_op_is_compare(expr->binary.op)
			|| expr->binary.op == BIN_OP_LOGICAL_AND
			|| expr->binary.op == BIN_OP_LOGICAL_OR;
	} else if (expr->kind == EXPR_UNARY && expr->unary.op == UNARY_OP_LOGICAL_NOT) {
		return true;
	}

	return false;
}

ValueKind expr_get_value_kind(Expr* expr) {
	switch (expr->kind) {
	case EXPR_VARIABLE_REFERENCE:
		return VALUE_L;
	case EXPR_FUNCTION_PARAM:
		return VALUE_L;
	case EXPR_ARRAY_INDEX:
		return VALUE_L;
	case EXPR_UNARY:
		switch (expr->unary.op) {
		case UNARY_OP_PRE_INCREMENT:
		case UNARY_OP_POST_INCREMENT:
		case UNARY_OP_PRE_DECREMENT:
		case UNARY_OP_POST_DECREMENT:
			return VALUE_L;
		case UNARY_OP_DEREFERENCE:
			return VALUE_L;
		default:
			return VALUE_R;
		}
	case EXPR_INDIRECT_FIELD_ACCESS:
		return VALUE_L;
	case EXPR_DIRECT_FIELD_ACCESS:
		return VALUE_L;
	default:
		return VALUE_R;
	}

	unreachable();
	return 0;
}

PackedSourceRange expr_get_source_range(const Expr* expr) {
	assert(expr);

	switch (expr->kind) {
	case EXPR_CALL:
		return source_range_merge(
				expr_get_source_range(expr->call.callable),
				expr->call.right_paren_source_range);
	case EXPR_BINARY:
		return source_range_merge(
				expr_get_source_range(expr->binary.left),
				expr_get_source_range(expr->binary.right));
	case EXPR_UNARY:
		return source_range_merge(
				expr_get_source_range(expr->unary.operand),
				expr->unary.operator_source_range);
	case EXPR_FUNCTION_REFERENCE:
		return expr->function_ref.source_range;
	case EXPR_VARIABLE_REFERENCE:
		return expr->variable_ref.source_range;
	case EXPR_INTEGER_LITERAL:
		return expr->int_literal.source_range;
	case EXPR_STRING_LITERAL:
		return expr->string_literal.source_range;
	case EXPR_CHAR_LITERAL:
		return expr->char_literal.source_range;
	case EXPR_ENUM_CONSTANT:
		return expr->enum_constant.source_range;
	case EXPR_FUNCTION_PARAM:
		return expr->function_param.source_range;
	case EXPR_ARRAY_INDEX:
		return source_range_merge(
				expr_get_source_range(expr->array_index.array),
				expr->array_index.right_bracket_source_range);
	case EXPR_CAST:
		return source_range_merge(
				expr->cast.left_paren_source_range,
				expr_get_source_range(expr->cast.expr));
	case EXPR_DIRECT_FIELD_ACCESS:
	case EXPR_INDIRECT_FIELD_ACCESS:
		return expr->field_access.source_range;
	case EXPR_SIZE_OF_EXPR:
		return expr->size_of_expr.source_range;
	case EXPR_SIZE_OF_TYPE:
		return expr->size_of_type.source_range;
	case EXPR_COMPOUND_LITERAL:
		return expr->compound_literal.source_range;
	}

	unreachable();
	return (PackedSourceRange) {};
}

size_t struct_field_namespace_index_of(const StructFieldNamespace* struct_namespace, String name) {
	if (struct_namespace->capacity == 0) {
		return SIZE_MAX;
	}

	size_t index = hash_string(name) % struct_namespace->capacity;
	
	while (true) {
		String key = struct_namespace->keys[index];
		if (key.v == NULL) {
			return SIZE_MAX;
		} else if (str_equal(key, name)) {
			return index;
		}

		index = (index + 1) % struct_namespace->capacity;
	}

	unreachable();
	return SIZE_MAX;
}

typedef struct {
	size_t indent;
} PrinterState;

void printer_indent(const PrinterState* printer) {
for (size_t i = 0; i < printer->indent; i += 1) {
		printf("  ");
	}
}

void printer_begin_struct(PrinterState* printer, const char* struct_name) {
	printf("%s {\n", struct_name);
	printer->indent += 1;
}

void printer_end_struct(PrinterState* printer) {
	assert(printer->indent > 0);
	printer->indent -= 1;
	printer_indent(printer);
	printf("}\n");
}

void printer_begin_array(PrinterState* printer) {
	printf("[\n");
	printer->indent += 1;
}

void printer_end_array(PrinterState* printer) {
	assert(printer->indent > 0);
	printer->indent -= 1;
	printer_indent(printer);
	printf("]\n");
}

void printer_array_element(PrinterState* printer, size_t index) {
	printer_indent(printer);
	printf("%zu = ", index);
}

void printer_field(PrinterState* printer, const char* field_name) {
	printer_indent(printer);
	printf("%s = ", field_name);
}

void printer_string_value(PrinterState* printer, String value) {
	printf("%.*s\n", STR_FMT(value));
}

void printer_string_field(PrinterState* printer, const char* name, String value) {
	printer_indent(printer);
	printf("%s = %.*s\n", name, STR_FMT(value));
}

void printer_bool_field(PrinterState* printer, const char* name, bool value) {
	printer_indent(printer);
	printf("%s = %s\n", name, (value ? "true" : "false"));
}

//
// AST Printing
//

void print_type(PrinterState* printer, const Type* type);
void print_single_node(PrinterState* printer, const AstNode* node);
void print_decl_spec(PrinterState* printer, const DeclSpec* decl_spec) {
	String decl_spec_name = {};

	switch (decl_spec->kind) {
	case DECL_SPEC_DEPRECATED:
		decl_spec_name = STR_LIT("deprecated");
		break;
	case DECL_SPEC_NO_INLINE:
		decl_spec_name = STR_LIT("noinline");
		break;
	case DECL_SPEC_NO_RETURN:
		decl_spec_name = STR_LIT("noreturn");
		break;
	case DECL_SPEC_DLL_IMPORT:
		decl_spec_name = STR_LIT("dllimport");
		break;
	case DECL_SPEC_DLL_EXPORT:
		decl_spec_name = STR_LIT("dllexport");
		break;
	case DECL_SPEC_RESTRICT:
		decl_spec_name = STR_LIT("restrict");
		break;
	}

	printer_begin_struct(printer, "decl_spec");
	printer_string_field(printer, "name", decl_spec_name);

	if (decl_spec->kind == DECL_SPEC_DEPRECATED) {
		printer_string_field(printer, "deprecation_text", decl_spec->deprecation_text.full_string);
	}

	printer_end_struct(printer);
}

void print_expr(PrinterState* printer, const Expr* expr) {
	assert(expr != NULL);

	switch (expr->kind) {
	case EXPR_FUNCTION_REFERENCE:
		printer_begin_struct(printer, "function_ref");
		printer_string_field(printer, "name", expr->function_ref.func->proto.name);
		printer_end_struct(printer);
		break;
	case EXPR_VARIABLE_REFERENCE:
		printer_begin_struct(printer, "variable_ref");
		printer_string_field(printer, "name", expr->variable_ref.var->name);
		printer_end_struct(printer);
		break;
	case EXPR_BINARY: {
		Type common_type = {
			.kind = expr->binary.common_type_kind,
			.pointer_base_type = expr->binary.pointer_base_type
		};

		printer_begin_struct(printer, "binary_expr");
		printer_string_field(printer, "kind", bin_op_kind_to_string(expr->binary.op));
		printer_field(printer, "common_type");
		print_type(printer, &common_type);
		printer_field(printer, "left");
		print_expr(printer, expr->binary.left);
		printer_field(printer, "right");
		print_expr(printer, expr->binary.right);
		printer_end_struct(printer);
		break;
	}
	case EXPR_UNARY:
		printer_begin_struct(printer, "unary_expr");
		printer_string_field(printer, "kind", unary_op_kind_to_string(expr->unary.op));
		printer_field(printer, "operand");
		print_expr(printer, expr->unary.operand);
		printer_end_struct(printer);
		break;
	case EXPR_INTEGER_LITERAL:
		printer_begin_struct(printer, "int_literal");
		printer_string_field(printer, "format", int_literal_format_to_string(expr->int_literal.format));
		printer_field(printer, "type");

		Type type = { .kind = expr->int_literal.integer_type };
		print_type(printer, &type);
		printer_field(printer, "value");
		printf("%llu\n", expr->int_literal.value);
		printer_end_struct(printer);
		break;
	case EXPR_STRING_LITERAL:
		printer_begin_struct(printer, "string");
		printer_string_field(printer, "value", expr->string_literal.full_string);
		printer_field(printer, "array_size");
		print_expr(printer, expr->string_literal.array_size_expr);
		printer_end_struct(printer);
		break;
	case EXPR_CHAR_LITERAL:
		printer_begin_struct(printer, "char");
		printer_field(printer, "value");
		printf("%u %c\n", expr->char_literal.value, (char)(expr->char_literal.value));
		printer_end_struct(printer);
		break;
	case EXPR_CALL: {
		printer_begin_struct(printer, "call");
		printer_field(printer, "callable");
		print_expr(printer, expr->call.callable);

		printer_field(printer, "args");
		printer_begin_array(printer);

		for (size_t i = 0; i < expr->call.args.count; i += 1) {
			printer_array_element(printer, i);
			print_expr(printer, expr->call.args.exprs[i]);
		}

		printer_end_array(printer);
		printer_end_struct(printer);
		break;
	}
	case EXPR_ENUM_CONSTANT: {
		const Enum* enum_def = expr->enum_constant.enum_def;
		printer_begin_struct(printer, "enum_constant");
		printer_string_field(printer, "enum_name", enum_def->name);
		printer_string_field(printer, "variant_name", enum_def->variants[expr->enum_constant.variant_index].name);
		printer_end_struct(printer);
		break;
	}
	case EXPR_FUNCTION_PARAM: {
		const Function* func_def = expr->function_param.function_def;
		printer_begin_struct(printer, "function_param");
		printer_string_field(printer, "func_name", func_def->proto.name);
		printer_string_field(printer,
				"param_name",
				func_def->proto.parameters[expr->function_param.param_index].name);
		printer_end_struct(printer);
		break;
	}
	case EXPR_ARRAY_INDEX: {
		printer_begin_struct(printer, "array_index");
		printer_field(printer, "array");
		print_expr(printer, expr->array_index.array);
		printer_field(printer, "index");
		print_expr(printer, expr->array_index.index);
		printer_end_struct(printer);
		break;
	}
	case EXPR_CAST: {
		printer_begin_struct(printer, "cast");
		printer_field(printer, "target_type");
		print_type(printer, expr->cast.target_type);
		printer_field(printer, "expr");
		print_expr(printer, expr->cast.expr);
		printer_end_struct(printer);
		break;
	}
	case EXPR_INDIRECT_FIELD_ACCESS: {
		Type type;
		expr_get_type(expr->field_access.target, &type);

		assert(type.kind == TYPE_POINTER);

		Type* inner_type = type.pointer_base_type;
		const Struct* compound_type = NULL;
		if (inner_type->kind == TYPE_STRUCT) {
			compound_type = inner_type->struct_def;
		} else if (inner_type->kind == TYPE_UNION) {
			compound_type = inner_type->union_def;
		} else {
			panic("Not a compound type");
		}

		StructFieldNamespaceEntry entry =
			compound_type->field_namespace->entries[expr->field_access.field_index];

		assert(entry.struct_def == compound_type);

		StructField field = entry.struct_def->fields[entry.field_index];

		printer_begin_struct(printer, "indirect_field_access");
		printer_field(printer, "target");
		print_expr(printer, expr->field_access.target);
		printer_string_field(printer, "field_name", field.name);
		printer_end_struct(printer);
		break;
	}
	case EXPR_DIRECT_FIELD_ACCESS: {
		Type type;
		expr_get_type(expr->field_access.target, &type);

		const Struct* compound_type = NULL;
		if (type.kind == TYPE_STRUCT) {
			compound_type = type.struct_def;
		} else if (type.kind == TYPE_UNION) {
			compound_type = type.union_def;
		} else {
			panic("Not a compound type");
		}

		StructFieldNamespaceEntry entry =
			compound_type->field_namespace->entries[expr->field_access.field_index];

		assert(entry.struct_def == compound_type);

		StructField field = entry.struct_def->fields[entry.field_index];

		printer_begin_struct(printer, "direct_field_access");
		printer_field(printer, "target");
		print_expr(printer, expr->field_access.target);
		printer_string_field(printer, "field_name", field.name);
		printer_end_struct(printer);
		break;
	}
	case EXPR_SIZE_OF_EXPR: {
		printer_begin_struct(printer, "size_of_expr");
		printer_field(printer, "expr");
		print_expr(printer, expr->size_of_expr.expr);
		printer_end_struct(printer);
		break;
		break;
	}
	case EXPR_SIZE_OF_TYPE: {
		printer_begin_struct(printer, "size_of_type");
		printer_field(printer, "type");
		print_type(printer, expr->size_of_type.type);
		printer_end_struct(printer);
		break;
	}
	case EXPR_COMPOUND_LITERAL: {
		printer_begin_struct(printer, "compound_literal");
		if (expr->compound_literal.type) {
			printer_field(printer, "type");
			print_type(printer, expr->compound_literal.type);
		}

		printer_field(printer, "entries");
		printer_begin_array(printer);

		for (size_t i = 0; i < expr->compound_literal.entry_count; i += 1) {
			printer_array_element(printer, i);

			const CompoundLiteralEntry* entry = &expr->compound_literal.entries[i];
			switch (entry->kind) {
			case COMPOUND_LITERAL_VALUE:
				printer_begin_struct(printer, "value");
				printer_field(printer, "index");
				printf("%zu\n", entry->not_designated.index);
				printer_field(printer, "value");
				print_expr(printer, entry->value);
				printer_end_struct(printer);
				break;
			case COMPOUND_LITERAL_FIELD_INIT:
				printer_begin_struct(printer, "field");
				printer_string_field(printer, "field", entry->field.name);
				printer_field(printer, "value");
				print_expr(printer, entry->value);
				printer_end_struct(printer);
				break;
			case COMPOUND_LITERAL_ARRAY_ELEMENT_INIT:
				printer_begin_struct(printer, "array_element");
				printer_field(printer, "index");
				print_expr(printer, entry->array_element.index);
				printer_field(printer, "value");
				print_expr(printer, entry->value);
				printer_end_struct(printer);
				break;
			}
		}

		printer_end_array(printer);

		printer_end_struct(printer);
		break;
	}
	}
}

void print_struct_def(PrinterState* printer, const Struct* struct_def) {
	assert(struct_def != NULL);

	printer_begin_struct(printer, struct_def->layout_kind == STRUCT_LAYOUT_KIND_STRUCT
			? "struct"
			: "union");

	printer_string_field(printer, "name", struct_def->name);
	printer_field(printer, "id");
	printf("%u\n", struct_def->id);
	printer_bool_field(printer, "is_forward_declared", struct_def->is_forward_declared);
	
	if (!struct_def->is_forward_declared) {
		printer_field(printer, "members");
		printer_begin_array(printer);

		for (size_t i = 0; i < struct_def->field_count; i += 1) {
			const StructField* field = &struct_def->fields[i];
			printer_array_element(printer, i);
			printer_begin_struct(printer, "field");
			printer_string_field(printer, "name", field->name);
			printer_field(printer, "type");
			print_type(printer, &field->type);
			printer_end_struct(printer);
		}

		printer_end_array(printer);
	}

	printer_end_struct(printer);
}

void print_enum_def(PrinterState* printer, const Enum* enum_def) {
	assert(enum_def != NULL);

	printer_begin_struct(printer, "enum");

	printer_string_field(printer, "name", enum_def->name);

	printer_field(printer, "variants");
	printer_begin_array(printer);

	for (size_t i = 0; i < enum_def->variant_count; i += 1) {
		const EnumVariant* variant = &enum_def->variants[i];
		printer_array_element(printer, i);

		printer_begin_struct(printer, "variant");
		printer_string_field(printer, "name", variant->name);

		if (variant->value) {
			printer_field(printer, "value");
			print_expr(printer, variant->value);
		}

		printer_end_struct(printer);
	}
	printer_end_array(printer);

	printer_end_struct(printer);
}

void print_type(PrinterState* printer, const Type* type) {
	if (has_flag(type->qualifiers, TYPE_QUALIFIER_CONST)) {
		printf("const ");
	}

	if (has_flag(type->qualifiers, TYPE_QUALIFIER_VOLATILE)) {
		printf("volatile ");
	}

	switch (type->kind) {
	case TYPE_STRUCT:
		print_struct_def(printer, type->struct_def);
		break;
	case TYPE_UNION:
		print_struct_def(printer, type->union_def);
		break;
	case TYPE_ENUM:
		print_enum_def(printer, type->enum_def);
		break;
	case TYPE_VOID:
		printf("void\n");
		break;

	case TYPE_SIZE_T:
		printf("size_t\n");
		break;

	case TYPE_CHAR:
	case TYPE_INT:
	case TYPE_SHORT:
	case TYPE_LONG:
	case TYPE_LONG_LONG:
	case TYPE_INT8:
	case TYPE_INT16:
	case TYPE_INT32:
	case TYPE_INT64:

	case TYPE_SIGNED_CHAR:
	case TYPE_SIGNED_INT:
	case TYPE_SIGNED_SHORT:
	case TYPE_SIGNED_LONG:
	case TYPE_SIGNED_LONG_LONG:
	case TYPE_SIGNED_INT8:
	case TYPE_SIGNED_INT16:
	case TYPE_SIGNED_INT32:
	case TYPE_SIGNED_INT64:

	case TYPE_UNSIGNED_CHAR:
	case TYPE_UNSIGNED_INT:
	case TYPE_UNSIGNED_SHORT:
	case TYPE_UNSIGNED_LONG:
	case TYPE_UNSIGNED_LONG_LONG:
	case TYPE_UNSIGNED_INT8:
	case TYPE_UNSIGNED_INT16:
	case TYPE_UNSIGNED_INT32:
	case TYPE_UNSIGNED_INT64: {
		TypeKind base_kind = type->kind & (TypeKind)(~(TYPE_FLAG_SIGNED | TYPE_FLAG_UNSIGNED));
		const char* prefix = "";
		const char* base_type_name = "";

		if (has_flag(type->kind, (TypeKind)TYPE_FLAG_SIGNED)) {
			prefix = "signed ";
		} else if (has_flag(type->kind, (TypeKind)TYPE_FLAG_UNSIGNED)) {
			prefix = "unsigned ";
		}

		switch (base_kind) {
		case TYPE_CHAR:
			base_type_name = "char";
			break;
		case TYPE_INT:
			base_type_name = "int";
			break;
		case TYPE_SHORT:
			base_type_name = "short";
			break;
		case TYPE_LONG:
			base_type_name = "long";
			break;
		case TYPE_LONG_LONG:
			base_type_name = "long long";
			break;
		case TYPE_INT8:
			base_type_name = "__int8";
			break;
		case TYPE_INT16:
			base_type_name = "__int16";
			break;
		case TYPE_INT32:
			base_type_name = "__int32";
			break;
		case TYPE_INT64:
			base_type_name = "__int64";
			break;
		default:
			unreachable();
		}

		printf("%s%s\n", prefix, base_type_name);
		break;
	}

	case TYPE_FLOAT:
		printf("float\n");
		break;
	case TYPE_DOUBLE:
		printf("double\n");
		break;
	case TYPE_LONG_DOUBLE:
		printf("long double\n");
		break;
	case TYPE_POINTER:
		printer_begin_struct(printer, "pointer_type");
		printer_field(printer, "base_type");
		print_type(printer, type->pointer_base_type);
		printer_end_struct(printer);
		break;
	case TYPE_ARRAY:
		printer_begin_struct(printer, "array_type");
		printer_field(printer, "element_type");
		print_type(printer, type->array.element_type);

		if (type->array.size) {
			printer_field(printer, "size");
			print_expr(printer, type->array.size);
		}

		printer_end_struct(printer);
		break;
	case TYPE_BOOL:
		printf("_Bool\n");
		break;
	}
}

void print_type_def(PrinterState* printer, const TypeDef* type_def) {
	printer_begin_struct(printer, "typedef");

	printer_field(printer, "type");
	print_type(printer, &type_def->aliased_type);
	printer_string_field(printer, "name", type_def->new_name);

	printer_end_struct(printer);
}

void print_scope(PrinterState* printer, const Scope* scope) {
	printer_begin_array(printer);
	printer_field(printer, "id");
	printf("%llu\n", scope->id);

	AstNode* node = scope->nodes.first;
	size_t node_index = 0;

	while (node) {
		printer_array_element(printer, node_index);
		print_single_node(printer, node);
		node = node->next;
		node_index += 1;
	}

	printer_end_array(printer);
}

void print_function_def(PrinterState* printer, const Function* function_def) {
	printer_begin_struct(printer, "function");

	if (function_def->decl_spec) {
		printer_field(printer, "decl_spec");
		print_decl_spec(printer, function_def->decl_spec);
	}

	String storage_spec_string = {};
	switch (function_def->storage_specifier) {
	case STORAGE_SPEC_NONE:
		storage_spec_string = STR_LIT("none");
		break;
	case STORAGE_SPEC_EXTERNAL:
		storage_spec_string = STR_LIT("extern");
		break;
	case STORAGE_SPEC_STATIC:
		storage_spec_string = STR_LIT("static");
		break;
	}

	printer_string_field(printer, "storage_spec", storage_spec_string);

	const FunctionPrototype* proto = &function_def->proto;
	printer_string_field(printer, "name", proto->name);
	printer_string_field(printer, "calling_convetion",
			function_calling_convetion_to_string(proto->calling_convention));

	printer_field(printer, "return_type");
	print_type(printer, &proto->return_type);

	printer_field(printer, "parameters");
	printer_begin_array(printer);

	for (size_t i = 0; i < proto->parameter_count; i += 1) {
		const FunctionParam* param = &proto->parameters[i];

		printer_array_element(printer, i);
		printer_begin_struct(printer, "param");

		if (param->name.length > 0) {
			printer_string_field(printer, "name", param->name);
		}

		printer_field(printer, "type");
		print_type(printer, &param->type);
		printer_end_struct(printer);
	}

	printer_end_array(printer);
	printer_bool_field(printer, "is_forward_declared", function_def->is_forward_declared);
	printer_bool_field(printer, "has_va_args", proto->has_va_args);

	if (!function_def->is_forward_declared) {
		printer_field(printer, "body");
		if (function_def->body) {
			print_scope(printer, function_def->body);
		} else {
			printf("[]\n");
		}

	}

	printer_end_struct(printer);
}

void print_variable(PrinterState* printer, const Variable* variable) {
	printer_begin_struct(printer, "variable");
	printer_string_field(printer, "name", variable->name);
	printer_field(printer, "type");
	print_type(printer, &variable->type);

	if (variable->value) {
		printer_field(printer, "value");
		print_expr(printer, variable->value);
	}

	printer_end_struct(printer);
}

void print_return_stmt(PrinterState* printer, const ReturnStmt* return_stmt) {
	printer_begin_struct(printer, "return");

	if (return_stmt->value) {
		printer_field(printer, "value");
		print_expr(printer, return_stmt->value);
	}

	printer_end_struct(printer);
}

void print_single_node(PrinterState* printer, const AstNode* node) {
	switch (node->kind) {
	case AST_NODE_TYPE_DEF:
		print_type_def(printer, node->type_def);
		break;
	case AST_NODE_STRUCT:
		print_struct_def(printer, node->struct_def);
		break;
	case AST_NODE_UNION:
		print_struct_def(printer, node->union_def);
		break;
	case AST_NODE_ENUM:
		print_enum_def(printer, node->enum_def);
		break;
	case AST_NODE_FUNCTION_DEF:
	case AST_NODE_FUNCTION_DECL:
		print_function_def(printer, node->function_def);
		break;
	case AST_NODE_EXPR:
		print_expr(printer, &node->expr);
		break;
	case AST_NODE_VARIABLE:
		print_variable(printer, &node->variable);
		break;
	case AST_NODE_RETURN:
		print_return_stmt(printer, &node->return_stmt);
		break;
	case AST_NODE_BLOCK:
		printer_begin_struct(printer, "block");
		printer_field(printer, "body");
		print_scope(printer, &node->block);
		printer_end_struct(printer);
		break;
	case AST_NODE_IF:
		printer_begin_struct(printer, "if");

		printer_field(printer, "condition");
		print_expr(printer, &node->if_stmt.condition);

		printer_field(printer, "true_scope");
		print_scope(printer, node->if_stmt.true_scope);

		if (node->if_stmt.false_scope) {
			printer_field(printer, "false_scope");
			print_scope(printer, node->if_stmt.false_scope);
		}

		printer_end_struct(printer);
		break;
	case AST_NODE_WHILE_LOOP:
		printer_begin_struct(printer, "while");

		printer_field(printer, "condition");
		print_expr(printer, &node->while_loop.condition);

		printer_string_field(printer,
				"condition_kind",
				node->while_loop.condition_kind == WHILE_LOOP_PRE_CONDITION
					? STR_LIT("pre")
					: STR_LIT("post"));

		printer_field(printer, "body");
		print_scope(printer, node->while_loop.body_scope);

		printer_end_struct(printer);
		break;
	case AST_NODE_FOR_LOOP:
		printer_begin_struct(printer, "for");

		if (node->for_loop.init_stmt) {
			printer_field(printer, "init_stmt");
			print_single_node(printer, node->for_loop.init_stmt);
		}

		if (node->for_loop.condition) {
			printer_field(printer, "condition");
			print_expr(printer, node->for_loop.condition);
		}
	
		if (node->for_loop.advance_expr) {
			printer_field(printer, "advance");
			print_expr(printer, node->for_loop.advance_expr);
		}

		if (node->for_loop.body_scope) {
			printer_field(printer, "body");
			print_scope(printer, node->for_loop.body_scope);
		}

		printer_end_struct(printer);
		break;
	case AST_NODE_BREAK:
		printf("break\n");
		break;
	case AST_NODE_CONTINUE:
		printf("continue\n");
		break;
	case AST_NODE_SWITCH:
		printer_begin_struct(printer, "switch");
		printer_field(printer, "expr");
		print_expr(printer, node->switch_stmt.expr);

		printer_field(printer, "body");
		print_scope(printer, node->switch_stmt.body);

		printer_end_struct(printer);
		break;
	case AST_NODE_CASE:
		if (node->case_stmt.value) {
			printer_begin_struct(printer, "case");
			printer_field(printer, "expr");
			print_expr(printer, node->case_stmt.value);
			printer_end_struct(printer);
		} else {
			printer_begin_struct(printer, "default");
			printer_end_struct(printer);
		}
		break;
	}
}

void print_parsed_node(const AstNode* node) {
	PrinterState printer = {};

	while (node != NULL) {
		print_single_node(&printer, node);
		node = node->next;
	}
}
