#include "parser.h"

#include "parser/parse_tools.h"

#define PROFILE_COLOR 0xfffd0a0a

//
// IdentifierStorage
//

const void* REMOVED_SLOT_FLAG = (void*)0x1;

inline IdentifierNamespace* _ident_storage_get_namespace(IdentifierStorage* storage,
		IdentifierNamespaceKind namespace_kind) {

	assert(namespace_kind >= 0);
	assert(namespace_kind < IDENT_NAMESPACE_COUNT);
	return &storage->namespaces[namespace_kind];
}

inline bool _ident_storage_remove_entry_at(IdentifierNamespace* ident_namespace, size_t index) {
	assert(ident_namespace->count > 0);

	assert(index < ident_namespace->capacity);

	assert_msg(ident_namespace->keys[index].v != NULL, "Cannot remove empty slot");
	assert_msg(ident_namespace->keys[index].v != REMOVED_SLOT_FLAG, "Cannot remove already removed slot");

	ident_namespace->keys[index] = (String) { .v = REMOVED_SLOT_FLAG, .length = 0 };
	ident_namespace->count -= 1;
	return true;
}

static size_t _ident_storage_try_find_entry(IdentifierNamespace* ident_namespace, String name) {
	profile_func_colored(PROFILE_COLOR);
	assert(ident_namespace != NULL);
	assert(name.length > 0);

	size_t entry_index = hash_string(name) % ident_namespace->capacity;
	while (true) {
		String key = ident_namespace->keys[entry_index];
		if (key.v == NULL) {
			profile_scope_end();
			return SIZE_MAX;
		}

		if (str_equal(key, name)) {
			profile_scope_end();
			return entry_index;
		}

		entry_index = (entry_index + 1) % ident_namespace->capacity;
	}

	profile_scope_end();
	return SIZE_MAX;
}

inline static size_t _ident_storage_try_find_empty_entry(IdentifierNamespace* ident_namespace,
		String name) {

	size_t capacity = ident_namespace->capacity;
	String* keys = ident_namespace->keys;

	size_t entry_index = hash_string(name) % capacity;
	while (true) {
		String key = keys[entry_index];
		if (key.v == NULL || key.v == REMOVED_SLOT_FLAG) {
			return entry_index;
		}

		entry_index = (entry_index + 1) % capacity;
	}

	return SIZE_MAX;
}

inline static void _ident_storage_alloc_namespace_hash_map(IdentifierNamespace* ident_namespace,
		Allocator allocator) {

	size_t key_size = sizeof(*ident_namespace->keys);
	size_t entry_size = sizeof(*ident_namespace->entries);
	size_t buffer_size = (key_size + entry_size) * ident_namespace->capacity;
	uint8_t* new_buffer = allocator_alloc_array(allocator, uint8_t, buffer_size);

	size_t entries_offset = key_size * ident_namespace->capacity;

	ident_namespace->keys = (String*)new_buffer;
	ident_namespace->entries = (IdentifierEntry**)(new_buffer + entries_offset);

	memset(ident_namespace->keys, 0, key_size * ident_namespace->capacity);
}

static void _ident_storage_grow_namespace(IdentifierNamespace* ident_namespace, Allocator allocator) {
	profile_func_colored(PROFILE_COLOR);

	assert(ident_namespace);

	if (ident_namespace->count == 0) {
		assert(ident_namespace->keys == NULL);
		assert(ident_namespace->entries == NULL);
	}

	size_t old_capacity = ident_namespace->capacity;
	String* old_keys = ident_namespace->keys;
	IdentifierEntry** old_entries = ident_namespace->entries;

	ident_namespace->capacity = ident_namespace->capacity + ident_namespace->capacity / 2;
	_ident_storage_alloc_namespace_hash_map(ident_namespace, allocator);

	for (size_t i = 0; i < old_capacity; i += 1) {
		if (old_keys[i].v == NULL || old_keys[i].v == REMOVED_SLOT_FLAG) {
			continue;
		}

		size_t empty_slot = _ident_storage_try_find_empty_entry(ident_namespace, old_keys[i]);
		assert(empty_slot != SIZE_MAX);

		ident_namespace->keys[empty_slot] = old_keys[i];
		ident_namespace->entries[empty_slot] = old_entries[i];
	}

	profile_scope_end();
}

inline bool _ident_storage_try_insert(IdentifierNamespace* ident_namespace,
		Allocator allocator,
		String name,
		IdentifierEntry* entry) {

	profile_func_colored(PROFILE_COLOR);

	if (ident_namespace->count + 1 == ident_namespace->capacity / 2) {
		_ident_storage_grow_namespace(ident_namespace, allocator);
	}
	
	size_t empty_slot = _ident_storage_try_find_empty_entry(ident_namespace, name);
	if (empty_slot == SIZE_MAX) {
		profile_scope_end();
		return false;
	}

	ident_namespace->keys[empty_slot] = name;
	ident_namespace->entries[empty_slot] = entry;
	ident_namespace->count += 1;
	profile_scope_end();
	return true;
}

IdentifierEntry* ident_storage_find(IdentifierStorage* storage,
		IdentifierNamespaceKind namespace_kind,
		IdentifierFindOption option,
		String name) {
	profile_func_colored(PROFILE_COLOR);

	IdentifierNamespace* ident_namespace = _ident_storage_get_namespace(storage, namespace_kind);
	size_t index = _ident_storage_try_find_entry(ident_namespace, name);
	if (index == SIZE_MAX) {
		profile_scope_end();
		return NULL;
	}

	IdentifierEntry* entry = ident_namespace->entries[index];
	switch (option) {
	case IDENT_FIND_IN_CURRENT_SCOPE:
		if (entry->owner_scope == storage->current_scope) {
			profile_scope_end();
			return entry;
		}

		profile_scope_end();
		return NULL;
	case IDENT_FIND_IN_ALL_PARENT_SCOPES:
		profile_scope_end();
		return entry;
	}

	unreachable();
	profile_scope_end();
	return NULL;
}

void _ident_storage_init_namespace(IdentifierNamespace* ident_namespace) {
}

void ident_storage_init(IdentifierStorage* storage, Allocator namespace_allocator, Arena* allocator) {
	profile_func_colored(PROFILE_COLOR);
	assert(storage != NULL);

	storage->allocator = allocator;
	storage->namespace_allocator = namespace_allocator;

	for (size_t i = 0; i < IDENT_NAMESPACE_COUNT; i += 1) {
		_ident_storage_init_namespace(&storage->namespaces[i]);

		storage->namespaces[i].count = 0;
		storage->namespaces[i].capacity = 64;

		_ident_storage_alloc_namespace_hash_map(&storage->namespaces[i], storage->namespace_allocator);
	}

	storage->current_scope = arena_alloc(storage->allocator, IdentifierScope);
	memset(storage->current_scope, 0, sizeof(*storage->current_scope));

	storage->next_scope_id = 1;
	storage->next_free_entry = NULL;
	storage->next_free_scope = NULL;

	profile_scope_end();
}

void ident_storage_release(IdentifierStorage* storage) {
	profile_func_colored(PROFILE_COLOR);

	for (size_t i = 0; i < IDENT_NAMESPACE_COUNT; i += 1) {
		IdentifierNamespace* ident_namespace = &storage->namespaces[i];

		if (ident_namespace->count > 0) {
			assert(ident_namespace->keys);
			assert(ident_namespace->entries);

			allocator_release(storage->namespace_allocator, ident_namespace->keys);

			ident_namespace->keys = NULL;
			ident_namespace->entries = NULL;
		}
	}
	
	profile_scope_end();
}

static IdentifierEntry* _ident_storage_alloc_entry(IdentifierStorage* storage) {
	IdentifierEntry* entry = NULL;
	if (storage->next_free_entry) {
		// Reuse a free entry
		entry = storage->next_free_entry;
		storage->next_free_entry = storage->next_free_entry->next_in_scope;
	} else {
		entry = arena_alloc(storage->allocator, IdentifierEntry);
	}

	assert(entry);

	memset(entry, 0, sizeof(*entry));
	return entry;
}

IdentifierEntry* ident_storage_insert(IdentifierStorage* storage,
		IdentifierNamespaceKind namespace_kind,
		IdentifierEntryKind entry_kind,
		String name,
		PackedSourceRange name_source_range) {
	profile_func_colored(PROFILE_COLOR);

	assert(storage != NULL);
	assert(name.length > 0);
	assert(name_source_range.length > 0);
	assert(storage->current_scope);

	IdentifierNamespace* ident_namespace = _ident_storage_get_namespace(storage, namespace_kind);
	IdentifierEntry* entry = NULL;

	size_t existing_entry_index = _ident_storage_try_find_entry(ident_namespace, name);
	if (existing_entry_index == SIZE_MAX) {
		entry = _ident_storage_alloc_entry(storage);
		entry->kind = entry_kind;

		bool entry_inserted = _ident_storage_try_insert(ident_namespace,
				storage->namespace_allocator,
				name, 
				entry);
		assert(entry_inserted);
	} else {
		assert_msg(ident_namespace->entries[existing_entry_index]->owner_scope != storage->current_scope,
				"Identifier with the given name is already defined in the current scope");

		entry = _ident_storage_alloc_entry(storage);
		entry->kind = entry_kind;

		entry->prev = ident_namespace->entries[existing_entry_index];
		ident_namespace->entries[existing_entry_index] = entry;
	}

	entry->name = name;
	entry->name_source_range = name_source_range;
	entry->owner_namespace = namespace_kind;
	entry->owner_scope = storage->current_scope;

	{
		IdentifierScope* current_scope = storage->current_scope;

		// Add to the current scope
		if (current_scope->first_identifier == NULL) {
			current_scope->first_identifier = entry;
		} else {
			current_scope->last_identifier->next_in_scope = entry;
		}

		current_scope->last_identifier = entry;

		uint8_t entry_kind_index = ((uint8_t)entry_kind) & IDENT_ENTRY_INDEX_MASK;
		assert((size_t)entry_kind_index < IDENT_ENTRY_KIND_COUNT);

		current_scope->nested_entry_count[entry_kind_index] += 1;
		current_scope->entry_count[entry_kind_index] += 1;
	}

	profile_scope_end();
	return entry;
}

void ident_storage_remove(IdentifierStorage* storage,
		IdentifierNamespaceKind namespace_kind,
		String name) {

	profile_func_colored(PROFILE_COLOR);

	assert(storage != NULL);
	assert(name.length > 0);
	assert(storage->current_scope);

	IdentifierNamespace* ident_namespace = _ident_storage_get_namespace(storage, namespace_kind);

	size_t existing_entry_index = _ident_storage_try_find_entry(ident_namespace, name);
	if (existing_entry_index == SIZE_MAX) {
		profile_scope_end();
		return;
	}

	IdentifierEntry* entry = ident_namespace->entries[existing_entry_index];
	memset(entry, 0, sizeof(*entry));

	if (entry->next_in_scope == NULL) {
		_ident_storage_remove_entry_at(ident_namespace, existing_entry_index);
	} else {
		ident_namespace->entries[existing_entry_index] = entry->next_in_scope;
	}

	entry->next_in_scope = storage->next_free_entry;
	storage->next_free_entry = entry;

	profile_scope_end();
}

IdentifierScope* ident_storage_begin_scope(IdentifierStorage* storage) {
	assert(storage != NULL);
	assert(storage->current_scope != NULL);

	IdentifierScope* scope;
	if (storage->next_free_scope == NULL) {
		scope = arena_alloc(storage->allocator, IdentifierScope);
	} else {
		scope = storage->next_free_scope;
		storage->next_free_scope = storage->next_free_scope->next_free;
	}

	memset(scope, 0, sizeof(*scope));

	scope->id = storage->next_scope_id;
	storage->next_scope_id += 1;

	scope->parent = storage->current_scope;
	storage->current_scope = scope;
	return scope;
}

void ident_storage_end_scope(IdentifierStorage* storage) {
	assert(storage->current_scope);

	IdentifierScope* scope = storage->current_scope;
	for (IdentifierEntry* entry = scope->first_identifier; entry != NULL;) {
		IdentifierEntry* next_entry = entry->next_in_scope;

		ident_storage_remove(storage, entry->owner_namespace, entry->name);

		entry = next_entry;
	}

	IdentifierScope* parent_scope = scope->parent;
	if (parent_scope) {
		for (size_t i = 0; i < IDENT_ENTRY_KIND_COUNT; i += 1) {
			parent_scope->nested_entry_count[i] += scope->nested_entry_count[i];

			assert(parent_scope->nested_entry_count[i] >= parent_scope->entry_count[i]);
		}
	}

	scope->next_free = storage->next_free_scope;
	storage->next_free_scope = scope;

	storage->current_scope = parent_scope;
}

//
// Loop & Switch State
//

static ParserLoopOrSwitchState* _parser_current_loop(Parser* parser) {
	ParserLoopOrSwitchState* state = parser->loop_or_switch_state;
	while (state) {
		if (state->node->kind == AST_NODE_WHILE_LOOP) {
			return state;
		} else if (state->node->kind == AST_NODE_FOR_LOOP) {
			return state;
		}

		state = state->parent;
	}

	return state;
}

//
// Parser
//

typedef enum {
	PARSE_TYPE_PARSED,
	PARSE_TYPE_NOT_PARSED,
	PARSE_TYPE_ERROR,
} ParseTypeResult;

ParseTypeResult _parser_try_parse_type(Parser* parser, Type* out_type, bool is_anonymous);
bool _parser_parse_type(Parser* parser, Type* out_type, bool is_anonymous);
ParseTypeResult _parser_try_parse_type_name(Parser* parser, Type* out_type);
bool _parser_parse_scope(Parser* parser, Scope* out_scope);
bool _parser_parse_pre_declaration_modifiers(Parser* parser,
		Type* base_type,
		Type* out_type,
		bool duplicate_base_type);

static AstNode* _parser_parse_single_node(Parser* parser);

typedef enum {
	EXPR_PARSE_OK,
	EXPR_PARSE_NOT_PARSED,
	EXPR_PARSE_ERROR,
} ExprParseResult;

static ExprParseResult _parser_try_parse_expr(Parser* parser, Expr* out_expr);
static ExprParseResult _parser_try_parse_bin_expr_operand(Parser* parser, Expr* out_expr);

//
// Declarator
//

typedef struct {
	String name;
	PackedSourceRange name_source_range;
	Type type;
} Declarator;

// Parses a declarator or an abstract declarator.
//
// An abstract declarator is pretty much the same as the declarator, but without the identifier.
static bool _parser_parse_declarator(Parser* parser,
		Type* type,
		Declarator* out_declarator,
		bool is_abstract);

//
// Parser Implementation
//

void _parser_skip_until_semicolon(Parser* parser) {
	profile_func_colored(PROFILE_COLOR);

	while (true) {
		Token token = preprocessor_view_next(parser->preprocessor);
		if (token.kind == TOKEN_SEMICOLON) {
			break;
		} else if (token.kind == TOKEN_EOF) {
			break;
		}

		preprocessor_next_token(parser->preprocessor);
	}

	profile_scope_end();
}

inline bool _parser_try_consume_token(Parser* parser, TokenKind expected_kind) {
	Token token = preprocessor_view_next(parser->preprocessor);
	if (token.kind == expected_kind) {
		preprocessor_next_token(parser->preprocessor);
		return true;
	}

	return false;
}

bool _parser_expect_semicolon(Parser* parser, String error_message) {
	Token token = preprocessor_next_token(parser->preprocessor);
	if (token.kind != TOKEN_SEMICOLON) {
		TokenKind expected_tokens[] = { TOKEN_SEMICOLON };
		diagnostics_report_unexpected_token(parser->diagnostics,
				token,
				expected_tokens,
				array_size(expected_tokens));
		return false;
	}

	return true;
}

bool _parser_consume_semicolon(Parser* parser) {
	Token token = preprocessor_next_token(parser->preprocessor);
	if (token.kind != TOKEN_SEMICOLON) {
		TokenKind expected_tokens[] = { TOKEN_SEMICOLON };
		diagnostics_report_unexpected_token(parser->diagnostics,
				token,
				expected_tokens,
				array_size(expected_tokens));
		return false;
	}

	return true;
}

void _parser_skip_until(Parser* parser, TokenKind token_kind_1, TokenKind token_kind_2) {
	profile_func_colored(PROFILE_COLOR);

	while (true) {
		Token token = preprocessor_view_next(parser->preprocessor);
		if (token.kind == token_kind_1) {
			break;
		} else if (token.kind == token_kind_2) {
			break;
		} else if (token.kind == TOKEN_EOF) {
			break;
		}

		preprocessor_next_token(parser->preprocessor);
	}

	profile_scope_end();
}

// TODO: Don't reset `ast_allocator` because, during testing that same `ast_allocator`
//       is used for diagnostics and reseting it corrupts diagnostics state
static bool _parser_parse_struct_fields(Parser* parser,
		size_t* out_field_count,
		StructField** out_fields) {
	profile_func_colored(PROFILE_COLOR);
	assert(out_field_count != NULL);
	assert(out_fields != NULL);

	Token left_brace = preprocessor_next_token(parser->preprocessor);
	assert(left_brace.kind == TOKEN_LEFT_BRACE);

	ArenaRegion ast_temp = arena_begin_temp(parser->ast_allocator);
	ArenaRegion temp = arena_begin_temp(parser->temp_allocator);

	StructField* fields = arena_alloc_array(parser->temp_allocator, StructField, 0);
	size_t field_count = 0;

	while (true) {
		{
			Token token = preprocessor_view_next(parser->preprocessor);
			if (token.kind == TOKEN_RIGHT_BRACE) {
				preprocessor_next_token(parser->preprocessor);
				break;
			}
		}

		Type field_type = {};
		Declarator field_declarator = {};

		if (!_parser_parse_type(parser, &field_type, true)) {
			_parser_skip_until(parser, TOKEN_SEMICOLON, TOKEN_RIGHT_BRACE);
		} else if (!_parser_parse_declarator(parser, &field_type, &field_declarator, false)) {
			_parser_skip_until(parser, TOKEN_SEMICOLON, TOKEN_RIGHT_BRACE);
		}

		StructField* field = arena_alloc_zeroed(parser->temp_allocator, StructField);
		field_count += 1;

		field->type = field_declarator.type;
		field->name = field_declarator.name;
		field->name_source_range = field_declarator.name_source_range;

		Token end_token = preprocessor_view_next(parser->preprocessor);
		if (end_token.kind == TOKEN_SEMICOLON) {
			// consume semicolon
			preprocessor_next_token(parser->preprocessor);
			continue;
		} else if (end_token.kind == TOKEN_RIGHT_BRACE) {
			break;
		}

		// Report unexpected token
		TokenKind expected_tokens[] = { TOKEN_SEMICOLON };
		diagnostics_report_unexpected_token(parser->diagnostics,
				end_token,
				expected_tokens,
				array_size(expected_tokens));

		// And try to recover
		_parser_skip_until(parser, TOKEN_SEMICOLON, TOKEN_RIGHT_BRACE);
		end_token = preprocessor_view_next(parser->preprocessor);
		if (end_token.kind == TOKEN_SEMICOLON) {
			preprocessor_next_token(parser->preprocessor);
			continue;
		} else if (end_token.kind == TOKEN_RIGHT_BRACE) {
			break;
		} else {
			break;
		}
	}

	*out_field_count = field_count;
	if (field_count == 0) {
		*out_fields = NULL;
	} else {
		*out_fields = arena_alloc_array(parser->ast_allocator, StructField, field_count);
		array_copy(*out_fields, fields, field_count);
	}

	arena_end_temp(temp);
	profile_scope_end();
	return true;
}

typedef struct {
	const Struct* struct_def;
	size_t field_index;
} NamedFieldLocation;

