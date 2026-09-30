#include "preprocessor.h"

#include "parser/parse_tools.h"

#define REMOVED_MACRO_FLAG (void*)0x1

#define PREPROCESSOR_LOG 0

static size_t _macro_table_find_empty_slot(MacroTable* table, String key) {
	profile_scope_start(__func__);

	size_t hash = hash_string(key);
	for (size_t i = 0; i < table->capacity; i += 1) {
		size_t index = (hash + i) % table->capacity;
		String macro_name = table->macros[index].name;

		if (macro_name.v == NULL || macro_name.v == REMOVED_MACRO_FLAG) {
			profile_scope_end();
			return index;
		}
	}

	profile_scope_end();
	return SIZE_MAX;
}

static void _macro_table_grow(MacroTable* table) {
	profile_scope_start(__func__);
	size_t new_capacity = max(MACRO_TABLE_INITIAL_CAPACITY, table->capacity * 2);

	MacroDefinition* old_macros = table->macros;
	size_t old_capactiy = table->capacity;

	table->macros = allocator_alloc_array(table->allocator,
			MacroDefinition,
			new_capacity);
	table->capacity = new_capacity;

	memset(table->macros, 0, new_capacity * sizeof(*table->macros));

	for (size_t i = 0; i < old_capactiy; i += 1) {
		String key = old_macros[i].name;
		if (key.v == NULL || key.v == REMOVED_MACRO_FLAG ) {
			continue;
		}

		size_t empty_slot = _macro_table_find_empty_slot(table, key);
		assert(empty_slot != SIZE_MAX);

		table->macros[empty_slot] = old_macros[i];
	}

	profile_scope_end();
}

void macro_table_append(MacroTable* table, const MacroDefinition* macro) {
	profile_scope_start(__func__);
	if (table->count * 2 >= table->capacity) {
		_macro_table_grow(table);
	}

	size_t slot = _macro_table_find_empty_slot(table, macro->name);
	assert(slot != SIZE_MAX);

	table->macros[slot] = *macro;
	table->count += 1;

	profile_scope_end();
}

bool macro_table_remove(MacroTable* table, String name) {
	profile_scope_start(__func__);

	size_t hash = hash_string(name);
	for (size_t i = 0; i < table->capacity; i += 1) {
		size_t index = (hash + i) % table->capacity;
		String macro_name = table->macros[index].name;

		if (macro_name.v == NULL || macro_name.v == REMOVED_MACRO_FLAG) {
			profile_scope_end();
			return false;
		} else if (str_equal(macro_name, name)) {
			table->macros[index].name.v = REMOVED_MACRO_FLAG;
			profile_scope_end();
			return true;
		}
	}

	profile_scope_end();
	return false;
}

const MacroDefinition* macro_table_find(const MacroTable* table, String name) {
	profile_scope_start(__func__);

	size_t hash = hash_string(name);
	for (size_t i = 0; i < table->capacity; i += 1) {
		size_t index = (hash + i) % table->capacity;
		String macro_name = table->macros[index].name;

		if (macro_name.v == NULL || macro_name.v == REMOVED_MACRO_FLAG) {
			profile_scope_end();
			return NULL;
		} else if (str_equal(macro_name, name)) {
			profile_scope_end();
			return &table->macros[index];
		}
	}

	profile_scope_end();
	return NULL;
}

//
// IncludeHistory
//

inline size_t _include_history_max_entry_count(size_t capacity) {
	return capacity * 80 / 100;
}

const SourceFile** _include_history_find_entry(IncludeHistory* history,
		const SourceFile* source_file) {

	profile_scope_start(__func__);
	assert(source_file);

	const SourceFile** insert_entry = NULL;

	size_t hash = hash_ptr(source_file);
	for (size_t i = 0; i < history->capacity; i += 1) {
		size_t index = (hash + i) % history->capacity;
		if (history->entries[index] == NULL) {
			insert_entry = &history->entries[index];
			break;
		}
	}

	profile_scope_end();
	return insert_entry;
}

void include_history_grow(IncludeHistory* history) {
	profile_scope_start(__func__);

	const SourceFile** old_entries = history->entries;
	size_t old_capacity = history->capacity;

	size_t new_capacity = max(16, history->capacity * 2);
	history->entries = allocator_alloc_array(history->allocator, const SourceFile*, new_capacity);
	history->capacity = new_capacity;

	memset(history->entries, 0, sizeof(*history->entries) * new_capacity);

	if (old_capacity) {
		assert(old_entries);
	}

	for (size_t i = 0; i < old_capacity; i += 1) {
		if (old_entries[i] == NULL) {
			continue;
		}

		const SourceFile** insert_entry = _include_history_find_entry(history, old_entries[i]);
		assert(insert_entry);

		*insert_entry = old_entries[i];
	}

	if (old_entries) {
		allocator_release(history->allocator, old_entries);
	}

	profile_scope_end();
}

void include_history_release(IncludeHistory* history) {
	profile_scope_start(__func__);

	if (history->entries) {
		allocator_release(history->allocator, history->entries);
	}

	*history = (IncludeHistory) {};

	profile_scope_end();
}

bool include_history_contains(IncludeHistory* history, const SourceFile* source_file) {
	profile_scope_start(__func__);

	if (history->count == 0) {
		profile_scope_end();
		return false;
	}

	size_t index = hash_ptr(source_file) % history->capacity;
	const SourceFile** it = history->entries + index;
	const SourceFile** end = history->entries + history->capacity;
	while (it != end) {
		if (*it == source_file) {
			profile_scope_end();
			return true;
		} else if (*it == NULL) {
			profile_scope_end();
			return false;
		}

		it += 1;
	}

	profile_scope_end();
	return false;
}

bool include_history_try_insert(IncludeHistory* history, const SourceFile* source_file) {
	profile_scope_start(__func__);

	if (history->count >= _include_history_max_entry_count(history->capacity)) {
		include_history_grow(history);
	}

	const SourceFile** entry = _include_history_find_entry(history, source_file);
	if (entry == NULL) {
		profile_scope_end();
		return false;
	}
	
	assert(*entry == NULL);

	*entry = source_file;
	history->count += 1;
	profile_scope_end();
	return true;
}

//
// TokenProvider
//

typedef Token(*TokenProvider_Next)(void* data);
typedef Token(*TokenProvider_ViewNext)(void* data);

typedef struct {
	TokenProvider_Next next;
	TokenProvider_ViewNext view_next;
} TokenProviderVTable;

typedef struct {
	void* data;
	TokenProviderVTable* vtable;
} TokenProvider;

inline Token token_provider_next(TokenProvider self) {
	return self.vtable->next(self.data);
}

inline Token token_provider_view_next(TokenProvider self) {
	return self.vtable->view_next(self.data);
}

//
// Preprocessor
//

void _preprocessor_skip_until_newline(Preprocessor* state);

// Returns SIZE_MAX if not found
size_t _macro_find_param_by_name(const MacroDefinition* macro, String param_name);
bool _preprocessor_push_file(Preprocessor* state, const SourceFile* source_file);
void _preprocessor_pop_file(Preprocessor* state);

static MacroCall* _preprocessor_init_macro_call(Preprocessor* state,
		TokenProvider token_provider,
		const MacroDefinition* macro,
		Token macro_call_ident);

static bool _preprocessor_get_next_macro_expansion_token(const SourceFile* source_file,
		MacroCallStack* macro_call_stack,
		Arena* generated_tokens_allocator,
		Token* out_token);

inline const SourceFile* _preprocessor_current_file(const Preprocessor* state) {
	return state->tokenizer->source_file;
}

void preprocessor_init(Preprocessor* state,
		SourceStorage* source_storage,
		const SourceFile* source_file,
		Diagnostics* diagnostics,
		Allocator gpa,
		Arena* allocator,
		Arena* temp_allocator,
		Arena* generated_tokens_allocator) {
	profile_scope_start(__func__);

	assert(allocator != generated_tokens_allocator);
	assert(temp_allocator != generated_tokens_allocator);

	state->allocator = allocator;
	state->temp_allocator = temp_allocator;
	state->generated_tokens_allocator = generated_tokens_allocator;
	state->source_storage = source_storage;
	state->diagnostics = diagnostics;
	state->initial_file = source_file;

	state->macro_table.allocator = gpa;

	state->include_stack.depth = 0;
	state->include_stack.capacity = 32;
	state->include_stack.includes = arena_alloc_array(state->allocator, Tokenizer, state->include_stack.capacity);
	
	state->include_history = (IncludeHistory) { .allocator = gpa };

	state->branch_stack_depth = MIN_BRANCH_REGION_STACK_DEPTH;
	state->branch_stack_capacity = 64;
	state->branch_stack = arena_alloc_array(
			state->allocator,
			PreprocessorBranchRegion,
			sizeof(*state->branch_stack) * state->branch_stack_capacity);

	state->branch_stack[0].is_enabled = true;

	bool file_included = _preprocessor_push_file(state, source_file);
	assert_msg(file_included, "Failed to push initial source file to the stack");

	{
		MacroDefinition line_macro = {
			.name = STR_LIT("__LINE__"),
			.builtin_kind = BUILTIN_MACRO_LINE,
			.token_count = 1,
		};

		macro_table_append(&state->macro_table, &line_macro);

		MacroDefinition file_macro = {
			.name = STR_LIT("__FILE__"),
			.builtin_kind = BUILTIN_MACRO_FILE,
			.token_count = 1,
		};

		macro_table_append(&state->macro_table, &file_macro);

		MacroDefinition stdc_macro = {
			.name = STR_LIT("__STDC__"),
			.builtin_kind = BUILTIN_MACRO_STDC,
			.token_count = 1,
		};

		macro_table_append(&state->macro_table, &stdc_macro);

		MacroDefinition __pragma_macro = {
			.name = STR_LIT("__pragma"),
			.style = MACRO_STYLE_FUNCTION,
			.builtin_kind = BUILTIN_MACRO_NONE,
			.has_va_args = true,
			.token_count = 0,
			.tokens = NULL,
			.token_hints = NULL,
		};

		macro_table_append(&state->macro_table, &__pragma_macro);

		MacroDefinition _win64_macro = {
			.name = STR_LIT("_WIN64"),
			.style = MACRO_STYLE_DEFAULT,
			.builtin_kind = BUILTIN_MACRO_NONE,
			.has_va_args = 0,
			.token_count = 0,
			.tokens = NULL,
			.token_hints = NULL,
		};

		macro_table_append(&state->macro_table, &_win64_macro);

		MacroDefinition __crankshaft__macro = {
			.name = STR_LIT("__crankshaft__"),
			.style = MACRO_STYLE_DEFAULT,
			.builtin_kind = BUILTIN_MACRO_NONE,
			.has_va_args = 0,
			.token_count = 0,
			.tokens = NULL,
			.token_hints = NULL,
		};

		macro_table_append(&state->macro_table, &__crankshaft__macro);
	}

	const size_t call_stack_capacity = 32;
	state->macro_call_stack = (MacroCallStack) {
		.depth = 0,
		.capacity = call_stack_capacity,
		.frames = arena_alloc_array(state->allocator, MacroCall, call_stack_capacity),
	};

	profile_scope_end();
}

