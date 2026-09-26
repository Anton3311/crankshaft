#ifndef PARSER_H
#define PARSER_H

#include "core/core.h"
#include "parser/ast.h"

// A bit unnecessary, a forward declaration would've been enough
#include "parser/tokenizer.h"
#include "parser/diagnostics.h"
#include "parser/preprocessor.h"

typedef struct IdentifierStorage IdentifierStorage;
typedef struct IdentifierEntry IdentifierEntry;
typedef struct IdentifierScope IdentifierScope;
typedef struct IdentifierNamespace IdentifierNamespace;

//
// IdentifierStorage
//

#define IDENT_CATEGORY_BIT_OFFSET 4

typedef enum {
	IDENT_CATEGORY_TYPE        = 1 << IDENT_CATEGORY_BIT_OFFSET,
	IDENT_CATEGORY_VAR_OR_FUNC = 2 << IDENT_CATEGORY_BIT_OFFSET,
	IDENT_CATEGORY_CONSTANT    = 4 << IDENT_CATEGORY_BIT_OFFSET,
} IdentifierCategory;

typedef enum {
	IDENT_STRUCT         = IDENT_CATEGORY_TYPE        | 0,
	IDENT_UNION          = IDENT_CATEGORY_TYPE        | 1,
	IDENT_ENUM           = IDENT_CATEGORY_TYPE        | 2,
	IDENT_TYPE_DEF       = IDENT_CATEGORY_TYPE        | 3,
	IDENT_VARIABLE       = IDENT_CATEGORY_VAR_OR_FUNC | 4,
	IDENT_FUNCTION       = IDENT_CATEGORY_VAR_OR_FUNC | 5,
	IDENT_FUNCTION_PARAM = IDENT_CATEGORY_VAR_OR_FUNC | 6,
	IDENT_ENUM_CONSTANT  = IDENT_CATEGORY_CONSTANT    | 7,

	IDENT_KIND_MAX,
} IdentifierEntryKind;

#define IDENT_ENTRY_INDEX_MASK ((1 << IDENT_CATEGORY_BIT_OFFSET) - 1)
#define IDENT_ENTRY_KIND_COUNT (((IDENT_KIND_MAX - 1) & IDENT_ENTRY_INDEX_MASK) + 1)

inline uint8_t ident_entry_kind_index(IdentifierEntryKind kind) {
	return (uint8_t)kind & IDENT_ENTRY_INDEX_MASK;
}

typedef enum {
	IDENT_NAMESPACE_DEFAULT,
	IDENT_NAMESPACE_TAGGED,
	IDENT_NAMESPACE_ALIAS,
	IDENT_NAMESPACE_COUNT
} IdentifierNamespaceKind;

struct IdentifierEntry {
	String name;
	PackedSourceRange name_source_range;
	IdentifierEntryKind kind;
	IdentifierNamespaceKind owner_namespace;

	IdentifierScope* owner_scope;

	IdentifierEntry* next_in_scope;
	IdentifierEntry* prev;

	union {
		Struct* struct_def;
		Struct* union_def;
		Enum* enum_def;
		TypeDef* type_def;
		Function* function_def;
		Variable* variable;

		struct {
			Enum* enum_def;
			size_t variant_index;
		} enum_constant;

		struct {
			Function* function_def;
			size_t param_index;
		} function_param;
	};
};

typedef enum {
	IDENT_FIND_IN_CURRENT_SCOPE,
	IDENT_FIND_IN_ALL_PARENT_SCOPES,

	IDENT_FIND_DEFAULT = IDENT_FIND_IN_ALL_PARENT_SCOPES,
} IdentifierFindOption;

struct IdentifierScope {
	// Incremented by 1 for each new instance.
	uint64_t id;

	union {
		IdentifierScope* parent;

		// NOTE: Only used for reusing instances in `IdentifierStorage`
		IdentifierScope* next_free;
	};

	IdentifierEntry* first_identifier;
	IdentifierEntry* last_identifier;

	uint32_t nested_entry_count[IDENT_ENTRY_KIND_COUNT];
	uint32_t entry_count[IDENT_ENTRY_KIND_COUNT];
};

struct IdentifierNamespace {
	size_t count;
	size_t capacity;

	// NOTE: Keys and entries are allocated from the same memory region
	String* keys;
	IdentifierEntry** entries;
};

struct IdentifierStorage {
	Arena* allocator;

	Allocator namespace_allocator;
	IdentifierNamespace namespaces[IDENT_NAMESPACE_COUNT];

	uint64_t next_scope_id;
	IdentifierScope* current_scope;

	IdentifierEntry* next_free_entry;
	IdentifierScope* next_free_scope;
};

void ident_storage_init(IdentifierStorage* storage, Allocator namespace_allocator, Arena* allocator);
void ident_storage_release(IdentifierStorage* storage);

IdentifierEntry* ident_storage_find(IdentifierStorage* storage,
		IdentifierNamespaceKind namespace_kind,
		IdentifierFindOption option,
		String name);

IdentifierEntry* ident_storage_insert(IdentifierStorage* storage,
		IdentifierNamespaceKind namespace_kind,
		IdentifierEntryKind entry_kind,
		String name,
		PackedSourceRange name_source_range);

void ident_storage_remove(IdentifierStorage* storage,
		IdentifierNamespaceKind namespace_kind,
		String name);

IdentifierScope* ident_storage_begin_scope(IdentifierStorage* storage);
void ident_storage_end_scope(IdentifierStorage* storage);

//
// Loop & Switch State
//

typedef struct ParserLoopOrSwitchState ParserLoopOrSwitchState;
struct ParserLoopOrSwitchState {
	AstNode* node;
	ParserLoopOrSwitchState* parent;
};

//
// Parser
//

typedef struct {
	Arena* ast_allocator;
	Arena* temp_allocator;

	Diagnostics* diagnostics;
	Preprocessor* preprocessor;

	IdentifierStorage* ident_storage;
	const TypeContext* type_context;

	uint32_t next_var_id;

	AST* ast;
	Function* current_function;

	AstNode* dummy_node;
	
	ParserLoopOrSwitchState* loop_or_switch_state;
} Parser;

void parser_init(Parser* parser,
		Arena* ast_allocator,
		Arena* temp_allocator,
		IdentifierStorage* ident_storage,
		const TypeContext* type_context,
		Preprocessor* preprocessor,
		Diagnostics* diagnostics);

void parser_parse(Parser* parser, AST* ast);

#endif