typedef struct {
	NamedFieldLocation* locations;
	size_t count;
	Arena* allocator;
} NamedFieldLocationArray;

static void _parser_gather_named_field_locations_of_anonymous_type_defs(const Struct* struct_def,
		NamedFieldLocationArray* out_field_locations) {
	profile_func_colored(PROFILE_COLOR);

	for (size_t i = 0; i < struct_def->field_count; i += 1) {
		const StructField* field = &struct_def->fields[i];
		if (field->name.length == 0) {
			if (field->type.kind == TYPE_STRUCT) {
				const Struct* inner_def = field->type.struct_def;
				_parser_gather_named_field_locations_of_anonymous_type_defs(inner_def, out_field_locations);
			} else if (field->type.kind == TYPE_UNION) {
				const Struct* inner_def = field->type.union_def;
				_parser_gather_named_field_locations_of_anonymous_type_defs(inner_def, out_field_locations);
			}
		} else {
			arena_alloc(out_field_locations->allocator, NamedFieldLocation);
			out_field_locations->locations[out_field_locations->count] = (NamedFieldLocation) {
				.struct_def = struct_def,
				.field_index = i,
			};
			out_field_locations->count += 1;
		}
	}

	profile_scope_end();
}

static void _parser_initialize_struct_fields_namespace(Struct* struct_def,
		Diagnostics* diagnostics,
		Arena* allocator,
		Arena* temp_allocator) {
	profile_func_colored(PROFILE_COLOR);

	ArenaRegion temp = arena_begin_temp(temp_allocator);
	NamedFieldLocationArray named_field_locations = {};
	named_field_locations.locations = arena_alloc_array(temp_allocator, NamedFieldLocation, 0);
	named_field_locations.count = 0;
	named_field_locations.allocator = temp_allocator;

	_parser_gather_named_field_locations_of_anonymous_type_defs(
			struct_def,
			&named_field_locations);

	StructFieldNamespace* field_namespace = arena_alloc_zeroed(allocator, StructFieldNamespace);
	field_namespace->size = 0;
	field_namespace->capacity = named_field_locations.count * 2;
	field_namespace->keys = arena_alloc_array_zeroed(allocator,
			String,
			field_namespace->capacity);

	field_namespace->entries = arena_alloc_array_zeroed(allocator,
			StructFieldNamespaceEntry,
			field_namespace->capacity);

	for (size_t i = 0; i < named_field_locations.count; i += 1) {
		NamedFieldLocation loc = named_field_locations.locations[i];
		const StructField* field = &loc.struct_def->fields[loc.field_index];
		size_t index = hash_string(field->name) % field_namespace->capacity;

		while (true) {
			String key = field_namespace->keys[index];
			if (key.v == NULL) {
				field_namespace->keys[index] = field->name;
				field_namespace->entries[index] = (StructFieldNamespaceEntry) {
					.struct_def = loc.struct_def,
					.field_index = loc.field_index,
				};

				field_namespace->size += 1;
				break;
			} else if (str_equal(key, field->name)) {
				report_error(diagnostics,
						field->name_source_range,
						str_format(diagnostics->allocator,
							"Duplicate field '%.*s'",
							STR_FMT(field->name)),
						NULL);
				break;
			}

			index = (index + 1) % field_namespace->capacity;
		}
	}

	struct_def->field_namespace = field_namespace;
	
	arena_end_temp(temp);

	profile_scope_end();
}

bool _parser_parse_struct_def(Parser* parser, Struct** out_struct_def, bool is_anonymous) {
	profile_func_colored(PROFILE_COLOR);
	assert(out_struct_def != NULL);

	Token keyword_token = preprocessor_next_token(parser->preprocessor);
	assert(keyword_token.kind == TOKEN_KEYWORD_STRUCT || keyword_token.kind == TOKEN_KEYWORD_UNION);

	bool is_struct = keyword_token.kind == TOKEN_KEYWORD_STRUCT;
	StructLayoutKind layout_kind = is_struct
		? STRUCT_LAYOUT_KIND_STRUCT
		: STRUCT_LAYOUT_KIND_UNION;
	IdentifierEntryKind ident_kind = is_struct
		? IDENT_STRUCT
		: IDENT_UNION;

	String struct_name = {};
	PackedSourceRange struct_name_range = {};
	StructField* fields = NULL;
	size_t* field_offsets = NULL;
	size_t field_count = 0;
	TypeLayout type_layout = {};
	bool is_forward_declared = true;

	Token token = preprocessor_view_next(parser->preprocessor);
	if (token.kind == TOKEN_IDENT) {
		preprocessor_next_token(parser->preprocessor); // consume identifier

		struct_name_range = source_range_pack(token.source_range);
		struct_name = token.string;
		token = preprocessor_view_next(parser->preprocessor);
	}

	if (token.kind == TOKEN_LEFT_BRACE) {
		is_forward_declared = false;
		if (_parser_parse_struct_fields(parser, &field_count, &fields)) {
			field_offsets = arena_alloc_array(parser->ast_allocator, size_t, field_count);

			type_layout.alignment = 1;

			for (size_t i = 0; i < field_count; i += 1) {
				const Type* field_type = &fields[i].type;
				TypeLayout field_type_layout = type_get_layout(parser->type_context, field_type);

				assert(field_type_layout.size > 0);
				assert(field_type_layout.alignment > 0);
				assert(field_type_layout.size % field_type_layout.alignment == 0);

				switch (layout_kind) {
				case STRUCT_LAYOUT_KIND_STRUCT:
					type_layout.alignment = max(field_type_layout.alignment, type_layout.alignment);
					type_layout.size = align(type_layout.size, field_type_layout.alignment);

					field_offsets[i] = type_layout.size;
					type_layout.size += field_type_layout.size;
					break;
				case STRUCT_LAYOUT_KIND_UNION:
					field_offsets[i] = 0;

					type_layout.alignment = max(field_type_layout.alignment, type_layout.alignment);
					type_layout.size = max(type_layout.size, field_type_layout.size);
					break;
				}
			}

			type_layout.size = max(type_layout.size, 1);
			type_layout.size = align(type_layout.size, type_layout.alignment);
		}
	}

	bool struct_def_initialized = false;
	Struct* struct_def = NULL;
	if (struct_name.length == 0) {
		struct_def = arena_alloc_zeroed(parser->ast_allocator, Struct);
		struct_def_initialized = false;
	} else if (!is_anonymous) {
		IdentifierEntry* entry = ident_storage_find(parser->ident_storage,
				IDENT_NAMESPACE_TAGGED,
				IDENT_FIND_DEFAULT,
				struct_name);
		if (entry) {
			if (!has_flag(entry->kind, ident_kind)) {
				StringBuilder builder = { .arena = parser->diagnostics->allocator };
				str_builder_append_char(&builder, '\'');
				str_builder_append(&builder, entry->name);
				str_builder_append(&builder, STR_LIT("' is previously defined with a different tag type"));

				DiagnosticsEntry* error = report_error(parser->diagnostics,
						struct_name_range,
						builder.string,
						NULL);

				report_error(parser->diagnostics,
						entry->name_source_range,
						STR_LIT("Previously defined here"),
						error);
				profile_scope_end();
				return false;
			}
			
			struct_def = is_struct ? entry->struct_def : entry->union_def;
			assert(struct_def);

			struct_def_initialized = true;

			if (!struct_def->is_forward_declared && !is_forward_declared) {
				StringBuilder builder = { .arena = parser->diagnostics->allocator };
				str_builder_append(&builder, STR_LIT("Redefinition of '"));
				str_builder_append(&builder, entry->name);
				str_builder_append_char(&builder, '\'');

				DiagnosticsEntry* error = report_error(parser->diagnostics,
						struct_name_range,
						builder.string,
						NULL);

				report_error(parser->diagnostics,
						entry->name_source_range,
						STR_LIT("Previously defined here"),
						error);
				profile_scope_end();
				return false;
			}
		} else {
			entry = ident_storage_insert(parser->ident_storage,
					IDENT_NAMESPACE_TAGGED,
					ident_kind,
					struct_name,
					struct_name_range);

			struct_def = arena_alloc_zeroed(parser->ast_allocator, Struct);

			if (is_struct) {
				entry->struct_def = struct_def;
			} else {
				entry->union_def = struct_def;
			}
		}
	} else {
		struct_def = arena_alloc_zeroed(parser->ast_allocator, Struct);
		struct_def_initialized = false;
	}

	assert(struct_def);

	if (!struct_def_initialized) {
		struct_def->name = struct_name;
		struct_def->layout_kind = layout_kind;
		struct_def->is_forward_declared = is_forward_declared;

		assert(struct_def->next == NULL);

		struct_def->id = parser->ast->stats.compound_type_count;
		parser->ast->stats.compound_type_count += 1;

		AST* ast = parser->ast;
		if (ast->first_compound_type) {
			assert(ast->last_compound_type != NULL);

			ast->last_compound_type->next = struct_def;
			ast->last_compound_type = struct_def;
		} else {
			assert(ast->last_compound_type == NULL);

			ast->first_compound_type = struct_def;
			ast->last_compound_type = struct_def;
		}
	}

	if (is_forward_declared) {
		assert(field_count == 0);
		assert(fields == NULL);
	} else {
		assert(field_offsets != NULL);
		assert(type_layout.size > 0);
		assert(type_layout.alignment > 0);
		assert(type_layout.size % type_layout.alignment == 0);

		struct_def->field_count = field_count;
		struct_def->fields = fields;
		struct_def->is_forward_declared = false;
		struct_def->field_offsets = field_offsets;
		struct_def->type_layout = type_layout;

		_parser_initialize_struct_fields_namespace(struct_def,
				parser->diagnostics,
				parser->ast_allocator,
				parser->temp_allocator);
	}

	*out_struct_def = struct_def;
	profile_scope_end();
	return true;
}

bool _parser_parse_enum_variants(Parser* parser, size_t* out_variant_count, EnumVariant** out_variants) {
	profile_func_colored(PROFILE_COLOR);
	assert(out_variant_count != NULL);
	assert(out_variants != NULL);

	{
		Token left_brace = preprocessor_next_token(parser->preprocessor);
		assert(left_brace.kind == TOKEN_LEFT_BRACE);
	}

	ArenaRegion temp = arena_begin_temp(parser->temp_allocator);

	bool result = true;
	size_t variant_count = 0;
	EnumVariant* variants = arena_alloc_array(parser->temp_allocator, EnumVariant, 0);

	while (true) {
		{
			Token token = preprocessor_view_next(parser->preprocessor);
			if (token.kind == TOKEN_RIGHT_BRACE) {
				preprocessor_next_token(parser->preprocessor);
				break;
			}
		}

		Token name_token = preprocessor_next_token(parser->preprocessor);
		if (name_token.kind != TOKEN_IDENT) {
			TokenKind expected_tokens[] = { TOKEN_IDENT };
			diagnostics_report_unexpected_token(parser->diagnostics,
					name_token,
					expected_tokens,
					array_size(expected_tokens));
			result = false;
			break;
		}

		EnumVariant* variant = arena_alloc_zeroed(parser->temp_allocator, EnumVariant);
		variant->name = name_token.string;
		variant->name_source_range = source_range_pack(name_token.source_range);
		variant_count += 1;

		// Parse an optional value
		Token equal_token = preprocessor_view_next(parser->preprocessor);
		if (equal_token.kind == TOKEN_EQUAL) {
			preprocessor_next_token(parser->preprocessor);

			Expr* value = arena_alloc(parser->ast_allocator, Expr);
			switch (_parser_try_parse_expr(parser, value)) {
			case EXPR_PARSE_OK:
				variant->value = value;
				break;
			case EXPR_PARSE_ERROR:
			case EXPR_PARSE_NOT_PARSED:
				diagnostics_report_error(parser->diagnostics,
						equal_token.source_range,
						STR_LIT("Expected a enum variant value"),
						NULL);
				break;
			}
		}

		Token comma_or_right_brace = preprocessor_view_next(parser->preprocessor);
		if (comma_or_right_brace.kind == TOKEN_RIGHT_BRACE) {
			preprocessor_next_token(parser->preprocessor); // consume TOKEN_RIGHT_BRACE
			break;
		} else if (comma_or_right_brace.kind == TOKEN_COMMA) {
			preprocessor_next_token(parser->preprocessor); // consume TOKEN_COMMA
			continue;
		} else {
			TokenKind expected_tokens[] = { TOKEN_RIGHT_BRACE, TOKEN_COMMA };
			diagnostics_report_unexpected_token(parser->diagnostics,
					name_token,
					expected_tokens,
					array_size(expected_tokens));

			arena_end_temp(temp);
			result = false;
			break;
		}
	}

	if (!result) {
		arena_end_temp(temp);
		profile_scope_end();
		return false;
	}

	if (variant_count > 0) {
		*out_variant_count = variant_count;
		*out_variants = arena_alloc_array(parser->ast_allocator, EnumVariant, variant_count);
		array_copy(*out_variants, variants, variant_count);
	} else {
		*out_variant_count = 0;
		*out_variants = NULL;
	}

	arena_end_temp(temp);
	profile_scope_end();
	return true;
}

void _parser_register_enum_variants(Parser* parser, Enum* enum_def) {
	profile_func_colored(PROFILE_COLOR);
	for (size_t i = 0; i < enum_def->variant_count; i += 1) {
		EnumVariant variant = enum_def->variants[i];
		IdentifierEntry* entry = ident_storage_find(parser->ident_storage,
				IDENT_NAMESPACE_DEFAULT,
				IDENT_FIND_DEFAULT,
				variant.name);

		if (entry) {
			StringBuilder builder = { .arena = parser->diagnostics->allocator };
			str_builder_append(&builder, STR_LIT("Name \'"));
			str_builder_append(&builder, entry->name);
			str_builder_append(&builder, STR_LIT("' is already defined"));

			DiagnosticsEntry* error = report_error(parser->diagnostics,
					variant.name_source_range,
					builder.string,
					NULL);

			report_error(parser->diagnostics,
					entry->name_source_range,
					STR_LIT("Previously defined here"),
					error);
		} else {
			entry = ident_storage_insert(parser->ident_storage,
					IDENT_NAMESPACE_DEFAULT,
					IDENT_ENUM_CONSTANT,
					variant.name,
					variant.name_source_range);

			entry->enum_constant.enum_def = enum_def;
			entry->enum_constant.variant_index = i;
		}
	}

	profile_scope_end();
}

bool _parser_parse_enum_def(Parser* parser, Enum** out_enum_def, bool is_anonymous) {
	profile_func_colored(PROFILE_COLOR);
	assert(out_enum_def != NULL);

	Token keyword_token = preprocessor_next_token(parser->preprocessor);
	assert(keyword_token.kind == TOKEN_KEYWORD_ENUM);

	String enum_name = {};
	PackedSourceRange enum_name_source_range = {};
	EnumVariant* variants = NULL;
	size_t variant_count = 0;
	bool is_forward_declared = true;

	Token token = preprocessor_view_next(parser->preprocessor);
	if (token.kind == TOKEN_IDENT) {
		preprocessor_next_token(parser->preprocessor); // consume identifier

		enum_name = token.string;
		enum_name_source_range = source_range_pack(token.source_range);
		token = preprocessor_view_next(parser->preprocessor);
	}

	if (token.kind == TOKEN_LEFT_BRACE) {
		is_forward_declared = false;
		if (!_parser_parse_enum_variants(parser, &variant_count, &variants)) {
			profile_scope_end();
			return false;
		}
	}

	if (variants != NULL) {
		assert(variant_count > 0);
	}

	Enum* enum_def = NULL;
	if (enum_name.length > 0 && !is_anonymous) {
		IdentifierEntry* entry = ident_storage_find(parser->ident_storage,
				IDENT_NAMESPACE_TAGGED,
				IDENT_FIND_DEFAULT,
				enum_name);

		if (entry) {
			if (!has_flag(entry->kind, IDENT_ENUM)) {
				StringBuilder builder = { .arena = parser->diagnostics->allocator };
				str_builder_append_char(&builder, '\'');
				str_builder_append(&builder, entry->name);
				str_builder_append(&builder, STR_LIT("' is previously defined with a different tag type"));

				DiagnosticsEntry* error = report_error(parser->diagnostics,
						enum_name_source_range,
						builder.string,
						NULL);

				report_error(parser->diagnostics,
						entry->name_source_range,
						STR_LIT("Previously defined here"),
						error);
				profile_scope_end();
				return false;
			}

			enum_def = entry->enum_def;
			assert(enum_def);

			if (!enum_def->is_forward_declared && !is_forward_declared) {
				StringBuilder builder = { .arena = parser->diagnostics->allocator };
				str_builder_append(&builder, STR_LIT("Redefinition of '"));
				str_builder_append(&builder, entry->name);
				str_builder_append_char(&builder, '\'');

				DiagnosticsEntry* error = report_error(parser->diagnostics,
						enum_name_source_range,
						builder.string,
						NULL);

				report_error(parser->diagnostics,
						entry->name_source_range,
						STR_LIT("Previously defined here"),
						error);
				profile_scope_end();
				return false;
			}
		} else {
			entry = ident_storage_insert(parser->ident_storage,
					IDENT_NAMESPACE_TAGGED,
					IDENT_ENUM,
					enum_name,
					enum_name_source_range);

			enum_def = arena_alloc(parser->ast_allocator, Enum);
			memset(enum_def, 0, sizeof(*enum_def));
			
			enum_def->name = enum_name;
			enum_def->name_source_range = enum_name_source_range;
			enum_def->is_forward_declared = is_forward_declared;

			entry->enum_def = enum_def;
		}
	} else {
		enum_def = arena_alloc(parser->ast_allocator, Enum);
		memset(enum_def, 0, sizeof(*enum_def));

		enum_def->name = enum_name;
		enum_def->is_forward_declared = is_forward_declared;
	}

	assert(enum_def);

	if (is_forward_declared) {
		assert(variant_count == 0);
		assert(variants == NULL);
	} else {
		enum_def->variant_count = variant_count;
		enum_def->variants = variants;
		enum_def->is_forward_declared = false;

		_parser_register_enum_variants(parser, enum_def);
	}

	*out_enum_def = enum_def;
	profile_scope_end();
	return true;
}

TypeQualifiers _parser_parse_type_qualifiers(Parser* parser) {
	TypeQualifiers qualifiers = TYPE_QUALIFIER_NONE;
	while (true) {
		Token token = preprocessor_view_next(parser->preprocessor);
		if (token.kind == TOKEN_KEYWORD_CONST) {
			preprocessor_next_token(parser->preprocessor);
			qualifiers |= TYPE_QUALIFIER_CONST;
		} else if (token.kind == TOKEN_KEYWORD_VOLATILE) {
			preprocessor_next_token(parser->preprocessor);
			qualifiers |= TYPE_QUALIFIER_VOLATILE;
		} else {
			break;
		}
	}

	return qualifiers;
}