void preprocessor_release(Preprocessor* state) {
	profile_scope_start(__func__);
	MacroTable* table = &state->macro_table;
	if (table->count > 0) {
		assert(table->macros);
		allocator_release(table->allocator, table->macros);
	} else {
		assert(table->macros == NULL);
	}

	include_history_release(&state->include_history);
	profile_scope_end();
}

bool _preprocessor_push_file(Preprocessor* state, const SourceFile* source_file) {
	IncludeStack* stack = &state->include_stack;

	if (stack->depth >= stack->capacity) {
		return false;
	}

	stack->depth += 1;

	Tokenizer* tokenizer = &stack->includes[stack->depth - 1];
	tokenizer_init(tokenizer, source_file);

	state->tokenizer = tokenizer;
	return true;
}

void _preprocessor_pop_file(Preprocessor* state) {
	IncludeStack* stack = &state->include_stack;
	assert(stack->depth > 0);

	const SourceFile* included_file = stack->includes[stack->depth - 1].source_file;
#if PREPROCESSOR_LOG
	debug_log_info("end of include %.*s", STR_FMT(included_file->path));
#endif

	stack->depth -= 1;

	if (stack->depth > 0) {
		state->tokenizer = &stack->includes[stack->depth - 1];
	} else {
		state->tokenizer = NULL;
	}
}

//
// MacroOrSourceTokenProviderState
//

typedef struct {
	const SourceFile* source_file;
	MacroCallStack* macro_call_stack;
	Arena* generated_tokens_allocator;

	TokenProvider fallback_token_provider;

	Token next_token;
	bool has_next;
	bool next_token_generated_by_tokenizer;
} MacroOrSourceTokenProviderState;

static TokenProviderVTable s_macro_or_source_token_provider_vtable;

static Token _macro_or_source_token_provider_next(void* data);
static Token _macro_or_source_token_provider_view_next(void* data);

void _macro_or_source_token_provider_init(Preprocessor* preprocessor,
		TokenProvider* provider,
		MacroOrSourceTokenProviderState* state,
		TokenProvider fallback_token_provider) {
	assert(provider);
	assert(state);

	if (s_macro_or_source_token_provider_vtable.next == NULL) {
		s_macro_or_source_token_provider_vtable.next = _macro_or_source_token_provider_next;
		s_macro_or_source_token_provider_vtable.view_next = _macro_or_source_token_provider_view_next;
	}

	state->source_file = _preprocessor_current_file(preprocessor);
	state->macro_call_stack = &preprocessor->macro_call_stack;
	state->generated_tokens_allocator = preprocessor->generated_tokens_allocator;
	state->fallback_token_provider = fallback_token_provider;

	provider->data = (void*)state;
	provider->vtable = &s_macro_or_source_token_provider_vtable;
}

Token _macro_or_source_token_provider_next(void* data) {
	MacroOrSourceTokenProviderState* state = (MacroOrSourceTokenProviderState*)data;
	if (state->has_next) {
		state->has_next = false;

		if (state->next_token_generated_by_tokenizer) {
			state->next_token_generated_by_tokenizer = false;
			
			// Advance the tokenizer, because the `next_token`
			// was generated by `tokenizer_view_next` in the
			// `_macro_or_source_token_provider_view_next` call
			Token token = token_provider_next(state->fallback_token_provider);
			Token next_token = state->next_token;

			assert(token.kind == next_token.kind);
			assert(token.string.v == next_token.string.v);
			assert(token.string.length == next_token.string.length);
		}

		return state->next_token;
	}

	Token token = {};
	if (state->macro_call_stack->depth > 0) {
		bool result = _preprocessor_get_next_macro_expansion_token(state->source_file,
				state->macro_call_stack,
				state->generated_tokens_allocator,
				&token);

		if (result) {
			return token;
		}
	}

	return token_provider_next(state->fallback_token_provider);
}

Token _macro_or_source_token_provider_view_next(void* data) {
	MacroOrSourceTokenProviderState* state = (MacroOrSourceTokenProviderState*)data;
	if (state->has_next) {
		return state->next_token;
	}

	MacroCallStack* macro_call_stack = state->macro_call_stack;

	Token token = {};
	if (macro_call_stack->depth > 0) {
		bool result = _preprocessor_get_next_macro_expansion_token(state->source_file,
				macro_call_stack,
				state->generated_tokens_allocator,
				&token);

		if (result) {
			state->has_next = true;
			state->next_token = token;
			state->next_token_generated_by_tokenizer = false;
			return token;
		}
	}

	token = token_provider_view_next(state->fallback_token_provider);

	state->has_next = true;
	state->next_token = token;
	state->next_token_generated_by_tokenizer = true;
	return token;
}

//
// TokenizerTokenProvider
//

static TokenProviderVTable s_tokenizer_token_provider_vtable;

static void _tokenizer_token_provider_init(TokenProvider* token_provider, Tokenizer* tokenizer) {
	assert(token_provider);
	assert(tokenizer);

	if (s_tokenizer_token_provider_vtable.next == NULL) {
		s_tokenizer_token_provider_vtable.next = (TokenProvider_Next)tokenizer_next_token;
		s_tokenizer_token_provider_vtable.view_next = (TokenProvider_ViewNext)tokenizer_view_next;
	}

	token_provider->data = (void*)tokenizer;
	token_provider->vtable = &s_tokenizer_token_provider_vtable;
}

//
// ArrayTokenProvider
//

static TokenProviderVTable s_array_token_provider_vtable;

typedef struct {
	const SourceFile* source_file;
	const Token* tokens;
	size_t count;
	size_t next_index;
} ArrayTokenProvider;

static Token _array_token_provider_next(void* data);
static Token _array_token_provider_view_next(void* data);

void _array_token_provider_init(TokenProvider* token_provider, ArrayTokenProvider* state) {
	assert(token_provider);

	if (s_array_token_provider_vtable.next == NULL) {
		s_array_token_provider_vtable.next = _array_token_provider_next;
		s_array_token_provider_vtable.view_next = _array_token_provider_view_next;
	}

	token_provider->data = state;
	token_provider->vtable = &s_array_token_provider_vtable;
}

Token _array_token_provider_next(void* data) {
	ArrayTokenProvider* state = (ArrayTokenProvider*)data;
	if (state->next_index >= state->count) {
		size_t source_length = state->source_file->source_code.length;

		Token token;
		token.kind = TOKEN_EOF;
		token.source_range = (SourceRange) {
			.source_file = state->source_file,
			.start = source_length,
			.end = source_length,
		};

		return token;
	}

	state->next_index += 1;
	return state->tokens[state->next_index - 1];
}

Token _array_token_provider_view_next(void* data) {
	ArrayTokenProvider* state = (ArrayTokenProvider*)data;

	if (state->next_index >= state->count) {
		size_t source_length = state->source_file->source_code.length;

		Token token;
		token.kind = TOKEN_EOF;
		token.source_range = (SourceRange) {
			.source_file = state->source_file,
			.start = source_length,
			.end = source_length,
		};

		return token;
	}

	return state->tokens[state->next_index];
}

// Generates an array of tokens that are encountered on the same line.
// Whenever a backslash is encountered, it continues consuming tokens of the next line.
//
// Used to get an array of tokens that belong to a directive.
TokenArray _generate_directive_token_stream(Arena* allocator,
		Tokenizer* tokenizer,
		const SourceFile* source_file,
		uint32_t initial_line_index) {
	profile_scope_start(__func__);

	TokenArray tokens = {};
	tokens.tokens = arena_alloc_array(allocator, Token, 0);
	tokens.count = 0;

	uint32_t current_line_index = initial_line_index;
	SourceRange current_line_range  = line_info_get_line_range(&source_file->line_info, current_line_index);
	current_line_range.source_file = source_file;

	while (true) {
		Token token = tokenizer_view_next(tokenizer);
		if (token.kind == TOKEN_BACKWARD_SLASH) {
			current_line_index += 1;
			tokenizer_reset_to_token(tokenizer, token);

			SourceRange source_range = line_info_get_line_range(&source_file->line_info, current_line_index);
			current_line_range.start = source_range.start;
			current_line_range.end = source_range.end;
			continue;
		}

		bool is_on_current_line = token.source_range.start >= current_line_range.start
			&& token.source_range.end <= current_line_range.end;

		if (is_on_current_line) {
			*arena_alloc(allocator, Token) = token;
			tokens.count += 1;

			tokenizer_reset_to_token(tokenizer, token);
		} else {
			break;
		}
	}

	profile_scope_end();
	return tokens;
}

//
// MacroCallStack
//

void _macro_call_stack_pop(MacroCallStack* call_stack) {
	assert(call_stack);
	assert(call_stack->depth > 0);
	assert(call_stack->depth <= call_stack->capacity);

	MacroCall* macro_call = &call_stack->frames[call_stack->depth - 1];
	Arena* call_allocator = macro_call->used_allocator;
	
	// NOTE: The whole process of macro expansion doesn't allocate anything,
	//       except when it comes to nested macro calls.
	//       Allocations are only performed during call setup (argument parsing).
	//       So in order to not overflow the arena, we want to deallocate
	//       all the memory used by the macro call, after the call has finished.
	//
	// WARN: It is prohibited to allocate anything during macro expansion, except the macro calls.
	//
	//       So validate here, that nothing was allocated in that process.
	assert(macro_call->arena_size_before_call <= macro_call->arena_size_after_call);
	assert_msg(call_allocator->allocated >= macro_call->arena_size_after_call,
			"`state->allocator` was most likely reset during the expansion of the current macro"
			" call");
	assert_msg(call_allocator->allocated == macro_call->arena_size_after_call,
			"`state->allocator` was used in an allocation during a macro call expansion. "
			"Ending the macro call will lead to invalidation of "
			"all allocations performed during the macro expansion.");

	// NOTE: Deallocate the call memory, by reseting the arena size back,
	//       to where it was before the call.
	arena_end_temp((ArenaRegion) {
		.arena = call_allocator,
		.allocated_state = macro_call->arena_size_before_call
	});

	// And finally pop the frame
	call_stack->depth -= 1;
}

