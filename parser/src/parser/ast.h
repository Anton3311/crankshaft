#ifndef AST_H
#define AST_H

#include "core/core.h"
#include "parser/source_info.h"
#include "parser/parse_tools.h"

typedef struct AstNode AstNode;
typedef struct Block Block;
typedef struct Type Type;
typedef struct Struct Struct;
typedef struct StructField StructField;
typedef struct StructFieldNamespace StructFieldNamespace;
typedef struct StructFieldNamespaceEntry StructFieldNamespaceEntry;
typedef struct Enum Enum;
typedef struct EnumVariant EnumVariant;
typedef struct TypeDef TypeDef;
typedef struct Function Function;
typedef struct FunctionPrototype FunctionPrototype;
typedef struct FunctionParam FunctionParam;
typedef struct Variable Variable;
typedef struct Scope Scope;
typedef struct Call Call;
typedef struct StringLiteral StringLiteral;
typedef struct CharLiteral CharLiteral;
typedef struct Expr Expr;
typedef struct ExprArray ExprArray;
typedef struct BinExpr BinExpr;
typedef struct UnaryExpr UnaryExpr;
typedef struct IntegerLiteral IntegerLiteral;
typedef struct ReturnStmt ReturnStmt;
typedef struct DeclSpec DeclSpec;
typedef struct IfStmt IfStmt;
typedef struct WhileLoop WhileLoop;
typedef struct ForLoop ForLoop;
typedef struct ArrayIndex ArrayIndex;
typedef struct CompoundLiteralEntry CompoundLiteralEntry;
typedef struct CompoundLiteral CompoundLiteral;
typedef struct Switch Switch;
typedef struct Case Case;

//
// AST
//

typedef enum {
	AST_NODE_TYPE_DEF,
	AST_NODE_STRUCT,
	AST_NODE_UNION,
	AST_NODE_ENUM,
	AST_NODE_FUNCTION_DEF,
	AST_NODE_FUNCTION_DECL,
	AST_NODE_EXPR,
	AST_NODE_VARIABLE,
	AST_NODE_RETURN,
	AST_NODE_BLOCK,
	AST_NODE_IF,
	AST_NODE_WHILE_LOOP,
	AST_NODE_FOR_LOOP,
	AST_NODE_BREAK,
	AST_NODE_CONTINUE,
	AST_NODE_SWITCH,
	AST_NODE_CASE,
} AstNodeKind;

typedef enum {
	STORAGE_SPEC_NONE,
	STORAGE_SPEC_EXTERNAL,
	STORAGE_SPEC_STATIC,
} StorageSpecifier;

typedef struct {
	AstNode* first;
	AstNode* last;
	size_t count;
} NodeList;

struct Scope {
	uint64_t id;
	NodeList nodes;
};

void scope_append(Scope* scope, AstNode* node);

struct ExprArray {
	Expr** exprs;
	size_t count;
};

void parsed_node_list_append(NodeList* list, AstNode* node);

struct Block {
	NodeList nodes;
};

typedef enum {
	TYPE_QUALIFIER_NONE     = 0,
	TYPE_QUALIFIER_CONST    = 1 << 0,
	TYPE_QUALIFIER_VOLATILE = 1 << 1,
} TypeQualifiers;

typedef enum {
	TYPE_FLAG_NONE     = 0,
	TYPE_FLAG_SIGNED   = 1 << 8,
	TYPE_FLAG_UNSIGNED = 2 << 8,
} TypeKindFlags;