ParseTypeResult _parser_try_parse_primitive_type(Parser* parser, Type* out_type) {
	profile_func_colored(PROFILE_COLOR);
	assert(out_type != NULL);

	Token token = preprocessor_view_next(parser->preprocessor);
	if (token.kind == TOKEN_IDENT) {
		if (is_digit(token.string.v[0])) {
			profile_scope_end();
			return PARSE_TYPE_NOT_PARSED;
		}

		IdentifierEntry* entry = ident_storage_find(parser->ident_storage,
				IDENT_NAMESPACE_ALIAS,
				IDENT_FIND_DEFAULT,
				token.string);
		if (entry == NULL) {
			profile_scope_end();
			return PARSE_TYPE_NOT_PARSED;
		}

		switch (entry->kind) {
		case IDENT_TYPE_DEF:
			preprocessor_next_token(parser->preprocessor);

			*out_type = entry->type_def->aliased_type;
			out_type->alias_definition = entry->type_def;
			profile_scope_end();
			return PARSE_TYPE_PARSED;
		case IDENT_FUNCTION:
		case IDENT_VARIABLE:
		case IDENT_STRUCT:
		case IDENT_UNION:
		case IDENT_ENUM:
		case IDENT_ENUM_CONSTANT:
		case IDENT_FUNCTION_PARAM:
		case IDENT_KIND_MAX:
			unreachable();
		}

		unreachable();
	}

	if (token.kind == TOKEN_KEYWORD_VOID) {
		preprocessor_next_token(parser->preprocessor);
		out_type->kind = TYPE_VOID;
		profile_scope_end();
		return PARSE_TYPE_PARSED;
	} else if (token.kind == TOKEN_KEYWORD_FLOAT) {
		preprocessor_next_token(parser->preprocessor);
		out_type->kind = TYPE_FLOAT;
		profile_scope_end();
		return PARSE_TYPE_PARSED;
	} else if (token.kind == TOKEN_KEYWORD_DOUBLE) {
		preprocessor_next_token(parser->preprocessor);
		out_type->kind = TYPE_DOUBLE;
		profile_scope_end();
		return PARSE_TYPE_PARSED;
	} else if (token.kind == TOKEN_KEYWORD_SIZE_T) {
		preprocessor_next_token(parser->preprocessor);
		out_type->kind = TYPE_SIZE_T;
		profile_scope_end();
		return PARSE_TYPE_PARSED;
	} else if (token.kind == TOKEN_KEYWORD_BOOL ){
		preprocessor_next_token(parser->preprocessor);
		out_type->kind = TYPE_BOOL;
		profile_scope_end();
		return PARSE_TYPE_PARSED;
	}

	TypeKind type_kind = TYPE_VOID;
	TypeKindFlags type_flags = TYPE_FLAG_NONE;
	if (token.kind == TOKEN_KEYWORD_SIGNED) {
		preprocessor_next_token(parser->preprocessor);

		type_kind = TYPE_INT;
		type_flags |= TYPE_FLAG_SIGNED;
		token = preprocessor_view_next(parser->preprocessor);
	} else if (token.kind == TOKEN_KEYWORD_UNSIGNED) {
		preprocessor_next_token(parser->preprocessor);

		type_kind = TYPE_INT;
		type_flags |= TYPE_FLAG_UNSIGNED;
		token = preprocessor_view_next(parser->preprocessor);
	}

	if (token.kind == TOKEN_KEYWORD_CHAR) {
		preprocessor_next_token(parser->preprocessor);
		type_kind = TYPE_CHAR;
	} else if (token.kind == TOKEN_KEYWORD_INT) {
		preprocessor_next_token(parser->preprocessor);
		type_kind = TYPE_INT;
	} else if (token.kind == TOKEN_KEYWORD_SHORT) {
		preprocessor_next_token(parser->preprocessor);
		type_kind = TYPE_SHORT;
	} else if (token.kind == TOKEN_KEYWORD_INT8) {
		preprocessor_next_token(parser->preprocessor);
		type_kind = TYPE_INT8;
	} else if (token.kind == TOKEN_KEYWORD_INT16) {
		preprocessor_next_token(parser->preprocessor);
		type_kind = TYPE_INT16;
	} else if (token.kind == TOKEN_KEYWORD_INT32) {
		preprocessor_next_token(parser->preprocessor);
		type_kind = TYPE_INT32;
	} else if (token.kind == TOKEN_KEYWORD_INT64) {
		preprocessor_next_token(parser->preprocessor);
		type_kind = TYPE_INT64;
	} else if (token.kind == TOKEN_KEYWORD_LONG) {
		preprocessor_next_token(parser->preprocessor);

		Token next_token = preprocessor_view_next(parser->preprocessor);
		if (next_token.kind == TOKEN_KEYWORD_LONG) {
			preprocessor_next_token(parser->preprocessor);
			type_kind = TYPE_LONG_LONG;
		} else if (next_token.kind == TOKEN_KEYWORD_DOUBLE) {
			preprocessor_next_token(parser->preprocessor);
			type_kind = TYPE_LONG_DOUBLE;
		} else {
			type_kind = TYPE_LONG;
		}
	}

	if (type_kind != TYPE_VOID) {
		out_type->kind = type_kind | (TypeKind)type_flags;
		profile_scope_end();
		return PARSE_TYPE_PARSED;
	}

	profile_scope_end();
	return PARSE_TYPE_NOT_PARSED;
}

ParseTypeResult _parser_try_parse_type_specifier(Parser* parser, Type* out_type, bool is_anonymous) {
	profile_func_colored(PROFILE_COLOR);
	assert(out_type != NULL);

	ParseTypeResult primitive_parse_result = _parser_try_parse_primitive_type(parser, out_type);
	switch (primitive_parse_result) {
	case PARSE_TYPE_ERROR:
	case PARSE_TYPE_PARSED:
		profile_scope_end();
		return primitive_parse_result;
	case PARSE_TYPE_NOT_PARSED:
		break;
	}

	Token token = preprocessor_view_next(parser->preprocessor);
	if (token.kind == TOKEN_IDENT) {
		if (is_digit(token.string.v[0])) {
			profile_scope_end();
			return PARSE_TYPE_NOT_PARSED;
		}

		IdentifierEntry* entry = ident_storage_find(parser->ident_storage,
				IDENT_NAMESPACE_ALIAS,
				IDENT_FIND_DEFAULT,
				token.string);
		if (entry == NULL) {
			profile_scope_end();
			return PARSE_TYPE_NOT_PARSED;
		}

		// NOTE: Here since we only search in the alias namespace, we only care about the type def,
		//       and since the alias namespace contains only type defs, any other ident kind is
		//       expected
		switch (entry->kind) {
		case IDENT_TYPE_DEF:
			preprocessor_next_token(parser->preprocessor);

			*out_type = entry->type_def->aliased_type;
			out_type->alias_definition = entry->type_def;
			profile_scope_end();
			return PARSE_TYPE_PARSED;
		case IDENT_FUNCTION:
		case IDENT_VARIABLE:
		case IDENT_STRUCT:
		case IDENT_UNION:
		case IDENT_ENUM:
		case IDENT_ENUM_CONSTANT:
		case IDENT_FUNCTION_PARAM:
		case IDENT_KIND_MAX:
			unreachable();
		}

		unreachable();
	} else if (token.kind == TOKEN_KEYWORD_STRUCT || token.kind == TOKEN_KEYWORD_UNION) {
		Struct* struct_def = {};
		if (!_parser_parse_struct_def(parser, &struct_def, is_anonymous)) {
			profile_scope_end();
			return PARSE_TYPE_ERROR;
		}


		if (struct_def->layout_kind == STRUCT_LAYOUT_KIND_STRUCT) {
			out_type->kind = TYPE_STRUCT;
			out_type->struct_def = struct_def;
		} else {
			out_type->kind = TYPE_UNION;
			out_type->union_def = struct_def;
		}

		profile_scope_end();
		return PARSE_TYPE_PARSED;
	} else if (token.kind == TOKEN_KEYWORD_ENUM) {
		Enum* enum_def = {};
		if (!_parser_parse_enum_def(parser, &enum_def, is_anonymous)) {
			profile_scope_end();
			return PARSE_TYPE_ERROR;
		}

		out_type->kind = TYPE_ENUM;
		out_type->enum_def = enum_def;
		profile_scope_end();
		return PARSE_TYPE_PARSED;
	}

	profile_scope_end();
	return PARSE_TYPE_NOT_PARSED;
}

static ParseTypeResult _parser_try_parse_type(Parser* parser, Type* out_type, bool is_anonymous) {
	assert(out_type != NULL);

	// First parse qualifiers
	TypeQualifiers qualifiers = _parser_parse_type_qualifiers(parser);

	Token first_token = preprocessor_view_next(parser->preprocessor);

	ParseTypeResult result = _parser_try_parse_type_specifier(parser, out_type, is_anonymous);
	if (result == PARSE_TYPE_NOT_PARSED && qualifiers) {
		diagnostics_report_error(parser->diagnostics,
				first_token.source_range,
				STR_LIT("Expected a type name"),
				NULL);
		return PARSE_TYPE_ERROR;
	}

	if (result == PARSE_TYPE_PARSED) {
		out_type->qualifiers |= qualifiers | _parser_parse_type_qualifiers(parser);
	}

	return result;
}

static bool _parser_parse_type(Parser* parser, Type* out_type, bool is_anonymous) {
	assert(out_type != NULL);

	Token first_token = preprocessor_view_next(parser->preprocessor);
	switch (_parser_try_parse_type(parser, out_type, is_anonymous)) {
	case PARSE_TYPE_NOT_PARSED:
		diagnostics_report_error(parser->diagnostics,
				first_token.source_range,
				STR_LIT("Expected a type name"),
				NULL);
		return false;
	case PARSE_TYPE_ERROR:
		return false;
	case PARSE_TYPE_PARSED:
		return true;
	}

	unreachable();
	return false;
}

ParseTypeResult _parser_try_parse_type_name(Parser* parser, Type* out_type) {
	Type type = {};
	ParseTypeResult result = _parser_try_parse_type(parser, &type, true);

	if (result != PARSE_TYPE_PARSED) {
		return result;
	}

	Declarator declarator = {};
	if (!_parser_parse_declarator(parser, &type, &declarator, true)) {
		return PARSE_TYPE_ERROR;
	}

	*out_type = declarator.type;
	return PARSE_TYPE_PARSED;
}

AstNode* _parser_parse_type_def(Parser* parser) {
	profile_func_colored(PROFILE_COLOR);
	Token keyword_token = preprocessor_next_token(parser->preprocessor);
	assert(keyword_token.kind == TOKEN_KEYWORD_TYPEDEF);

	TypeDef* type_def = arena_alloc(parser->ast_allocator, TypeDef);

	Type aliased_type = {};
	if (!_parser_parse_type(parser, &aliased_type, false)) {
		_parser_skip_until_semicolon(parser);
		profile_scope_end();
		return parser->dummy_node;
	}

	while (true) {
		Declarator declarator = {};
		if (!_parser_parse_declarator(parser, &aliased_type, &declarator, false)) {
			_parser_skip_until_semicolon(parser);
			profile_scope_end();
			return parser->dummy_node;
		}

		TypeDefVariant* variant = arena_alloc_zeroed(parser->ast_allocator, TypeDefVariant);
		variant->new_name = declarator.name;
		variant->new_name_source_range = declarator.name_source_range;
		variant->aliased_type = declarator.type;
		variant->parent = type_def;

		if (type_def->first_variant == NULL) {
			type_def->first_variant = variant;
			type_def->last_variant = variant;
		} else {
			type_def->last_variant->next = variant;
			type_def->last_variant = variant;
		}

		if (variant->new_name.length > 0) {
			IdentifierEntry* entry = ident_storage_find(parser->ident_storage,
					IDENT_NAMESPACE_ALIAS,
					IDENT_FIND_DEFAULT,
					variant->new_name);

			if (!entry) {
				entry = ident_storage_insert(parser->ident_storage,
						IDENT_NAMESPACE_ALIAS,
						IDENT_TYPE_DEF,
						variant->new_name,
						variant->new_name_source_range);
			}

			entry->type_def = variant;
		} else {
			report_error(parser->diagnostics,
					source_range_pack(keyword_token.source_range),
					STR_LIT("typedef requires a name"),
					NULL);
		}

		Token semicolon_or_comma = preprocessor_next_token(parser->preprocessor);
		if (semicolon_or_comma.kind == TOKEN_SEMICOLON) {
			break;
		} else if (semicolon_or_comma.kind == TOKEN_COMMA) {
			continue;
		} else {
			TokenKind expected_tokens[] = {
				TOKEN_SEMICOLON,
				TOKEN_COMMA,
			};

			diagnostics_report_unexpected_token(parser->diagnostics,
					semicolon_or_comma,
					expected_tokens,
					array_size(expected_tokens));
			profile_scope_end();
			return NULL;
		}
	}

	AstNode* node = arena_alloc_zeroed(parser->ast_allocator, AstNode);
	node->kind = AST_NODE_TYPE_DEF;
	node->type_def = type_def;
	profile_scope_end();
	return node;
}

static bool _parser_parse_function_params(Parser* parser,
		FunctionParam** out_params,
		size_t* out_param_count,
		bool* out_has_va_args) {
	profile_func_colored(PROFILE_COLOR);

	Token left_paren = preprocessor_next_token(parser->preprocessor);
	assert(left_paren.kind == TOKEN_LEFT_PAREN);

	ArenaRegion ast_temp = arena_begin_temp(parser->ast_allocator);
	ArenaRegion temp = arena_begin_temp(parser->temp_allocator);

	FunctionParam* params = arena_alloc_array(parser->temp_allocator, FunctionParam, 0);
	size_t param_count = 0;
	bool has_va_args = false;

	while (true) {
		{
			Token token = preprocessor_view_next(parser->preprocessor);
			if (token.kind == TOKEN_RIGHT_PAREN) {
				preprocessor_next_token(parser->preprocessor);
				break;
			} else if (token.kind == TOKEN_ELLIPSES) {
				preprocessor_next_token(parser->preprocessor);
				has_va_args = true;

				Token right_paren = preprocessor_next_token(parser->preprocessor);
				if (right_paren.kind != TOKEN_RIGHT_PAREN) {
					TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };
					diagnostics_report_unexpected_token(parser->diagnostics,
							right_paren,
							expected_tokens,
							array_size(expected_tokens));

					arena_end_temp(ast_temp);
					arena_end_temp(temp);
					profile_scope_end();
					return false;
				}

				break;
			}
		}

		Type param_type = {};
		Declarator param_declarator = {};
		if (!_parser_parse_type(parser, &param_type, true)) {
			_parser_skip_until(parser, TOKEN_COMMA, TOKEN_RIGHT_PAREN);
		} else if (!_parser_parse_declarator(parser, &param_type, &param_declarator, false)) {
			_parser_skip_until(parser, TOKEN_COMMA, TOKEN_RIGHT_PAREN);
		} else if (param_declarator.type.kind == TYPE_VOID
				&& param_declarator.type.qualifiers == TYPE_QUALIFIER_NONE) {
			assert(param_count == 0);

			Token token = preprocessor_view_next(parser->preprocessor);
			if (token.kind == TOKEN_RIGHT_PAREN) {
				preprocessor_next_token(parser->preprocessor);
				break;
			} else {
				TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };
				diagnostics_report_unexpected_token(parser->diagnostics,
						token,
						expected_tokens,
						array_size(expected_tokens));
				break;
			}
		}

		FunctionParam* param = arena_alloc_zeroed(parser->temp_allocator, FunctionParam);
		param->type = param_declarator.type;
		param->name = param_declarator.name;
		param->name_source_range = param_declarator.name_source_range;

		param_count += 1;

		Token token = preprocessor_view_next(parser->preprocessor);
		if (token.kind == TOKEN_COMMA) {
			preprocessor_next_token(parser->preprocessor);
			continue; // we most likely have more parameters
		} else if (token.kind == TOKEN_RIGHT_PAREN) {
			preprocessor_next_token(parser->preprocessor);
			break;
		}
	}

	if (param_count == 0) {
		*out_params = NULL;
	} else {
		*out_params = arena_alloc_array(parser->ast_allocator, FunctionParam, param_count);
		array_copy(*out_params, params, param_count);
	}

	*out_param_count = param_count;

	// NOTE: This might still be true is there are no parameters
	*out_has_va_args = has_va_args;

	arena_end_temp(temp);
	profile_scope_end();
	return true;
}

//
// Expr
//

bool _token_kind_to_bin_op(TokenKind kind, BinOpKind* out_op) {
#define ret(op) *out_op = op; return true;

	switch (kind) {
	case TOKEN_PLUS: ret(BIN_OP_ADD);
	case TOKEN_MINUS: ret(BIN_OP_SUB);
	case TOKEN_ASTERISK: ret(BIN_OP_MUL);
	case TOKEN_FORWARD_SLASH: ret(BIN_OP_DIV);
	case TOKEN_PERCENT: ret(BIN_OP_MOD);

	case TOKEN_LOGIC_AND: ret(BIN_OP_LOGICAL_AND);
	case TOKEN_LOGIC_OR: ret(BIN_OP_LOGICAL_OR);

	case TOKEN_DOUBLE_EQUAL: ret(BIN_OP_LOGICAL_EQUAL);
	case TOKEN_NOT_EQUAL: ret(BIN_OP_LOGICAL_NOT_EQUAL);
	case TOKEN_LESS: ret(BIN_OP_LOGICAL_LESS);
	case TOKEN_GREATER: ret(BIN_OP_LOGICAL_GREATER);
	case TOKEN_LESS_OR_EQUAL: ret(BIN_OP_LOGICAL_LESS_OR_EQUAL);
	case TOKEN_GREATER_OR_EQUAL: ret(BIN_OP_LOGICAL_GREATER_OR_EQUAL);

	case TOKEN_AMPERSAND: ret(BIN_OP_BITWISE_AND);
	case TOKEN_PIPE: ret(BIN_OP_BITWISE_OR);
	case TOKEN_BITWISE_XOR: ret(BIN_OP_BITWISE_XOR);
	case TOKEN_BITWISE_SHIFT_LEFT: ret(BIN_OP_BITWISE_SHIFT_LEFT);
	case TOKEN_BITWISE_SHIFT_RIGHT: ret(BIN_OP_BITWISE_SHIFT_RIGHT);

	case TOKEN_EQUAL: ret(BIN_OP_ASSIGNMENT);

	case TOKEN_ASSIGNMENT_BY_SUM: ret(BIN_OP_ASSIGNMENT_BY_SUM);
	case TOKEN_ASSIGNMENT_BY_DIFFERENCE: ret(BIN_OP_ASSIGNMENT_BY_DIFFERENCE);
	case TOKEN_ASSIGNMENT_BY_PRODUCT: ret(BIN_OP_ASSIGNMENT_BY_PRODUCT);
	case TOKEN_ASSIGNMENT_BY_QUOTIENT: ret(BIN_OP_ASSIGNMENT_BY_QUOTIENT);
	case TOKEN_ASSIGNMENT_BY_REMAINDER: ret(BIN_OP_ASSIGNMENT_BY_REMAINDER);

	case TOKEN_ASSIGNMENT_BY_BITWISE_AND: ret(BIN_OP_ASSIGNMENT_BY_BITWISE_AND);
	case TOKEN_ASSIGNMENT_BY_BITWISE_OR: ret(BIN_OP_ASSIGNMENT_BY_BITWISE_OR);
	case TOKEN_ASSIGNMENT_BY_BITWISE_XOR: ret(BIN_OP_ASSIGNMENT_BY_BITWISE_XOR);
	case TOKEN_ASSIGNMENT_BY_BITWISE_SHIFT_LEFT: ret(BIN_OP_ASSIGNMENT_BY_BITWISE_SHIFT_LEFT);
	case TOKEN_ASSIGNMENT_BY_BITWISE_SHIFT_RIGHT: ret(BIN_OP_ASSIGNMENT_BY_BITWISE_SHIFT_RIGHT);

	default:
		return false;
	}

#undef ret

	return false;
}