void _preprocessor_parse_macro_token_stream(Preprocessor* state, MacroDefinition* macro) {
	profile_scope_start(__func__);
	macro->tokens = arena_alloc_array(state->allocator, Token, 0);

	ArenaRegion hints_temp_region = arena_begin_temp(state->temp_allocator);
	MacroTokenHint* token_hints = arena_alloc_array(state->temp_allocator, MacroTokenHint, 0);
	size_t token_hint_count = 0;

	PackedSourceRange macro_name_range = macro->name_source_range;

	const LineInfo* line_info = &_preprocessor_current_file(state)->line_info;
	uint32_t current_line = line_info_pos_to_source_location(
			line_info,
			macro_name_range.start).line;

	bool next_token_is_part_of_insert_operator = false;

	while (!tokenizer_is_end(state->tokenizer)) {
		Token token = tokenizer_view_next(state->tokenizer);
		uint32_t token_line = line_info_pos_to_source_location(
				line_info,
				token.source_range.end).line;

		if (token_line != current_line) {
			break;
		}

		// Consume the token, since it definitely belongs to this macros token stream.
		tokenizer_reset_to_token(state->tokenizer, token);

		if (token.kind == TOKEN_BACKWARD_SLASH) {
			current_line += 1;
			continue;
		}

		MacroTokenHint token_hint = { .kind = MACRO_TOKEN_HINT_NONE };

		switch (token.kind) {
		case TOKEN_IDENT: {
			if (macro->style != MACRO_STYLE_FUNCTION) {
				break;
			}

			bool is_va_args = str_equal(token.string, STR_LIT("__VA_ARGS__"));
			if (is_va_args) {
				assert(macro->has_va_args);
				token_hint.kind = MACRO_TOKEN_HINT_VA_ARGS;
				break;
			}

			size_t parameter_index = _macro_find_param_by_name(macro, token.string);

			bool is_insert_operator = false;
			if (next_token_is_part_of_insert_operator) {
				is_insert_operator = true;
				next_token_is_part_of_insert_operator = false;
			}

			Token maybe_token_insert_operator = tokenizer_view_next(state->tokenizer);
			if (maybe_token_insert_operator.kind == TOKEN_DOUBLE_HASH) {
				tokenizer_reset_to_token(state->tokenizer, maybe_token_insert_operator);
				is_insert_operator = true;
				next_token_is_part_of_insert_operator = true;
			} else if (parameter_index != SIZE_MAX) {
				token_hint.kind = MACRO_TOKEN_HINT_PARAMETER;
				token_hint.param.index = parameter_index;
			}

			if (is_insert_operator) {
				assert(token_hint.kind == MACRO_TOKEN_HINT_NONE
						|| token_hint.kind == MACRO_TOKEN_HINT_PARAMETER);
				token_hint.kind = MACRO_TOKEN_HINT_TOKEN_INSERT_OPERATOR;
				// NOTE: here an invalid param index is allowed
				token_hint.token_insert_op.param_index = parameter_index;
			}

			break;
		}
		case TOKEN_HASH: {
			Token param_name_token = tokenizer_next_token(state->tokenizer);
			if (param_name_token.kind != TOKEN_IDENT) {
				TokenKind expected_token = TOKEN_IDENT;

				diagnostics_report_unexpected_token(state->diagnostics,
						param_name_token,
						&expected_token,
						1);

				// NOTE: Terminate parsing here
				arena_end_temp(hints_temp_region);
				profile_scope_end();
				return;
			}

			size_t param_index = _macro_find_param_by_name(macro, param_name_token.string);
			if (param_index == SIZE_MAX) {
				String message = str_format(
						state->diagnostics->allocator,
						"'#' must be followed by parameter name. '%.*s' is not a valid macro "
						"parameter",
						STR_FMT(param_name_token.string));

				report_error(state->diagnostics,
						source_range_pack(param_name_token.source_range),
						message,
						NULL);

				// NOTE: Terminate parsing here
				arena_end_temp(hints_temp_region);
				profile_scope_end();
				return;
			}

			token_hint.kind = MACRO_TOKEN_HINT_STRING_OPERATOR;
			token_hint.string_op.param_index = param_index;

			// Initially token was a TOKEN_HASH. Replace it with the param name token, so the
			// tokenizer continues after the `param_name_token`.
			//
			// This has a side effect that the macro token stream, won't contain the hash token, but
			// only the param name token with `MACRO_TOKEN_HINT_STRING_OPERATOR`
			token = param_name_token;
			break;
		}
		case TOKEN_DOUBLE_HASH: {
			unreachable();
			break;
		}
		default: {
			bool is_insert_operator = false;
			if (next_token_is_part_of_insert_operator) {
				is_insert_operator = true;
				next_token_is_part_of_insert_operator = false;
			}

			Token maybe_token_insert_operator = tokenizer_view_next(state->tokenizer);
			if (maybe_token_insert_operator.kind == TOKEN_DOUBLE_HASH) {
				tokenizer_reset_to_token(state->tokenizer, maybe_token_insert_operator);
				is_insert_operator = true;
				next_token_is_part_of_insert_operator = true;
			}

			if (is_insert_operator) {
				assert(token_hint.kind == MACRO_TOKEN_HINT_NONE
						|| token_hint.kind == MACRO_TOKEN_HINT_PARAMETER);

				token_hint.kind = MACRO_TOKEN_HINT_TOKEN_INSERT_OPERATOR;
				// NOTE: here an invalid param index is allowed
				token_hint.token_insert_op.param_index = SIZE_MAX;
			}

			break;
		}
		}

		if (macro->style != MACRO_STYLE_FUNCTION) {
			assert(token_hint.kind == MACRO_TOKEN_HINT_NONE);
		}

		if (macro->style == MACRO_STYLE_FUNCTION) {
			*arena_alloc(state->temp_allocator, MacroTokenHint) = token_hint;
			token_hint_count += 1;
		}

		// Token is on the same line as the macro definition, so it belongs to the token stream of
		// this macro
		*arena_alloc(state->allocator, Token) = token;
		macro->token_count += 1;
	}

	if (macro->style == MACRO_STYLE_FUNCTION) {
		assert(token_hint_count == macro->token_count);

		// Copy token hints, because they were allocated using the temporary allocator
		macro->token_hints = arena_alloc_array(state->allocator, MacroTokenHint, token_hint_count);
		memcpy(macro->token_hints, token_hints, sizeof(*token_hints) * token_hint_count);
	}

	arena_end_temp(hints_temp_region);
	profile_scope_end();
}

bool _preprocessor_parse_macro(Preprocessor* state, MacroDefinition* macro) {
	profile_scope_start(__func__);

	Token name_token = tokenizer_next_token(state->tokenizer);
	if (name_token.kind != TOKEN_IDENT) {
		report_error(state->diagnostics,
				source_range_pack(name_token.source_range),
				STR_LIT("Expected macro name"),
				NULL);

		profile_scope_end();
		return false;
	}

	ArenaRegion temp_region = arena_begin_temp(state->allocator);

	macro->style = MACRO_STYLE_DEFAULT;
	macro->name = name_token.string;
	macro->name_source_range = source_range_pack(name_token.source_range);

	Token maybe_paren = tokenizer_view_next(state->tokenizer);

	// A function style is defined by a name followed by a left paren right after it (without any
	// whitespace)
	if (maybe_paren.kind == TOKEN_LEFT_PAREN
			&& name_token.source_range.end == maybe_paren.source_range.start) {

		// We have a function style macro => parse paremeter names
		macro->style = MACRO_STYLE_FUNCTION;
		macro->parameter_names = arena_alloc_array(state->allocator, String, 0);

		// Consume '('
		tokenizer_reset_to_token(state->tokenizer, maybe_paren);

		while (true) {
			Token token = tokenizer_next_token(state->tokenizer);
			if (token.kind == TOKEN_IDENT) {
				String* param_name = arena_alloc(state->allocator, String);			
				*param_name = token.string;
				macro->parameter_count += 1;

				token = tokenizer_next_token(state->tokenizer);
				if (token.kind == TOKEN_COMMA) {
					// There are potentially more paremeters
					continue;
				}
			} else if (token.kind == TOKEN_ELLIPSES) {
				Token right_paren = tokenizer_next_token(state->tokenizer);
				if (right_paren.kind != TOKEN_RIGHT_PAREN) {
					TokenKind expected_tokens[] = {
						TOKEN_RIGHT_PAREN,
					};

					diagnostics_report_unexpected_token(state->diagnostics,
							token,
							expected_tokens,
							array_size(expected_tokens));

					arena_end_temp(temp_region);
					return false;
				}

				macro->has_va_args = true;
				break;
			}

			if (token.kind == TOKEN_RIGHT_PAREN) {
				break;
			} else {
				TokenKind expected_tokens[] = {
					TOKEN_COMMA,
					TOKEN_RIGHT_PAREN,
				};

				diagnostics_report_unexpected_token(state->diagnostics,
						token,
						expected_tokens,
						array_size(expected_tokens));

				arena_end_temp(temp_region);
				profile_scope_end();
				return false;
			}
		}
	}

	_preprocessor_parse_macro_token_stream(state, macro);
	profile_scope_end();
	return true;
}

size_t _macro_find_param_by_name(const MacroDefinition* macro, String param_name) {
	profile_scope_start(__func__);
	assert(macro->style == MACRO_STYLE_FUNCTION);

	size_t result = SIZE_MAX;
	for (size_t i = 0; i < macro->parameter_count; i += 1) {
		if (str_equal(macro->parameter_names[i], param_name)) {
			result = i;
			break;
		}
	}

	profile_scope_end();
	return result;
}

//
// Expr and Expr parsing
//
// These are the expressions used in conditional directives like #if and #elif
//

typedef enum {
	EXPR_IDENT,
	EXPR_INT_LITERAL,
	EXPR_OP_DEFINED,
	EXPR_UNARY,
	EXPR_BINARY,
} ExprKind;

typedef enum {
	BIN_OP_LOGICAL_AND,
	BIN_OP_LOGICAL_OR,

	BIN_OP_EQUAL,
	BIN_OP_NOT_EQUAL,

	BIN_OP_LESS,
	BIN_OP_GREATER,

	BIN_OP_LESS_OR_EQUAL,
	BIN_OP_GREATER_OR_EQUAL,

	BIN_OP_BITWISE_AND,
	BIN_OP_BITWISE_OR,
	BIN_OP_BITWISE_XOR,

	BIN_OP_COUNT,
} BinOpKind;

typedef enum {
	UNARY_NEGATE,
	UNARY_PLUS,

	UNARY_LOGICAL_NOT,
} UnaryOp;

typedef struct Expr Expr;
struct Expr {
	ExprKind kind;
	SourceRange source_range;

	union {
		struct {
			uint64_t value;
		} integer_literal;

		struct {
			Expr* macro;
		} op_defined;

		struct {
			UnaryOp op;
			Expr* operand;
		} unary;

		struct {
			Expr* left;
			Expr* right;
			BinOpKind op_kind;
		} bin;

		Token ident;
		Expr* next_free;
	};
};

inline static Expr* _expr_to_int_literal(Expr* expr, uint64_t value) {
	expr->kind = EXPR_INT_LITERAL;
	expr->integer_literal.value = value;
	return expr;
}

static BinOpKind _token_kind_to_bin_op(TokenKind kind) {
	switch (kind) {
	case TOKEN_LOGIC_AND:
		return BIN_OP_LOGICAL_AND;
	case TOKEN_LOGIC_OR:
		return BIN_OP_LOGICAL_OR;
	case TOKEN_DOUBLE_EQUAL:
		return BIN_OP_EQUAL;
	case TOKEN_NOT_EQUAL:
		return BIN_OP_NOT_EQUAL;
	case TOKEN_LESS:
		return BIN_OP_LESS;
	case TOKEN_LESS_OR_EQUAL:
		return BIN_OP_LESS_OR_EQUAL;
	case TOKEN_GREATER:
		return BIN_OP_GREATER;
	case TOKEN_GREATER_OR_EQUAL:
		return BIN_OP_GREATER_OR_EQUAL;
	
	case TOKEN_AMPERSAND:
		return BIN_OP_BITWISE_AND;
	case TOKEN_PIPE:
		return BIN_OP_BITWISE_OR;
	case TOKEN_BITWISE_XOR:
		return BIN_OP_BITWISE_XOR;
	
	default:
		return BIN_OP_COUNT;
	}

	unreachable();
	return BIN_OP_COUNT;
}