typedef enum {
	TYPE_VOID               = 0,

	TYPE_CHAR               = 1,
	TYPE_INT                = 2,
	TYPE_SHORT              = 3,
	TYPE_LONG               = 4,
	TYPE_LONG_LONG          = 5,
	TYPE_INT8               = 6,
	TYPE_INT16              = 7,
	TYPE_INT32              = 8,
	TYPE_INT64              = 9,

	TYPE_SIGNED_CHAR        = 1 | TYPE_FLAG_SIGNED,
	TYPE_SIGNED_INT         = 2 | TYPE_FLAG_SIGNED,
	TYPE_SIGNED_SHORT       = 3 | TYPE_FLAG_SIGNED,
	TYPE_SIGNED_LONG        = 4 | TYPE_FLAG_SIGNED,
	TYPE_SIGNED_LONG_LONG   = 5 | TYPE_FLAG_SIGNED,
	TYPE_SIGNED_INT8        = 6 | TYPE_FLAG_SIGNED,
	TYPE_SIGNED_INT16       = 7 | TYPE_FLAG_SIGNED,
	TYPE_SIGNED_INT32       = 8 | TYPE_FLAG_SIGNED,
	TYPE_SIGNED_INT64       = 9 | TYPE_FLAG_SIGNED,

	TYPE_UNSIGNED_CHAR      = 1 | TYPE_FLAG_UNSIGNED,
	TYPE_UNSIGNED_INT       = 2 | TYPE_FLAG_UNSIGNED,
	TYPE_UNSIGNED_SHORT     = 3 | TYPE_FLAG_UNSIGNED,
	TYPE_UNSIGNED_LONG      = 4 | TYPE_FLAG_UNSIGNED,
	TYPE_UNSIGNED_LONG_LONG = 5 | TYPE_FLAG_UNSIGNED,
	TYPE_UNSIGNED_INT8      = 6 | TYPE_FLAG_UNSIGNED,
	TYPE_UNSIGNED_INT16     = 7 | TYPE_FLAG_UNSIGNED,
	TYPE_UNSIGNED_INT32     = 8 | TYPE_FLAG_UNSIGNED,
	TYPE_UNSIGNED_INT64     = 9 | TYPE_FLAG_UNSIGNED,
	
	TYPE_SIZE_T             = 10,

	TYPE_FLOAT              = 11,
	TYPE_DOUBLE             = 12,
	TYPE_LONG_DOUBLE        = 13,

	TYPE_STRUCT             = 14,
	TYPE_UNION              = 15,
	TYPE_ENUM               = 16,

	TYPE_POINTER            = 17,
	TYPE_ARRAY              = 18,
	TYPE_FUNCTION           = 19,

	// _Bool
	TYPE_BOOL               = 20,
} TypeKind;

struct Type {
	TypeKind kind;

	TypeQualifiers qualifiers;
	TypeDef* alias_definition;

	union {
		Struct* struct_def;
		Struct* union_def;
		Enum* enum_def;
		Type* pointer_base_type;
		const FunctionPrototype* function;

		struct {
			Type* element_type;
			Expr* size;
		} array;
	};
};

typedef struct {
	TypeLayout pointer_type_layout;
} TypeContext;

TypeLayout type_get_layout(const TypeContext* context, const Type* type);
bool type_equal(const Type* a, const Type* b);
bool type_equal_ignore_qualifiers(const Type* a, const Type* b);
void type_array_to_pointer(const Type* type, Type* out_type);
void type_format(const Type* type, StringBuilder* builder);

inline bool type_kind_is_int(TypeKind kind) {
	TypeKind kind_without_sign_flags = kind & (TypeKind)(~(TYPE_FLAG_SIGNED | TYPE_FLAG_UNSIGNED));
	return (kind_without_sign_flags >= TYPE_CHAR
		&& kind_without_sign_flags <= TYPE_INT64)
		|| kind == TYPE_SIZE_T;
}

uint32_t type_get_int_convertion_rank(const Type* type);

inline bool type_kind_is_pointer_like(TypeKind kind) {
	return kind == TYPE_POINTER || kind == TYPE_ARRAY;
}

inline Type* type_extract_pointer_base_type(Type* type) {
	switch (type->kind) {
	case TYPE_POINTER:
		return type->pointer_base_type;
	case TYPE_ARRAY:
		return type->array.element_type;
	default:
		break;
	}

	unreachable();
	return NULL;
}

inline Struct* type_extract_compound(Type* type) {
	if (type->kind == TYPE_STRUCT) {
		return type->struct_def;
	}

	if (type->kind == TYPE_UNION) {
		return type->union_def;
	}

	return NULL;
}

bool type_is_struct(const Type* type, const Struct* struct_def);
bool type_is_enum(const Type* type, const Enum* enum_def);
bool type_is_callable(const Type* type);

//
// L & R Values
//

typedef enum {
	VALUE_L = 1,
	VALUE_R = 2,
} ValueKind;

//
// Expr
//