bool _token_kind_to_unary_pre_op(TokenKind kind, UnaryOpKind* out_op) {
#define ret(op) *out_op = op; return true;
	switch (kind) {
	case TOKEN_PLUS: ret(UNARY_OP_PLUS);
	case TOKEN_MINUS: ret(UNARY_OP_NEGATE);
	case TOKEN_DOUBLE_PLUS: ret(UNARY_OP_PRE_INCREMENT);
	case TOKEN_DOUBLE_MINUS: ret(UNARY_OP_PRE_DECREMENT);
	case TOKEN_AMPERSAND: ret(UNARY_OP_ADDRESS);
	case TOKEN_ASTERISK: ret(UNARY_OP_DEREFERENCE);
	case TOKEN_EXCLAMATION_MARK: ret(UNARY_OP_LOGICAL_NOT);
	case TOKEN_BITWISE_NOT: ret(UNARY_OP_BITWISE_NOT);
	default:
		return false;
	}
#undef ret

	unreachable();
	return false;
}

void _parser_parse_string_literal(Parser* parser, StringLiteral* out_literal) {
	profile_func_colored(PROFILE_COLOR);
	StringBuilder builder = { .arena = parser->ast_allocator };

	Token first_string_token = preprocessor_view_next(parser->preprocessor);
	assert_msg(first_string_token.kind == TOKEN_STRING, "Expected at least a single string token");

	PackedSourceRange source_range = source_range_pack(first_string_token.source_range);

	while (true) {
		Token string_token = preprocessor_view_next(parser->preprocessor);
		if (string_token.kind != TOKEN_STRING) {
			break;
		}

		source_range = source_range_merge(
				source_range,
				source_range_pack(string_token.source_range));

		preprocessor_next_token(parser->preprocessor);

		const SourceFile* source_file = string_token.source_range.source_file;
		String str_content = sub_str(string_token.string, 1, string_token.string.length - 2);
		parse_escaped_string(&builder, str_content, source_file, parser->diagnostics);
	}

	Expr* size_expr = arena_alloc(parser->ast_allocator, Expr);
	size_expr->kind = EXPR_INTEGER_LITERAL;
	size_expr->int_literal.format = INT_LIT_FMT_DECIMAL;
	size_expr->int_literal.integer_type = TYPE_SIZE_T;
	// size including null-terminator
	size_expr->int_literal.value = builder.string.length + 1;

	out_literal->full_string = builder.string;
	out_literal->array_size_expr = size_expr;
	out_literal->source_range = source_range;

	profile_scope_end();
}

static ExprParseResult _parser_try_parse_compound_literal(Parser* parser,
		Type* prefered_type,
		Expr* out_literal);

static ExprParseResult _parser_parse_compound_literal_entry(Parser* parser,
		Type* type,
		size_t* next_value_index,
		CompoundLiteralEntry* out_entry) {

	profile_func_colored(PROFILE_COLOR);

	if (type->kind != TYPE_STRUCT && type->kind != TYPE_UNION) {
		profile_scope_end();
		return EXPR_PARSE_ERROR;
	}

	Type* expected_slot_type = NULL;

	Token token = preprocessor_view_next(parser->preprocessor);
	if (token.kind == TOKEN_DOT) {
		preprocessor_next_token(parser->preprocessor);

		Struct* compound_type = type_extract_compound(type);

		out_entry->kind = COMPOUND_LITERAL_FIELD_INIT;

		Token field_name = preprocessor_next_token(parser->preprocessor);
		if (field_name.kind != TOKEN_IDENT) {
			TokenKind expected_tokens[] = { TOKEN_IDENT };
			diagnostics_report_unexpected_token(parser->diagnostics,
					field_name, 
					expected_tokens, 
					array_size(expected_tokens));
			profile_scope_end();
			return EXPR_PARSE_ERROR;
		}

		out_entry->field.name = field_name.string;
		out_entry->field.index = struct_field_namespace_index_of(
				compound_type->field_namespace,
				field_name.string);

		if (out_entry->field.index == SIZE_MAX) {
			StringBuilder builder = { .arena = parser->diagnostics->allocator };

			str_builder_append(&builder, STR_LIT("'"));
			type_format(type, &builder);
			str_builder_format(&builder,
					"' has no field named '%.*s'", STR_FMT(field_name.string));

			report_error(parser->diagnostics,
					source_range_pack(field_name.source_range),
					builder.string,
					NULL);
			profile_scope_end();
			return EXPR_PARSE_ERROR;
		}

		StructFieldNamespaceEntry field_entry =
			compound_type->field_namespace->entries[out_entry->field.index];

		expected_slot_type = &field_entry.struct_def->fields[field_entry.field_index].type;

		Token equal = preprocessor_next_token(parser->preprocessor);
		if (equal.kind != TOKEN_EQUAL) {
			TokenKind expected_tokens[] = { TOKEN_EQUAL };
			diagnostics_report_unexpected_token(parser->diagnostics,
					equal, 
					expected_tokens, 
					array_size(expected_tokens));
			profile_scope_end();
			return EXPR_PARSE_ERROR;
		}
	} else if (token.kind == TOKEN_LEFT_BRACKET) {
		if (type->kind != TYPE_ARRAY) {
			StringBuilder builder = { .arena = parser->diagnostics->allocator };
			str_builder_append(&builder, STR_LIT("Cannot initialize non-array type '"));
			type_format(type, &builder);
			str_builder_append_char(&builder, '\'');
				
			report_error(parser->diagnostics,
					source_range_pack(token.source_range),
					builder.string,
					NULL);
			profile_scope_end();
			return EXPR_PARSE_ERROR;
		}

		panic("todo");
	} else {
		out_entry->kind = COMPOUND_LITERAL_VALUE;
		out_entry->not_designated.index = *next_value_index;

		*next_value_index += 1;

		Struct* compound_type = type_extract_compound(type);
		assert(compound_type);

		if (out_entry->not_designated.index >= compound_type->field_count) {
			report_error(parser->diagnostics,
					source_range_pack(preprocessor_view_next(parser->preprocessor).source_range),
					STR_LIT("Too many initializers"),
					NULL);
			profile_scope_end();
			return EXPR_PARSE_ERROR;
		}

		expected_slot_type = &compound_type->fields[out_entry->not_designated.index].type;
	}

	Expr* value = arena_alloc_zeroed(parser->ast_allocator, Expr);
	out_entry->value = value;

	ExprParseResult expr_result;
	if (preprocessor_view_next(parser->preprocessor).kind == TOKEN_LEFT_BRACE) {
		expr_result = _parser_try_parse_compound_literal(parser, expected_slot_type, value);
	} else {
		expr_result = _parser_try_parse_expr(parser, value);
	}

	if (expr_result != EXPR_PARSE_OK) {
		_parser_skip_until(parser, TOKEN_COMMA, TOKEN_RIGHT_BRACE);
		profile_scope_end();
		return EXPR_PARSE_ERROR;
	}

	profile_scope_end();
	return EXPR_PARSE_OK;
}

static ExprParseResult _parser_try_parse_compound_literal(Parser* parser,
		Type* prefered_type,
		Expr* out_literal) {

	profile_func_colored(PROFILE_COLOR);
	assert(preprocessor_view_next(parser->preprocessor).kind == TOKEN_LEFT_BRACE);

	preprocessor_next_token(parser->preprocessor); // consume {

	ArenaRegion temp = arena_begin_temp(parser->temp_allocator);

	size_t entry_count = 0;
	CompoundLiteralEntry* entries = arena_alloc_array(parser->temp_allocator,
			CompoundLiteralEntry,
			0);
	
	size_t next_value_index = 0;

	while (true) {
		Token token = preprocessor_view_next(parser->preprocessor);
		if (token.kind == TOKEN_RIGHT_BRACE) {
			preprocessor_next_token(parser->preprocessor);
			break;
		}

		CompoundLiteralEntry entry;
		if (_parser_parse_compound_literal_entry(parser,
					prefered_type,
					&next_value_index,
					&entry) == EXPR_PARSE_OK) {

			arena_alloc(parser->temp_allocator, CompoundLiteralEntry);
			entries[entry_count] = entry;
			entry_count += 1;
		} else {
			_parser_skip_until(parser, TOKEN_COMMA, TOKEN_RIGHT_BRACE);
		}

		token = preprocessor_view_next(parser->preprocessor);
		if (token.kind == TOKEN_COMMA) {
			preprocessor_next_token(parser->preprocessor);
			continue;
		} else if (token.kind == TOKEN_RIGHT_BRACE) {
			continue;
		} else {
			TokenKind expected_tokens[] = { TOKEN_COMMA, TOKEN_RIGHT_BRACE };
			diagnostics_report_unexpected_token(parser->diagnostics,
					token,
					expected_tokens,
					array_size(expected_tokens));
			break;
		}
	}

	out_literal->kind = EXPR_COMPOUND_LITERAL;
	out_literal->compound_literal.type = prefered_type;
	out_literal->compound_literal.entry_count = entry_count;
	out_literal->compound_literal.entries = arena_alloc_array(parser->ast_allocator, 
			CompoundLiteralEntry,
			entry_count);

	array_copy(out_literal->compound_literal.entries, entries, entry_count);

	arena_end_temp(temp);

	profile_scope_end();
	return EXPR_PARSE_OK;
}

static ExprParseResult _parser_try_parse_expr_operand_without_post_fix_operator(Parser* parser, Expr* out_expr) {
	profile_func_colored(PROFILE_COLOR);
	Token token = preprocessor_view_next(parser->preprocessor);

	UnaryOpKind unary_op;
	if (_token_kind_to_unary_pre_op(token.kind, &unary_op)) {
		preprocessor_next_token(parser->preprocessor);

		out_expr->kind = EXPR_UNARY;
		out_expr->unary.operand = arena_alloc(parser->ast_allocator, Expr);
		out_expr->unary.op = unary_op;
		out_expr->unary.operator_source_range = source_range_pack(token.source_range);

		Token operand_token = preprocessor_view_next(parser->preprocessor);
		ExprParseResult result = _parser_try_parse_bin_expr_operand(parser, out_expr->unary.operand);

		bool requires_l_value = unary_op == UNARY_OP_PRE_INCREMENT
			|| unary_op == UNARY_OP_PRE_DECREMENT;

		bool requires_int_or_pointer_operand = unary_op == UNARY_OP_PRE_INCREMENT
			|| unary_op == UNARY_OP_POST_INCREMENT
			|| unary_op == UNARY_OP_PRE_DECREMENT
			|| unary_op == UNARY_OP_POST_DECREMENT;

		if (result == EXPR_PARSE_OK && requires_int_or_pointer_operand) {
			Type operand_type;
			expr_get_type(out_expr->unary.operand, &operand_type);

			bool is_int = type_kind_is_int(operand_type.kind);
			bool is_pointer = operand_type.kind == TYPE_POINTER;

			if (!is_int && !is_pointer) {
				StringBuilder builder = { parser->diagnostics->allocator };
				str_builder_append(&builder, STR_LIT("Cannot apply '"));
				str_builder_append(&builder, token.string);
				str_builder_append(&builder, STR_LIT("' to an operand of type '"));
				type_format(&operand_type, &builder);
				str_builder_append(&builder, STR_LIT("'"));

				report_error(parser->diagnostics,
						expr_get_source_range(out_expr->unary.operand),
						builder.string,
						NULL);
			}
		}

		if (result == EXPR_PARSE_OK && requires_l_value) {
			ValueKind operand_value_kind = expr_get_value_kind(out_expr->unary.operand);
			if (operand_value_kind != VALUE_L) {
				report_error(parser->diagnostics,
						expr_get_source_range(out_expr->unary.operand),
						STR_LIT("Expected an l-value"),
						NULL);
			}
		}

		if (unary_op == UNARY_OP_ADDRESS) {
			Type* pointer_base_type = arena_alloc(parser->ast_allocator, Type);

			if (result == EXPR_PARSE_OK) {
				expr_get_type(out_expr->unary.operand, pointer_base_type);
			} else {
				pointer_base_type->kind = TYPE_VOID;
			}

			out_expr->unary.pointer_base_type = pointer_base_type;
		}

		profile_scope_end();
		return result;
	}
	
	if (token.kind == TOKEN_IDENT) {
		preprocessor_next_token(parser->preprocessor);

		assert(token.string.length > 0);
		bool is_int_literal = is_digit(token.string.v[0]);
		if (is_int_literal) {
			IntLiteral literal = {};
			bool literal_parsed = parse_int_literal(token, parser->diagnostics, &literal);
			if (!literal_parsed) {
				profile_scope_end();
				return EXPR_PARSE_ERROR;
			}

			TypeKind int_type = TYPE_VOID;
			if (literal.has_sufix) {
				switch (literal.sufix_kind) {
				case INT_SUFIX_NONE: {
					assert(literal.sufix_bit_count > 0);
					uint8_t bit_count_index = count_trailing_zeros(literal.sufix_bit_count / 8);
					int_type = TYPE_INT8 + bit_count_index;
					break;
				}
				case INT_SUFIX_U: {
					if (literal.sufix_bit_count > 0) {
						uint8_t bit_count_index = count_trailing_zeros(literal.sufix_bit_count / 8);
						int_type = TYPE_INT8 + bit_count_index;
					} else {
						int_type = TYPE_INT;
					}

					int_type |= (TypeKind)TYPE_FLAG_UNSIGNED;
					break;
				}
				case INT_SUFIX_L:
					assert(literal.sufix_bit_count == 0);
					int_type = TYPE_LONG;
					break;
				case INT_SUFIX_UL:
					assert(literal.sufix_bit_count == 0);
					int_type = TYPE_UNSIGNED_LONG;
					break;
				case INT_SUFIX_LL:
					assert(literal.sufix_bit_count == 0);
					int_type = TYPE_LONG_LONG;
					break;
				case INT_SUFIX_ULL:
					assert(literal.sufix_bit_count == 0);
					int_type = TYPE_UNSIGNED_LONG_LONG;
					break;
				}
			} else if (literal.value <= INT32_MAX) {
				int_type = TYPE_INT;
			} else if (literal.value <= UINT32_MAX) {
				int_type = TYPE_UNSIGNED_INT;
			} else if (literal.value <= INT64_MAX) {
				int_type = TYPE_LONG_LONG;
			} else {
				int_type = TYPE_UNSIGNED_LONG_LONG;
			}

			out_expr->kind = EXPR_INTEGER_LITERAL;
			out_expr->int_literal = (IntegerLiteral) {
				.format = literal.format,
				.integer_type = int_type,
				.value = literal.value,
				.source_range = source_range_pack(token.source_range),
			};

			profile_scope_end();
			return EXPR_PARSE_OK;
		}

		IdentifierEntry* entry = ident_storage_find(parser->ident_storage,
				IDENT_NAMESPACE_DEFAULT,
				IDENT_FIND_DEFAULT,
				token.string);
		if (entry == NULL) {
			diagnostics_report_error(parser->diagnostics,
					token.source_range,
					STR_LIT("Use of undeclared identifier"),
					NULL);
			profile_scope_end();
			return EXPR_PARSE_ERROR;
		}

		switch (entry->kind) {
		case IDENT_FUNCTION:
			out_expr->kind = EXPR_FUNCTION_REFERENCE;
			out_expr->function_ref.func = entry->function_def;
			out_expr->function_ref.source_range = source_range_pack(token.source_range);
			profile_scope_end();
			return EXPR_PARSE_OK;
		case IDENT_VARIABLE:
			out_expr->kind = EXPR_VARIABLE_REFERENCE;
			out_expr->variable_ref.var = entry->variable;
			out_expr->variable_ref.source_range = source_range_pack(token.source_range);
			profile_scope_end();
			return EXPR_PARSE_OK;
		case IDENT_TYPE_DEF:
		case IDENT_STRUCT:
		case IDENT_UNION:
		case IDENT_ENUM:
			unreachable();
		case IDENT_ENUM_CONSTANT:
			out_expr->kind = EXPR_ENUM_CONSTANT;
			out_expr->enum_constant.enum_def = entry->enum_constant.enum_def;
			out_expr->enum_constant.variant_index = entry->enum_constant.variant_index;
			out_expr->enum_constant.source_range = source_range_pack(token.source_range);
			profile_scope_end();
			return EXPR_PARSE_OK;
		case IDENT_FUNCTION_PARAM:
			out_expr->kind = EXPR_FUNCTION_PARAM;
			out_expr->function_param.function_def = entry->function_param.function_def;
			out_expr->function_param.param_index = entry->function_param.param_index;
			out_expr->function_param.source_range = source_range_pack(token.source_range);
			profile_scope_end();
			return EXPR_PARSE_OK;
		case IDENT_KIND_MAX:
			unreachable();
		}

		unreachable();
	} else if (token.kind == TOKEN_STRING) {
		// we have a string literal
		_parser_parse_string_literal(parser, &out_expr->string_literal);
		out_expr->kind = EXPR_STRING_LITERAL;
		profile_scope_end();
		return EXPR_PARSE_OK;
	} else if (token.kind == TOKEN_CHAR) {
		// we have a char literal

		assert(token.string.length >= 2);
		size_t char_literal_length = token.string.length - 2; // the token includes quotes

		String char_literal = sub_str(token.string, 1, token.string.length - 2);
		if (char_literal_length == 0) {
			diagnostics_report_error(parser->diagnostics,
					token.source_range,
					STR_LIT("Empty character constant"),
					NULL);

			preprocessor_next_token(parser->preprocessor);
			profile_scope_end();
			return EXPR_PARSE_ERROR;
		}

		enum {
			CHAR_STATE_NONE,
			CHAR_STATE_OK,
			CHAR_STATE_TOO_LONG,
		} char_state = CHAR_STATE_NONE;

		uint32_t char_value = 0;
		if (char_literal.length == 1) {
			char_state = CHAR_STATE_OK;
			char_value = (uint32_t)token.string.v[1]; // the first char is a quote
		} else {
			if (char_literal.v[0] == '\\') {
				EscapedChar escaped_char = parse_escaped_char(char_literal,
						token.source_range.source_file,
						parser->diagnostics);

				if (escaped_char.escape_sequence_length == char_literal.length) {
					char_value = (uint32_t)escaped_char.char_value;
					char_state = CHAR_STATE_OK;
				} else if (escaped_char.escape_sequence_length <= char_literal.length) {
					char_state = CHAR_STATE_TOO_LONG;
				} else {
					unreachable();
				}
			} else {
				char_state = CHAR_STATE_TOO_LONG;
			}
		}

		switch (char_state) {
		case CHAR_STATE_NONE:
			unreachable();
		case CHAR_STATE_OK:
			out_expr->kind = EXPR_CHAR_LITERAL;
			out_expr->char_literal.value = char_value;
			out_expr->char_literal.source_range = source_range_pack(token.source_range);
			preprocessor_next_token(parser->preprocessor);
			profile_scope_end();
			return EXPR_PARSE_OK;
		case CHAR_STATE_TOO_LONG:
			diagnostics_report_error(parser->diagnostics,
					token.source_range,
					STR_LIT("Character constant is too long"),
					NULL);

			preprocessor_next_token(parser->preprocessor);
			profile_scope_end();
			return EXPR_PARSE_ERROR;
		}

		unreachable();
	} else if (token.kind == TOKEN_LEFT_PAREN) {
		preprocessor_next_token(parser->preprocessor);

		Type cast_target_type = {};
		ParseTypeResult type_result = _parser_try_parse_type_name(parser, &cast_target_type);

		switch (type_result) {
		case PARSE_TYPE_PARSED: {
			Token right_paren = preprocessor_next_token(parser->preprocessor);
			if (right_paren.kind != TOKEN_RIGHT_PAREN) {
				TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };

				diagnostics_report_unexpected_token(parser->diagnostics,
						right_paren,
						expected_tokens,
						array_size(expected_tokens));
				profile_scope_end();
				return EXPR_PARSE_ERROR;
			}

			if (preprocessor_view_next(parser->preprocessor).kind == TOKEN_LEFT_BRACE) {
				out_expr->kind = EXPR_COMPOUND_LITERAL;

				Type* prefered_type = arena_alloc(parser->ast_allocator, Type);
				*prefered_type = cast_target_type;

				ExprParseResult result = _parser_try_parse_compound_literal(parser,
						prefered_type,
						out_expr);

				profile_scope_end();
				return result;
			}

			out_expr->kind = EXPR_CAST;
			out_expr->cast.target_type = arena_alloc(parser->ast_allocator, Type);
			out_expr->cast.expr = arena_alloc(parser->ast_allocator, Expr);
			out_expr->cast.left_paren_source_range = source_range_pack(token.source_range);

			*out_expr->cast.target_type = cast_target_type;

			ExprParseResult expr_result = _parser_try_parse_bin_expr_operand(
					parser,
					out_expr->cast.expr);

			profile_scope_end();
			return expr_result;
		}
		case PARSE_TYPE_NOT_PARSED:
			break;
		case PARSE_TYPE_ERROR:
			profile_scope_end();
			return EXPR_PARSE_ERROR;
		}

		ExprParseResult result = _parser_try_parse_expr(parser, out_expr);
		if (result != EXPR_PARSE_OK) {
			profile_scope_end();
			return result;
		}

		Token right_paren = preprocessor_next_token(parser->preprocessor);
		if (right_paren.kind != TOKEN_RIGHT_PAREN) {
			TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };

			diagnostics_report_unexpected_token(parser->diagnostics,
					right_paren,
					expected_tokens,
					array_size(expected_tokens));
			profile_scope_end();
			return EXPR_PARSE_ERROR;
		}

		profile_scope_end();
		return EXPR_PARSE_OK;
	} else if (token.kind == TOKEN_KEYWORD_SIZE_OF) {
		preprocessor_next_token(parser->preprocessor);
		
		bool is_wrapped_in_parens = false;
		Token maybe_paren = preprocessor_view_next(parser->preprocessor);

		PackedSourceRange source_range = source_range_pack(token.source_range);

		if (maybe_paren.kind == TOKEN_LEFT_PAREN) {
			is_wrapped_in_parens = true;
			preprocessor_next_token(parser->preprocessor); // consume '('
		}

		Type target_type;
		Expr target_expr;

		ExprKind size_of_kind = 0;

		ParseTypeResult type_result = _parser_try_parse_type_name(parser, &target_type);
		switch (type_result) {
		case PARSE_TYPE_PARSED:
			size_of_kind = EXPR_SIZE_OF_TYPE;
			break;
		case PARSE_TYPE_NOT_PARSED: {
			ExprParseResult expr_result = _parser_try_parse_expr(
					parser,
					&target_expr);

			switch (expr_result) {
			case EXPR_PARSE_OK:
				size_of_kind = EXPR_SIZE_OF_EXPR;
				break;
			case EXPR_PARSE_ERROR:
				profile_scope_end();
				return EXPR_PARSE_ERROR;
			case EXPR_PARSE_NOT_PARSED:
				report_error(parser->diagnostics,
						source_range,
						STR_LIT("Expected an expression or a type after 'sizeof'"),
						NULL);
				profile_scope_end();
				return EXPR_PARSE_ERROR;
			}

			break;
		}
		case PARSE_TYPE_ERROR:
			profile_scope_end();
			return EXPR_PARSE_ERROR;
		}

		if (is_wrapped_in_parens) {
			Token right_paren = preprocessor_next_token(parser->preprocessor);
			if (right_paren.kind != TOKEN_RIGHT_PAREN) {
				TokenKind expected_tokens[] = { TOKEN_LEFT_PAREN };
				diagnostics_report_unexpected_token(parser->diagnostics,
						right_paren,
						expected_tokens,
						array_size(expected_tokens));
			}

			source_range = source_range_merge(
					source_range,
					source_range_pack(right_paren.source_range));
		}

		if (size_of_kind == EXPR_SIZE_OF_TYPE) {
			out_expr->kind = EXPR_SIZE_OF_TYPE;
			out_expr->size_of_type.type = arena_alloc(parser->ast_allocator, Type);
			*out_expr->size_of_type.type = target_type;
			profile_scope_end();
			return EXPR_PARSE_OK;
		} else if (size_of_kind == EXPR_SIZE_OF_EXPR) {
			out_expr->kind = EXPR_SIZE_OF_EXPR;
			out_expr->size_of_expr.expr = arena_alloc(parser->ast_allocator, Expr);
			*out_expr->size_of_expr.expr = target_expr;
			profile_scope_end();
			return EXPR_PARSE_OK;
		}

		unreachable();
	} else if (token.kind == TOKEN_LEFT_BRACE) {
		out_expr->kind = EXPR_COMPOUND_LITERAL;

		report_error(parser->diagnostics,
				source_range_pack(token.source_range), 
				STR_LIT("Expected type name before '{"), 
				NULL);

		_parser_skip_until(parser, TOKEN_RIGHT_BRACE, TOKEN_RIGHT_BRACE);
		profile_scope_end();
		return EXPR_PARSE_ERROR;
	}

	profile_scope_end();
	return EXPR_PARSE_NOT_PARSED;
}