static uint32_t bin_op_precedence(BinOpKind op) {
	switch (op) {
	case BIN_OP_LESS:
	case BIN_OP_GREATER:
	case BIN_OP_LESS_OR_EQUAL:
	case BIN_OP_GREATER_OR_EQUAL:
		return 6;
	case BIN_OP_EQUAL:
	case BIN_OP_NOT_EQUAL:
		return 7;
	case BIN_OP_BITWISE_AND:
		return 8;
	case BIN_OP_BITWISE_XOR:
		return 9;
	case BIN_OP_BITWISE_OR:
		return 10;
	case BIN_OP_LOGICAL_AND:
		return 11;
	case BIN_OP_LOGICAL_OR:
		return 12;
	case BIN_OP_COUNT:
		unreachable();
	}

	unreachable();
	return UINT32_MAX;
}

static Expr* _preprocessor_parse_expr(Preprocessor* state,
		TokenProvider token_provider,
		Arena* allocator,
		bool expand_macro_calls);
static Expr* _preprocessor_parse_expr_operand(Preprocessor* state,
		TokenProvider token_provider,
		Arena* allocator,
		bool expand_macro_calls);
static Expr* _expr_simplify(Preprocessor* state, Expr* expr);

Expr* _preprocessor_parse_expr_operand(Preprocessor* state,
		TokenProvider token_provider,
		Arena* allocator,
		bool expand_macro_calls) {

	Token token = token_provider_next(token_provider);

	if (token.kind == TOKEN_LEFT_PAREN) {
		Expr* expr = _preprocessor_parse_expr(state, token_provider, allocator, expand_macro_calls);

		Token closing_paren = token_provider_next(token_provider);
		if (closing_paren.kind != TOKEN_RIGHT_PAREN) {
			TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };
			diagnostics_report_unexpected_token(state->diagnostics,
					closing_paren,
					expected_tokens,
					array_size(expected_tokens));
			return NULL;
		}

		return expr;
	} else if (token.kind == TOKEN_EXCLAMATION_MARK || token.kind == TOKEN_MINUS || token.kind == TOKEN_PLUS) {
		UnaryOp op = -1;
		switch (token.kind) {
		case TOKEN_EXCLAMATION_MARK:
			op = UNARY_LOGICAL_NOT;
			break;
		case TOKEN_PLUS:
			op = UNARY_PLUS;
			break;
		case TOKEN_MINUS:
			op = UNARY_NEGATE;
			break;
		default:
			unreachable();
		}

		Expr* operand = _preprocessor_parse_expr_operand(state, token_provider, allocator, expand_macro_calls);
		if (!operand) {
			return NULL;
		}

		Expr* expr = arena_alloc(allocator, Expr);
		expr->kind = EXPR_UNARY;
		expr->unary.op = op;
		expr->unary.operand = operand;
		expr->source_range = (SourceRange) {
			.start = token.source_range.start,
			.end = operand->source_range.end,
		};
		return expr;
	} else if (token.kind == TOKEN_IDENT) {
		if (str_equal(token.string, STR_LIT("defined"))) {
			Expr* macro = _preprocessor_parse_expr_operand(state, token_provider, allocator, false);
			if (!macro) {
				return NULL;
			}

			Expr* expr = arena_alloc(allocator, Expr);
			expr->kind = EXPR_OP_DEFINED;
			expr->source_range = (SourceRange) {
				.start = token.source_range.start,
				.end = macro->source_range.end,
			};
			expr->op_defined.macro = macro;
			return expr;
		} else if (is_digit(token.string.v[0])) {
			IntLiteral literal = {};
			bool result = parse_int_literal(token, state->diagnostics, &literal);
			if (!result) {
				return NULL;
			}

			// NOTE: The int sufix is ignored
			// assert_msg(!literal.has_sufix, "todo: handle int literals with sufixes");

			Expr* expr = arena_alloc(allocator, Expr);
			expr->kind = EXPR_INT_LITERAL;
			expr->integer_literal.value = literal.value;
			return expr;
		} else {
			if (expand_macro_calls) {
				const MacroDefinition* macro = macro_table_find(&state->macro_table, token.string);
				if (macro != NULL) {
					_preprocessor_init_macro_call(state, token_provider, macro, token);
					return _preprocessor_parse_expr_operand(state, token_provider, allocator, expand_macro_calls);
				}
			}

			Expr* expr = arena_alloc(allocator, Expr);
			expr->kind = EXPR_IDENT;
			expr->ident = token;
			expr->source_range = token.source_range;
			return expr;
		}
	}

	unreachable_msg("Unhandled token during parsing of preprocessor expression");
	return NULL;
}

Expr* _preprocessor_parse_expr(Preprocessor* state, TokenProvider token_provider, Arena* allocator, bool expand_macro_calls) {
	profile_scope_start(__func__);

	Expr* expr = _preprocessor_parse_expr_operand(state, token_provider, allocator, expand_macro_calls);
	Expr** current_expr = &expr;

	while (true) {
		Token op_token = token_provider_view_next(token_provider);

		BinOpKind current_bin_op = _token_kind_to_bin_op(op_token.kind);
		if (current_bin_op != BIN_OP_COUNT) {
			token_provider_next(token_provider);

			uint32_t current_op_precedence = bin_op_precedence(current_bin_op);
			uint32_t next_op_precedence = UINT32_MAX;

			Expr* right_operand = _preprocessor_parse_expr_operand(state, token_provider, allocator, expand_macro_calls);

			{
				Token maybe_next_nin_op = token_provider_view_next(token_provider);
				BinOpKind next_bin_op = _token_kind_to_bin_op(maybe_next_nin_op.kind);
				if (next_bin_op != BIN_OP_COUNT) {
					next_op_precedence = bin_op_precedence(next_bin_op);
				}
			}

			Expr* bin_expr = arena_alloc(allocator, Expr);
			bin_expr->kind = EXPR_BINARY;
			bin_expr->bin.op_kind = current_bin_op;
			bin_expr->bin.left = *current_expr;
			bin_expr->bin.right = right_operand;

			*current_expr = bin_expr;
			
			if (current_op_precedence > next_op_precedence) {
				current_expr = &bin_expr->bin.right;
			}
		} else {
			break;
		}
	}

	profile_scope_end();
	return expr;
}

Expr* _expr_simplify(Preprocessor* state, Expr* expr) {
	switch (expr->kind) {
	case EXPR_BINARY: {
		switch (expr->bin.op_kind) {
		case BIN_OP_LOGICAL_OR: {
			Expr* left = _expr_simplify(state, expr->bin.left);
			assert(left->kind == EXPR_INT_LITERAL);
			if (left->integer_literal.value) {
				return _expr_to_int_literal(expr, 1);
			}

			Expr* right = _expr_simplify(state, expr->bin.right);
			assert(right->kind == EXPR_INT_LITERAL);
			return right;
		}
		case BIN_OP_LOGICAL_AND: {
			Expr* left = _expr_simplify(state, expr->bin.left);
			assert(left->kind == EXPR_INT_LITERAL);
			if (!left->integer_literal.value) {
				return _expr_to_int_literal(expr, 0);
			}

			Expr* right = _expr_simplify(state, expr->bin.right);
			assert(right->kind == EXPR_INT_LITERAL);
			return right;
		}
		default:
			break;
		}

		if (expr->bin.left == NULL || expr->bin.right == NULL) {
			return _expr_to_int_literal(expr, 0);
		}

		Expr* left_expr = _expr_simplify(state, expr->bin.left);
		Expr* right_expr = _expr_simplify(state, expr->bin.right);

		assert(left_expr->kind == EXPR_INT_LITERAL);
		assert(right_expr->kind == EXPR_INT_LITERAL);

		uint64_t left_value = left_expr->integer_literal.value;
		uint64_t right_value = right_expr->integer_literal.value;

		switch (expr->bin.op_kind) {
		case BIN_OP_LOGICAL_OR:
		case BIN_OP_LOGICAL_AND:
			unreachable();
		case BIN_OP_EQUAL:
			return _expr_to_int_literal(expr, left_value == right_value);
		case BIN_OP_NOT_EQUAL:
			return _expr_to_int_literal(expr, left_value != right_value);
		case BIN_OP_LESS:
			return _expr_to_int_literal(expr, left_value < right_value);
		case BIN_OP_GREATER:
			return _expr_to_int_literal(expr, left_value > right_value);
		case BIN_OP_LESS_OR_EQUAL:
			return _expr_to_int_literal(expr, left_value <= right_value);
		case BIN_OP_GREATER_OR_EQUAL:
			return _expr_to_int_literal(expr, left_value >= right_value);

		case BIN_OP_BITWISE_AND:
			return _expr_to_int_literal(expr, left_value & right_value);
		case BIN_OP_BITWISE_OR:
			return _expr_to_int_literal(expr, left_value | right_value);
		case BIN_OP_BITWISE_XOR:
			return _expr_to_int_literal(expr, left_value ^ right_value);

		case BIN_OP_COUNT:
			unreachable();
		}

		unreachable();
	}
	case EXPR_UNARY: {
		Expr* operand = _expr_simplify(state, expr->unary.operand);
		assert(operand->kind == EXPR_INT_LITERAL);

		switch (expr->unary.op) {
		case UNARY_PLUS:
			return operand;
		case UNARY_NEGATE:
			assert_msg(false, "Not yet supported");
		case UNARY_LOGICAL_NOT:
			operand->integer_literal.value = !operand->integer_literal.value;
			return operand;
		}

		unreachable();
	}
	case EXPR_INT_LITERAL:
		return expr;
	case EXPR_OP_DEFINED: {
		assert(expr->op_defined.macro->kind == EXPR_IDENT);
		const MacroDefinition* macro = macro_table_find(&state->macro_table, expr->op_defined.macro->ident.string);

		uint64_t value = macro != NULL;
		return _expr_to_int_literal(expr, value);
	}
	case EXPR_IDENT:
		return _expr_to_int_literal(expr, 0);
	}

	unreachable();
	return NULL;
}

//
// Directive parsing
//

const DirectiveKind INVALID_DIRECTIVE = -1;

String directive_kind_to_string(DirectiveKind kind) {
	switch (kind) {
	case DIRECTIVE_INCLUDE:
		return STR_LIT("include");
	case DIRECTIVE_DEFINE:
		return STR_LIT("define");
	case DIRECTIVE_UNDEF:
		return STR_LIT("undef");
	case DIRECTIVE_IF:
		return STR_LIT("if");
	case DIRECTIVE_ELIF:
		return STR_LIT("elif");
	case DIRECTIVE_ELSE:
		return STR_LIT("else");
	case DIRECTIVE_ENDIF:
		return STR_LIT("endif");
	case DIRECTIVE_IFDEF:
		return STR_LIT("ifdef");
	case DIRECTIVE_IFNDEF:
		return STR_LIT("ifndef");
	case DIRECTIVE_PRAGMA:
		return STR_LIT("pragma");
	case DIRECTIVE_ERROR:
		return STR_LIT("error");
	}

	unreachable();
	return (String) {};
}

