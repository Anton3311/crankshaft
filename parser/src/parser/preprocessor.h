#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H

#include "parser/tokenizer.h"
#include "parser/source_info.h"
#include "parser/diagnostics.h"

typedef struct Preprocessor Preprocessor;
typedef struct MacroDefinition MacroDefinition;
typedef struct PreprocessorBranchRegion PreprocessorBranchRegion;

#define MACRO_TABLE_INITIAL_CAPACITY 64

typedef struct {
	Allocator allocator;
	size_t capacity;
	size_t count;

	String* keys;
	MacroDefinition* macros;
} MacroTable;

void macro_table_append(MacroTable* table, const MacroDefinition* macro);
bool macro_table_remove(MacroTable* table, String name);
const MacroDefinition* macro_table_find(const MacroTable* table, String name);

//
// Preprocessor
//

typedef enum {
	MACRO_CALL_TOKEN,
	MACRO_CALL_ARGUMENT_EXPANSION,
	MACRO_CALL_VA_ARGS_EXPANSION,
} MacroCallState;

typedef struct {
	// Which macro call argument is being expanded
	size_t arg_index;
	size_t arg_token_index;
} MacroCallArgExpansion;

typedef struct {
	size_t arg_index;
	size_t arg_token_index;
} MacroCallVaArgsExpansion;

typedef struct {
	const MacroDefinition* macro;
	size_t token_index;

	MacroCallState state;

	union {
		MacroCallArgExpansion arg_expansion;
		MacroCallVaArgsExpansion va_args_expansion;
	};

	// Size matches the number of macro parameters
	TokenArray* argument_tokens;
	size_t argument_count;
	SourceRange call_source_range;

	Arena* used_allocator;
	size_t arena_size_before_call;
	size_t arena_size_after_call;
} MacroCall;

typedef enum {
	DIRECTIVE_INCLUDE,
	DIRECTIVE_DEFINE,
	DIRECTIVE_UNDEF,
	DIRECTIVE_IF,
	DIRECTIVE_ELIF,
	DIRECTIVE_ELSE,
	DIRECTIVE_ENDIF,
	DIRECTIVE_IFDEF,
	DIRECTIVE_IFNDEF,
	DIRECTIVE_PRAGMA,
	DIRECTIVE_ERROR,
} DirectiveKind;

String directive_kind_to_string(DirectiveKind kind);

typedef struct {
	DirectiveKind kind;
	SourceRange source_range;
} ParsedDirective;

// Here a region is piece of code between conditional directives, like #if and #endif or #if, #else and #endif.
struct PreprocessorBranchRegion {
	ParsedDirective current_directive; // are we in an #if, #elif or #else etc block?
	bool is_enabled;
	bool alternative_branch_is_taken;
};

typedef struct {
	MacroCall* frames;
	size_t depth;
	size_t capacity;
} MacroCallStack;

typedef struct {
	Tokenizer* includes;
	size_t depth;
	size_t capacity;
} IncludeStack;

typedef struct {
	Allocator allocator;
	size_t capacity;
	size_t count;
	const SourceFile** entries;
} IncludeHistory;

void include_history_release(IncludeHistory* history);
bool include_history_contains(IncludeHistory* history, const SourceFile* source_file);
bool include_history_try_insert(IncludeHistory* history, const SourceFile* source_file);

typedef struct {
	Token path_token;
} IncludeEvent;

typedef void(*IncludeCallback)(Preprocessor* state, void* user_data, const IncludeEvent* event);

static size_t MIN_BRANCH_REGION_STACK_DEPTH = 1;

struct Preprocessor {
	Arena* allocator;
	Arena* temp_allocator;

	// `generated_tokens_allocator` is used to allocate tokens
	// that are generated on the fly, and can't be encountered
	// in the actual source code. Such tokens are produced by
	// builtin macros like: __FILE__ or __LINE__.
	Arena* generated_tokens_allocator;
	Diagnostics* diagnostics;
	SourceStorage* source_storage;

	IncludeStack include_stack;
	IncludeHistory include_history;

	// NOTE: This is the current tokenizer,
	//       which is stored on top of the `include_stack`
	//       and must at all times reflect it.
	Tokenizer* tokenizer;
	MacroTable macro_table;
	MacroCallStack macro_call_stack;

	bool has_pending_next_token;
	Token pending_next_token;

	// Used to keep track of regions marked by #if and #endif (and other #else, #elif directives)
	//
	// There must always be at least one active region in the stack.
	// This region represents is the root of the file. It is always enabled
	PreprocessorBranchRegion* branch_stack;
	size_t branch_stack_depth;
	size_t branch_stack_capacity;

	IncludeCallback include_callback;
	void* include_callback_user_data;

	const SourceFile* initial_file;
};

typedef enum {
	MACRO_STYLE_DEFAULT,
	MACRO_STYLE_FUNCTION,
} MacroStyle;

// Tells the preprocessor how to interpret the identifier token
// in the defintion of the macro
typedef enum {
	MACRO_TOKEN_HINT_NONE,
	MACRO_TOKEN_HINT_PARAMETER,
	MACRO_TOKEN_HINT_STRING_OPERATOR,
	MACRO_TOKEN_HINT_TOKEN_INSERT_OPERATOR,
	MACRO_TOKEN_HINT_VA_ARGS,
} MacroTokenHintKind;

typedef struct {
	MacroTokenHintKind kind;
	union {
		struct {
			size_t index;
		} param;

		struct {
			size_t param_index;
		} string_op;
		
		struct {
			size_t param_index;
		} token_insert_op;
	};
} MacroTokenHint;

typedef enum {
	BUILTIN_MACRO_NONE,
	BUILTIN_MACRO_LINE,
	BUILTIN_MACRO_FILE,
	BUILTIN_MACRO_STDC,
} BuiltinMacroKind;

struct MacroDefinition {
	String name;
	PackedSourceRange name_source_range;
	BuiltinMacroKind builtin_kind;

	MacroStyle style;

	String* parameter_names;
	size_t parameter_count;

	bool has_va_args;
	
	Token* tokens;
	MacroTokenHint* token_hints;
	size_t token_count;
};

void preprocessor_init(Preprocessor* state,
		SourceStorage* source_storage,
		const SourceFile* source_file,
		Diagnostics* diagnostics,
		Allocator gpa,
		Arena* allocator,
		Arena* temp_allocator,
		Arena* generated_tokens_allocator);

void preprocessor_release(Preprocessor* state);

// Returns the next token, without consuming it.
// The next call to `preprocessor_view_next` or
// `preprocessor_next_token` will return the same token.
Token preprocessor_view_next(Preprocessor* state);

// Returns the next token, while also consuming it.
Token preprocessor_next_token(Preprocessor* state);

#endif