static ExprParseResult _parser_parse_arg_list(Parser* parser, ExprArray* out_expr_array) {
	profile_func_colored(PROFILE_COLOR);

	Token left_paren = preprocessor_next_token(parser->preprocessor);
	assert(left_paren.kind == TOKEN_LEFT_PAREN);

	ArenaRegion temp = arena_begin_temp(parser->temp_allocator);

	ExprArray args = {};
	args.count = 0;
	args.exprs = arena_alloc_array(parser->temp_allocator, Expr*, 0);

	while (true) {
		Token maybe_right_paren = preprocessor_view_next(parser->preprocessor);
		if (maybe_right_paren.kind == TOKEN_RIGHT_PAREN) {
			// Let the caller consume the ')'
			break;
		}

		Expr* arg = arena_alloc(parser->ast_allocator, Expr);
		ExprParseResult result = _parser_try_parse_expr(parser, arg);
		if (result != EXPR_PARSE_OK) {
			arena_end_temp(temp);
			profile_scope_end();
			return result;
		}

		arena_alloc(parser->temp_allocator, Expr);
		args.exprs[args.count] = arg;
		args.count += 1;

		Token comma_or_right_paren = preprocessor_view_next(parser->preprocessor);
		if (comma_or_right_paren.kind == TOKEN_COMMA) {
			preprocessor_next_token(parser->preprocessor);
			continue;
		} else if (comma_or_right_paren.kind == TOKEN_RIGHT_PAREN) {
			// Let the caller consume the ')'
			break;
		} else {
			TokenKind expected_tokens[] = { TOKEN_COMMA, TOKEN_RIGHT_PAREN };
			diagnostics_report_unexpected_token(parser->diagnostics,
					comma_or_right_paren,
					expected_tokens,
					array_size(expected_tokens));
			
			arena_end_temp(temp);
			profile_scope_end();
			return EXPR_PARSE_ERROR;
		}
	}

	out_expr_array->count = args.count;
	out_expr_array->exprs = arena_alloc_array(parser->ast_allocator, Expr*, args.count);
	memcpy(out_expr_array->exprs, args.exprs, sizeof(*args.exprs) * args.count);

	arena_end_temp(temp);
	profile_scope_end();
	return EXPR_PARSE_OK;
}

static bool _check_is_convertable(Parser* parser,
		Type* from,
		Type* to,
		PackedSourceRange from_source_range) {

	if (type_equal(from, to)) {
		return true;
	}

	bool from_is_int_like = type_kind_is_int(from->kind) || from->kind == TYPE_ENUM;
	bool to_is_int_like = type_kind_is_int(to->kind) || to->kind == TYPE_ENUM;
	if (from_is_int_like && to_is_int_like) {
		return true;
	}

	if (to->kind == TYPE_POINTER && type_kind_is_pointer_like(from->kind)) {
		Type* to_base_type = to->pointer_base_type;
		Type* from_base_type = type_extract_pointer_base_type(from);

		if ((from_base_type->qualifiers & to_base_type->qualifiers) != from_base_type->qualifiers) {
			TypeQualifiers discarded = from_base_type->qualifiers & (~to_base_type->qualifiers);
			assert(discarded != TYPE_QUALIFIER_NONE);

			StringBuilder builder = { parser->diagnostics->allocator };
			str_builder_append(&builder, STR_LIT("Convertion from '"));
			type_format(from, &builder);
			str_builder_append(&builder, STR_LIT("' to '"));
			type_format(to, &builder);

			if (discarded == (TYPE_QUALIFIER_CONST)) {
				str_builder_append(&builder, STR_LIT("' discards 'const' qualifier"));
			}

			if (discarded == (TYPE_QUALIFIER_VOLATILE)) {
				str_builder_append(&builder, STR_LIT("' discards 'volatile' qualifier"));
			}

			if (discarded == (TYPE_QUALIFIER_CONST | TYPE_QUALIFIER_VOLATILE)) {
				str_builder_append(&builder,
						STR_LIT("' discards 'const' and 'volatile' qualifiers"));
			}

			report_error(parser->diagnostics,
					from_source_range,
					builder.string,
					NULL);

			return false;
		}
		
		if (type_equal_ignore_qualifiers(to_base_type, from_base_type)) {
			return true;
		}

		if (to_base_type->kind == TYPE_VOID) {
			return true;
		}

		if (from_base_type->kind == TYPE_VOID) {
			return true;
		}
	}

	StringBuilder builder = { parser->diagnostics->allocator };
	str_builder_append(&builder, STR_LIT("Cannot implicitly convert from '"));
	type_format(from, &builder);
	str_builder_append(&builder, STR_LIT("' to '"));
	type_format(to, &builder);
	str_builder_append(&builder, STR_LIT("'"));

	report_error(parser->diagnostics,
			from_source_range,
			builder.string,
			NULL);
	return false;
}

static void _type_check_call(Parser* parser, Expr* call) {
	profile_func_colored(PROFILE_COLOR);

	assert(call->kind == EXPR_CALL);

	Expr* callable = call->call.callable;
	ExprArray args = call->call.args;

	Type callable_type;
	expr_get_type(callable, &callable_type);

	if (!type_is_callable(&callable_type)) {
		report_error(parser->diagnostics,
				expr_get_source_range(callable),
				STR_LIT("Expression is not callable"),
				NULL);

		profile_scope_end();
		return;
	}

	const FunctionPrototype* proto = NULL;
	if (callable_type.kind == TYPE_POINTER
			&& callable_type.pointer_base_type->kind == TYPE_FUNCTION) {
		proto = callable_type.pointer_base_type->function;
	} else if (callable_type.kind == TYPE_FUNCTION) {
		proto = callable_type.function;
	} else {
		unreachable();
	}

	if (args.count < proto->parameter_count) {
		report_error(parser->diagnostics,
				expr_get_source_range(call),
				STR_LIT("Too few arguments for a call"),
				NULL);
		return;
	}

	if (!proto->has_va_args && args.count > proto->parameter_count) {
		report_error(parser->diagnostics,
				expr_get_source_range(call),
				STR_LIT("Too many arguments for a call"),
				NULL);
		return;
	}

	assert(args.count >= proto->parameter_count);
	for (size_t i = 0; i < proto->parameter_count; i += 1) {
		Type arg_type;
		expr_get_type(args.exprs[i], &arg_type);

		_check_is_convertable(parser,
				&arg_type,
				&proto->parameters[i].type,
				expr_get_source_range(args.exprs[i]));
	}

	if (proto->has_va_args) {
		for (size_t i = proto->parameter_count; i < args.count; i += 1) {
			Type arg_type;
			expr_get_type(args.exprs[i], &arg_type);

			if (arg_type.kind != TYPE_VOID) {
				continue;
			}

			report_error(parser->diagnostics,
					expr_get_source_range(args.exprs[i]),
					STR_LIT("Argument has type 'void'"),
					NULL);
		}
	}

	profile_scope_end();
}

// Tries to parse an expression operand + any post fix operators,
// like increment, decrement, array access, member access or a call
static ExprParseResult _parser_try_parse_bin_expr_operand(Parser* parser, Expr* out_expr) {
	profile_func_colored(PROFILE_COLOR);

	Token expr_token = preprocessor_view_next(parser->preprocessor);

	ExprParseResult result = _parser_try_parse_expr_operand_without_post_fix_operator(parser, out_expr);
	if (result != EXPR_PARSE_OK) {
		profile_scope_end();
		return result;
	}

	while (true) {
		Token operator_token = preprocessor_view_next(parser->preprocessor);
		if (operator_token.kind == TOKEN_LEFT_PAREN) {
			// We've got a function call
			
			ExprArray args;
			ExprParseResult result = _parser_parse_arg_list(parser, &args);
			if (result != EXPR_PARSE_OK) {
				profile_scope_end();
				return result;
			}

			Token right_paren = preprocessor_next_token(parser->preprocessor);
			if (right_paren.kind != TOKEN_RIGHT_PAREN) {
				TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };
				diagnostics_report_unexpected_token(parser->diagnostics,
						right_paren,
						expected_tokens,
						array_size(expected_tokens));
			}

			Expr* callable = arena_alloc(parser->ast_allocator, Expr);
			memcpy(callable, out_expr, sizeof(*out_expr));

			out_expr->kind = EXPR_CALL;
			out_expr->call.callable = callable;
			out_expr->call.args = args;
			out_expr->call.right_paren_source_range = source_range_pack(right_paren.source_range);

			if (parser->current_function) {
				parser->current_function->function_call_count += 1;
			}

			_type_check_call(parser, out_expr);
		} else if (operator_token.kind == TOKEN_LEFT_BRACKET) {
			preprocessor_next_token(parser->preprocessor);

			Expr* array = arena_alloc(parser->ast_allocator, Expr);
			memcpy(array, out_expr, sizeof(*out_expr));

			Expr* index = arena_alloc(parser->ast_allocator, Expr);
			ExprParseResult result = _parser_try_parse_expr(parser, index);
			if (result != EXPR_PARSE_OK) {
				profile_scope_end();
				return result;
			}

			out_expr->kind = EXPR_ARRAY_INDEX;
			out_expr->array_index.array = array;
			out_expr->array_index.index = index;

			Token closing_bracket = preprocessor_next_token(parser->preprocessor);

			out_expr->array_index.right_bracket_source_range =
				source_range_pack(closing_bracket.source_range);

			if (closing_bracket.kind != TOKEN_RIGHT_BRACKET) {
				TokenKind expected_tokens[] = { TOKEN_RIGHT_BRACKET };
				diagnostics_report_unexpected_token(parser->diagnostics,
						closing_bracket,
						expected_tokens,
						array_size(expected_tokens));
				
				profile_scope_end();
				return EXPR_PARSE_ERROR;
			}
		} else if (operator_token.kind == TOKEN_DOUBLE_PLUS
				|| operator_token.kind == TOKEN_DOUBLE_MINUS) {

			preprocessor_next_token(parser->preprocessor);

			Expr* operand = arena_alloc(parser->ast_allocator, Expr);
			memcpy(operand, out_expr, sizeof(*out_expr));

			Type operand_type;
			expr_get_type(operand, &operand_type);

			if (!type_kind_is_int(operand_type.kind) && operand_type.kind != TYPE_POINTER) {
				StringBuilder builder = { parser->diagnostics->allocator };
				str_builder_append(&builder, STR_LIT("Cannot apply '"));
				str_builder_append(&builder, operator_token.string);
				str_builder_append(&builder, STR_LIT("' to an operand of type '"));
				type_format(&operand_type, &builder);
				str_builder_append(&builder, STR_LIT("'"));

				report_error(parser->diagnostics,
						expr_get_source_range(operand),
						builder.string,
						NULL);
			}

			ValueKind operand_value_kind = expr_get_value_kind(operand);
			if (operand_value_kind != VALUE_L) {
				report_error(parser->diagnostics,
						expr_get_source_range(operand),
						STR_LIT("Expected an l-value"),
						NULL);
			}

			out_expr->kind = EXPR_UNARY;
			out_expr->unary.op = operator_token.kind == TOKEN_DOUBLE_PLUS
				? UNARY_OP_POST_INCREMENT
				: UNARY_OP_POST_DECREMENT;
			out_expr->unary.operator_source_range = source_range_pack(operator_token.source_range);
			out_expr->unary.operand = operand;
		} else if (operator_token.kind == TOKEN_ARROW) {
			// consume ->
			preprocessor_next_token(parser->preprocessor);

			Token field_name_token = preprocessor_next_token(parser->preprocessor);
			if (field_name_token.kind != TOKEN_IDENT) {
				diagnostics_report_error(parser->diagnostics,
						field_name_token.source_range,
						STR_LIT("Expected field name after '->'"),
						NULL);

				profile_scope_end();
				return EXPR_PARSE_ERROR;
			}

			Type target_type = {};
			expr_get_type(out_expr, &target_type);

			if (target_type.kind != TYPE_POINTER) {
				StringBuilder builder = { .arena = parser->diagnostics->allocator };
				str_builder_append(&builder,
						STR_LIT("'->' can only be applied to a pointer type. Got '"));
				type_format(&target_type, &builder);
				str_builder_append(&builder, STR_LIT("' instead."));

				report_error(parser->diagnostics,
						expr_get_source_range(out_expr),
						builder.string,
						NULL);
				profile_scope_end();
				return EXPR_PARSE_ERROR;
			}

			Type* inner_type = target_type.pointer_base_type;
			const Struct* compound_type = NULL;

			if (inner_type->kind == TYPE_STRUCT) {
				compound_type = inner_type->struct_def;
			} else if (inner_type->kind == TYPE_UNION) {
				compound_type = inner_type->union_def;
			} else {
				StringBuilder builder = { .arena = parser->diagnostics->allocator };
				str_builder_append(&builder,
						STR_LIT("'->' can only be applied to struct or union type. Got '"));
				type_format(inner_type, &builder);
				str_builder_append(&builder, STR_LIT("' instead."));

				report_error(parser->diagnostics,
						expr_get_source_range(out_expr),
						builder.string,
						NULL);
				profile_scope_end();
				return EXPR_PARSE_ERROR;
			}

			if (compound_type->is_forward_declared) {
				report_error(parser->diagnostics,
						expr_get_source_range(out_expr),
						STR_LIT("Expression has an incomplete type"),
						NULL);
				profile_scope_end();
				return EXPR_PARSE_ERROR;
			}

			size_t field_index = struct_field_namespace_index_of(
					compound_type->field_namespace,
					field_name_token.string);

			if (field_index == SIZE_MAX) {
				StringBuilder builder = { .arena = parser->diagnostics->allocator };

				str_builder_append(&builder, STR_LIT("'"));
				type_format(&target_type, &builder);
				str_builder_format(&builder,
						"' has no field named '%.*s'", STR_FMT(field_name_token.string));

				diagnostics_report_error(parser->diagnostics,
						field_name_token.source_range,
						builder.string,
						NULL);
				profile_scope_end();
				return EXPR_PARSE_ERROR;
			}

			Expr* target = arena_alloc(parser->ast_allocator, Expr);
			memcpy(target, out_expr, sizeof(*out_expr));

			out_expr->kind = EXPR_INDIRECT_FIELD_ACCESS;
			out_expr->field_access.target = target;
			out_expr->field_access.field_index = field_index;
			out_expr->field_access.source_range = source_range_merge(
					expr_get_source_range(target),
					source_range_pack(field_name_token.source_range));
		} else if (operator_token.kind == TOKEN_DOT) {
			preprocessor_next_token(parser->preprocessor);

			Token field_name_token = preprocessor_next_token(parser->preprocessor);
			if (field_name_token.kind != TOKEN_IDENT) {
				diagnostics_report_error(parser->diagnostics,
						field_name_token.source_range,
						STR_LIT("Expected field name after '.'"),
						NULL);

				profile_scope_end();
				return EXPR_PARSE_ERROR;
			}

			Type target_type = {};
			expr_get_type(out_expr, &target_type);

			const Struct* compound_type = NULL;
			if (target_type.kind == TYPE_STRUCT) {
				compound_type = target_type.struct_def;
			} else if (target_type.kind == TYPE_UNION) {
				compound_type = target_type.union_def;
			} else {
				StringBuilder builder = { .arena = parser->diagnostics->allocator };
				str_builder_append(&builder,
						STR_LIT("'.' can only be applied to struct or union type. Got '"));
				type_format(&target_type, &builder);
				str_builder_append(&builder, STR_LIT("' instead."));

				report_error(parser->diagnostics,
						expr_get_source_range(out_expr),
						builder.string,
						NULL);
				profile_scope_end();
				return EXPR_PARSE_ERROR;
			}

			size_t field_index = struct_field_namespace_index_of(
					compound_type->field_namespace,
					field_name_token.string);

			if (field_index == SIZE_MAX) {
				StringBuilder builder = { .arena = parser->diagnostics->allocator };

				str_builder_append(&builder, STR_LIT("'"));
				type_format(&target_type, &builder);
				str_builder_format(&builder,
						"' has no field named '%.*s'", STR_FMT(field_name_token.string));

				report_error(parser->diagnostics,
						source_range_pack(field_name_token.source_range),
						builder.string,
						NULL);
				profile_scope_end();
				return EXPR_PARSE_ERROR;
			}

			Expr* target = arena_alloc(parser->ast_allocator, Expr);
			memcpy(target, out_expr, sizeof(*out_expr));

			out_expr->kind = EXPR_DIRECT_FIELD_ACCESS;
			out_expr->field_access.target = target;
			out_expr->field_access.field_index = field_index;
			out_expr->field_access.source_range = source_range_merge(
					expr_get_source_range(target),
					source_range_pack(field_name_token.source_range));
		} else {
			break;
		}
	}

	profile_scope_end();
	return EXPR_PARSE_OK;
}