DirectiveKind _directive_kind_from_string(String string) {
	if (str_equal(string, STR_LIT("include"))) {
		return DIRECTIVE_INCLUDE;
	} else if (str_equal(string, STR_LIT("define"))) {
		return DIRECTIVE_DEFINE;
	} else if (str_equal(string, STR_LIT("undef"))) {
		return DIRECTIVE_UNDEF;
	} else if (str_equal(string, STR_LIT("if"))) {
		return DIRECTIVE_IF;
	} else if (str_equal(string, STR_LIT("elif"))) {
		return DIRECTIVE_ELIF;
	} else if (str_equal(string, STR_LIT("else"))) {
		return DIRECTIVE_ELSE;
	} else if (str_equal(string, STR_LIT("endif"))) {
		return DIRECTIVE_ENDIF;
	} else if (str_equal(string, STR_LIT("ifdef"))) {
		return DIRECTIVE_IFDEF;
	} else if (str_equal(string, STR_LIT("ifndef"))) {
		return DIRECTIVE_IFNDEF;
	} else if (str_equal(string, STR_LIT("pragma"))) {
		return DIRECTIVE_PRAGMA;
	} else if (str_equal(string, STR_LIT("error"))) {
		return DIRECTIVE_ERROR;
	}

	return INVALID_DIRECTIVE;
}

static bool _preprocessor_parse_condition(Preprocessor* state, bool* out_result, ParsedDirective directive) {
	profile_scope_start(__func__);
	ArenaRegion temp = arena_begin_temp(state->temp_allocator);

	ArrayTokenProvider fallback_provider_state;

	{
		const SourceFile* file = _preprocessor_current_file(state);
		uint32_t line = line_info_pos_to_source_location(&file->line_info, directive.source_range.end).line;

		TokenArray tokens = _generate_directive_token_stream(state->temp_allocator,
				state->tokenizer,
				file,
				line);

		fallback_provider_state.tokens = tokens.tokens;
		fallback_provider_state.count = tokens.count;
		fallback_provider_state.next_index = 0;
		fallback_provider_state.source_file = file;
	}

	TokenProvider fallback_token_provider;
	_array_token_provider_init(&fallback_token_provider, &fallback_provider_state);

	MacroOrSourceTokenProviderState token_provider_state = {};
	TokenProvider token_provider = {};
	_macro_or_source_token_provider_init(state, &token_provider, &token_provider_state, fallback_token_provider);

	Expr* expr = _preprocessor_parse_expr(state, token_provider, state->temp_allocator, true);

	assert(state->macro_call_stack.depth == 0);

	if (!expr) {
		arena_end_temp(temp);
		profile_scope_end();
		return false;
	}

	expr = _expr_simplify(state, expr);

	switch (expr->kind) {
	case EXPR_INT_LITERAL:
		*out_result = expr->integer_literal.value;
		break;
	default:
		unreachable();
	}

	arena_end_temp(temp);
	profile_scope_end();
	return true;
}

inline bool _is_root_branch_region(Preprocessor* state) {
	return state->branch_stack_depth == MIN_BRANCH_REGION_STACK_DEPTH;
}

inline bool _is_current_region_enabled(Preprocessor* state) {
	assert(state->branch_stack_depth >= MIN_BRANCH_REGION_STACK_DEPTH);
	return state->branch_stack[state->branch_stack_depth - 1].is_enabled;
}

inline bool _is_parent_region_enabled(Preprocessor* state) {
	assert_msg(state->branch_stack_depth >= MIN_BRANCH_REGION_STACK_DEPTH + 1, "Root region doesn't have a parent");
	return state->branch_stack[state->branch_stack_depth - 2].is_enabled;
}

inline PreprocessorBranchRegion* _current_branch_region(Preprocessor* state) {
	return &state->branch_stack[state->branch_stack_depth - 1];
}

inline PreprocessorBranchRegion* _push_branch_region(Preprocessor* state) {
	assert(state->branch_stack_depth < state->branch_stack_capacity);
	PreprocessorBranchRegion* region = &state->branch_stack[state->branch_stack_depth];
	state->branch_stack_depth += 1;

	memset(region, 0, sizeof(*region));
	return region;
}

inline void _pop_branch_region(Preprocessor* state) {
	assert_msg(state->branch_stack_depth > MIN_BRANCH_REGION_STACK_DEPTH, "Can't remove root region from the stack");
	state->branch_stack_depth -= 1;
}

bool _preprocessor_parse_directive(Preprocessor* state, ParsedDirective directive) {
	profile_scope_start(__func__);

	const SourceFile* source_file = _preprocessor_current_file(state);
	const LineInfo* line_info = &source_file->line_info;

	uint32_t directive_line = line_info_pos_to_source_location(line_info, directive.source_range.end).line + 1;

	switch (directive.kind) {
	case DIRECTIVE_INCLUDE: {
		tokenizer_skip_whitespace_and_comments(state->tokenizer);

		Token string_token = {};

		char opening_quote = state->tokenizer->source_code.v[state->tokenizer->read_position];

		IncludeEvent include_event = {};
		if (opening_quote == '<') {
			StringTokenizerResult result = tokenizer_try_create_string_token(state->tokenizer, '<', '>', &string_token);
			assert(result == STR_TOKEN_RESULT_NONE);

			include_event.path_token = string_token;
		} else if (opening_quote == '"') {
			StringTokenizerResult result = tokenizer_try_create_string_token(state->tokenizer, '"', '"', &string_token);
			assert(result == STR_TOKEN_RESULT_NONE);

			include_event.path_token = string_token;
		} else {
			Token next_token = tokenizer_next_token(state->tokenizer);
			diagnostics_report_error(state->diagnostics,
					next_token.source_range,
					STR_LIT("Expected include path"),
					NULL);
			profile_scope_end();
			return false;
		}

		if (_is_current_region_enabled(state)) {
			String path_string = sub_str(string_token.string, 1, string_token.string.length - 2);
			String resolved_include_path = source_storage_resolve_include_path(
					state->source_storage,
					path_string,
					source_file,
					state->allocator,
					state->temp_allocator);

			if (resolved_include_path.length == 0) {
				StringBuilder builder = { .arena = state->diagnostics->allocator };
				str_builder_append(&builder, STR_LIT("File '"));
				str_builder_append(&builder, path_string);
				str_builder_append(&builder, STR_LIT("' not found"));

				diagnostics_report_error(state->diagnostics,
						directive.source_range,
						builder.string,
						NULL);

#if PREPROCESSOR_LOG
				debug_log_error("line: %u include %.*s failed", directive_line, STR_FMT(path_string));
#endif
				profile_scope_end();
				return false;
			}

			const SourceFile* included_file = source_storage_find_file(state->source_storage, resolved_include_path);
			if (included_file == NULL) {
				included_file = source_storage_append_from_path(state->source_storage,
						resolved_include_path,
						state->temp_allocator);
			}

			if (include_history_contains(&state->include_history, included_file)) {
#if PREPROCESSOR_LOG
				debug_log_info("lone: %u include %.*s found in include history and was skipped",
						directive_line,
						STR_FMT(included_file->path));
#endif
			} else {
				if (!_preprocessor_push_file(state, included_file)) {
					diagnostics_report_error(state->diagnostics,
							directive.source_range,
							STR_LIT("Include stack overflow"),
							NULL);
					profile_scope_end();
					return false;
				}

#if PREPROCESSOR_LOG
				debug_log_info("line: %u include %.*s", directive_line, STR_FMT(included_file->path));
#endif
			}
		}

		if (state->include_callback) {
			state->include_callback(state, state->include_callback_user_data, &include_event);
		}

		break;
	}
	case DIRECTIVE_PRAGMA: {
		Token token = tokenizer_view_next(state->tokenizer);
		if (token.kind == TOKEN_IDENT && str_equal(token.string, STR_LIT("once"))) {
			tokenizer_reset_to_token(state->tokenizer, token);

			bool inserted = include_history_try_insert(&state->include_history, _preprocessor_current_file(state));
#if PREPROCESSOR_LOG
			debug_log_info("inserted file in include history");
#endif
			assert(inserted);
		} else {
			_preprocessor_skip_until_newline(state);
		}

		profile_scope_end();
		return true;
	}
	case DIRECTIVE_DEFINE: {
		MacroDefinition macro = {};
		if (_preprocessor_parse_macro(state, &macro)) {
			if (_is_current_region_enabled(state)) {
#if PREPROCESSOR_LOG
				debug_log_info("line: %u define %.*s", directive_line, STR_FMT(macro.name.string));
#endif
				macro_table_append(&state->macro_table, &macro);
			}
		}
		break;
	}
	case DIRECTIVE_UNDEF: {
		Token macro_name = tokenizer_next_token(state->tokenizer);
		if (_is_current_region_enabled(state)) {
			if (macro_name.kind != TOKEN_IDENT) {
				TokenKind expected_tokens[] = { TOKEN_IDENT };
				diagnostics_report_unexpected_token(state->diagnostics,
						macro_name,
						expected_tokens,
						array_size(expected_tokens));
				profile_scope_end();
				return false;
			}

			bool result = macro_table_remove(&state->macro_table, macro_name.string);
			if (result) {
#if PREPROCESSOR_LOG
				debug_log_info("line: %u undef %.*s", directive_line, STR_FMT(macro_name.string));
#endif
			} else {
#if PREPROCESSOR_LOG
				debug_log_warn("line: %u undef %.*s failed", directive_line, STR_FMT(macro_name.string));
#endif
			}
		}
		break;
	}
	case DIRECTIVE_IF: {
		PreprocessorBranchRegion* branch_state = _push_branch_region(state);
		branch_state->current_directive = directive;

		bool predicate = false;
		if (_is_parent_region_enabled(state)) {
			if (!_preprocessor_parse_condition(state, &predicate, directive)) {
				profile_scope_end();
				return false;
			}

			branch_state->alternative_branch_is_taken = predicate;
			branch_state->is_enabled = predicate;

#if PREPROCESSOR_LOG
			debug_log_info("line: %u if %s", directive_line, branch_state->is_enabled ? "taken" : "not taken");
#endif
		} else {
			_preprocessor_skip_until_newline(state);
		}

		break;
	}
	case DIRECTIVE_IFDEF:
	case DIRECTIVE_IFNDEF: {
		PreprocessorBranchRegion* branch_state = _push_branch_region(state);
		branch_state->current_directive = directive;

		if (_is_parent_region_enabled(state)) {
			Token macro_name = tokenizer_next_token(state->tokenizer);
			if (macro_name.kind != TOKEN_IDENT) {
				TokenKind expected_tokens[] = { TOKEN_IDENT };
				diagnostics_report_unexpected_token(state->diagnostics,
						macro_name,
						expected_tokens,
						array_size(expected_tokens));
				profile_scope_end();
				return false;
			}
			
			bool flip_predicate = directive.kind == DIRECTIVE_IFNDEF;
			bool is_macro_defined = macro_table_find(&state->macro_table, macro_name.string) != NULL;
			bool predicate = is_macro_defined ^ flip_predicate;

			branch_state->alternative_branch_is_taken = predicate;
			branch_state->is_enabled = predicate;

#if PREPROCESSOR_LOG
			debug_log_info("line: %u if %s", directive_line, branch_state->is_enabled ? "taken" : "not taken");
#endif
		} else {
			_preprocessor_skip_until_newline(state);
		}

		break;
	}
	case DIRECTIVE_ELSE: {
		if (_is_root_branch_region(state)) {
			diagnostics_report_error(state->diagnostics,
					directive.source_range,
					STR_LIT("#else has no matching #if directive"),
					NULL);
			profile_scope_end();
			return false;
		}

		PreprocessorBranchRegion* branch_state = _current_branch_region(state);
		switch (branch_state->current_directive.kind) {
		case DIRECTIVE_IF:
		case DIRECTIVE_IFDEF:
		case DIRECTIVE_IFNDEF:
		case DIRECTIVE_ELIF:
			break;
		default: {
			StringBuilder builder = { state->diagnostics->allocator };
			str_builder_append(&builder, STR_LIT("#elif directive can't appear after "));
			str_builder_append(&builder, directive_kind_to_string(branch_state->current_directive.kind));

			DiagnosticsEntry* error = diagnostics_report_error(state->diagnostics,
					directive.source_range,
					builder.string,
					NULL);

			diagnostics_report_error(state->diagnostics,
					branch_state->current_directive.source_range,
					STR_LIT("Previous directive here"),
					error);
			profile_scope_end();
			return false;
		}
		}

		if (_is_parent_region_enabled(state)) {
			branch_state->is_enabled = !branch_state->alternative_branch_is_taken;
#if PREPROCESSOR_LOG
			debug_log_info("line: %u else %s", directive_line, branch_state->is_enabled ? "taken" : "not taken");
#endif
		}

		branch_state->current_directive = directive;
		break;
	}
	case DIRECTIVE_ELIF: {
		if (_is_root_branch_region(state)) {
			diagnostics_report_error(state->diagnostics,
					directive.source_range,
					STR_LIT("#elif has no matching #if directive"),
					NULL);
			profile_scope_end();
			return false;
		}

		PreprocessorBranchRegion* branch_state = _current_branch_region(state);
		switch (branch_state->current_directive.kind) {
		case DIRECTIVE_IF:
		case DIRECTIVE_IFDEF:
		case DIRECTIVE_IFNDEF:
		case DIRECTIVE_ELIF:
			break;
		default: {
			StringBuilder builder = { state->diagnostics->allocator };
			str_builder_append(&builder, STR_LIT("#elif directive can't appear after "));
			str_builder_append(&builder, directive_kind_to_string(branch_state->current_directive.kind));

			DiagnosticsEntry* error = diagnostics_report_error(state->diagnostics,
					directive.source_range,
					builder.string,
					NULL);

			diagnostics_report_error(state->diagnostics,
					branch_state->current_directive.source_range,
					STR_LIT("Previous directive here"),
					error);
			profile_scope_end();
			return false;
		}
		}

		if (_is_parent_region_enabled(state)) {
			bool current_predicate_value = false;
			if (!_preprocessor_parse_condition(state, &current_predicate_value, directive)) {
				profile_scope_end();
				return false;
			}

			bool is_taken = !branch_state->alternative_branch_is_taken && current_predicate_value;
			branch_state->is_enabled = is_taken;
			branch_state->alternative_branch_is_taken |= is_taken;

#if PREPROCESSOR_LOG
			debug_log_info("line: %u elif %s", directive_line, branch_state->is_enabled ? "taken" : "not taken");
#endif
		} else {
			_preprocessor_skip_until_newline(state);
		}

		branch_state->current_directive = directive;
		break;
	}
	case DIRECTIVE_ENDIF: {
		if (_is_root_branch_region(state)) {
			diagnostics_report_error(state->diagnostics,
					directive.source_range,
					STR_LIT("#endif used without #if"),
					NULL);
			profile_scope_end();
			return false;
		}

		_pop_branch_region(state);
		break;
	}
	case DIRECTIVE_ERROR: {
		uint32_t initial_line = line_info_pos_to_source_location(line_info, directive.source_range.end).line;

		Token first_token = { .kind = TOKEN_COUNT };
		Token last_token = { .kind = TOKEN_COUNT };

		while (true) {
			Token token = tokenizer_view_next(state->tokenizer);
			
			uint32_t token_line = line_info_pos_to_source_location(line_info, token.source_range.end).line;

			if (token_line > initial_line) {
				break;
			} else if (token.kind == TOKEN_EOF) {
				// Stop only if the EOF token is on this line
				tokenizer_reset_to_token(state->tokenizer, token);
				break;
			} else {
				tokenizer_reset_to_token(state->tokenizer, token);

				if (first_token.kind == TOKEN_COUNT) {
					first_token = token;
					last_token = token;
				} else {
					last_token = token;
				}
			}
		}

		if (_is_current_region_enabled(state)) {
			SourceRange error_message_range = (SourceRange) {
				.start = first_token.source_range.start,
				.end = last_token.source_range.end,
			};

			String error_message = sub_str(state->tokenizer->source_code,
					error_message_range.start,
					error_message_range.end - error_message_range.start);

			diagnostics_report_error(state->diagnostics,
					directive.source_range,
					error_message,
					NULL);
		}
		profile_scope_end();
		return false;
	}
	}

	profile_scope_end();
	return true;
}