typedef enum {
	BIN_OP_ADD,
	BIN_OP_SUB,
	BIN_OP_MUL,
	BIN_OP_DIV,
	BIN_OP_MOD,

	BIN_OP_LOGICAL_AND,
	BIN_OP_LOGICAL_OR,

	BIN_OP_LOGICAL_EQUAL,
	BIN_OP_LOGICAL_NOT_EQUAL,
	BIN_OP_LOGICAL_LESS,
	BIN_OP_LOGICAL_GREATER,
	BIN_OP_LOGICAL_LESS_OR_EQUAL,
	BIN_OP_LOGICAL_GREATER_OR_EQUAL,

	BIN_OP_BITWISE_AND,
	BIN_OP_BITWISE_OR,
	BIN_OP_BITWISE_XOR,
	BIN_OP_BITWISE_SHIFT_LEFT,
	BIN_OP_BITWISE_SHIFT_RIGHT,

	BIN_OP_ASSIGNMENT,

	BIN_OP_ASSIGNMENT_BY_SUM,
	BIN_OP_ASSIGNMENT_BY_DIFFERENCE,
	BIN_OP_ASSIGNMENT_BY_PRODUCT,
	BIN_OP_ASSIGNMENT_BY_QUOTIENT,
	BIN_OP_ASSIGNMENT_BY_REMAINDER,

	BIN_OP_ASSIGNMENT_BY_BITWISE_AND,
	BIN_OP_ASSIGNMENT_BY_BITWISE_OR,
	BIN_OP_ASSIGNMENT_BY_BITWISE_XOR,
	BIN_OP_ASSIGNMENT_BY_BITWISE_SHIFT_LEFT,
	BIN_OP_ASSIGNMENT_BY_BITWISE_SHIFT_RIGHT,
} BinOpKind;

inline bool bin_op_is_compare(BinOpKind kind) {
	return kind >= BIN_OP_LOGICAL_EQUAL && kind <= BIN_OP_LOGICAL_GREATER_OR_EQUAL;
}

inline bool bin_op_is_assignment(BinOpKind kind) {
	return kind >= BIN_OP_ASSIGNMENT && kind <= BIN_OP_ASSIGNMENT_BY_BITWISE_SHIFT_RIGHT;
}

typedef enum {
	UNARY_OP_NEGATE,
	UNARY_OP_PLUS,
	UNARY_OP_ADDRESS,
	UNARY_OP_DEREFERENCE,

	UNARY_OP_LOGICAL_NOT,

	UNARY_OP_BITWISE_NOT,

	UNARY_OP_PRE_INCREMENT,
	UNARY_OP_POST_INCREMENT,

	UNARY_OP_PRE_DECREMENT,
	UNARY_OP_POST_DECREMENT,
} UnaryOpKind;

String bin_op_kind_to_string(BinOpKind op);
String unary_op_kind_to_string(UnaryOpKind op);
uint32_t bin_op_precedence(BinOpKind op);

struct BinExpr {
	BinOpKind op;

	// Common type represent a type both operands should be casted to before performing the binary
	// operation. This isn't necessarily same as the result type, although in case of an arithmetic
	// operation they match. For compare operations, the result is distict from the common type, and
	// is just a `TYPE_INT`
	//
	// `TypeKind` and `pointer_base_type` is enough to represent all possible arithmetic types,
	// since binary operations are only supported by arithmetic types 
	//
	// TODO: Include type qualifiers here
	TypeKind common_type_kind;
	Type* pointer_base_type;

	Expr* left;
	Expr* right;
};

// TODO: Include type qualifiers for UNARY_OP_ADDRESS
struct UnaryExpr {
	UnaryOpKind op;
	Expr* operand;
	PackedSourceRange operator_source_range;

	// For `UNARY_OP_ADDRESS`
	Type* pointer_base_type;
};

struct Call {
	Expr* callable;
	ExprArray args;
	PackedSourceRange right_paren_source_range;
};

struct StringLiteral {
	String full_string;

	// A string literal is just a fixed size array of chars.
	// This expression store the array size which matches the size of `full_string`.
	//
	// Why not just store the size as an int (or even completely avoid, as it is already packed
	// in the above string)?
	//
	// The string literal must also work with `expr_get_type`, which returns a result type of
	// an expr. For a string literal it must be an array type of fixed size.
	//
	// In the `Type` struct array size is kept as an expr.
	// So inside `expr_get_type` for a string literal, we need to return an array type that points
	// to size expr, however the `expr_get_type` is not allowed to allocate anything, so the only
	// way to this is to store a size expr in `StringLiteral` and make the array type point here.
	Expr* array_size_expr;
	PackedSourceRange source_range;
};

// TODO: When implmenenting wide char support,
//       can just add a EXPR_WIDE_CHAR_LITERAL,
//       instead of adding a flag here.
struct CharLiteral {
	uint32_t value;
	PackedSourceRange source_range;
};

struct IntegerLiteral {
	IntergerLiteralFormat format;
	TypeKind integer_type;
	uint64_t value;
	PackedSourceRange source_range;
};

struct ArrayIndex {
	Expr* array;
	Expr* index;
	PackedSourceRange right_bracket_source_range;
};