static void _bin_expr_select_common_type(const Type* left_type,
		const Type* right_type,
		Diagnostics* diagnostics,
		BinOpKind operator,
		PackedSourceRange source_range,
		Type* out_type) {
	out_type->kind = TYPE_VOID;

	if (type_equal(left_type, right_type)) {
		*out_type = *left_type;
		return;
	}

	bool left_is_int_like = type_kind_is_int(left_type->kind) || left_type->kind == TYPE_ENUM;
	bool right_is_int_like = type_kind_is_int(right_type->kind) || right_type->kind == TYPE_ENUM;

	if (left_type->kind == TYPE_POINTER && type_kind_is_int(right_type->kind)) {
		*out_type = *left_type;
		return;
	}

	if (left_type->kind == TYPE_ARRAY && type_kind_is_int(right_type->kind)) {
		type_array_to_pointer(left_type, out_type);
		return;
	}

	if (right_type->kind == TYPE_POINTER && type_kind_is_int(left_type->kind)) {
		*out_type = *right_type;
		return;
	}

	if (right_type->kind == TYPE_ARRAY && type_kind_is_int(left_type->kind)) {
		type_array_to_pointer(right_type, out_type);
		return;
	}

	if (left_type->kind == TYPE_POINTER && right_type->kind == TYPE_POINTER) {
		*out_type = *left_type;
		return;
	}

	if (!left_is_int_like  || !right_is_int_like) {
		StringBuilder builder = { .arena = diagnostics->allocator };
		str_builder_format(&builder,
				"Binary operator '%.*s' is not support between types '",
				STR_FMT(bin_op_kind_to_string(operator)));

		type_format(left_type, &builder);
		str_builder_append(&builder, STR_LIT("' and '"));
		type_format(right_type, &builder);
		str_builder_append(&builder, STR_LIT("'"));

		report_error(diagnostics, source_range, builder.string, NULL);
		return;
	}

	uint32_t left_convertion_rank = type_get_int_convertion_rank(left_type);
	uint32_t right_convertion_rank = type_get_int_convertion_rank(right_type);
	if (left_convertion_rank == right_convertion_rank) {
		if (left_type->kind == TYPE_SIZE_T || right_type->kind == TYPE_SIZE_T) {
			out_type->kind = TYPE_SIZE_T;
		} else if (has_flag(left_type->kind, (TypeKind)TYPE_FLAG_UNSIGNED)) {
			*out_type = *left_type;
		} else if (has_flag(right_type->kind, (TypeKind)TYPE_FLAG_UNSIGNED)) {
			*out_type = *right_type;
		} else {
			*out_type = *left_type;
		}
	} else if (left_convertion_rank > right_convertion_rank) {
		*out_type = *left_type;
	} else  {
		*out_type = *right_type;
	}
}

ExprParseResult _parser_try_parse_expr(Parser* parser, Expr* out_expr) {
	profile_func_colored(PROFILE_COLOR);

	Token left_operand_token = preprocessor_view_next(parser->preprocessor);
	ExprParseResult left_operand_result = _parser_try_parse_bin_expr_operand(parser, out_expr);
	if (left_operand_result != EXPR_PARSE_OK) {
		profile_scope_end();
		return left_operand_result;
	}

	Expr* current_expr = out_expr;

	while (true) {
		Token op_token = preprocessor_view_next(parser->preprocessor);

		BinOpKind current_bin_op;
		if (_token_kind_to_bin_op(op_token.kind, &current_bin_op)) {
			preprocessor_next_token(parser->preprocessor);
				
			Expr* right_operand = arena_alloc(parser->ast_allocator, Expr);

			Token first_operand_token = preprocessor_view_next(parser->preprocessor);
			if (_parser_try_parse_bin_expr_operand(parser, right_operand) != EXPR_PARSE_OK) {
				diagnostics_report_error(parser->diagnostics,
						first_operand_token.source_range,
						STR_LIT("Expected binary expression operand"),
						NULL);

				profile_scope_end();
				return EXPR_PARSE_ERROR;
			}

			Expr* left_operand = arena_alloc(parser->ast_allocator, Expr);

			uint32_t current_op_precedence = bin_op_precedence(current_bin_op);
			uint32_t next_op_precedence = UINT32_MAX;

			{
				Token maybe_next_bin_op = preprocessor_view_next(parser->preprocessor);
				BinOpKind next_bin_op;
				if (_token_kind_to_bin_op(maybe_next_bin_op.kind, &next_bin_op)) {
					next_op_precedence = bin_op_precedence(next_bin_op);
				}
			}

			*left_operand = *current_expr;

			Type left_type;
			Type right_type;
			expr_get_type(left_operand, &left_type);
			expr_get_type(right_operand, &right_type);

			ValueKind left_value_kind = expr_get_value_kind(left_operand);
			ValueKind right_value_kind = expr_get_value_kind(right_operand);

			if (bin_op_is_assignment(current_bin_op)) {
				if (left_value_kind != VALUE_L) {
					diagnostics_report_error(parser->diagnostics,
							left_operand_token.source_range,
							STR_LIT("Expected an l-value"),
							NULL);
				}
			}

			*current_expr = (Expr) {
				.kind = EXPR_BINARY,
				.binary = (BinExpr) {
					.op = current_bin_op,
					.left = left_operand,
					.right = right_operand,
				}
			};

			Type common_type;
			_bin_expr_select_common_type(&left_type,
					&right_type,
					parser->diagnostics,
					current_bin_op,
					expr_get_source_range(current_expr),
					&common_type);

			current_expr->binary.common_type_kind = common_type.kind;
			current_expr->binary.pointer_base_type = common_type.pointer_base_type;

			if (current_op_precedence > next_op_precedence) {
				current_expr = right_operand;
			}
		} else {
			break;
		}
	}

	profile_scope_end();
	return EXPR_PARSE_OK;
}

bool _parser_parse_pre_declaration_modifiers(Parser* parser,
		Type* base_type,
		Type* out_type,
		bool duplicate_base_type) {
	if (base_type == out_type) {
		assert(duplicate_base_type);
	}

	while (true) {
		Token maybe_asterisk = preprocessor_view_next(parser->preprocessor);
		if (maybe_asterisk.kind == TOKEN_ASTERISK) {
			preprocessor_next_token(parser->preprocessor);

			TypeQualifiers qualifiers = _parser_parse_type_qualifiers(parser);

			assert(duplicate_base_type);

			Type* inner_type = arena_alloc(parser->ast_allocator, Type);
			*inner_type = *base_type;

			*out_type = (Type) {
				.kind = TYPE_POINTER,
				.pointer_base_type = inner_type,
				.qualifiers = qualifiers,
			};
		} else {
			TypeQualifiers qualifiers = _parser_parse_type_qualifiers(parser);
			out_type->qualifiers |= qualifiers;
			break;
		}
	}

	return true;
}

bool _check_for_var_redefinition(Parser* parser, String var_name, PackedSourceRange name_source_range) {
	profile_func_colored(PROFILE_COLOR);
	IdentifierEntry* existing_identifier = ident_storage_find(parser->ident_storage,
			IDENT_NAMESPACE_DEFAULT,
			IDENT_FIND_IN_CURRENT_SCOPE,
			var_name);

	if (existing_identifier != NULL) {
		StringBuilder builder = { .arena = parser->diagnostics->allocator };
		str_builder_append(&builder, STR_LIT("Redefinition of '"));
		str_builder_append(&builder, var_name);
		str_builder_append_char(&builder, '\'');

		DiagnosticsEntry* error = report_error(parser->diagnostics,
				name_source_range,
				builder.string,
				NULL);

		report_error(parser->diagnostics,
				existing_identifier->name_source_range,
				STR_LIT("Previously defined here"),
				error);
		profile_scope_end();
		return false;
	} 

	profile_scope_end();
	return true;
}

static void _parser_register_function_param_identifiers(Parser* parser, Function* function_def) {
	profile_func_colored(PROFILE_COLOR);
	assert(!function_def->is_forward_declared);

	FunctionPrototype* proto = &function_def->proto;
	for (size_t i = 0; i < proto->parameter_count; i += 1) {
		const FunctionParam* param = &proto->parameters[i];
		if (param->name.length == 0) {
			continue;
		}

		IdentifierEntry* entry = ident_storage_find(parser->ident_storage,
				IDENT_NAMESPACE_DEFAULT,
				IDENT_FIND_DEFAULT,
				param->name);

		if (entry) {
			StringBuilder builder = { .arena = parser->diagnostics->allocator };
			str_builder_append(&builder, STR_LIT("Name \'"));
			str_builder_append(&builder, entry->name);
			str_builder_append(&builder, STR_LIT("' is already defined"));

			DiagnosticsEntry* error = report_error(parser->diagnostics,
					param->name_source_range,
					builder.string,
					NULL);

			report_error(parser->diagnostics,
					entry->name_source_range,
					STR_LIT("Previously defined here"),
					error);
		} else {
			entry = ident_storage_insert(parser->ident_storage,
					IDENT_NAMESPACE_DEFAULT,
					IDENT_FUNCTION_PARAM,
					param->name,
					param->name_source_range);

			entry->function_param.function_def = function_def;
			entry->function_param.param_index = i;
		}
	}

	profile_scope_end();
}

static AstNode* _parser_parse_function_declaration(Parser* parser,
		DeclSpec* decl_spec,
		StorageSpecifier storage_specifier,
		Declarator* declarator) {
	profile_func_colored(PROFILE_COLOR);

	assert(declarator->type.kind == TYPE_FUNCTION);

	const FunctionPrototype* prototype = declarator->type.function;

	FunctionParam* params = prototype->parameters;
	size_t param_count = prototype->parameter_count;
	bool has_va_args = prototype->has_va_args;

	// Register the declaration
	Function* function_def = NULL;
	IdentifierEntry* entry = ident_storage_find(parser->ident_storage,
			IDENT_NAMESPACE_DEFAULT,
			IDENT_FIND_DEFAULT,
			declarator->name);

	if (entry) {
		if (!has_flag(entry->kind, IDENT_FUNCTION)) {
			StringBuilder builder = { .arena = parser->diagnostics->allocator };
			str_builder_append_char(&builder, '\'');
			str_builder_append(&builder, entry->name);
			str_builder_append(&builder, STR_LIT("' is previously defined with a different tag type"));

			DiagnosticsEntry* error = report_error(parser->diagnostics,
					declarator->name_source_range,
					builder.string,
					NULL);

			report_error(parser->diagnostics,
					entry->name_source_range,
					STR_LIT("Previously defined here"),
					error);
			profile_scope_end();
			return NULL;
		}

		function_def = entry->function_def;
		assert(function_def);

		// TODO: Verify that return types also match
		if (function_def->proto.parameter_count != param_count || function_def->proto.has_va_args != has_va_args) {
			DiagnosticsEntry* error = report_error(parser->diagnostics,
					declarator->name_source_range,
					STR_LIT("Function was previously defined with a different parameter count"),
					NULL);

			report_error(parser->diagnostics,
					entry->name_source_range,
					STR_LIT("Previously defined here"),
					error);
			profile_scope_end();
			return NULL;
		} else {
			FunctionParam* prev_def_param = function_def->proto.parameters;
			FunctionParam* new_def_param = params;

			for (size_t i = 0; i < param_count; i += 1) {
				bool param_types_are_equal = type_equal(&prev_def_param->type, &new_def_param->type);

				if (!param_types_are_equal) {
					DiagnosticsEntry* error = report_error(parser->diagnostics,
						new_def_param->name_source_range,
						STR_LIT("Function previously defined with different parameter types"),
						NULL);

					report_error(parser->diagnostics,
							prev_def_param->name_source_range,
							STR_LIT("Previously defined here"),
							error);
					profile_scope_end();
					return NULL;
				}
				
				prev_def_param = prev_def_param + 1;
				new_def_param = new_def_param + 1;
			}
		}
	} else {
		entry = ident_storage_insert(parser->ident_storage,
				IDENT_NAMESPACE_DEFAULT,
				IDENT_FUNCTION,
				declarator->name,
				declarator->name_source_range);

		function_def = arena_alloc_zeroed(parser->ast_allocator, Function); 
		
		function_def->proto.name = declarator->name;
		function_def->proto.return_type = prototype->return_type;
		function_def->proto.parameters = params;
		function_def->proto.parameter_count = param_count;
		function_def->is_forward_declared = true;
		function_def->decl_spec = decl_spec;
		function_def->storage_specifier = storage_specifier;
		function_def->var_count = 0;
		function_def->proto.has_va_args = has_va_args;
		function_def->type = (Type) {
			.kind = TYPE_FUNCTION,
			.function = &function_def->proto,
		};

		entry->function_def = function_def;
	}

	// Check whether the function has a body
	bool has_body = false;
	Token left_brace_or_semicolon = preprocessor_view_next(parser->preprocessor);
	switch (left_brace_or_semicolon.kind) {
	case TOKEN_LEFT_BRACE:
		has_body = true;
		break;
	case TOKEN_SEMICOLON:
		has_body = false;
		preprocessor_next_token(parser->preprocessor);
		break;
	default: {
		TokenKind expected_tokens[] = { TOKEN_LEFT_BRACE, TOKEN_SEMICOLON };
		diagnostics_report_unexpected_token(parser->diagnostics,
				left_brace_or_semicolon,
				expected_tokens,
				array_size(expected_tokens));
		profile_scope_end();
		return NULL;
	}
	}

	// Check for redefinition
	if (!function_def->is_forward_declared && has_body) {
		StringBuilder builder = { .arena = parser->diagnostics->allocator };
		str_builder_append(&builder, STR_LIT("Redefinition of '"));
		str_builder_append(&builder, entry->name);
		str_builder_append_char(&builder, '\'');

		DiagnosticsEntry* error = report_error(parser->diagnostics,
				declarator->name_source_range,
				builder.string,
				NULL);

		report_error(parser->diagnostics,
				entry->name_source_range,
				STR_LIT("Previously defined here"),
				error);
		profile_scope_end();
		return NULL;
	}

	// Parse the body
	if (has_body) {
		assert(function_def->is_forward_declared);
		function_def->is_forward_declared = false;

		Scope* body = arena_alloc(parser->ast_allocator, Scope);
		memset(body, 0, sizeof(*body));

		ident_storage_begin_scope(parser->ident_storage);
		_parser_register_function_param_identifiers(parser, function_def);

		uint32_t last_var_id_state = parser->next_var_id;

		assert(parser->current_function == NULL);
		parser->current_function = function_def;

		bool result = _parser_parse_scope(parser, body);

		parser->current_function = NULL;

		uint32_t var_count = parser->next_var_id - last_var_id_state;
		parser->next_var_id = last_var_id_state;

		ident_storage_end_scope(parser->ident_storage);

		if (!result) {
			profile_scope_end();
			return NULL;
		}

		function_def->body = body;
		function_def->var_count = var_count;
	}

	AstNode* node = arena_alloc_zeroed(parser->ast_allocator, AstNode);
	node->kind = function_def->is_forward_declared ? AST_NODE_FUNCTION_DECL : AST_NODE_FUNCTION_DEF;
	node->function_def = function_def;

	if (node->kind == AST_NODE_FUNCTION_DEF) {
		assert(node->function_def->body);
		node->function_def->id = parser->ast->stats.function_def_count;
		parser->ast->stats.function_def_count += 1;
	}

	profile_scope_end();
	return node;
}

static AstNode* _parser_parse_variable_declaration(Parser* parser,
		Declarator* declarator,
		DeclSpec* decl_spec,
		StorageSpecifier storage_specifier) {
	profile_func_colored(PROFILE_COLOR);

	Token token = preprocessor_view_next(parser->preprocessor);
	if (token.kind == TOKEN_EQUAL) {
		preprocessor_next_token(parser->preprocessor);

		assert(decl_spec == NULL);

		Expr* value = arena_alloc(parser->ast_allocator, Expr);
		ExprParseResult expr_result;
		if (preprocessor_view_next(parser->preprocessor).kind == TOKEN_LEFT_BRACE) {
			Type* prefered_type = arena_alloc(parser->ast_allocator, Type);
			*prefered_type = declarator->type;

			expr_result = _parser_try_parse_compound_literal(parser, prefered_type, value);
		} else {
			expr_result = _parser_try_parse_expr(parser, value);
		}

		switch (expr_result) {
		case EXPR_PARSE_OK:
			break;
		case EXPR_PARSE_ERROR:
			profile_scope_end();
			return NULL;
		case EXPR_PARSE_NOT_PARSED:
			diagnostics_report_error(parser->diagnostics,
					token.source_range,
					STR_LIT("Expected variable value after the '='"),
					NULL);
			profile_scope_end();
			return NULL;
		}

		if (!_parser_expect_semicolon(parser, STR_LIT("Expected semicolon after the variable definition"))) {
			profile_scope_end();
			return NULL;
		}

		if (!_check_for_var_redefinition(parser, declarator->name, declarator->name_source_range)) {
			profile_scope_end();
			return NULL;
		}

		Type value_type;
		expr_get_type(value, &value_type);
		_check_is_convertable(parser, &value_type, &declarator->type, expr_get_source_range(value));

		AstNode* node = arena_alloc_zeroed(parser->ast_allocator, AstNode);
		node->kind = AST_NODE_VARIABLE;
		node->variable.name = declarator->name;
		node->variable.type = declarator->type;
		node->variable.value = value;
		node->variable.storage_specifier = storage_specifier;
		node->variable.id = parser->next_var_id;

		parser->next_var_id += 1;

		IdentifierEntry* entry = ident_storage_insert(parser->ident_storage,
				IDENT_NAMESPACE_DEFAULT,
				IDENT_VARIABLE,
				declarator->name,
				declarator->name_source_range);

		entry->variable = &node->variable;
		profile_scope_end();
		return node;
	} else if (token.kind == TOKEN_SEMICOLON) {
		preprocessor_next_token(parser->preprocessor);

		assert(decl_spec == NULL);

		if (!_check_for_var_redefinition(parser,
					declarator->name,
					declarator->name_source_range)) {
			profile_scope_end();
			return NULL;
		}

		// A variable declaration
		AstNode* node = arena_alloc_zeroed(parser->ast_allocator, AstNode);
		node->kind = AST_NODE_VARIABLE;
		node->variable = (Variable) {
			.name = declarator->name,
			.type = declarator->type,
			.value = NULL,
			.storage_specifier = storage_specifier,
			.id = parser->next_var_id,
		};

		parser->next_var_id += 1;

		IdentifierEntry* entry = ident_storage_insert(parser->ident_storage,
				IDENT_NAMESPACE_DEFAULT,
				IDENT_VARIABLE,
				declarator->name,
				declarator->name_source_range);

		entry->variable = &node->variable;
		profile_scope_end();
		return node;
	} else {
		TokenKind expected_tokens[] = {
			TOKEN_LEFT_PAREN,
			TOKEN_EQUAL,
			TOKEN_SEMICOLON,
			TOKEN_LEFT_BRACKET
		};

		diagnostics_report_unexpected_token(parser->diagnostics,
				token,
				expected_tokens,
				array_size(expected_tokens));
		profile_scope_end();
		return NULL;
	}

	unreachable();
	profile_scope_end();
	return NULL;
}