bool _preprocessor_parse_directive_statement(Preprocessor* state, ParsedDirective* out_directive) {
	profile_scope_start(__func__);
	assert(out_directive != NULL);

	Token hash_token = tokenizer_next_token(state->tokenizer);
	assert(hash_token.kind == TOKEN_HASH);

	Token directive_name_token = tokenizer_next_token(state->tokenizer);
	if (directive_name_token.kind != TOKEN_IDENT
			&& directive_name_token.kind != TOKEN_KEYWORD_IF
			&& directive_name_token.kind != TOKEN_KEYWORD_ELSE) {
		TokenKind expected_tokens[] = { TOKEN_IDENT };
		diagnostics_report_unexpected_token(state->diagnostics,
				directive_name_token,
				expected_tokens,
				array_size(expected_tokens));

		profile_scope_end();
		return false;
	}

	DirectiveKind directive_kind = _directive_kind_from_string(directive_name_token.string);
	if (directive_kind == INVALID_DIRECTIVE) {
		StringBuilder builder = { .arena = state->diagnostics->allocator };
		str_builder_append(&builder, STR_LIT("Unknown preprocessor directive type: "));
		str_builder_append(&builder, directive_name_token.string);

		diagnostics_report_error(state->diagnostics,
				directive_name_token.source_range,
				builder.string,
				NULL);

		profile_scope_end();
		return false;
	}

	out_directive->kind = directive_kind;
	out_directive->source_range = (SourceRange) {
		.source_file = hash_token.source_range.source_file,
		.start = hash_token.source_range.start,
		.end = directive_name_token.source_range.end,
	};

	profile_scope_end();
	return true;
}

void _preprocessor_skip_until_newline(Preprocessor* state) {
	profile_scope_start(__func__);

	const SourceFile* source_file = _preprocessor_current_file(state);
	const LineInfo* line_info = &source_file->line_info;

	SourceLocation current_line_column = line_info_pos_to_source_location(
			line_info,
			state->tokenizer->read_position);

	uint32_t initial_line = current_line_column.line;
	uint32_t next_line_start = line_info->line_starts[initial_line + 1];

	state->tokenizer->read_position = next_line_start;

	profile_scope_end();
}

void _preprocessor_skip_directive(Preprocessor* state) {
	profile_scope_start(__func__);

	ParsedDirective directive = {};
	if (!_preprocessor_parse_directive_statement(state, &directive)) {
		_preprocessor_skip_until_newline(state);
		profile_scope_end();
		return;
	}

	if (!_preprocessor_parse_directive(state, directive)) {
		_preprocessor_skip_until_newline(state);
		profile_scope_end();
		return;
	}

	profile_scope_end();
}

//
// Macro call expansion state machine
//

// Merges subsequent tokens with `MACRO_TOKEN_HINT_TOKEN_INSERT_OPERATOR` hints into a single identifier token.
//
// NOTE: Supports merging only if each of the macro arguments has exactly one token.
bool _preprocessor_apply_token_insert_operator(Arena* generated_tokens_allocator,
		MacroCall* macro_call,
		Token* out_token) {

	profile_scope_start(__func__);

	const MacroDefinition* macro = macro_call->macro;
	assert(macro->style == MACRO_STYLE_FUNCTION);

	StringBuilder builder = { .arena = generated_tokens_allocator };
	SourceRange source_range = { NULL, SIZE_MAX, SIZE_MAX };

	size_t token_count = macro->token_count;
	while (macro_call->token_index < token_count) {
		const Token token = macro->tokens[macro_call->token_index];
		const MacroTokenHint hint = macro->token_hints[macro_call->token_index];

		if (hint.kind != MACRO_TOKEN_HINT_TOKEN_INSERT_OPERATOR) {
			break;
		}

		if (source_range.start == SIZE_MAX) {
			source_range = token.source_range;
		} else {
			assert(source_range.source_file == token.source_range.source_file);
			source_range.end = token.source_range.end;
		}

		size_t param_index = hint.token_insert_op.param_index;
		if (param_index == SIZE_MAX) {
			str_builder_append(&builder, token.string);
		} else {
			assert(param_index < macro->parameter_count);

			const TokenArray argument_tokens = macro_call->argument_tokens[param_index];
			assert(argument_tokens.count == 1);
			str_builder_append(&builder, argument_tokens.tokens[0].string);
		}

		macro_call->token_index += 1;
	}

	Token token = (Token) {
		.kind = TOKEN_IDENT,
		.source_range = source_range,
		.string = builder.string,
	};

	*out_token = token;
	profile_scope_end();
	return true;
}

inline bool _macro_call_finished(const MacroCall* call) {
	return call->token_index == call->macro->token_count;
}