typedef enum {
	COMPOUND_LITERAL_VALUE,
	COMPOUND_LITERAL_FIELD_INIT,
	COMPOUND_LITERAL_ARRAY_ELEMENT_INIT,
} CompoundLiteralEntryKind;

struct CompoundLiteralEntry {
	CompoundLiteralEntryKind kind;

	union {
		struct {
			String name;
			size_t index;
		} field;
		struct {
			Expr* index;
		} array_element;
		struct {
			size_t index;
		} not_designated;
	};

	Expr* value;
};

struct CompoundLiteral {
	PackedSourceRange source_range;
	Type* type;

	CompoundLiteralEntry* entries;
	size_t entry_count;
};

typedef enum {
	EXPR_CALL,
	EXPR_BINARY,
	EXPR_UNARY,
	EXPR_FUNCTION_REFERENCE,
	EXPR_VARIABLE_REFERENCE,
	EXPR_INTEGER_LITERAL,
	EXPR_STRING_LITERAL,
	EXPR_CHAR_LITERAL,
	EXPR_COMPOUND_LITERAL,
	EXPR_ENUM_CONSTANT,
	EXPR_FUNCTION_PARAM,
	EXPR_ARRAY_INDEX,
	EXPR_INDIRECT_FIELD_ACCESS, // -> operator
	EXPR_DIRECT_FIELD_ACCESS, // . operator
	EXPR_CAST,
	EXPR_SIZE_OF_EXPR,
	EXPR_SIZE_OF_TYPE,
} ExprKind;

struct Expr {
	ExprKind kind;

	union {
		Call call;
		struct {
			Function* func;
			PackedSourceRange source_range;
		} function_ref;

		struct {
			Variable* var;
			PackedSourceRange source_range;
		} variable_ref;

		BinExpr binary;
		UnaryExpr unary;
		IntegerLiteral int_literal;
		StringLiteral string_literal;
		CharLiteral char_literal;
		ArrayIndex array_index;
		
		struct {
			const Enum* enum_def;
			size_t variant_index;
			PackedSourceRange source_range;
		} enum_constant;

		struct {
			const Function* function_def;
			size_t param_index;
			PackedSourceRange source_range;
		} function_param;

		struct {
			PackedSourceRange left_paren_source_range;
			Type* target_type;
			Expr* expr;
		} cast;

		struct {
			Expr* target;
			size_t field_index;
			PackedSourceRange source_range;
		} field_access;

		struct {
			Expr* expr;
			PackedSourceRange source_range;
		} size_of_expr;
		
		struct {
			Type* type;
			PackedSourceRange source_range;
		} size_of_type;

		CompoundLiteral compound_literal;
	};
};

void expr_get_type(Expr* expr, Type* out_type);
bool expr_is_bool(Expr* expr);
ValueKind expr_get_value_kind(Expr* expr);
PackedSourceRange expr_get_source_range(const Expr* expr);

//
// Struct
//

// `StructFieldNamespace` is a hash map that is used to map the field name
// to an actual field of this or an inner anonymous struct.
//
// Example:
//
// struct Nested {
//     struct Inner1 {
//         int a;
//         
//         struct Inner2 {
//             int inner_most_value;
//         };
//     };
//
//     String text;
// };
//
// For the above struct the next expressions are valid:
// 1. nested.text;
// 2. nested.a;
// 3. nested.inner_most_value;
//
// Thus the hash map would contain the next mappings:
// 1. text             -> Nested.text
// 2. a                -> Nested.Inner1.a
// 3. inner_most_value -> Nested.Inner1.Inner2.inner_most_value
struct StructFieldNamespaceEntry {
	const Struct* struct_def;
	size_t field_index;
};

struct StructFieldNamespace {
	String* keys;
	StructFieldNamespaceEntry* entries;
	size_t size;
	size_t capacity;
};

// Performs a lookup in the hashmap and returns the index of the found entry, or SIZE_MAX if not found
size_t struct_field_namespace_index_of(const StructFieldNamespace* struct_namespace, String name);

struct StructField {
	String name;
	PackedSourceRange name_source_range;
	Type type;
};

typedef enum {
	STRUCT_LAYOUT_KIND_STRUCT,
	STRUCT_LAYOUT_KIND_UNION,
} StructLayoutKind;

struct Struct {
	String name;
	PackedSourceRange name_source_range;
	StructLayoutKind layout_kind;
	uint32_t id;

	StructField* fields;
	size_t* field_offsets;
	size_t field_count;