//
// Declarator parsing
// 

static bool _parser_try_parse_calling_convention(Parser* parser, FunctionCallingConvention* out) {
	Token token = preprocessor_view_next(parser->preprocessor);

	FunctionCallingConvention call_conv = FUNC_CALL_CONV_CDECL;
	if (str_equal(token.string, STR_LIT("__cdecl"))) {
		preprocessor_next_token(parser->preprocessor);
		call_conv = FUNC_CALL_CONV_CDECL;
		return true;
	}

	return false;
}

static Type* _find_declarator_inner_type(Type* type, Type* inner_type) {
	while (true) {
		if (type->kind == TYPE_POINTER) {
			if (type_equal(type->pointer_base_type, inner_type)) {
				break;
			}

			type = type->pointer_base_type;
		} else if (type->kind == TYPE_ARRAY) {
			if (type_equal(type->array.element_type, inner_type)) {
				break;
			}

			type = type->array.element_type;
		} else {
			break;
		}
	}

	return type;
}

// Parses a direct declarator or an abstract direct declarator.
//
// An abstract direct declarator is pretty much the same as the direct declarator, but without the
// identifier.
static bool _parser_parse_direct_declarator(Parser* parser,
		Declarator* out_declarator,
		bool is_abstract) {
	bool result = true;
	Token token = preprocessor_view_next(parser->preprocessor);

	bool has_inner_declarator = false;
	Type inner_declarator_type;

	FunctionCallingConvention call_conv = FUNC_CALL_CONV_CDECL;
	bool has_call_conv = false;

	if (_parser_try_parse_calling_convention(parser, &call_conv)) {
		has_call_conv = true;
		token = preprocessor_view_next(parser->preprocessor);
	}

	// TODO: if (has_call_conv) make sure the next declarator is a function

	if (token.kind == TOKEN_IDENT) {
		preprocessor_next_token(parser->preprocessor);

		if (is_abstract) {
			report_error(parser->diagnostics,
					source_range_pack(token.source_range),
					STR_LIT("An indentifier is not allowed in an abstract declarator"),
					NULL);
		} else {
			out_declarator->name = token.string;
			out_declarator->name_source_range = source_range_pack(token.source_range);
		}
	} else if (token.kind == TOKEN_LEFT_PAREN) {
		preprocessor_next_token(parser->preprocessor);

		if (_parser_try_parse_calling_convention(parser, &call_conv)) {
			has_call_conv = true;
		}

		has_inner_declarator = result;
		inner_declarator_type = out_declarator->type;

		result = _parser_parse_declarator(parser, &out_declarator->type, out_declarator, false);
		if (!result) {
			_parser_skip_until(parser, TOKEN_RIGHT_PAREN, TOKEN_SEMICOLON);
		}

		Token right_paren = preprocessor_next_token(parser->preprocessor);
		if (right_paren.kind != TOKEN_RIGHT_PAREN) {
			TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };
			diagnostics_report_unexpected_token(parser->diagnostics,
					right_paren,
					expected_tokens,
					array_size(expected_tokens));
			result = false;
		}
	}

	token = preprocessor_view_next(parser->preprocessor);
	if (token.kind == TOKEN_LEFT_PAREN) {
		FunctionParam* params = NULL;
		size_t param_count = 0;
		bool has_va_args = false;

		if (!_parser_parse_function_params(parser, &params, &param_count, &has_va_args)) {
			_parser_skip_until(parser, TOKEN_RIGHT_PAREN, TOKEN_EOF);
			result = false;
		}

		// FIXME: `_parser_parse_function_params` already consumes `TOKEN_RIGHT_PAREN`
#if 0
		Token right_paren = preprocessor_next_token(parser->preprocessor);
		if (right_paren.kind != TOKEN_RIGHT_PAREN) {
			TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };
			diagnostics_report_unexpected_token(parser->diagnostics,
					right_paren,
					expected_tokens,
					array_size(expected_tokens));
			result = false;
		}
#endif

		FunctionPrototype* prototype = arena_alloc_zeroed(parser->ast_allocator, FunctionPrototype);
		prototype->return_type = has_inner_declarator
			? inner_declarator_type
			: out_declarator->type;

		if (has_call_conv) {
			prototype->calling_convention = call_conv;
		}
		prototype->name = out_declarator->name;
		prototype->has_va_args = has_va_args;
		prototype->parameter_count = param_count;
		prototype->parameters = params;

		Type* inner_type = &out_declarator->type;
		if (has_inner_declarator) {
			Type* t = _find_declarator_inner_type(&out_declarator->type, &inner_declarator_type);
			if (t->kind == TYPE_POINTER) {
				inner_type = arena_alloc_zeroed(parser->ast_allocator, Type);
				t->pointer_base_type = inner_type;
			} else if (t->kind == TYPE_ARRAY) {
				inner_type = arena_alloc_zeroed(parser->ast_allocator, Type);
				t->array.element_type = inner_type;
			}
		}

		assert(inner_type != NULL);

		*inner_type = (Type) {
			.kind = TYPE_FUNCTION,
			.function = prototype,
		};
	} else if (token.kind == TOKEN_LEFT_BRACKET) {
		preprocessor_next_token(parser->preprocessor);

		Token next_token = preprocessor_view_next(parser->preprocessor);
		bool has_size_expr = next_token.kind != TOKEN_RIGHT_BRACKET;

		Expr* size_expr = NULL;
		if (has_size_expr) {
			size_expr = arena_alloc(parser->ast_allocator, Expr);

			if (_parser_try_parse_expr(parser, size_expr) != EXPR_PARSE_OK) {
				diagnostics_report_error(parser->diagnostics,
						next_token.source_range,
						STR_LIT("Expected array size experession"),
						NULL);
				return false;
			}
		}

		Token closing_bracket = preprocessor_next_token(parser->preprocessor);
		if (closing_bracket.kind != TOKEN_RIGHT_BRACKET) {
			TokenKind expected_tokens[] = { TOKEN_RIGHT_BRACKET };
			diagnostics_report_unexpected_token(parser->diagnostics,
					closing_bracket,
					expected_tokens,
					array_size(expected_tokens));
			return false;
		}

		Type* element_type = arena_alloc(parser->ast_allocator, Type);
		*element_type = out_declarator->type;

		out_declarator->type = (Type) {
			.kind = TYPE_ARRAY,
			.array = {
				.element_type = element_type,
				.size = size_expr,
			}
		};
	}

	return result;
}

static bool _parser_parse_declarator(Parser* parser,
		Type* type,
		Declarator* out_declarator,
		bool is_abstract) {
	profile_func_colored(PROFILE_COLOR);

	out_declarator->name = (String) {};
	out_declarator->type = *type;

	while (true) {
		Token token = preprocessor_view_next(parser->preprocessor);
		if (token.kind == TOKEN_ASTERISK) {
			preprocessor_next_token(parser->preprocessor);

			TypeQualifiers qualifiers = _parser_parse_type_qualifiers(parser);

			Type* pointer_base_type = arena_alloc_zeroed(parser->ast_allocator, Type);
			*pointer_base_type = out_declarator->type;

			out_declarator->type = (Type) {
				.kind = TYPE_POINTER,
				.qualifiers = qualifiers,
				.pointer_base_type = pointer_base_type,
			};
		} else {
			break;
		}
	}
	
	bool result = _parser_parse_direct_declarator(parser, out_declarator, is_abstract);

	profile_scope_end();
	return result;
}

static DeclSpec* _parser_parse_decl_spec(Parser* parser) {
	Token decl_spec_token = preprocessor_view_next(parser->preprocessor);
	if (decl_spec_token.kind != TOKEN_DECLSPEC) {
		return NULL;
	}

	preprocessor_next_token(parser->preprocessor);

	Token left_paren = preprocessor_next_token(parser->preprocessor);
	if (left_paren.kind != TOKEN_LEFT_PAREN) {
		TokenKind expected_tokens[] = { TOKEN_LEFT_PAREN };
		diagnostics_report_unexpected_token(parser->diagnostics,
				left_paren,
				expected_tokens,
				array_size(expected_tokens));
		return NULL;
	}

	Token token = preprocessor_next_token(parser->preprocessor);

	DeclSpecKind kind = -1;
	if (str_equal(token.string, STR_LIT("deprecated"))) {
		kind = DECL_SPEC_DEPRECATED;
	} else if (str_equal(token.string, STR_LIT("noinline"))) {
		kind = DECL_SPEC_NO_INLINE;
	} else if (str_equal(token.string, STR_LIT("noreturn"))) {
		kind = DECL_SPEC_NO_RETURN;
	} else if (str_equal(token.string, STR_LIT("dllimport"))) {
		kind = DECL_SPEC_DLL_IMPORT;
	} else if (str_equal(token.string, STR_LIT("dllexport"))) {
		kind = DECL_SPEC_DLL_EXPORT;
	} else if (str_equal(token.string, STR_LIT("restrict"))) {
		kind = DECL_SPEC_RESTRICT;
	}

	if (kind == -1) {
		diagnostics_report_error(parser->diagnostics,
				token.source_range,
				STR_LIT("Unsupported __declspec modifier"),
				NULL);
		return NULL;
	}

	StringLiteral deprecation_text;

	if (kind == DECL_SPEC_DEPRECATED) {
		Token left_paren = preprocessor_next_token(parser->preprocessor);
		if (left_paren.kind != TOKEN_LEFT_PAREN) {
			TokenKind expected_tokens[] = { TOKEN_LEFT_PAREN };
			diagnostics_report_unexpected_token(parser->diagnostics,
					left_paren,
					expected_tokens,
					array_size(expected_tokens));
			return NULL;
		}

		_parser_parse_string_literal(parser, &deprecation_text);

		Token right_paren = preprocessor_next_token(parser->preprocessor);
		if (right_paren.kind != TOKEN_RIGHT_PAREN) {
			TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };
			diagnostics_report_unexpected_token(parser->diagnostics,
					right_paren,
					expected_tokens,
					array_size(expected_tokens));
			return NULL;
		}
	}

	Token right_paren = preprocessor_next_token(parser->preprocessor);
	if (right_paren.kind != TOKEN_RIGHT_PAREN) {
		TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };
		diagnostics_report_unexpected_token(parser->diagnostics,
				right_paren,
				expected_tokens,
				array_size(expected_tokens));
		return NULL;
	}

	DeclSpec* decl_spec = arena_alloc(parser->ast_allocator, DeclSpec);
	decl_spec->kind = kind;

	if (kind == DECL_SPEC_DEPRECATED) {
		decl_spec->deprecation_text = deprecation_text;
	}

	return decl_spec;
}

AstNode* _parser_parse_declaration_or_expr(Parser* parser) {
	bool has_inline = false;

	{
		Token maybe_inline = preprocessor_view_next(parser->preprocessor);
		if (maybe_inline.kind == TOKEN_KEYWORD_INLINE) {
			preprocessor_next_token(parser->preprocessor);
			has_inline = true;
		} else if (maybe_inline.kind == TOKEN_IDENT) {
			if (str_equal(maybe_inline.string, STR_LIT("__inline"))) {
				preprocessor_next_token(parser->preprocessor);
				has_inline = true;
			} else if (str_equal(maybe_inline.string, STR_LIT("__forceinline"))) {
				preprocessor_next_token(parser->preprocessor);
				has_inline = true;
			}
		}
	}

	DeclSpec* decl_spec = _parser_parse_decl_spec(parser);
	StorageSpecifier storage_specifier = STORAGE_SPEC_NONE;

	{
		Token maybe_storage_specifier = preprocessor_view_next(parser->preprocessor);
		if (maybe_storage_specifier.kind == TOKEN_KEYWORD_STATIC) {
			preprocessor_next_token(parser->preprocessor);
			storage_specifier = STORAGE_SPEC_STATIC;
		} else if (maybe_storage_specifier.kind == TOKEN_KEYWORD_EXTERN) {
			preprocessor_next_token(parser->preprocessor);
			storage_specifier = STORAGE_SPEC_EXTERNAL;
		}
	}

	bool has_type = false;
	Type type = {};

	switch (_parser_try_parse_type(parser, &type, true)) {
	case PARSE_TYPE_PARSED:
		has_type = true;
		break;
	case PARSE_TYPE_NOT_PARSED:
		has_type = false;
		break;
	case PARSE_TYPE_ERROR:
		return NULL;
	}

	if (has_type) {
		Declarator declarator = {};
		if (!_parser_parse_declarator(parser, &type, &declarator, false)) {
			return NULL;
		}

		if (declarator.name.length == 0) {
			return NULL;
		}

		if (declarator.type.kind == TYPE_FUNCTION) {
			AstNode* node = _parser_parse_function_declaration(parser,
					decl_spec,
					storage_specifier,
					&declarator);

			if (has_inline) {
				assert(node->kind == AST_NODE_FUNCTION_DEF || node->kind == AST_NODE_FUNCTION_DECL);
				node->function_def->is_inline = true;
			}

			return node;
		} else {
			AstNode* node = _parser_parse_variable_declaration(parser,
					&declarator,
					decl_spec,
					storage_specifier);

			return node;
		}
	} else {
		if (decl_spec) {
			debug_log_info("__declspec ignore before expression");
		}

		if (storage_specifier != STORAGE_SPEC_NONE) {
			debug_log_info("storage specifier skipped before expression");
		}

		Expr expr;
		ExprParseResult result = _parser_try_parse_expr(parser, &expr);
		if (result == EXPR_PARSE_OK) {
			if (!_parser_expect_semicolon(parser, STR_LIT("Expected ';' after an expression"))) {
				return NULL;
			}

			AstNode* node = arena_alloc_zeroed(parser->ast_allocator, AstNode);
			node->kind = AST_NODE_EXPR;
			node->expr = expr;
			return node;
		} else {
			return NULL;
		}
	}

	unreachable();
	return NULL;
}

static AstNode* _parser_parse_if_stmt(Parser* parser) {
	profile_func_colored(PROFILE_COLOR);

	Token if_token = preprocessor_next_token(parser->preprocessor);
	assert(if_token.kind == TOKEN_KEYWORD_IF);

	Token left_paren = preprocessor_next_token(parser->preprocessor);
	if (left_paren.kind != TOKEN_LEFT_PAREN) {
		TokenKind expected_tokens[] = { TOKEN_LEFT_PAREN };
		diagnostics_report_unexpected_token(parser->diagnostics,
				left_paren,
				expected_tokens,
				array_size(expected_tokens));
		profile_scope_end();
		return NULL;
	}

	Expr condition = {};
	if (_parser_try_parse_expr(parser, &condition) != EXPR_PARSE_OK) {
		profile_scope_end();
		return NULL;
	}
	
	Token right_paren = preprocessor_next_token(parser->preprocessor);
	if (right_paren.kind != TOKEN_RIGHT_PAREN) {
		TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };
		diagnostics_report_unexpected_token(parser->diagnostics,
				right_paren,
				expected_tokens,
				array_size(expected_tokens));
		profile_scope_end();
		return NULL;
	}

	AstNode* node = arena_alloc_zeroed(parser->ast_allocator, AstNode);
	node->kind = AST_NODE_IF;
	node->if_stmt.condition = condition;

	Token true_node_token = preprocessor_view_next(parser->preprocessor);

	{
		ident_storage_begin_scope(parser->ident_storage);

		node->if_stmt.true_scope= arena_alloc_zeroed(parser->ast_allocator, Scope);
		node->if_stmt.true_scope->id = parser->ident_storage->current_scope->id;

		AstNode* true_node = _parser_parse_single_node(parser);
		if (true_node) {
			scope_append(node->if_stmt.true_scope, true_node);
		} else {
			diagnostics_report_error(parser->diagnostics,
					true_node_token.source_range,
					STR_LIT("Expected a statement after if condition"),
					NULL);
		}

		ident_storage_end_scope(parser->ident_storage);
	}

	Token maybe_else = preprocessor_view_next(parser->preprocessor);
	if (maybe_else.kind == TOKEN_KEYWORD_ELSE) {
		preprocessor_next_token(parser->preprocessor);

		Token false_node_token = preprocessor_view_next(parser->preprocessor);

		{
			ident_storage_begin_scope(parser->ident_storage);

			node->if_stmt.false_scope = arena_alloc_zeroed(parser->ast_allocator, Scope);
			node->if_stmt.false_scope->id = parser->ident_storage->current_scope->id;

			AstNode* false_node = _parser_parse_single_node(parser);
			if (false_node) {
				scope_append(node->if_stmt.false_scope, false_node);
			} else {
				diagnostics_report_error(parser->diagnostics,
						false_node_token.source_range,
						STR_LIT("Expected a statement after else"),
						NULL);
			}

			ident_storage_end_scope(parser->ident_storage);
		}
	}

	profile_scope_end();
	return node;
}

typedef struct {
	AstNode* node;
	Scope* scope;
} LoopBody;

static LoopBody _parser_parse_loop_body(Parser* parser, AstNode* node) {
	assert(node->kind == AST_NODE_FOR_LOOP || node->kind == AST_NODE_WHILE_LOOP);

	ParserLoopOrSwitchState current_loop = {
		.node = node,
		.parent = parser->loop_or_switch_state
	};
	parser->loop_or_switch_state = &current_loop;
	
	AstNode* body = NULL;
	Scope* scope = arena_alloc_zeroed(parser->ast_allocator, Scope);

	Token body_token = preprocessor_view_next(parser->preprocessor);

	ident_storage_begin_scope(parser->ident_storage);
	scope->id = parser->ident_storage->current_scope->id;

	if (body_token.kind != TOKEN_SEMICOLON) {
		body = _parser_parse_single_node(parser);
		if (body) {
			scope_append(scope, body);
		}
	}

	ident_storage_end_scope(parser->ident_storage);

	parser->loop_or_switch_state = parser->loop_or_switch_state->parent;

	LoopBody result = {};
	result.node = body;
	result.scope = scope;
	return result;
}