bool _preprocessor_expand_user_defined_macro(Arena* generated_tokens_allocator, Token* out_token, MacroCall* call) {
	profile_scope_start(__func__);
	const MacroDefinition* macro = call->macro;

	assert(!_macro_call_finished(call));
	assert(macro->builtin_kind == BUILTIN_MACRO_NONE);

	// NOTE: Loop until we get a token
	while (true) {
		if (_macro_call_finished(call)) {
			profile_scope_end();
			return false;
		}
		
		switch (call->state) {
		case MACRO_CALL_TOKEN: {
			Token next_token = macro->tokens[call->token_index];

			MacroTokenHint hint = {};
			if (macro->style == MACRO_STYLE_FUNCTION) {
				assert(macro->token_hints);
				hint = macro->token_hints[call->token_index];
			}

			switch (hint.kind) {
			case MACRO_TOKEN_HINT_NONE:
				*out_token = next_token;
				call->token_index += 1;
				profile_scope_end();
				return true;
			case MACRO_TOKEN_HINT_PARAMETER: {
				call->state = MACRO_CALL_ARGUMENT_EXPANSION;
				call->arg_expansion = (MacroCallArgExpansion) {
					.arg_index = hint.param.index,
					.arg_token_index = 0,
				};

				break; // keep looping so that the new state gets processed
			}
			case MACRO_TOKEN_HINT_STRING_OPERATOR: {
				StringBuilder builder = { .arena = generated_tokens_allocator };
				str_builder_append_char(&builder, '"');

				assert(hint.string_op.param_index < macro->parameter_count);

				TokenArray argument_tokens = call->argument_tokens[hint.string_op.param_index];

				if (argument_tokens.count > 0) {
					str_builder_append(&builder, argument_tokens.tokens[0].string);
				}

				for (size_t i = 1; i < argument_tokens.count; i += 1) {
					str_builder_append_char(&builder, ' ');
					str_builder_append(&builder, argument_tokens.tokens[i].string);
				}

				str_builder_append_char(&builder, '"');

				Token token = (Token) {
					.kind = TOKEN_STRING,
					.source_range = next_token.source_range,
					.string = builder.string,
				};

				// We've precessed the string operator token => go to the next one in the token stream of the macro.
				call->token_index += 1;

				*out_token = token;
				profile_scope_end();
				return true;
			}
			case MACRO_TOKEN_HINT_TOKEN_INSERT_OPERATOR:
				profile_scope_end();
				return _preprocessor_apply_token_insert_operator(generated_tokens_allocator, call, out_token);
			case MACRO_TOKEN_HINT_VA_ARGS:
				call->state = MACRO_CALL_VA_ARGS_EXPANSION;
				call->va_args_expansion = (MacroCallVaArgsExpansion) {
					.arg_index = macro->parameter_count,
					.arg_token_index = 0,
				};
				break;
			}
			break;
		}
		case MACRO_CALL_ARGUMENT_EXPANSION: {
			MacroCallArgExpansion* expansion = &call->arg_expansion;
			assert(expansion->arg_index < call->macro->parameter_count);

			TokenArray argument_tokens = call->argument_tokens[expansion->arg_index];
			bool argument_is_fully_expanded = expansion->arg_token_index == argument_tokens.count;
			if (argument_is_fully_expanded) {
				call->state = MACRO_CALL_TOKEN;

				// Move to the next token
				call->token_index += 1;
				break;
			}

			assert(expansion->arg_token_index < argument_tokens.count);

			*out_token = argument_tokens.tokens[expansion->arg_token_index];
			expansion->arg_token_index += 1;

			profile_scope_end();
			return true;
		}
		case MACRO_CALL_VA_ARGS_EXPANSION: {
			MacroCallVaArgsExpansion* expansion = &call->va_args_expansion;
			assert(call->argument_count >= macro->parameter_count);
			assert(expansion->arg_index < call->argument_count);

			size_t token_count_in_arg = call->argument_tokens[expansion->arg_index].count;
			bool argument_is_fully_expanded = expansion->arg_token_index == token_count_in_arg;
			bool is_last_arg = expansion->arg_index + 1 == call->argument_count;

			if (argument_is_fully_expanded) {
				if (is_last_arg) {
					call->state = MACRO_CALL_TOKEN;

					// Move to the next token
					call->token_index += 1;
					break;
				} else {
					// Move to the next argument
					expansion->arg_index += 1;
					expansion->arg_token_index = 0;

					// But first immediately return a comma token.
					// Keep `arg_token_index` at 0, so that the next time a token gets requested
					// it return the first token of the argument.

					// TODO: figure out how to generate a proper source range for this comma token.
					Token va_args_token = macro->tokens[call->token_index];
					*out_token = (Token) {
						.kind = TOKEN_COMMA,
						.string = STR_LIT(","),
						.source_range = va_args_token.source_range,
					};

					profile_scope_end();
					return true;
				}
			}

			TokenArray argument_tokens = call->argument_tokens[expansion->arg_index];
			assert(expansion->arg_token_index < argument_tokens.count);

			*out_token = argument_tokens.tokens[expansion->arg_token_index];
			expansion->arg_token_index += 1;
			profile_scope_end();
			return true;
		}
		}
	}

	unreachable();
	profile_scope_end();
	return false;
}

bool _preprocessor_expand_builtin_macro(const SourceFile* source_file,
		const MacroCallStack* call_stack,
		Arena* generated_tokens_allocator,
		Token* out_token,
		MacroCall* call) {
	profile_scope_start(__func__);
	const MacroDefinition* macro = call->macro;

	assert(!_macro_call_finished(call));
	assert(macro->builtin_kind != BUILTIN_MACRO_NONE);
	assert(call_stack->depth > 0);

	switch (macro->builtin_kind) {
	case BUILTIN_MACRO_NONE:
		unreachable();
	case BUILTIN_MACRO_LINE: {
		SourceRange first_call_range = call_stack->frames[0].call_source_range;
		uint32_t call_source_line = line_info_pos_to_source_location(
				&source_file->line_info,
				first_call_range.start).line + 1;

		StringBuilder builder = { .arena = generated_tokens_allocator };
		str_builder_append_int(&builder, call_source_line);

		Token generated_token = (Token) {
			.kind = TOKEN_IDENT,
			.source_range = first_call_range,
			.string = builder.string,
		};

		// Macro call finished
		call->token_index = macro->token_count;

		*out_token = generated_token;
		profile_scope_end();
		return true;
	}
	case BUILTIN_MACRO_FILE: {
		SourceRange first_call_range = call_stack->frames[0].call_source_range;

		StringBuilder builder = { .arena = generated_tokens_allocator };
		str_builder_append_char(&builder, '"');
		for (size_t i = 0; i < source_file->path.length; i += 1) {
			char c = source_file->path.v[i];
			if (c == '\\') {
				str_builder_append(&builder, STR_LIT("\\\\"));
			} else {
				str_builder_append_char(&builder, c);
			}
		}
		str_builder_append_char(&builder, '"');

		Token generated_token = (Token) {
			.kind = TOKEN_STRING,
			.source_range = first_call_range,
			.string = builder.string,
		};

		// Mark the call as finished
		call->token_index = macro->token_count;

		*out_token = generated_token;
		profile_scope_end();
		return true;
	}
	case BUILTIN_MACRO_STDC: {
		SourceRange first_call_range = call_stack->frames[0].call_source_range;

		// TODO: Move to preprocessor flags/options
		bool conform_to_stdc = true;
		Token generated_token = (Token) {
			.kind = TOKEN_IDENT,
			.source_range = first_call_range,
			.string = conform_to_stdc ? STR_LIT("1") : STR_LIT("0"),
		};

		// Macro call finished
		call->token_index = macro->token_count;

		*out_token = generated_token;
		profile_scope_end();
		return true;
	}
	}

	unreachable();
	profile_scope_end();
	return false;
}

bool _preprocessor_get_next_macro_expansion_token(const SourceFile* source_file,
		MacroCallStack* macro_call_stack,
		Arena* generated_tokens_allocator,
		Token* out_token) {

	while (macro_call_stack->depth > 0) {
		MacroCall* call = &macro_call_stack->frames[macro_call_stack->depth - 1];
		if (_macro_call_finished(call)) {
			_macro_call_stack_pop(macro_call_stack);
			continue;
		}

		bool result;
		if (call->macro->builtin_kind == BUILTIN_MACRO_NONE) {
			result = _preprocessor_expand_user_defined_macro(generated_tokens_allocator, out_token, call);
		} else {
			result = _preprocessor_expand_builtin_macro(source_file,
					macro_call_stack,
					generated_tokens_allocator,
					out_token,
					call);
		}

		if (result) {
			return true;
		}
	}

	return false;
}

void _preprocessor_macro_call_to_diagnostics(const Preprocessor* state,
		size_t call_index,
		DiagnosticsEntry* root_error) {
	assert(root_error != NULL);
	assert(call_index < state->macro_call_stack.depth);

	const MacroCall* call = &state->macro_call_stack.frames[call_index];
	const MacroDefinition* macro = call->macro;

	StringBuilder builder = { .arena = state->diagnostics->allocator };

	str_builder_append(&builder, STR_LIT("Expended from macro '"));
	str_builder_append(&builder, call->macro->name);
	str_builder_append(&builder, STR_LIT("'\n"));

	for (size_t param_index = 0; param_index < macro->parameter_count; param_index += 1) {
		str_builder_append_char(&builder, '\t');
		str_builder_append(&builder, macro->parameter_names[param_index]);
		str_builder_append(&builder, STR_LIT(" = "));

		for (size_t token_index = 0; token_index < call->argument_tokens[param_index].count; token_index += 1) {
			Token token = call->argument_tokens[param_index].tokens[token_index];
			str_builder_append(&builder, token.string);
			str_builder_append_char(&builder, ' ');
		}

		str_builder_append_char(&builder, '\n');
	}

	diagnostics_report_error(state->diagnostics,
			call->call_source_range,
			builder.string,
			root_error);
}

void _preprocessor_macro_call_stack_to_diagnostics(const Preprocessor* state, DiagnosticsEntry* root_error) {
	for (size_t call_index = 0; call_index < state->macro_call_stack.depth; call_index += 1) {
		_preprocessor_macro_call_to_diagnostics(state, call_index, root_error);
	}
}

typedef struct {
	SourceRange source_range;
	TokenArray* token_streams;
	size_t count;
} ParsedMacroCallArgs;

//
// MacroCall parsing
//

typedef enum {
	// This was the last argument
	PARSE_MACRO_ARG_END,
	PARSE_MACRO_ARG_EOF,

	// We hit a `,` and expect one more argument
	PARSE_MACRO_ARG_EXPECT_ONE_MORE,
} ParseMacroArgResult;

ParseMacroArgResult _preprocessor_parse_single_macro_call_arg(TokenProvider token_provider,
		Arena* token_allocator,
		TokenArray* out_tokens) {
	profile_scope_start(__func__);

#define paren_stack_capacity 64
	size_t paren_stack_size = 0;
	TokenKind paren_stack[paren_stack_capacity];

	while (true) {
		Token token = token_provider_next(token_provider);

		if (token.kind == TOKEN_EOF) {
			profile_scope_end();
			return PARSE_MACRO_ARG_EOF;
		}

		if (paren_stack_size == 0) {
			if (token.kind == TOKEN_COMMA) {
				profile_scope_end();
				return PARSE_MACRO_ARG_EXPECT_ONE_MORE;
			} else if (token.kind == TOKEN_RIGHT_PAREN) {
				profile_scope_end();
				return PARSE_MACRO_ARG_END;
			}
		}

		if (token.kind == TOKEN_LEFT_PAREN
				|| token.kind == TOKEN_LEFT_BRACKET
				|| token.kind == TOKEN_LEFT_BRACE) {
			assert(paren_stack_size < paren_stack_capacity);
			paren_stack[paren_stack_size] = token.kind;
			paren_stack_size += 1;
		} else if (paren_stack_size > 0) {
			TokenKind expected_paren = paren_stack[paren_stack_size - 1];

			bool match = false;
			switch (expected_paren) {
			case TOKEN_LEFT_PAREN:
				match = token.kind == TOKEN_RIGHT_PAREN;
				break;
			case TOKEN_LEFT_BRACKET:
				match = token.kind == TOKEN_RIGHT_BRACKET;
				break;
			case TOKEN_LEFT_BRACE:
				match = token.kind == TOKEN_RIGHT_BRACE;
				break;
			default:
				unreachable_msg("Some other token type which is not a paren, square bracket nor a "
						" curly brace was pushed onto the stack");
			}

			if (match) {
				// We got a matching closing paren, 
				// so pop it off of the stack,
				// but still add it to the tokens list
				paren_stack_size -= 1;
			}
		}

		arena_alloc(token_allocator, Token);

		out_tokens->tokens[out_tokens->count] = token;
		out_tokens->count += 1;
	}

	unreachable();
	profile_scope_end();
	return PARSE_MACRO_ARG_EOF;
}