	StructFieldNamespace* field_namespace;
	const Struct* next;

	TypeLayout type_layout;

	bool is_forward_declared;
};

inline const StructField* struct_find_field(const Struct* struct_def, String field_name) {
	assert(struct_def->field_namespace);
	size_t entry_index = struct_field_namespace_index_of(struct_def->field_namespace, field_name);
	if (entry_index == SIZE_MAX) {
		return NULL;
	}

	const StructFieldNamespaceEntry entry = struct_def->field_namespace->entries[entry_index];
	return &entry.struct_def->fields[entry.field_index];
}

//
// Enum
//

struct EnumVariant {
	String name;
	PackedSourceRange name_source_range;
	Expr* value;
};

struct Enum {
	String name;
	PackedSourceRange name_source_range;

	bool is_forward_declared;

	EnumVariant* variants;
	size_t variant_count;
};

//
// TypeDef
//

struct TypeDef {
	Type aliased_type;
	String new_name;
	PackedSourceRange new_name_source_range;
};

//
// DeclSpec
// 

typedef enum {
	DECL_SPEC_DEPRECATED,
	DECL_SPEC_NO_INLINE,
	DECL_SPEC_NO_RETURN,
	DECL_SPEC_DLL_IMPORT,
	DECL_SPEC_DLL_EXPORT,
	DECL_SPEC_RESTRICT,
} DeclSpecKind;

struct DeclSpec {
	DeclSpecKind kind;

	union {
		StringLiteral deprecation_text;
	};

	DeclSpec* next;
};

//
// Function
//

typedef enum {
	FUNC_CALL_CONV_CDECL,
} FunctionCallingConvention;

String function_calling_convetion_to_string(FunctionCallingConvention conv);

struct FunctionParam {
	Type type;
	String name;
	PackedSourceRange name_source_range;
};

struct FunctionPrototype {
	Type return_type;
	String name;
	FunctionCallingConvention calling_convention;
	bool has_va_args;
	size_t parameter_count;
	FunctionParam* parameters;
};

struct Function {
	FunctionPrototype proto;

	// The function type
	Type type;

	uint32_t id;
	bool is_inline;
	bool is_forward_declared;
	Scope* body;
	DeclSpec* decl_spec;
	StorageSpecifier storage_specifier;
	uint32_t var_count;

	// Number of `EXPR_CALL` in the body of this function.
	uint32_t function_call_count;
};

//
// Variable
//

struct Variable {
	String name;
	Type type;
	Expr* value;
	StorageSpecifier storage_specifier;
	uint32_t id;
};

//
// ReturnStmt
//

struct ReturnStmt {
	Expr* value;
};

//
// IfStmt
//

struct IfStmt {
	Expr condition;
	Scope* true_scope;

	// This one is optional
	Scope* false_scope;
};

//
// Loops
//

typedef enum {
	WHILE_LOOP_PRE_CONDITION,
	WHILE_LOOP_POST_CONDITION,
} WhileLoopConditionKind;

struct WhileLoop {
	WhileLoopConditionKind condition_kind;
	Expr condition;
	Scope* body_scope;

	uint32_t break_count;
	uint32_t continue_count;
};

struct ForLoop {
	// Loop Scope that includes the `init_stmt`
	Scope* loop_scope;
	AstNode* init_stmt;
	Expr* condition;
	Expr* advance_expr;
	Scope* body_scope;

	uint32_t break_count;
	uint32_t continue_count;
};

//
// Switch
//

struct Switch {
	Expr* expr;
	Scope* body;
	uint32_t break_count;
	bool has_default;
};

struct Case {
	// If `value` is null this is a `default` case.
	Expr* value;
};

//
// Node
//

struct AstNode {
	AstNodeKind kind;
	AstNode* next;

	Scope* parent_scope;

	union {
		Struct* struct_def;
		Struct* union_def;
		Enum* enum_def;
		TypeDef* type_def;
		Function* function_def;
		Expr expr;
		Variable variable;
		ReturnStmt return_stmt;
		Scope block;
		IfStmt if_stmt;
		WhileLoop while_loop;
		ForLoop for_loop;
		Switch switch_stmt;
		Case case_stmt;
	};
};

//
// AST
//

typedef struct {
	uint32_t compound_type_count;
	uint32_t function_def_count;
} ASTStatistics;

typedef struct {
	NodeList root_nodes;
	ASTStatistics stats;

	Struct* first_compound_type;
	Struct* last_compound_type;
} AST;

void print_parsed_node(const AstNode* node);

#endif