static AstNode* _parser_parse_while_loop(Parser* parser) {
	Token while_token = preprocessor_next_token(parser->preprocessor);
	assert(while_token.kind == TOKEN_KEYWORD_WHILE);

	Token left_paren = preprocessor_next_token(parser->preprocessor);
	if (left_paren.kind != TOKEN_LEFT_PAREN) {
		TokenKind expected_tokens[] = { TOKEN_LEFT_PAREN };
		diagnostics_report_unexpected_token(parser->diagnostics,
				left_paren,
				expected_tokens,
				array_size(expected_tokens));
		return NULL;
	}

	AstNode* loop = arena_alloc_zeroed(parser->ast_allocator, AstNode);
	loop->kind = AST_NODE_WHILE_LOOP;
	loop->while_loop.condition_kind = WHILE_LOOP_PRE_CONDITION;

	{
		ident_storage_begin_scope(parser->ident_storage);

		if (_parser_try_parse_expr(parser, &loop->while_loop.condition) != EXPR_PARSE_OK) {
			return NULL;
		}
		
		Token right_paren = preprocessor_next_token(parser->preprocessor);
		if (right_paren.kind != TOKEN_RIGHT_PAREN) {
			TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };
			diagnostics_report_unexpected_token(parser->diagnostics,
					right_paren,
					expected_tokens,
					array_size(expected_tokens));
			return NULL;
		}

		LoopBody body = _parser_parse_loop_body(parser, loop);
		loop->while_loop.body_scope = body.scope;

		ident_storage_end_scope(parser->ident_storage);
	}

	return loop;
}

static AstNode* _parser_parse_do_while_loop(Parser* parser) {
	Token do_token = preprocessor_next_token(parser->preprocessor);
	assert(do_token.kind == TOKEN_KEYWORD_DO);;

	// Parse body
	AstNode* loop = arena_alloc_zeroed(parser->ast_allocator, AstNode);
	loop->kind = AST_NODE_WHILE_LOOP;
	loop->while_loop.condition_kind = WHILE_LOOP_POST_CONDITION;

	{
		ident_storage_begin_scope(parser->ident_storage);

		Token body_token = preprocessor_view_next(parser->preprocessor);

		LoopBody body = _parser_parse_loop_body(parser, loop);
		loop->while_loop.body_scope = body.scope;

		if (body.node == NULL) {
			diagnostics_report_error(parser->diagnostics,
					body_token.source_range,
					STR_LIT("Expected a do-while loop body"),
					NULL);
			return NULL;
		}

		// Parse while part
		Token while_token = preprocessor_next_token(parser->preprocessor);
		if (while_token.kind != TOKEN_KEYWORD_WHILE) {
			TokenKind expected_tokens[] = { TOKEN_KEYWORD_WHILE };
			diagnostics_report_unexpected_token(parser->diagnostics,
					while_token,
					expected_tokens,
					array_size(expected_tokens));
			return NULL;
		}

		// Parse condition
		Token left_paren = preprocessor_next_token(parser->preprocessor);
		if (left_paren.kind != TOKEN_LEFT_PAREN) {
			TokenKind expected_tokens[] = { TOKEN_LEFT_PAREN };
			diagnostics_report_unexpected_token(parser->diagnostics,
					left_paren,
					expected_tokens,
					array_size(expected_tokens));
			return NULL;
		}

		if (_parser_try_parse_expr(parser, &loop->while_loop.condition) != EXPR_PARSE_OK) {
			return NULL;
		}
		
		Token right_paren = preprocessor_next_token(parser->preprocessor);
		if (right_paren.kind != TOKEN_RIGHT_PAREN) {
			TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };
			diagnostics_report_unexpected_token(parser->diagnostics,
					right_paren,
					expected_tokens,
					array_size(expected_tokens));
			return NULL;
		}

		ident_storage_end_scope(parser->ident_storage);
	}
	return loop;
}

static AstNode* _parser_parse_for_loop(Parser* parser) {
	profile_func_colored(PROFILE_COLOR);

	Token for_token = preprocessor_next_token(parser->preprocessor);
	assert(for_token.kind == TOKEN_KEYWORD_FOR);

	Token left_paren = preprocessor_next_token(parser->preprocessor);
	if (left_paren.kind != TOKEN_LEFT_PAREN) {
		TokenKind expected_tokens[] = { TOKEN_LEFT_PAREN };
		diagnostics_report_unexpected_token(parser->diagnostics,
				left_paren,
				expected_tokens,
				array_size(expected_tokens));
	}

	bool has_condition_expr = false;
	Expr condition_expr;
	bool has_advance_expr = false;
	Expr advance_expr;

	AstNode* loop = arena_alloc_zeroed(parser->ast_allocator, AstNode);
	loop->kind = AST_NODE_FOR_LOOP;

	{
		ident_storage_begin_scope(parser->ident_storage);
	
		Scope* scope = arena_alloc_zeroed(parser->ast_allocator, Scope);
		scope->id = parser->ident_storage->current_scope->id;
		loop->for_loop.loop_scope = scope;

		Token init_stmt_token = preprocessor_view_next(parser->preprocessor);

		if (init_stmt_token.kind != TOKEN_SEMICOLON) {
			// FIXME: `_parser_parse_single_node` also consumes the `;`
			AstNode* init_stmt = _parser_parse_single_node(parser);
			loop->for_loop.init_stmt = init_stmt;

			if (init_stmt) {
				scope_append(scope, init_stmt);
			}
		} else {
			_parser_consume_semicolon(parser);
		}

		if (_parser_try_parse_expr(parser, &condition_expr) == EXPR_PARSE_OK) {
			has_condition_expr = true;
		}

		_parser_consume_semicolon(parser);

		if (_parser_try_parse_expr(parser, &advance_expr) == EXPR_PARSE_OK) {
			has_advance_expr = true;
		}

		Token right_paren = preprocessor_next_token(parser->preprocessor);
		if (right_paren.kind != TOKEN_RIGHT_PAREN) {
			TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };
			diagnostics_report_unexpected_token(parser->diagnostics,
					right_paren,
					expected_tokens,
					array_size(expected_tokens));
		}

		LoopBody body = _parser_parse_loop_body(parser, loop);
		loop->for_loop.body_scope = body.scope;

		ident_storage_end_scope(parser->ident_storage);
	}

	if (has_condition_expr) {
		loop->for_loop.condition = arena_alloc(parser->ast_allocator, Expr);
		*loop->for_loop.condition = condition_expr;
	}

	if (has_advance_expr) {
		loop->for_loop.advance_expr = arena_alloc(parser->ast_allocator, Expr);
		*loop->for_loop.advance_expr = advance_expr;
	}

	profile_scope_end();
	return loop;
}

static AstNode* _parser_parse_switch(Parser* parser) {
	profile_func_colored(PROFILE_COLOR);

	assert(preprocessor_next_token(parser->preprocessor).kind == TOKEN_KEYWORD_SWITCH);

	AstNode* node = arena_alloc_zeroed(parser->ast_allocator, AstNode);
	node->kind = AST_NODE_SWITCH;

	Token left_paren = preprocessor_next_token(parser->preprocessor);
	if (left_paren.kind != TOKEN_LEFT_PAREN) {
		TokenKind expected_tokens[] = { TOKEN_LEFT_PAREN };
		diagnostics_report_unexpected_token(parser->diagnostics,
				left_paren,
				expected_tokens,
				array_size(expected_tokens));
		profile_scope_end();
		return NULL;
	}

	node->switch_stmt.expr = arena_alloc_zeroed(parser->ast_allocator, Expr);

	ExprParseResult expr_result = _parser_try_parse_expr(parser, node->switch_stmt.expr);
	if (expr_result) {
		report_error(parser->diagnostics,
				source_range_pack(left_paren.source_range),
				STR_LIT("Expected an expression after '('"),
				NULL);
	}

	Token right_paren = preprocessor_next_token(parser->preprocessor);
	if (right_paren.kind != TOKEN_RIGHT_PAREN) {
		TokenKind expected_tokens[] = { TOKEN_RIGHT_PAREN };
		diagnostics_report_unexpected_token(parser->diagnostics,
				right_paren,
				expected_tokens,
				array_size(expected_tokens));
		profile_scope_end();
		return NULL;
	}

	Token body_token = preprocessor_view_next(parser->preprocessor);
	if (body_token.kind != TOKEN_SEMICOLON) {
		ParserLoopOrSwitchState current_loop = {
			.node = node,
			.parent = parser->loop_or_switch_state
		};
		parser->loop_or_switch_state = &current_loop;

		AstNode* body = _parser_parse_single_node(parser);
		if (body) {
			assert(body->kind == AST_NODE_BLOCK);
			node->switch_stmt.body = &body->block;
		}

		parser->loop_or_switch_state = parser->loop_or_switch_state->parent;
	} else {
		node->switch_stmt.body = arena_alloc_zeroed(parser->ast_allocator, Scope);
	}

	profile_scope_end();
	return node;
}

static AstNode* _parser_parse_case(Parser* parser) {
	profile_func_colored(PROFILE_COLOR);

	Token case_token = preprocessor_next_token(parser->preprocessor);
	assert(case_token.kind == TOKEN_KEYWORD_CASE);

	AstNode* node = arena_alloc_zeroed(parser->ast_allocator, AstNode);
	node->kind = AST_NODE_CASE;
	node->case_stmt.value = arena_alloc_zeroed(parser->ast_allocator, Expr);

	ExprParseResult expr_result = _parser_try_parse_expr(parser, node->case_stmt.value);
	if (expr_result == EXPR_PARSE_ERROR) {
		report_error(parser->diagnostics,
				source_range_pack(case_token.source_range),
				STR_LIT("Expected an expression after 'case'"),
				NULL);
	}

	Token colon = preprocessor_next_token(parser->preprocessor);
	if (colon.kind != TOKEN_COLON) {
		TokenKind expected_tokens[] = { TOKEN_COLON };
		diagnostics_report_unexpected_token(parser->diagnostics,
				colon,
				expected_tokens,
				array_size(expected_tokens));
	}

	profile_scope_end();
	return node;
}

static AstNode* _parser_parse_default_case(Parser* parser) {
	profile_func_colored(PROFILE_COLOR);

	Token case_token = preprocessor_next_token(parser->preprocessor);
	assert(case_token.kind == TOKEN_KEYWORD_DEFAULT);

	AstNode* node = arena_alloc_zeroed(parser->ast_allocator, AstNode);
	node->kind = AST_NODE_CASE;
	node->case_stmt.value = NULL;

	Token colon = preprocessor_next_token(parser->preprocessor);
	if (colon.kind != TOKEN_COLON) {
		TokenKind expected_tokens[] = { TOKEN_COLON };
		diagnostics_report_unexpected_token(parser->diagnostics,
				colon,
				expected_tokens,
				array_size(expected_tokens));
	}

	profile_scope_end();
	return node;
}

AstNode* _parser_parse_single_node(Parser* parser) {
	Token initial_token = preprocessor_view_next(parser->preprocessor);
	switch (initial_token.kind) {
	case TOKEN_LEFT_BRACE: {
		AstNode* node = arena_alloc_zeroed(parser->ast_allocator, AstNode);
		node->kind = AST_NODE_BLOCK;
		node->block = (Scope) {};

		if (!_parser_parse_scope(parser, &node->block)) {
			return NULL;
		}

		return node;
	}
	case TOKEN_KEYWORD_TYPEDEF: {
		return _parser_parse_type_def(parser);
	}
	case TOKEN_KEYWORD_STRUCT: {
		Struct* struct_def = NULL;
		if (!_parser_parse_struct_def(parser, &struct_def, false)) {
			return NULL;
		}

		assert(struct_def->layout_kind == STRUCT_LAYOUT_KIND_STRUCT);

		if (!_parser_expect_semicolon(parser, STR_LIT("Expected ';' after the struct"))) {
			return NULL;
		}

		AstNode* node = arena_alloc_zeroed(parser->ast_allocator, AstNode);

		node->kind = AST_NODE_STRUCT;
		node->struct_def = struct_def;
		return node;
	}
	case TOKEN_KEYWORD_UNION: {
		Struct* union_def = NULL;
		if (!_parser_parse_struct_def(parser, &union_def, false)) {
			return NULL;
		}

		assert(union_def->layout_kind == STRUCT_LAYOUT_KIND_UNION);

		if (!_parser_expect_semicolon(parser, STR_LIT("Expected ';' after the struct"))) {
			return NULL;
		}

		AstNode* node = arena_alloc_zeroed(parser->ast_allocator, AstNode);

		node->kind = AST_NODE_STRUCT;
		node->union_def = union_def;
		return node;
	}
	case TOKEN_KEYWORD_ENUM: {
		Enum* enum_def = NULL;

		if (!_parser_parse_enum_def(parser, &enum_def, false)) {
			return NULL;
		}

		if (!_parser_expect_semicolon(parser, STR_LIT("Expected ';' after the enum"))) {
			return NULL;
		}

		AstNode* node = arena_alloc_zeroed(parser->ast_allocator, AstNode);
		memset(node, 0, sizeof(*node));

		node->kind = AST_NODE_ENUM;
		node->enum_def = enum_def;
		return node;
	}
	case TOKEN_KEYWORD_RETURN: {
		preprocessor_next_token(parser->preprocessor);

		Token token = preprocessor_view_next(parser->preprocessor);
		bool has_value = true;
		if (token.kind == TOKEN_SEMICOLON) {
			has_value = false;
		}
		
		Expr* return_value = NULL;
		if (has_value) {
			return_value = arena_alloc(parser->ast_allocator, Expr);

			if (_parser_try_parse_expr(parser, return_value) != EXPR_PARSE_OK) {
				_parser_skip_until_semicolon(parser);
			}
		}

		if (!_parser_expect_semicolon(parser, STR_LIT("Expected ';' after the return"))) {
			_parser_skip_until(parser, TOKEN_RIGHT_BRACE, TOKEN_EOF);
		}

		AstNode* node = arena_alloc_zeroed(parser->ast_allocator, AstNode);
		memset(node, 0, sizeof(*node));

		node->kind = AST_NODE_RETURN;
		node->return_stmt.value = return_value;
		return node;
	}
	case TOKEN_KEYWORD_IF:
		return _parser_parse_if_stmt(parser);
	case TOKEN_KEYWORD_WHILE:
		return _parser_parse_while_loop(parser);
	case TOKEN_KEYWORD_DO:
		return _parser_parse_do_while_loop(parser);
	case TOKEN_KEYWORD_FOR:
		return _parser_parse_for_loop(parser);
	case TOKEN_KEYWORD_BREAK: {
		preprocessor_next_token(parser->preprocessor);

		ParserLoopOrSwitchState* state = parser->loop_or_switch_state;
		if (state) {
			switch (state->node->kind) {
			case AST_NODE_SWITCH:
				state->node->switch_stmt.break_count += 1;
				break;
			case AST_NODE_WHILE_LOOP:
				state->node->while_loop.break_count += 1;
				break;
			case AST_NODE_FOR_LOOP:
				state->node->for_loop.break_count += 1;
				break;
			default:
				unreachable();
			}
		} else {
			diagnostics_report_error(parser->diagnostics,
					initial_token.source_range,
					STR_LIT("`break` outside of a loop or a switch"),
					NULL);
		}

		_parser_expect_semicolon(parser, STR_LIT("Expected ';' after break"));

		AstNode* stmt = arena_alloc_zeroed(parser->ast_allocator, AstNode);
		stmt->kind = AST_NODE_BREAK;
		return stmt;
	}
	case TOKEN_KEYWORD_CONTINUE: {
		preprocessor_next_token(parser->preprocessor);

		ParserLoopOrSwitchState* state = _parser_current_loop(parser);
		if (state) {
			switch (state->node->kind) {
			case AST_NODE_WHILE_LOOP:
				state->node->while_loop.continue_count += 1;
				break;
			case AST_NODE_FOR_LOOP:
				state->node->for_loop.continue_count += 1;
				break;
			default:
				unreachable();
			}
		} else {
			diagnostics_report_error(parser->diagnostics,
					initial_token.source_range,
					STR_LIT("`continue` outside of a loop"),
					NULL);
		}

		_parser_expect_semicolon(parser, STR_LIT("Expected ';' after continue"));

		AstNode* stmt = arena_alloc_zeroed(parser->ast_allocator, AstNode);
		stmt->kind = AST_NODE_CONTINUE;
		return stmt;
	}
	case TOKEN_KEYWORD_SWITCH:
		return _parser_parse_switch(parser);
	case TOKEN_KEYWORD_CASE:
		return _parser_parse_case(parser);
	case TOKEN_KEYWORD_DEFAULT:
		return _parser_parse_default_case(parser);
	default:
		return _parser_parse_declaration_or_expr(parser);
	}

	unreachable();
	return NULL;
}

bool _parser_parse_scope(Parser* parser, Scope* out_scope) {
	profile_func_colored(PROFILE_COLOR);

	assert_msg(arena_contains_memory_region(parser->ast_allocator, out_scope, sizeof(*out_scope)),
			"`out_scope` must be allocated using the `ast_allocator`. This one wasn't. "
			"Scope is not allowed to move in memory, because other `AstNode`s reference it through "
			"the pointer. Any moves of the `Scope` would invalidate the references. To avoid the "
			"moves, the `Scope` must be allocated using `ast_allocator` before ever being used");

	ident_storage_begin_scope(parser->ident_storage);
	out_scope->id = parser->ident_storage->current_scope->id;

	Token token = preprocessor_next_token(parser->preprocessor);
	assert(token.kind == TOKEN_LEFT_BRACE);

	while (true) {
		Token token = preprocessor_view_next(parser->preprocessor);
		if (token.kind == TOKEN_EOF) {
			diagnostics_report_error(parser->diagnostics,
					token.source_range,
					STR_LIT("Unexpected end of file"),
					NULL);

			ident_storage_end_scope(parser->ident_storage);
			profile_scope_end();
			return false;
		} else if (token.kind == TOKEN_RIGHT_BRACE) {
			preprocessor_next_token(parser->preprocessor);
			break;
		} else if (token.kind == TOKEN_SEMICOLON) {
			preprocessor_next_token(parser->preprocessor);
			continue;
		}

		AstNode* node = _parser_parse_single_node(parser);
		if (node) {
			scope_append(out_scope, node);
		} else {
			preprocessor_next_token(parser->preprocessor);
		}
	}

	ident_storage_end_scope(parser->ident_storage);

	profile_scope_end();
	return true;
}

void parser_init(Parser* parser,
		Arena* ast_allocator,
		Arena* temp_allocator,
		IdentifierStorage* ident_storage,
		const TypeContext* type_context,
		Preprocessor* preprocessor,
		Diagnostics* diagnostics) {
	parser->ast_allocator = ast_allocator;
	parser->temp_allocator = temp_allocator;
	parser->diagnostics = diagnostics;
	parser->preprocessor = preprocessor;
	parser->ident_storage = ident_storage;
	parser->type_context = type_context;

	parser->dummy_node = arena_alloc_zeroed(ast_allocator, AstNode);
}

void parser_parse(Parser* parser, AST* ast) {
	profile_func_colored(PROFILE_COLOR);

	ast->root_nodes = (NodeList) {};
	ast->first_compound_type = NULL;
	ast->last_compound_type = NULL;

	parser->ast = ast;

	bool run = true;
	while (run) {
		Token token = preprocessor_view_next(parser->preprocessor);
		if (token.kind == TOKEN_EOF) {
			preprocessor_next_token(parser->preprocessor);
			break;
		} else if (token.kind == TOKEN_SEMICOLON) {
			preprocessor_next_token(parser->preprocessor);
			continue;
		}

		AstNode* node = _parser_parse_single_node(parser);
		if (node == parser->dummy_node) {
			continue;
		}

		if (node) {
			parsed_node_list_append(&ast->root_nodes, node);

			// NOTE: We don't a root scope, it is a just a list of nodes,
			//       thus we can't assign a parent scope for each node
		} else {
			preprocessor_next_token(parser->preprocessor);
		}
	}

	profile_scope_end();
}