bool _preprocessor_parse_macro_call_args(Diagnostics* diagnostics,
		TokenProvider token_provider,
		Arena* allocator,
		Arena* temp_allocator,
		ParsedMacroCallArgs* out_args) {

	profile_scope_start(__func__);

	Token maybe_left_paren = token_provider_next(token_provider);

	if (maybe_left_paren.kind != TOKEN_LEFT_PAREN) {
		TokenKind expected_tokens[] = { TOKEN_LEFT_PAREN };
		diagnostics_report_unexpected_token(diagnostics,
				maybe_left_paren,
				expected_tokens,
				array_size(expected_tokens));
		profile_scope_end();
		return false;
	}

	SourceRange source_range = (SourceRange) {
		.start = maybe_left_paren.source_range.start,
		.end = SIZE_MAX
	};

	ArenaRegion args_region = arena_begin_temp(temp_allocator);
	ArenaRegion streams_region = arena_begin_temp(allocator);

	TokenArray* token_streams = arena_alloc_array(temp_allocator, TokenArray, 0);
	size_t token_stream_count = 0;

	bool has_reached_end = false;
	while (!has_reached_end) {
		TokenArray current_token_stream = {};
		current_token_stream.tokens = arena_alloc_array(allocator, Token, 0);
		current_token_stream.count = 0;

		ParseMacroArgResult result = _preprocessor_parse_single_macro_call_arg(token_provider,
				allocator,
				&current_token_stream);

		bool accept_stream = result == PARSE_MACRO_ARG_EXPECT_ONE_MORE
			|| (result == PARSE_MACRO_ARG_END && current_token_stream.count > 0);

		if (accept_stream) {
			arena_alloc(temp_allocator, TokenArray);
			token_streams[token_stream_count] = current_token_stream;
			token_stream_count += 1;
		}

		switch (result) {
		case PARSE_MACRO_ARG_EOF:
			arena_end_temp(args_region);
			arena_end_temp(streams_region);
			profile_scope_end();
			return false;
		case PARSE_MACRO_ARG_EXPECT_ONE_MORE:
			break;
		case PARSE_MACRO_ARG_END:
			has_reached_end = true;
			break;
		}
	}

	// Copy `token_streams` array to the main arena
	out_args->source_range = source_range;
	out_args->count = token_stream_count;
	out_args->token_streams = arena_alloc_array(allocator, TokenArray, token_stream_count);
	array_copy(out_args->token_streams, token_streams, token_stream_count);

	arena_end_temp(args_region);
	profile_scope_end();
	return true;
}

// NOTE: Returns null in case a call to a macro doesn't produce any tokens
MacroCall* _preprocessor_init_macro_call(Preprocessor* state,
		TokenProvider token_provider,
		const MacroDefinition* macro,
		Token macro_call_ident) {

	profile_scope_start(__func__);

	ParsedMacroCallArgs args = {};
	SourceRange call_source_range = macro_call_ident.source_range;

	size_t arena_size_before_call = state->allocator->allocated;

	if (macro->style == MACRO_STYLE_FUNCTION) {
		// Parse macro arguments

		if (!_preprocessor_parse_macro_call_args(state->diagnostics,
					token_provider,
					state->allocator,
					state->temp_allocator,
					&args)) {
			profile_scope_end();
			return NULL;
		}
		
		if (args.count < macro->parameter_count) {
			// Not enough macro arguments
			StringBuilder builder = { state->diagnostics->allocator };
			str_builder_append(&builder, STR_LIT("Not enough arguments during a call of macro called '"));
			str_builder_append(&builder, macro->name);
			str_builder_append(&builder, STR_LIT("'. Expected "));
			str_builder_append_int(&builder, macro->parameter_count);
			str_builder_append(&builder, STR_LIT(" but only "));
			str_builder_append_int(&builder, args.count);
			str_builder_append(&builder, STR_LIT(" were provided."));

			DiagnosticsEntry* error = report_error(state->diagnostics,
					macro->name_source_range,
					builder.string,
					NULL);

			_preprocessor_macro_call_stack_to_diagnostics(state, error);
			profile_scope_end();
			return NULL;
		} else if (args.count > macro->parameter_count && !macro->has_va_args) {
			// Too many macro arguments
			StringBuilder builder = { state->diagnostics->allocator };
			str_builder_append(&builder, STR_LIT("Too many arguments during a call of macro called '"));
			str_builder_append(&builder, macro->name);
			str_builder_append(&builder, STR_LIT("'. Expected "));
			str_builder_append_int(&builder, macro->parameter_count);
			str_builder_append(&builder, STR_LIT(" but "));
			str_builder_append_int(&builder, args.count);
			str_builder_append(&builder, STR_LIT(" were provided."));

			DiagnosticsEntry* error = report_error(state->diagnostics,
					macro->name_source_range,
					builder.string,
					NULL);

			_preprocessor_macro_call_stack_to_diagnostics(state, error);
			profile_scope_end();
			return NULL;
		}
	}

	// NOTE: Even if the macro call doesn't produce any tokens,
	//       still push it onto the stack, so that we don't have
	//       to deal with the same edge cases (like deallocation of token streams).
	//       When such macro is pushed onto the stack, it goes through the same common path,
	//       like all macros do, where all edges are already taken into account.

	if (state->macro_call_stack.depth >= state->macro_call_stack.capacity) {
		DiagnosticsEntry* overflow_error = diagnostics_report_error(state->diagnostics,
				call_source_range,
				STR_LIT("Macro callstack overflow"),
				NULL);

		_preprocessor_macro_call_stack_to_diagnostics(state, overflow_error);
		profile_scope_end();
		return NULL;
	}

	MacroCall* macro_call = &state->macro_call_stack.frames[state->macro_call_stack.depth];
	state->macro_call_stack.depth += 1;

	memset(macro_call, 0, sizeof(*macro_call));
	
	macro_call->macro = macro;
	macro_call->call_source_range = call_source_range;
	macro_call->state = MACRO_CALL_TOKEN;
	macro_call->argument_tokens = args.token_streams;
	macro_call->argument_count = args.count;

	macro_call->used_allocator = state->allocator;

	// Set arena sizes before and after the call,
	// since they are required for deallocating token
	// streams and other memory used by the macro call.
	macro_call->arena_size_before_call = arena_size_before_call;
	macro_call->arena_size_after_call = state->allocator->allocated;

	assert(macro_call->arena_size_after_call >= macro_call->arena_size_before_call);

	profile_scope_end();
	return macro_call;
}

inline Token _preprocessor_return_eof(Preprocessor* state) {
	const SourceFile* source_file = state->initial_file;
	return (Token) {
		.source_range = (SourceRange) {
			.source_file = source_file,
			.start = source_file->source_code.length,
			.end = source_file->source_code.length,
		},
		.string = (String) {},
		.kind = TOKEN_EOF,
	};
}

Token preprocessor_view_next(Preprocessor* state) {
	if (state->has_pending_next_token) {
		return state->pending_next_token;
	}

	if (state->include_stack.depth == 0) {
		assert(state->tokenizer == NULL);
		return _preprocessor_return_eof(state);
	}

	assert(state->include_stack.depth > 0);
	assert(state->tokenizer);

	Token next_token = preprocessor_next_token(state);
	state->pending_next_token = next_token;
	state->has_pending_next_token = true;
	return next_token;
}

Token preprocessor_next_token(Preprocessor* state) {
	profile_scope_start(__func__);

	if (state->has_pending_next_token) {
		state->has_pending_next_token = false;

		Token token = state->pending_next_token;
		state->pending_next_token = (Token) {};
		assert(token.source_range.source_file);
		profile_scope_end();
		return token;
	}

	while (true) {
		if (state->include_stack.depth == 0) {
			assert(state->tokenizer == NULL);
			profile_scope_end();
			return _preprocessor_return_eof(state);
		}

		if (!_is_current_region_enabled(state)) {
			Token token = tokenizer_view_next(state->tokenizer);
			switch (token.kind) {
			case TOKEN_HASH:
				_preprocessor_skip_directive(state);
				break;
			case TOKEN_EOF:
				profile_scope_end();
				return token;
			default:
				tokenizer_reset_to_token(state->tokenizer, token);
			}

			continue;
		}

		Token next_token = { .kind = TOKEN_COUNT };
		if (state->macro_call_stack.depth > 0) {
			// NOTE: Inside an expanding macro call.
			//       Instead of getting tokens from the tokenizer
			//       return the onces from the macro.
			//
			//       In that way the call gets replaced with whatever code was defined in the macro.

			bool result = _preprocessor_get_next_macro_expansion_token(
					_preprocessor_current_file(state),
					&state->macro_call_stack,
					state->generated_tokens_allocator,
					&next_token);

			if (result) {
				assert(next_token.kind != TOKEN_COUNT);
			} else {
				assert(next_token.kind == TOKEN_COUNT);
			}
		}
		
		if (next_token.kind == TOKEN_COUNT) {
			next_token = tokenizer_view_next(state->tokenizer);

			if (next_token.kind == TOKEN_HASH) {
				_preprocessor_skip_directive(state);
				continue;
			} else {
				tokenizer_reset_to_token(state->tokenizer, next_token);
			}
		}

		if (next_token.kind == TOKEN_IDENT) {
			const MacroDefinition* macro = macro_table_find(&state->macro_table, next_token.string);
			if (macro == NULL) {
				profile_scope_end();
				return next_token;
			}

			TokenProvider fallback_token_provider;
			_tokenizer_token_provider_init(&fallback_token_provider, state->tokenizer);

			MacroOrSourceTokenProviderState token_provider_state = {};
			TokenProvider token_provider = {};
			_macro_or_source_token_provider_init(state, &token_provider, &token_provider_state, fallback_token_provider);
			_preprocessor_init_macro_call(state, token_provider, macro, next_token);

			// NOTE: Possible cases:
			//       1. The macro call started successfully, because it produces tokens.
			//       2. The macro call was skipped, since it doesn't produce any tokens.
			//       3. Macro call failed.
			//
			//       In the first case continueing goes to start of the loop and
			//       retreives the first token from the macro call.
			//
			//       In the second case it will find and process the next token
			//       after the macro call.
			continue;
		} else {
			if (next_token.kind == TOKEN_EOF && state->include_stack.depth > 0) {
				// NOTE: We've reached the end of the currently included file
				_preprocessor_pop_file(state);
				continue;
			}

			profile_scope_end();
			return next_token;
		}
	}

	unreachable();
	return (Token) {};
}
