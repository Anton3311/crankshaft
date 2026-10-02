#include "tests.h"

#include "parser/preprocessor.h"
#include "parser/parser.h"

#define DEFAULT_SOURCE_PATH "test.c"

//
// Source Info
//

void test_text_start_position_to_source_location(TestContext* context) {
	String source_code = STR_LIT("#define hello 100 + 100\nhello");

	LineInfo line_info = line_info_from_source(context->arena, source_code);

	SourceLocation position = line_info_pos_to_source_location(&line_info, 0);
	assert(position.line == 0);
	assert(position.column == 0);
}

void test_last_line_postion_to_source_location(TestContext* context) {
	String source_code = STR_LIT("#define hello 100 + 100\nhello");

	LineInfo line_info = line_info_from_source(context->arena, source_code);

	SourceLocation position = line_info_pos_to_source_location(&line_info, 26);
	assert(position.line == 1);
	assert(position.column == 2);
}

void test_line_range_of_one_line_source_code(TestContext* context) {
	String source_code = STR_LIT("hello");

	LineInfo line_info = line_info_from_source(context->arena, source_code);

	SourceRange range = line_info_get_line_range(&line_info, 0);
	assert(range.start == 0);
	assert(range.end == 5);
}

//
// Tokenizer
//

void test_token_has_valid_string_represenation(TestContext* context) {
	for (size_t i = 0; i < TOKEN_COUNT; i += 1) {
		String string = token_kind_to_string((TokenKind)i);
		assert(string.v != NULL);
		assert(string.length > 0);
	}
}

typedef struct {
	size_t token_count;
	TokenKind* generated_tokens;
	String generated_source;
} RandomTokenStream;

RandomTokenStream _generate_random_tokens(TestContext* context, size_t token_count) {
	size_t acceptable_token_count = TOKEN_COUNT - 1; // - 1 because TOKEN_EOF is exluded
	TokenKind* generated_tokens = arena_alloc_array(context->arena, TokenKind, token_count);

	StringBuilder builder = { .arena = context->temp_arena };
	for (size_t i = 0; i < token_count; i += 1) {
		size_t token_index = (size_t)rand() % acceptable_token_count;
		
		TokenKind token_kind = (TokenKind)(1 + token_index);
		assert(token_kind != TOKEN_EOF);

		generated_tokens[i] = token_kind;

		String token_string = {};
		switch (token_kind) {
		case TOKEN_IDENT:
			token_string = STR_LIT("ident");
			break;
		case TOKEN_STRING:
			token_string = STR_LIT("\"hello world\"");
			break;
		case TOKEN_CHAR:
			token_string = STR_LIT("\'h\'");
			break;
		default:
			token_string = token_kind_to_string(token_kind);
			if (token_string.length >= 2) {
				assert_msg(
						token_string.v[0] != '<' || token_string.v[token_string.length - 1] != '>',
						"Some tokens that don't have an explicit string representation were not handled");
			}
		}

		str_builder_append(&builder, token_string);
		str_builder_append_char(&builder, '\n');
	}

	return (RandomTokenStream) {
		.token_count = token_count,
		.generated_tokens = generated_tokens,
		.generated_source = builder.string,
	};
}

void test_token_source_range_matches_token_string(TestContext* context) {
	assert(TOKEN_EOF == 0);

	size_t token_count = 4096;
	RandomTokenStream random_tokens = _generate_random_tokens(context, token_count);

	Tokenizer tokenizer = {
		.source_code = random_tokens.generated_source,
	};

	size_t generated_token_count = 0;
	while (true) {
		Token token = tokenizer_next_token(&tokenizer);

		if (token.kind == TOKEN_EOF) {
			break;
		}

		generated_token_count += 1;
		assert(generated_token_count <= token_count);

		String source_sub_str = sub_str(random_tokens.generated_source,
				token.source_range.start,
				token.source_range.end - token.source_range.start);

		assert(str_equal(source_sub_str, token.string));
	}
}

void test_tokenizer_generates_expected_token(TestContext* context) {
	assert(TOKEN_EOF == 0);

	size_t token_count = 4096;
	RandomTokenStream random_tokens = _generate_random_tokens(context, token_count);

	Tokenizer tokenizer = {
		.source_code = random_tokens.generated_source,
	};

	size_t generated_token_count = 0;
	while (true) {
		Token token = tokenizer_next_token(&tokenizer);

		if (token.kind == TOKEN_EOF) {
			break;
		}

		TokenKind expected_kind = random_tokens.generated_tokens[generated_token_count];
		assert_msg(token.kind == expected_kind,
				"Expected: '%.*s' Actual: '%.*s'",
				STR_FMT(token_kind_to_string(expected_kind)),
				STR_FMT(token_kind_to_string(token.kind)));

		generated_token_count += 1;
		assert(generated_token_count <= token_count);
	}
}

void test_mutli_line_comment_with_asterisk_on_line_starts(TestContext* context) {
	String source_code = STR_LIT(
			"/* start of multi line comment\n"
			" * line starting with asterisk\n"
			" * another line starting with asterisk\n"
			" */");

	Tokenizer tokenizer = {
		.source_code = source_code,
	};

	Token token = tokenizer_next_token(&tokenizer);
	assert(token.kind == TOKEN_EOF);
}

void test_token_has_valid_source_file(TestContext* context) {
	size_t token_count = 4096;
	RandomTokenStream random_tokens = _generate_random_tokens(context, token_count);

	SourceFile source_file = {
		.path = STR_LIT(DEFAULT_SOURCE_PATH),
		.source_code = random_tokens.generated_source
	};

	Tokenizer tokenizer = {};
	tokenizer_init(&tokenizer, &source_file);

	while (true) {
		Token token = tokenizer_next_token(&tokenizer);

		if (token.kind == TOKEN_EOF) {
			break;
		}

		assert(token.source_range.source_file != NULL);
	}
}

//
// Preprocessor
//

static void init_preprocessor_test(TestContext* context,
		String source_code,
		Preprocessor* out_preprocessor,
		Diagnostics* out_diagnostics,
		LineInfo* out_line_info) {

	SourceStorage* source_storage = arena_alloc(context->arena, SourceStorage);
	source_storage_init(source_storage, (StringArray) {}, context->arena);
	SourceFile* source_file = source_storage_append(source_storage, STR_LIT(DEFAULT_SOURCE_PATH), source_code);

	Arena* generated_tokens_arena = arena_alloc(context->arena, Arena);
	*generated_tokens_arena = arena_alloc_sub_arena(context->arena, 2 * 4096);

	Arena* macros_allocator = arena_alloc(context->arena, Arena);
	*macros_allocator = arena_alloc_sub_arena(
			context->arena,
			(sizeof(MacroDefinition) + sizeof(String)) * MACRO_TABLE_INITIAL_CAPACITY);

	*out_diagnostics = (Diagnostics) {
		.allocator = context->arena,
		.source_storage = source_storage,
		.error_limit = 128,
	};
	
	preprocessor_init(out_preprocessor,
			source_storage,
			source_file,
			out_diagnostics,
			arena_allocator_new(macros_allocator),
			context->arena,
			context->temp_arena,
			generated_tokens_arena);
}

void test_non_function_style_macro_expansion(TestContext* context) {
	String source_code = STR_LIT(
			"#define hello 100 + 100\n"
			"hello");
	String expected_source_code = STR_LIT("100 + 100");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Tokenizer expected_source_tokenizer = (Tokenizer) {
		.source_code = expected_source_code,
	};

	while (true) {
		Token token = preprocessor_next_token(&preprocessor);
		Token expected_token = tokenizer_next_token(&expected_source_tokenizer);

		assert(token.kind == expected_token.kind);
		assert(str_equal(token.string, expected_token.string));

		if (token.kind == TOKEN_EOF) {
			break;
		}
	}
}

void test_expand_function_style_macro_with_two_params(TestContext* context) {
	String source_code = STR_LIT(
			"#define hello(a, b) a + b\n"
			"hello(10, 10)");
	String expected_source_code = STR_LIT("10 + 10");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Tokenizer expected_source_tokenizer = (Tokenizer) {
		.source_code = expected_source_code,
	};

	while (true) {
		Token token = preprocessor_next_token(&preprocessor);
		Token expected_token = tokenizer_next_token(&expected_source_tokenizer);

		assert(token.kind == expected_token.kind);
		assert(str_equal(token.string, expected_token.string));

		if (token.kind == TOKEN_EOF) {
			break;
		}
	}
}

void test_expand_function_style_macro_without_params(TestContext* context) {
	String source_code = STR_LIT(
			"#define hello() token\n"
			"hello()");
	String expected_source_code = STR_LIT("token");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Tokenizer expected_source_tokenizer = (Tokenizer) {
		.source_code = expected_source_code,
	};

	while (true) {
		Token token = preprocessor_next_token(&preprocessor);
		Token expected_token = tokenizer_next_token(&expected_source_tokenizer);

		assert(token.kind == expected_token.kind);
		assert(str_equal(token.string, expected_token.string));

		if (token.kind == TOKEN_EOF) {
			break;
		}
	}
}

void test_expand_empty_function_style_macro(TestContext* context) {
	String source_code = STR_LIT(
			"#define ignore(a)\n"
			"ignore(10)");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Token token = preprocessor_next_token(&preprocessor);
	assert(token.kind == TOKEN_EOF);
}

void test_expand_empty_style_macro(TestContext* context) {
	String source_code = STR_LIT(
			"#define empty\n"
			"empty");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Token token = preprocessor_next_token(&preprocessor);
	assert(token.kind == TOKEN_EOF);
}

void test_macro_call_with_not_enough_args_fails(TestContext* context) {
	String source_code = STR_LIT(
			"#define many_args(a, b, c, d) a + b + c + d\n"
			"many_args(10, 1)");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	preprocessor_next_token(&preprocessor);

	assert(diagnostics.first != NULL);

	String expected_error_message = STR_LIT("Not enough arguments during a call of macro called 'many_args'. "
			"Expected 4 but only 2 were provided.");

	assert(str_equal(diagnostics.first->message, expected_error_message));
}

void test_nested_macro(TestContext* context) {
	String source_code = STR_LIT(
			"#define inner(a, b) a + b\n"
			"#define outer(a, b, c) (inner(a, b)) * c\n"
			"outer(1, 2, 3)");

	String expected_source_code = STR_LIT("(1 + 2) * 3");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Tokenizer expected_source_tokenizer = (Tokenizer) {
		.source_code = expected_source_code,
	};

	while (true) {
		Token token = preprocessor_next_token(&preprocessor);
		Token expected_token = tokenizer_next_token(&expected_source_tokenizer);

		printf("%.*s %.*s\n", STR_FMT(token.string), STR_FMT(expected_token.string));

		assert(token.kind == expected_token.kind);
		assert(str_equal(token.string, expected_token.string));

		if (token.kind == TOKEN_EOF) {
			break;
		}
	}
}

void test_nested_macro_2(TestContext* context) {
	String source_code = STR_LIT(
			"#define inner(a, b) a + b\n"
			"#define inner2(a, b) inner(a, b) + b\n"
			"#define outer(a, b, c) (inner2(a, b) + inner2(b, a)) * c\n"
			"outer(1, 2, 3)");

	String expected_source_code = STR_LIT("(1 + 2 + 2 + 2 + 1 + 1) * 3");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Tokenizer expected_source_tokenizer = (Tokenizer) {
		.source_code = expected_source_code,
	};

	while (true) {
		Token token = preprocessor_next_token(&preprocessor);
		Token expected_token = tokenizer_next_token(&expected_source_tokenizer);

		printf("%.*s %.*s\n", STR_FMT(token.string), STR_FMT(expected_token.string));

		assert(token.kind == expected_token.kind);
		assert(str_equal(token.string, expected_token.string));

		if (token.kind == TOKEN_EOF) {
			break;
		}
	}
}

void test_builtin_line_macro_expantion(TestContext* context) {
	String source_code = STR_LIT(
		"__LINE__\n" // 1
		"__LINE__\n" // 2
		"__LINE__\n" // 3
		"__LINE__ __LINE__ __LINE__" // 4 4 4
	);

	String expected_source_code = STR_LIT("1 2 3 4 4 4");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Tokenizer expected_source_tokenizer = (Tokenizer) {
		.source_code = expected_source_code,
	};

	while (true) {
		Token token = preprocessor_next_token(&preprocessor);
		Token expected_token = tokenizer_next_token(&expected_source_tokenizer);

		printf("%.*s %.*s\n", STR_FMT(token.string), STR_FMT(expected_token.string));

		assert(token.kind == expected_token.kind);
		assert(str_equal(token.string, expected_token.string));

		if (token.kind == TOKEN_EOF) {
			break;
		}
	}
}

void test_assert_macro_expantion(TestContext* context) {
	String source_code = STR_LIT(
		"#define assert(expression) if (!(expression)) { printf(\"%s:%u\", __FILE__, __LINE__); }\n"
		"assert(true)"
	);

	String expected_source_code = STR_LIT("if (!(true)) { printf(\"%s:%u\", \"" DEFAULT_SOURCE_PATH "\", 2); }");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Tokenizer expected_source_tokenizer = (Tokenizer) {
		.source_code = expected_source_code,
	};

	while (true) {
		Token token = preprocessor_next_token(&preprocessor);
		Token expected_token = tokenizer_next_token(&expected_source_tokenizer);

		printf("%.*s %.*s\n", STR_FMT(token.string), STR_FMT(expected_token.string));

		assert(token.kind == expected_token.kind);
		assert(str_equal(token.string, expected_token.string));

		if (token.kind == TOKEN_EOF) {
			break;
		}
	}
}

void test_macro_string_operator(TestContext* context) {
	String source_code = STR_LIT(
		"#define to_string(a) #a\n"
		"to_string(hello world)"
	);

	String expected_source_code = STR_LIT("\"hello world\"");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Tokenizer expected_source_tokenizer = (Tokenizer) {
		.source_code = expected_source_code,
	};

	while (true) {
		Token token = preprocessor_next_token(&preprocessor);
		Token expected_token = tokenizer_next_token(&expected_source_tokenizer);

		printf("%.*s %.*s\n", STR_FMT(token.string), STR_FMT(expected_token.string));

		assert(token.kind == expected_token.kind);
		assert(str_equal(token.string, expected_token.string));

		if (token.kind == TOKEN_EOF) {
			break;
		}
	}
}

void test_macro_string_operator_with_invalid_param_name_fails(TestContext* context) {
	String source_code = STR_LIT(
		"#define to_string(a) #invalid_param\n"
		"to_string(hello world)"
	);

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	while (true) {
		Token token = preprocessor_next_token(&preprocessor);
		if (token.kind == TOKEN_EOF) {
			break;
		}
	}
	
	assert(diagnostics.first != NULL);

	// NOTE: Line indices are zero based
	assert(diagnostics.first->start_line + 1 == 1);
	assert(diagnostics.first->end_line + 1 == 1);
}

void test_simple_if_elif_directives(TestContext* context) {
	String source_code = STR_LIT(
			"#if 0\n"
			"hello\n"
			"#elif 1\n"
			"world\n"
			"#endif\n"
	);

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Token* tokens = arena_alloc_array(context->temp_arena, Token, 0);
	size_t token_count = 0;

	while (true) {
		Token token = preprocessor_next_token(&preprocessor);
		if (token.kind == TOKEN_EOF) {
			break;
		}

		*arena_alloc(context->temp_arena, Token) = token;
		token_count += 1;
	}

	assert(token_count == 1);
	assert(tokens[0].kind == TOKEN_IDENT);
	assert(str_equal(tokens[0].string, STR_LIT("world")));
}

void test_parsing_of_non_function_style_macro_with_paren_as_first_token_in_stream(TestContext* context) {
	String source_code = STR_LIT(
		"#define macro  (token)\n"
		"macro"
	);

	String expected_source_code = STR_LIT("(token)");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Tokenizer expected_source_tokenizer = (Tokenizer) {
		.source_code = expected_source_code,
	};

	while (true) {
		Token token = preprocessor_next_token(&preprocessor);
		Token expected_token = tokenizer_next_token(&expected_source_tokenizer);

		printf("%.*s %.*s\n", STR_FMT(token.string), STR_FMT(expected_token.string));

		assert(token.kind == expected_token.kind);
		assert(str_equal(token.string, expected_token.string));

		if (token.kind == TOKEN_EOF) {
			break;
		}
	}
}

void test_error_directive(TestContext* context) {
	String source_code = STR_LIT(
		"#error Error message"
	);

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	while (preprocessor_next_token(&preprocessor).kind != TOKEN_EOF);

	assert(diagnostics.first != NULL);
	assert(str_equal(diagnostics.first->message, STR_LIT("Error message")));
}

void test_multi_line_define(TestContext* context) {
	String source_code = STR_LIT(
		"#define macro hello\\\n"
		"world\n"
		"macro"
	);

	String expected_source_code = STR_LIT("hello\nworld");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Tokenizer expected_source_tokenizer = (Tokenizer) {
		.source_code = expected_source_code,
	};

	while (true) {
		Token token = preprocessor_next_token(&preprocessor);
		Token expected_token = tokenizer_next_token(&expected_source_tokenizer);

		printf("%.*s %.*s\n", STR_FMT(token.string), STR_FMT(expected_token.string));

		assert(token.kind == expected_token.kind);
		assert(str_equal(token.string, expected_token.string));

		if (token.kind == TOKEN_EOF) {
			break;
		}
	}
}

void test_token_insertion_operator(TestContext* context) {
	String source_code = STR_LIT(
		"#define macro(a) hello##a\n"
		"macro(_world)"
	);

	String expected_source_code = STR_LIT("hello_world");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Tokenizer expected_source_tokenizer = (Tokenizer) {
		.source_code = expected_source_code,
	};

	while (true) {
		Token token = preprocessor_next_token(&preprocessor);
		Token expected_token = tokenizer_next_token(&expected_source_tokenizer);

		printf("%.*s %.*s\n", STR_FMT(token.string), STR_FMT(expected_token.string));

		assert(token.kind == expected_token.kind);
		assert(str_equal(token.string, expected_token.string));

		if (token.kind == TOKEN_EOF) {
			break;
		}
	}
}

void test_va_args_macro(TestContext* context) {
	String source_code = STR_LIT(
		"#define macro(...) __VA_ARGS__\n"
		"macro(10, 11, 88)"
	);

	String expected_source_code = STR_LIT("10, 11, 88");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Tokenizer expected_source_tokenizer = (Tokenizer) {
		.source_code = expected_source_code,
	};

	while (true) {
		Token token = preprocessor_next_token(&preprocessor);
		Token expected_token = tokenizer_next_token(&expected_source_tokenizer);

		printf("%.*s %.*s\n", STR_FMT(token.string), STR_FMT(expected_token.string));

		assert(token.kind == expected_token.kind);
		assert(str_equal(token.string, expected_token.string));

		if (token.kind == TOKEN_EOF) {
			break;
		}
	}
}

void test_processor_next_returns_eof_when_include_stack_is_empty(TestContext* context) {
	String source_code = STR_LIT("hello");

	LineInfo line_info = {};
	Diagnostics diagnostics = {};
	Preprocessor preprocessor = {};

	init_preprocessor_test(context, source_code, &preprocessor, &diagnostics, &line_info);

	Token first_token = preprocessor_next_token(&preprocessor);
	assert(first_token.kind == TOKEN_IDENT);
	Token second_token = preprocessor_next_token(&preprocessor);
	assert(second_token.kind == TOKEN_EOF);

	assert_msg(preprocessor.include_stack.depth == 0,
			"Preprocessor has reached the eof and should have popped the "
			"file off of the include_stack");

	Token third_token = preprocessor_next_token(&preprocessor);
	assert_msg(third_token.kind == TOKEN_EOF,
			"Preprocessor has reached the eof and has no more files in the include stack, "
			"thus must keep on return the eof");
}

//
// Parser
//

void run_parser_test_2(TestContext* context,
		Diagnostics* out_diagnostics,
		SourceStorage* out_source_storage,
		String source_code,
		AST* out_ast,
		IdentifierStorage** out_ident_storage) {

	source_storage_init(out_source_storage, (StringArray) {}, context->arena);
	SourceFile* source_file = source_storage_append(out_source_storage, STR_LIT(DEFAULT_SOURCE_PATH), source_code);

	*out_diagnostics = (Diagnostics) {
		.allocator = context->temp_arena,
		.source_storage = out_source_storage,
		.error_limit = 128,
	};

	Arena generated_tokens_arena = arena_alloc_sub_arena(context->arena, 2 * 4096);

	Preprocessor preprocessor = {};
	preprocessor_init(&preprocessor,
			out_source_storage,
			source_file,
			out_diagnostics,
			heap_allocator_new(),
			context->arena,
			context->temp_arena,
			&generated_tokens_arena);

	*out_ident_storage = arena_alloc(context->arena, IdentifierStorage);

	// NOTE: This arena is used by the `IdentifierStorage` inside the parser,
	//       so it's lifetime is no longer than the one of the parser.
	Arena ident_arena = arena_alloc_sub_arena(context->arena, 16 * 1024);
	Arena ast_arena = arena_alloc_sub_arena(context->arena, 16 * 1024);

	ident_storage_init(*out_ident_storage, arena_allocator_new(&ident_arena), &ident_arena);

	TypeContext type_context = {
		.pointer_type_layout = {
			.size = sizeof(void*),
			.alignment = alignof(void*)
		}
	};

	Parser parser = {};
	parser_init(&parser,
			&ast_arena,
			context->temp_arena,
			*out_ident_storage,
			&type_context,
			&preprocessor,
			out_diagnostics);

	parser_parse(&parser, out_ast);

	preprocessor_release(&preprocessor);
}

void run_parser_test(TestContext* context,
		Diagnostics* out_diagnostics,
		SourceStorage* out_source_storage,
		String source_code,
		AST* out_ast) {
	IdentifierStorage* ident_storage;
	run_parser_test_2(context,
			out_diagnostics,
			out_source_storage,
			source_code,
			out_ast,
			&ident_storage);
}

void test_parse_type_def_of_primitive_type(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("typedef int int32;"), &ast);

	assert(diagnostics.first == NULL);

	assert(ast.root_nodes.count == 1);

	AstNode* first = ast.root_nodes.first;
	assert(first->kind == AST_NODE_TYPE_DEF);

	TypeDef* type_def = first->type_def;
	assert(type_def->first_variant);
	assert(type_def->first_variant == type_def->last_variant);
	assert(type_def->first_variant->aliased_type.kind == TYPE_INT);
	assert(str_equal(type_def->first_variant->new_name, STR_LIT("int32")));
}

void test_parse_type_def_of_struct_def(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("typedef struct Hello World;"), &ast);

	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	AstNode* first = ast.root_nodes.first;
	assert(first->kind == AST_NODE_TYPE_DEF);

	TypeDef* type_def = first->type_def;
	assert(type_def->first_variant);
	assert(type_def->first_variant->aliased_type.kind == TYPE_STRUCT);

	Struct* struct_def = type_def->first_variant->aliased_type.struct_def;
	assert(str_equal(struct_def->name, STR_LIT("Hello")));
	assert(struct_def->field_count == 0);

	assert(str_equal(type_def->first_variant->new_name, STR_LIT("World")));
}

void test_parse_type_def_of_struct_def_with_fields(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("typedef struct Hello {\n"
				"	int int_value;\n"
				"	float float_value;\n"
				"	struct InnerStruct { int inner_value; } inner;\n"
				"} World;"), &ast);

	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	AstNode* first = ast.root_nodes.first;
	assert(first->kind == AST_NODE_TYPE_DEF);

	TypeDef* type_def = first->type_def;
	assert(type_def->first_variant);
	assert(type_def->first_variant->aliased_type.kind == TYPE_STRUCT);
	assert(str_equal(type_def->first_variant->new_name, STR_LIT("World")));

	Struct* hello_struct_def = type_def->first_variant->aliased_type.struct_def;
	assert(str_equal(hello_struct_def->name, STR_LIT("Hello")));
	assert(hello_struct_def->field_count == 3);

	StructField* int_value_field = &hello_struct_def->fields[0];
	StructField* float_value_field = &hello_struct_def->fields[1];
	StructField* inner_field = &hello_struct_def->fields[2];
	
	assert(int_value_field->type.kind == TYPE_INT);
	assert(str_equal(int_value_field->name, STR_LIT("int_value")));

	assert(float_value_field->type.kind == TYPE_FLOAT);
	assert(str_equal(float_value_field->name, STR_LIT("float_value")));

	// Check InnerStruct
	assert(inner_field->type.kind == TYPE_STRUCT);

	Struct* inner_struct_def = inner_field->type.struct_def;
	assert(str_equal(inner_struct_def->name, STR_LIT("InnerStruct")));

	StructField* inner_value_field = inner_struct_def->fields;
	assert(inner_value_field->type.kind == TYPE_INT);
	assert(str_equal(inner_value_field->name, STR_LIT("inner_value")));
}

void test_aliased_type_resolution(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("typedef int int32;\n"
				"int32 number;"), &ast);

	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 2);

	AstNode* type_def_node = ast.root_nodes.first;
	AstNode* var_node = type_def_node->next;

	assert(type_def_node->kind == AST_NODE_TYPE_DEF);
	assert(var_node->kind == AST_NODE_VARIABLE);

	assert(var_node->variable.type.alias_definition == type_def_node->type_def->first_variant);
}

void test_parse_enum_def(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("enum Type {\n"
				"	TYPE_INT,\n"
				"	TYPE_FLOAT\n"
				"};"), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	AstNode* first = ast.root_nodes.first;
	assert(first->kind == AST_NODE_ENUM);

	Enum* enum_def = first->enum_def;
	assert(enum_def != NULL);
	assert(str_equal(enum_def->name, STR_LIT("Type")));
	assert(enum_def->variant_count == 2);

	EnumVariant* first_variant = &enum_def->variants[0];
	EnumVariant* second_variant = &enum_def->variants[1];

	assert(str_equal(first_variant->name, STR_LIT("TYPE_INT")));
	assert(str_equal(second_variant->name, STR_LIT("TYPE_FLOAT")));
}

void test_parse_function_def(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("void func(int a, int b);"), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	AstNode* first = ast.root_nodes.first;
	assert(first->kind == AST_NODE_FUNCTION_DECL);

	Function* function_def = first->function_def;
	assert(str_equal(function_def->proto.name, STR_LIT("func")));

	Type* return_type = &function_def->proto.return_type;
	assert(return_type->kind == TYPE_VOID);

	assert(function_def->proto.parameter_count == 2);
	const FunctionParam* first_param = &function_def->proto.parameters[0];
	const FunctionParam* second_param = &function_def->proto.parameters[1];

	assert(str_equal(first_param->name, STR_LIT("a")));
	assert(first_param->type.kind == TYPE_INT);

	assert(str_equal(second_param->name, STR_LIT("b")));
	assert(second_param->type.kind == TYPE_INT);
}

void test_parse_forward_declared_struct(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("struct Hello;"), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	AstNode* first_def = ast.root_nodes.first;
	assert(first_def->kind == AST_NODE_STRUCT);

	assert(first_def->struct_def->is_forward_declared);
}

void test_parse_forward_declared_struct_followed_by_definition(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("struct Hello; struct Hello {};"), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 2);

	AstNode* first_def = ast.root_nodes.first;
	assert(first_def->kind == AST_NODE_STRUCT);
	assert(first_def->next != NULL);

	AstNode* second_def = first_def->next;
	assert(second_def->kind == AST_NODE_STRUCT);

	assert(!first_def->struct_def->is_forward_declared);
	assert(first_def->struct_def == second_def->struct_def);
}

void test_parse_forward_declared_enum(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("enum Hello;"), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	AstNode* first_def = ast.root_nodes.first;
	assert(first_def->kind == AST_NODE_ENUM);

	assert(first_def->enum_def->is_forward_declared);
}

void test_parse_forward_declared_enum_followed_by_definition(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("enum Hello; enum Hello {};"), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 2);

	AstNode* first_def = ast.root_nodes.first;
	assert(first_def->kind == AST_NODE_ENUM);
	assert(first_def->next != NULL);

	AstNode* second_def = first_def->next;
	assert(second_def->kind == AST_NODE_ENUM);

	assert(!first_def->enum_def->is_forward_declared);
	assert(first_def->enum_def == second_def->enum_def);
}

void test_parse_forward_declared_function(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("int add(int a, int b);"), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	AstNode* first_def = ast.root_nodes.first;
	assert(first_def->kind == AST_NODE_FUNCTION_DECL);
	assert(first_def->function_def->is_forward_declared);
	assert(first_def->function_def->body == NULL);
}

void test_parse_forward_declared_function_followed_by_definition(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context,
			&diagnostics,
			&source_storage,
			STR_LIT(
				"int add(int a, int b);\n"
				"int add(int a, int b) {}"),
			&ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 2);

	AstNode* first_def = ast.root_nodes.first;
	assert(first_def->kind == AST_NODE_FUNCTION_DECL);
	assert(first_def->next != NULL);

	AstNode* second_def = first_def->next;
	assert(second_def->kind == AST_NODE_FUNCTION_DEF);

	assert(!first_def->function_def->is_forward_declared);
	assert(first_def->function_def == second_def->function_def);
	assert(first_def->function_def->body != NULL);
}

void test_parse_function_ref_expr(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage,
			STR_LIT(
				"void add(int a, int b);"
				"void add(int a, int b) { add; }"), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 2);

	AstNode* first_def = ast.root_nodes.first;
	assert(first_def->kind == AST_NODE_FUNCTION_DECL);
	assert(first_def->function_def->body != NULL);
	assert(first_def->function_def->body->nodes.count == 1);

	AstNode* body_node = first_def->function_def->body->nodes.first;
	assert(body_node->kind == AST_NODE_EXPR);
	assert(body_node->expr.kind == EXPR_FUNCTION_REFERENCE);
	assert(body_node->expr.function_ref.func == first_def->function_def);
}

void test_parse_primitive_integer_types(TestContext* context) {
	StringBuilder builder = { .arena = context->temp_arena };

	TypeKind type_kinds[] = {
		TYPE_CHAR,
		TYPE_INT,
		TYPE_SHORT,
		TYPE_LONG,
		TYPE_LONG_LONG
	};

	String type_kind_names[] = {
		STR_LIT("char"),
		STR_LIT("int"),
		STR_LIT("short"),
		STR_LIT("long"),
		STR_LIT("long long"),
	};

	TypeKindFlags type_flags[] = {
		TYPE_FLAG_NONE,
		TYPE_FLAG_SIGNED,
		TYPE_FLAG_UNSIGNED,
	};

	String type_flag_names[] = {
		STR_LIT(""),
		STR_LIT("signed"),
		STR_LIT("unsigned")
	};

	str_builder_append(&builder, STR_LIT("struct Types {\n"));

	size_t field_index = 0;
	for (size_t flag_index = 0; flag_index < array_size(type_flags); flag_index += 1) {
		for (size_t type_index = 0; type_index < array_size(type_kinds); type_index += 1) {
			str_builder_append(&builder, type_flag_names[flag_index]);
			str_builder_append_char(&builder, ' ');
			str_builder_append(&builder, type_kind_names[type_index]);
			str_builder_append_char(&builder, ' ');
			str_builder_append(&builder, STR_LIT("field"));
			str_builder_append_int(&builder, field_index);
			str_builder_append(&builder, STR_LIT(";\n"));

			field_index += 1;
		}
	}

	str_builder_append(&builder, STR_LIT("};\n"));

	printf("%.*s\n", STR_FMT(builder.string));

	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, builder.string, &ast);

	AstNode* first_def = ast.root_nodes.first;
	assert(first_def->kind == AST_NODE_STRUCT);

	Struct* struct_def = first_def->struct_def;
	assert(struct_def->field_count == field_index);

	{
		size_t field_index = 0;
		for (size_t flag_index = 0; flag_index < array_size(type_flags); flag_index += 1) {
			for (size_t type_index = 0; type_index < array_size(type_kinds); type_index += 1) {
				const StructField* field = &struct_def->fields[field_index];
				TypeKind type_kind = type_kinds[type_index] | (TypeKind)type_flags[flag_index];

				assert(field->type.kind == type_kind);

				field_index += 1;
			}
		}
	}
}

void test_parse_variable_declaration(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("int a;"), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	AstNode* first_def = ast.root_nodes.first;
	assert(first_def->kind == AST_NODE_VARIABLE);

	Variable* variable = &first_def->variable;
	assert(str_equal(variable->name, STR_LIT("a")));
	assert(variable->value == NULL);
	assert(variable->type.kind == TYPE_INT);
}

void test_parse_simple_bin_expr(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("0xff + 10;"), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	assert(ast.root_nodes.first->kind == AST_NODE_EXPR);
	Expr* expr = &ast.root_nodes.first->expr;
	assert(expr->kind == EXPR_BINARY);
	assert(expr->binary.op == BIN_OP_ADD);

	Expr* left = expr->binary.left;
	Expr* right = expr->binary.right;

	assert(left->kind == EXPR_INTEGER_LITERAL);
	assert(left->int_literal.integer_type == TYPE_INT);
	assert(left->int_literal.format == INT_LIT_FMT_HEX);
	assert(left->int_literal.value == 255);

	assert(right ->kind == EXPR_INTEGER_LITERAL);
	assert(right->int_literal.integer_type == TYPE_INT);
	assert(right->int_literal.format == INT_LIT_FMT_DECIMAL);
	assert(right->int_literal.value == 10);
}

void test_bin_op_precedence(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("0xff + 10 * 99 + 02;"), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	assert(ast.root_nodes.first->kind == AST_NODE_EXPR);

	Expr* expr = &ast.root_nodes.first->expr;
	assert(expr->kind == EXPR_BINARY);

	assert(expr->binary.left->kind == EXPR_INTEGER_LITERAL);
	assert(expr->binary.left->int_literal.value == 255);

	assert(expr->binary.right->kind == EXPR_BINARY);

	Expr* inner_expr = expr->binary.right;
	assert(inner_expr->kind == EXPR_BINARY);
	assert(inner_expr->binary.right->kind == EXPR_INTEGER_LITERAL);
	assert(inner_expr->binary.right->int_literal.value == 2);

	Expr* product_expr = inner_expr->binary.left;
	assert(product_expr->binary.left->kind == EXPR_INTEGER_LITERAL);
	assert(product_expr->binary.right->kind == EXPR_INTEGER_LITERAL);
}

void test_parse_variable_ref_expr(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("int a; a + a;"), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 2);

	AstNode* first_node = ast.root_nodes.first;
	AstNode* second_node = first_node->next;

	assert(first_node->kind == AST_NODE_VARIABLE);
	assert(second_node->kind == AST_NODE_EXPR);

	Variable* variable = &first_node->variable;

	Expr* expr = &second_node->expr;
	assert(expr->kind == EXPR_BINARY);

	assert(expr->binary.left->kind == EXPR_VARIABLE_REFERENCE);
	assert(expr->binary.left->variable_ref.var == variable);
	assert(expr->binary.right->kind == EXPR_VARIABLE_REFERENCE);
	assert(expr->binary.right->variable_ref.var == variable);
}

void test_parse_return_stmt(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("int main() { return 0; }"), &ast);

	diagnostics_print(&diagnostics);
	assert(ast.root_nodes.count == 1);

	AstNode* node = ast.root_nodes.first;
	assert(node->kind == AST_NODE_FUNCTION_DEF);

	Function* function = node->function_def;
	Scope* body = function->body;
	assert(body->nodes.count == 1);

	AstNode* return_node = body->nodes.first;
	assert(return_node->kind == AST_NODE_RETURN);
	assert(return_node->return_stmt.value);
}

void test_parse_return_stmt_without_value(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("void main() { return; }"), &ast);

	diagnostics_print(&diagnostics);
	assert(ast.root_nodes.count == 1);

	AstNode* node = ast.root_nodes.first;
	assert(node->kind == AST_NODE_FUNCTION_DEF);

	Function* function = node->function_def;
	Scope* body = function->body;
	assert(body->nodes.count == 1);

	AstNode* return_node = body->nodes.first;
	assert(return_node->kind == AST_NODE_RETURN);
	assert(return_node->return_stmt.value == NULL);
}

void test_multi_part_string_merging(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("const char* s = \"hello\" \"world\";"), &ast);

	diagnostics_print(&diagnostics);
	assert(ast.root_nodes.count == 1);

	AstNode* node = ast.root_nodes.first;
	assert(node->kind == AST_NODE_VARIABLE);

	Variable* var = &node->variable;
	assert(var->value != NULL);
	assert(var->value->kind == EXPR_STRING_LITERAL);
	assert(str_equal(var->value->string_literal.full_string, STR_LIT("helloworld")));
}

void test_parse_expr_inside_parens(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("int a = ((10) + 1);"), &ast);

	diagnostics_print(&diagnostics);
	assert(ast.root_nodes.count == 1);

	AstNode* node = ast.root_nodes.first;
	assert(node->kind == AST_NODE_VARIABLE);

	Variable* var = &node->variable;
	assert(var->value != NULL);
	assert(var->value->kind == EXPR_BINARY);

	Expr* bin_expr = var->value;
	assert(bin_expr->binary.left->kind == EXPR_INTEGER_LITERAL);
	assert(bin_expr->binary.left->int_literal.value == 10);

	assert(bin_expr->binary.right->kind == EXPR_INTEGER_LITERAL);
	assert(bin_expr->binary.right->int_literal.value == 1);
}

void test_allow_variable_shadowing_in_nested_blocks(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT(
				"int a = 1;"
				"{"
				"	int a = 10;"
				"}"
				), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 2);

	AstNode* node = ast.root_nodes.first;
	assert(node->kind == AST_NODE_VARIABLE);

	Variable* var = &node->variable;
	assert(var->value != NULL);
	assert(var->value->kind == EXPR_INTEGER_LITERAL);
	assert(var->value->int_literal.value == 1);

	AstNode* block_node = node->next;
	assert(block_node->block.nodes.count == 1);

	AstNode* var2_node = block_node->block.nodes.first;
	assert(var2_node->kind == AST_NODE_VARIABLE);

	Variable* var2 = &var2_node->variable;
	assert(var2->value != NULL);
	assert(var2->value->kind == EXPR_INTEGER_LITERAL);
	assert(var2->value->int_literal.value == 10);
}

void test_allow_variable_shadowing_in_if_statements(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT(
				"int a = 1;"
				"if (1)"
				"	int a = 10;"
				), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 2);

	AstNode* node = ast.root_nodes.first;
	assert(node->kind == AST_NODE_VARIABLE);

	Variable* var = &node->variable;
	assert(var->value != NULL);
	assert(var->value->kind == EXPR_INTEGER_LITERAL);
	assert(var->value->int_literal.value == 1);

	AstNode* if_stmt_node = node->next;
	assert(if_stmt_node->kind == AST_NODE_IF);
	assert(if_stmt_node->if_stmt.true_scope != NULL);

	AstNode* var2_node = if_stmt_node->if_stmt.true_scope->nodes.first;
	assert(var2_node->kind == AST_NODE_VARIABLE);

	Variable* var2 = &var2_node->variable;
	assert(var2->value != NULL);
	assert(var2->value->kind == EXPR_INTEGER_LITERAL);
	assert(var2->value->int_literal.value == 10);
}

void test_allow_variable_shadowing_in_else_branch_if_statements(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT(
				"int a = 1;"
				"if (1) {}"
				"else"
				"	int a = 10;"
				), &ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 2);

	AstNode* node = ast.root_nodes.first;
	assert(node->kind == AST_NODE_VARIABLE);

	Variable* var = &node->variable;
	assert(var->value != NULL);
	assert(var->value->kind == EXPR_INTEGER_LITERAL);
	assert(var->value->int_literal.value == 1);

	AstNode* if_stmt_node = node->next;
	assert(if_stmt_node->kind == AST_NODE_IF);
	assert(if_stmt_node->if_stmt.true_scope != NULL);
	assert(if_stmt_node->if_stmt.false_scope != NULL);

	AstNode* var2_node = if_stmt_node->if_stmt.false_scope->nodes.first;
	assert(var2_node->kind == AST_NODE_VARIABLE);

	Variable* var2 = &var2_node->variable;
	assert(var2->value != NULL);
	assert(var2->value->kind == EXPR_INTEGER_LITERAL);
	assert(var2->value->int_literal.value == 10);
}

void test_parse_recursive_function(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context,
			&diagnostics,
			&source_storage,
			STR_LIT("void main() { main(); }"),
			&ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	AstNode* node = ast.root_nodes.first;
	assert(node->kind == AST_NODE_FUNCTION_DEF);

	assert(node->function_def->body);
	const Function* func = node->function_def;
	
	assert(func->body->nodes.count == 1);
	const AstNode* call = func->body->nodes.first;
	assert(call->kind == AST_NODE_EXPR);
	assert(call->expr.kind == EXPR_CALL);

	const Expr* callable = call->expr.call.callable;
	assert(callable->kind == EXPR_FUNCTION_REFERENCE);
	assert(callable->function_ref.func == func);
}

void test_parse_function_param_in_expr(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context,
			&diagnostics,
			&source_storage,
			STR_LIT("void main(int argc) { argc; }"),
			&ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	AstNode* node = ast.root_nodes.first;
	assert(node->kind == AST_NODE_FUNCTION_DEF);

	assert(node->function_def->body);
	const Function* func = node->function_def;
	
	assert(func->body->nodes.count == 1);
	const AstNode* expr = func->body->nodes.first;
	assert(expr->kind == AST_NODE_EXPR);
	assert(expr->expr.kind == EXPR_FUNCTION_PARAM);

	assert(expr->expr.function_param.function_def == func);
	assert(expr->expr.function_param.param_index == 0);
}

void test_register_unnamed_function_param(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context,
			&diagnostics,
			&source_storage,
			STR_LIT("void main(int) {}"),
			&ast);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);
}

static void _run_anonymous_type_declaration_sub_test(TestContext* context, String source_code) {
	SourceStorage source_storage;

	source_storage_init(&source_storage, (StringArray) {}, context->arena);
	SourceFile* source_file = source_storage_append(&source_storage, STR_LIT(DEFAULT_SOURCE_PATH), source_code);

	Diagnostics diagnostics = (Diagnostics) {
		.allocator = context->temp_arena,
		.error_limit = 128,
	};

	Arena generated_tokens_arena = arena_alloc_sub_arena(context->arena, 2 * 4096);

	Preprocessor preprocessor = {};
	preprocessor_init(&preprocessor,
			&source_storage,
			source_file,
			&diagnostics,
			heap_allocator_new(),
			context->arena,
			context->temp_arena,
			&generated_tokens_arena);

	IdentifierStorage ident_storage;

	// NOTE: This arena is used by the `IdentifierStorage` inside the parser,
	//       so it's lifetime is no longer than the one of the parser.
	Arena ident_arena = arena_alloc_sub_arena(context->arena, 16 * 1024);
	Arena ast_arena = arena_alloc_sub_arena(context->arena, 16 * 1024);

	ident_storage_init(&ident_storage, arena_allocator_new(&ident_arena), &ident_arena);

	TypeContext type_context = {
		.pointer_type_layout = {
			.size = sizeof(void*),
			.alignment = alignof(void*)
		}
	};

	Parser parser = {};
	parser_init(&parser,
			&ast_arena,
			context->temp_arena,
			&ident_storage,
			&type_context,
			&preprocessor,
			&diagnostics);

	AST ast;
	parser_parse(&parser, &ast);

	preprocessor_release(&preprocessor);

	// WARN: Work's under the assuption that the root scope of `ident_storage`
	//       isn't cleared after finishing the parshing.
	IdentifierEntry* entry = ident_storage_find(&ident_storage,
			IDENT_NAMESPACE_TAGGED,
			IDENT_FIND_IN_ALL_PARENT_SCOPES,
			STR_LIT("Anonymous"));

	assert(entry == NULL);
}

void test_inner_struct_decl_is_anonymous(TestContext* context) {
	_run_anonymous_type_declaration_sub_test(context,
			STR_LIT("struct Hello { struct Anonymous {} inner; };"));
}

void test_inner_enum_decl_is_anonymous(TestContext* context) {
	_run_anonymous_type_declaration_sub_test(context,
			STR_LIT("struct Outer { enum Anonymous {} inner; };"));
}

void test_map_current_struct_fields(TestContext* context) {
	String source_code = STR_LIT("struct Struct {\n"
			"	int value0;\n"
			"	int value1;\n"
			"	float value2;\n"
			"	float;\n"
			"};");

	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	IdentifierStorage* ident_storage;
	run_parser_test_2(context,
			&diagnostics,
			&source_storage,
			source_code,
			&ast,
			&ident_storage);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	IdentifierEntry* entry = ident_storage_find(ident_storage,
			IDENT_NAMESPACE_TAGGED,
			IDENT_FIND_IN_ALL_PARENT_SCOPES,
			STR_LIT("Struct"));

	assert(entry != NULL);

	const Struct* struct_def = entry->struct_def;
	assert(entry != NULL);

	const StructFieldNamespace* fields = struct_def->field_namespace;
	assert(fields != NULL);
	assert(fields->size == 3);

	String expected_fields[] = {
		STR_LIT("value0"),
		STR_LIT("value1"),
		STR_LIT("value2"),
	};

	for (size_t i = 0; i < array_size(expected_fields); i += 1) {
		size_t index = struct_field_namespace_index_of(fields, expected_fields[i]);
		assert(index != SIZE_MAX);

		assert(fields->entries[index].struct_def == struct_def);
		assert(fields->entries[index].field_index == i);
	}
}

void test_map_current_and_inner_anonymous_struct_fields(TestContext* context) {
	String source_code = STR_LIT("struct Struct {\n"
			"	int value0;\n"
			"	struct Inner {\n"
			"		int inner_value0;\n"
			"		struct InnerMost {\n"
			"			float inner_most_value0;\n"
			"		};\n"
			"	};\n"
			"	int value1;\n"
			"	float value2;\n"
			"	float;\n"
			"};");

	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	IdentifierStorage* ident_storage;
	run_parser_test_2(context,
			&diagnostics,
			&source_storage,
			source_code,
			&ast,
			&ident_storage);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	IdentifierEntry* entry = ident_storage_find(ident_storage,
			IDENT_NAMESPACE_TAGGED,
			IDENT_FIND_IN_ALL_PARENT_SCOPES,
			STR_LIT("Struct"));

	assert(entry != NULL);

	const Struct* struct_def = entry->struct_def;
	assert(entry != NULL);

	const StructFieldNamespace* fields = struct_def->field_namespace;
	assert(fields != NULL);
	assert(fields->size == 5);

	String expected_fields[] = {
		STR_LIT("value0"),
		STR_LIT("inner_value0"),
		STR_LIT("inner_most_value0"),
		STR_LIT("value1"),
		STR_LIT("value2"),
	};

	for (size_t i = 0; i < array_size(expected_fields); i += 1) {
		size_t index = struct_field_namespace_index_of(fields, expected_fields[i]);
		assert(index != SIZE_MAX);
	}
}

void test_fields_not_mapped_for_struct_defined_inline_with_the_named_field(TestContext* context) {
	String source_code = STR_LIT("struct Struct {\n"
			"	int value0;\n"
			"	struct Inner {\n"
			"		int inner_value0;\n"
			"		struct InnerMost {\n"
			"			float inner_most_value0;\n"
			"		} inner;\n"
			"	};\n"
			"	int value1;\n"
			"	float value2;\n"
			"	float;\n"
			"};");

	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	IdentifierStorage* ident_storage;
	run_parser_test_2(context,
			&diagnostics,
			&source_storage,
			source_code,
			&ast,
			&ident_storage);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	IdentifierEntry* entry = ident_storage_find(ident_storage,
			IDENT_NAMESPACE_TAGGED,
			IDENT_FIND_IN_ALL_PARENT_SCOPES,
			STR_LIT("Struct"));

	assert(entry != NULL);

	const Struct* struct_def = entry->struct_def;
	assert(entry != NULL);

	const StructFieldNamespace* fields = struct_def->field_namespace;
	assert(fields != NULL);
	assert(fields->size == 5);

	String expected_fields[] = {
		STR_LIT("value0"),
		STR_LIT("inner_value0"),
		STR_LIT("inner"),
		STR_LIT("value1"),
		STR_LIT("value2"),
	};

	for (size_t i = 0; i < array_size(expected_fields); i += 1) {
		size_t index = struct_field_namespace_index_of(fields, expected_fields[i]);
		assert(index != SIZE_MAX);
	}

	String unexpected_fields[] = {
		STR_LIT("inner_most_value0"),
	};

	for (size_t i = 0; i < array_size(unexpected_fields); i += 1) {
		size_t index = struct_field_namespace_index_of(fields, unexpected_fields[i]);
		assert(index == SIZE_MAX);
	}
}

void test_parse_union_def(TestContext* context) {
	String source_code = STR_LIT("union Union { int i; float f; };");

	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	IdentifierStorage* ident_storage;
	run_parser_test_2(context,
			&diagnostics,
			&source_storage,
			source_code,
			&ast,
			&ident_storage);

	diagnostics_print(&diagnostics);
	assert(diagnostics.first == NULL);
	assert(ast.root_nodes.count == 1);

	IdentifierEntry* entry = ident_storage_find(ident_storage,
			IDENT_NAMESPACE_TAGGED,
			IDENT_FIND_IN_ALL_PARENT_SCOPES,
			STR_LIT("Union"));

	assert(entry != NULL);

	assert(entry->kind == IDENT_UNION);

	const Struct* union_def = entry->union_def;
	assert(union_def->layout_kind == STRUCT_LAYOUT_KIND_UNION);
}

inline Token _create_string_token(String string, const SourceFile* file) {
	String source_code = file->source_code;
	assert(string.v >= source_code.v);
	assert(string.v + string.length <= source_code.v + source_code.length);

	size_t range_start = (size_t)(string.v - source_code.v);

	return (Token) { .kind = TOKEN_STRING,
		.string = string,
		.source_range = (SourceRange) {
			.source_file = file,
			.start = range_start,
			.end = range_start + string.length,
		},
	};
}

void test_parse_int_literal_sufixes(TestContext* context) {
	IntegerLiteralSufixKind std_sufix_kinds[] = {
		INT_SUFIX_U,
		INT_SUFIX_L,
		INT_SUFIX_UL,
		INT_SUFIX_LL,
		INT_SUFIX_ULL,
	};

	String std_sufixes[] = {
		STR_LIT("u"),
		STR_LIT("l"),
		STR_LIT("ul"),
		STR_LIT("ll"),
		STR_LIT("ull"),
	};

	String int_lit = STR_LIT("100");
	uint64_t int_lit_value = 100;
	for (size_t i = 0; i < array_size(std_sufix_kinds); i += 1) {
		ArenaRegion temp = arena_begin_temp(context->temp_arena);
		Diagnostics diagnostics = { .allocator = context->temp_arena };

		StringBuilder builder = { .arena = context->temp_arena };
		str_builder_append(&builder, int_lit);
		str_builder_append(&builder, std_sufixes[i]);

		SourceFile source_file = (SourceFile) {
			.path = STR_LIT(DEFAULT_SOURCE_PATH),
			.source_code = builder.string,
			.line_info = line_info_from_source(context->temp_arena, builder.string),
		};

		IntLiteral literal = {};
		bool parsed = parse_int_literal(
				_create_string_token(builder.string, &source_file), 
				&diagnostics,
				&literal);

		assert(parsed);

		assert(literal.has_sufix);
		assert(literal.sufix_bit_count == 0);
		assert(literal.sufix_kind == std_sufix_kinds[i]);
		assert(literal.value == int_lit_value);
		assert(diagnostics.first == NULL);

		arena_end_temp(temp);
	}
}

void test_parse_int_literal_sufixes_with_bit_count(TestContext* context) {
	size_t bit_count[] = { 8, 16, 32, 64 };
	IntegerLiteralSufixKind std_sufix_kinds[] = {
		INT_SUFIX_NONE,
		INT_SUFIX_U,
	};

	String std_sufixes[] = {
		STR_LIT(""),
		STR_LIT("u"),
	};

	String int_lit = STR_LIT("100");

	for (size_t i = 0; i < array_size(std_sufix_kinds); i += 1) {
		for (size_t j = 0; j < array_size(bit_count); j += 1) {
			ArenaRegion temp = arena_begin_temp(context->temp_arena);
			Diagnostics diagnostics = { .allocator = context->temp_arena };

			StringBuilder builder = { .arena = context->temp_arena };
			str_builder_append(&builder, int_lit);
			str_builder_append(&builder, std_sufixes[i]);
			str_builder_append_char(&builder, 'i');
			str_builder_append_int(&builder, bit_count[j]);

			SourceFile source_file = (SourceFile) {
				.path = STR_LIT(DEFAULT_SOURCE_PATH),
				.source_code = builder.string,
				.line_info = line_info_from_source(context->temp_arena, builder.string),
			};

			IntLiteral literal = {};
			bool parsed = parse_int_literal(
					_create_string_token(builder.string, &source_file), 
					&diagnostics,
					&literal);

			assert(parsed);

			assert(literal.has_sufix);
			assert(literal.sufix_bit_count == bit_count[j]);
			assert(literal.sufix_kind == std_sufix_kinds[i]);
			assert(diagnostics.first == NULL);

			arena_end_temp(temp);
		}
	}
}

void test_invalid_int_literal_sufixes(TestContext* context) {
	String std_sufixes[] = {
		STR_LIT("l"),
		STR_LIT("ul"),
		STR_LIT("ll"),
		STR_LIT("ull"),
	};

	size_t bit_count[] = { 8, 16, 32, 64 };

	String int_lit = STR_LIT("100");
	for (size_t i = 0; i < array_size(std_sufixes); i += 1) {
		for (size_t j = 0; j < array_size(bit_count); j += 1) {
			ArenaRegion temp = arena_begin_temp(context->temp_arena);

			StringBuilder builder = { .arena = context->temp_arena };
			str_builder_append(&builder, int_lit);
			str_builder_append(&builder, std_sufixes[i]);
			str_builder_append_char(&builder, 'i');
			str_builder_append_int(&builder, bit_count[j]);

			SourceFile source_file = (SourceFile) {
				.path = STR_LIT(DEFAULT_SOURCE_PATH),
				.source_code = builder.string,
				.line_info = line_info_from_source(context->temp_arena, builder.string),
			};

			SourceStorage source_storage = { .files = &source_file, .count = 1 };
			Diagnostics diagnostics = {
				.allocator = context->temp_arena, 
				.source_storage = &source_storage,
				.error_limit = 128,
			};

			IntLiteral literal = {};
			bool parsed = parse_int_literal(
					_create_string_token(builder.string, &source_file), 
					&diagnostics,
					&literal);

			assert(!parsed);

			assert(!literal.has_sufix);
			assert(literal.sufix_bit_count == 0);
			assert(literal.sufix_kind == INT_SUFIX_NONE);
			assert(diagnostics.first != NULL);

			SourceRange sufix_range = diagnostics.first->highlighted_ranges[0];
			assert(sufix_range.start == int_lit.length);
			assert(sufix_range.end == builder.string.length);

			arena_end_temp(temp);
		}
	}
}

void test_parse_type_of_int_literal_with_sufix(TestContext* context) {
	String sub_tests[] = {
		STR_LIT("i8;"),
		STR_LIT("i16;"),
		STR_LIT("i32;"),
		STR_LIT("i64;"),

		STR_LIT("ui8;"),
		STR_LIT("ui16;"),
		STR_LIT("ui32;"),
		STR_LIT("ui64;"),

		STR_LIT("u;"),
		STR_LIT("l;"),
		STR_LIT("ul;"),
		STR_LIT("ll;"),
		STR_LIT("ull;"),
	};

	TypeKind expected_types[] = {
		TYPE_INT8,
		TYPE_INT16,
		TYPE_INT32,
		TYPE_INT64,

		TYPE_UNSIGNED_INT8,
		TYPE_UNSIGNED_INT16,
		TYPE_UNSIGNED_INT32,
		TYPE_UNSIGNED_INT64,

		TYPE_UNSIGNED_INT,
		TYPE_LONG,
		TYPE_UNSIGNED_LONG,
		TYPE_LONG_LONG,
		TYPE_UNSIGNED_LONG_LONG,
	};

	static_assert(array_size(sub_tests) == array_size(expected_types),
			"Number of expected results doesn't match the number of sub tests");

	uint64_t test_ints[] = { 0, 1 };
	String test_ints_as_str[] = {
		STR_LIT("0"),
		STR_LIT("1"),
	};

	for (size_t i = 0; i < array_size(sub_tests); i += 1) {
		for (size_t j = 0; j < array_size(test_ints); j += 1) {
			ArenaRegion temp1 = arena_begin_temp(context->arena);
			ArenaRegion temp2 = arena_begin_temp(context->temp_arena);

			StringBuilder builder = { .arena = context->temp_arena };
			str_builder_append(&builder, test_ints_as_str[j]);
			str_builder_append(&builder, sub_tests[i]);

			Diagnostics diagnostics;
			SourceStorage source_storage;
			AST ast;

			run_parser_test(context, &diagnostics, &source_storage, builder.string, &ast);
			assert(diagnostics.first == NULL);

			assert(ast.root_nodes.count == 1);
			AstNode* first = ast.root_nodes.first;
			assert(first->kind == AST_NODE_EXPR);

			Expr* expr = &first->expr;
			assert(expr->kind == EXPR_INTEGER_LITERAL);
			assert(expr->int_literal.format == INT_LIT_FMT_DECIMAL);
			assert(expr->int_literal.integer_type == expected_types[i]);
			assert(expr->int_literal.value == test_ints[j]);

			arena_end_temp(temp2);
			arena_end_temp(temp1);
		}
	}
}

static void _test_escape_string(Arena* allocator, String string, String expected) {
	ArenaRegion temp = arena_begin_temp(allocator);

	StringBuilder builder = { .arena = allocator };
	parse_escaped_string(&builder, string, NULL, NULL);

	assert(str_equal(builder.string, expected));

	arena_end_temp(temp);
}

void test_simple_escape_sequences(TestContext* context) {
	_test_escape_string(context->arena, STR_LIT("\\'"), STR_LIT("\'"));
	_test_escape_string(context->arena, STR_LIT("\\\""), STR_LIT("\""));
	_test_escape_string(context->arena, STR_LIT("\\\\"), STR_LIT("\\"));
	_test_escape_string(context->arena, STR_LIT("\\?"), STR_LIT("?"));
	_test_escape_string(context->arena, STR_LIT("\\a"), STR_LIT("\a"));
	_test_escape_string(context->arena, STR_LIT("\\b"), STR_LIT("\b"));
	_test_escape_string(context->arena, STR_LIT("\\f"), STR_LIT("\f"));
	_test_escape_string(context->arena, STR_LIT("\\n"), STR_LIT("\n"));
	_test_escape_string(context->arena, STR_LIT("\\r"), STR_LIT("\r"));
	_test_escape_string(context->arena, STR_LIT("\\t"), STR_LIT("\t"));
	_test_escape_string(context->arena, STR_LIT("\\v"), STR_LIT("\v"));
}

void test_invalid_escape_sequences(TestContext* context) {
	for (uint32_t i = 0; i < 256; i += 1) {
		bool skip = false;
		switch (i) {
		case 0:
		case '\\':
		case '\'':
		case '"':
		case '?':
		case 'a':
		case 'b':
		case 'f':
		case 'n':
		case 'r':
		case 't':
		case 'v':
		case 'x':
			skip = true;
			break;
		default:
			if (i >= '0' && i <= '7') {
				skip = true;
			}
		}

		if (skip) {
			continue;
		}

		const char source_buffer[2] = { '\\', (char)i };
		String target_string = (String) { .v = source_buffer, .length = 2 };

		ArenaRegion temp = arena_begin_temp(context->arena);

		SourceFile source_file = (SourceFile) {
			.path = STR_LIT(DEFAULT_SOURCE_PATH),
			.source_code = target_string,
			.line_info = line_info_from_source(context->arena, target_string),
		};

		SourceStorage source_storage = {
			.files = &source_file,
			.count = 1,
		};

		Diagnostics diagnostics = {
			.allocator = context->arena,
			.source_storage = &source_storage,
			.error_limit = 128,
		};

		StringBuilder builder = { .arena = context->arena };
		parse_escaped_string(&builder, target_string, &source_file, &diagnostics);

		assert(diagnostics.first != NULL);
		assert(str_equal(diagnostics.first->message, STR_LIT("Unknown escape sequence")));
		SourceRange range = diagnostics.first->highlighted_ranges[0];
		
		assert(range.start == 0);
		assert(range.end == 2);

		arena_end_temp(temp);
	}
}

void test_octal_escape_sequence(TestContext* context) {
	_test_escape_string(context->arena, STR_LIT("\\0"), STR_LIT("\0"));
	_test_escape_string(context->arena, STR_LIT("\\7"), STR_LIT("\7"));
	_test_escape_string(context->arena, STR_LIT("\\77"), STR_LIT("\77"));
	_test_escape_string(context->arena, STR_LIT("\\377"), STR_LIT("\377"));
	_test_escape_string(context->arena, STR_LIT("\\3777"), STR_LIT("\3777"));
}

void test_hex_escape_sequence(TestContext* context) {
	_test_escape_string(context->arena, STR_LIT("\\x0"), STR_LIT("\x0"));
	_test_escape_string(context->arena, STR_LIT("\\xf"), STR_LIT("\xf"));
	_test_escape_string(context->arena, STR_LIT("\\xff"), STR_LIT("\xff"));
}

void _test_escape_string_fail(Arena* allocator, String string, String expected_error_message) {
	ArenaRegion temp = arena_begin_temp(allocator);

	SourceFile source_file = (SourceFile) {
		.path = STR_LIT(DEFAULT_SOURCE_PATH),
		.source_code = string,
		.line_info = line_info_from_source(allocator, string),
	};

	SourceStorage source_storage = {
		.files = &source_file,
		.count = 1,
	};

	Diagnostics diagnostics = {
		.allocator = allocator,
		.source_storage = &source_storage,
		.error_limit = 128,
	};

	StringBuilder builder = { .arena = allocator };
	parse_escaped_string(&builder, string, &source_file, &diagnostics);

	assert(diagnostics.first != NULL);
	assert(str_equal(diagnostics.first->message, expected_error_message));
	SourceRange range = diagnostics.first->highlighted_ranges[0];
	
	assert(range.start == 0);
	assert(range.end == string.length);

	arena_end_temp(temp);
}

void test_out_of_range_octal_sequence(TestContext* context) {
	_test_escape_string_fail(context->arena,
			STR_LIT("\\777"),
			STR_LIT("Octal escape sequence is out of range"));
}

void test_out_of_range_hex_sequence(TestContext* context) {
	_test_escape_string_fail(context->arena,
			STR_LIT("\\xfff"),
			STR_LIT("Hex escape sequence is out of range"));

	_test_escape_string_fail(context->arena,
			STR_LIT("\\xffffffffa"),
			STR_LIT("Hex escape sequence is out of range"));
}

void test_hex_escape_sequence_without_following_digits_fails(TestContext* context) {
	_test_escape_string_fail(context->arena,
			STR_LIT("\\x"),
			STR_LIT("Used without the following hex digits"));
}

void test_parse_empty_char_fails(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("char a = '';"), &ast);

	assert(diagnostics.first != NULL);
	assert(str_equal(diagnostics.first->message, STR_LIT("Empty character constant")));
}

void test_parse_char_with_escape_sequence(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("char a = '\\n';"), &ast);


	AstNode* var_node = ast.root_nodes.first;
	assert(var_node->kind == AST_NODE_VARIABLE);
	Expr* value = var_node->variable.value;
	assert(value->kind == EXPR_CHAR_LITERAL);
	assert(value->char_literal.value == '\n');
}

void test_parse_char_const_with_escape_sequence_and_a_following_char_is_tool_long(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("char a = '\\nh';"), &ast);

	assert(diagnostics.first != NULL);
	assert(str_equal(diagnostics.first->message, STR_LIT("Character constant is too long")));
}

void test_parse_char_const_with_multiple_chars_is_tool_long(TestContext* context) {
	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, STR_LIT("char a = 'hello world';"), &ast);

	assert(diagnostics.first != NULL);
	assert(str_equal(diagnostics.first->message, STR_LIT("Character constant is too long")));
}

void test_unary_op_requires_l_value_error(TestContext* context) {
	String source = STR_LIT("0++;\n"
			"0--;\n"
			"++0;\n"
			"--0;\n");

	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, source, &ast);

	DiagnosticsEntry* error = diagnostics.first;
	for (size_t i = 0; i < 4; i += 1) {
		assert(error);

		assert(str_equal(error->message, STR_LIT("Expected an l-value")));
		assert(error->start_line == (uint32_t)(i));
		assert(error->end_line == (uint32_t)(i));

		error = error->next;
	}
}

void test_unary_op_requires_int_type(TestContext* context) {
	String source = STR_LIT("typedef struct {} A;\n"
			"A a;\n"
			"a++;\n"
			"a--;\n"
			"++a;\n"
			"--a;\n");

	SourceStorage source_storage;
	Diagnostics diagnostics;
	AST ast;
	run_parser_test(context, &diagnostics, &source_storage, source, &ast);

	String error_messages[4] = {
		STR_LIT("Cannot apply '++' to an operand of type 'A'"),
		STR_LIT("Cannot apply '--' to an operand of type 'A'"),
		STR_LIT("Cannot apply '++' to an operand of type 'A'"),
		STR_LIT("Cannot apply '--' to an operand of type 'A'"),
	};

	DiagnosticsEntry* error = diagnostics.first;
	for (size_t i = 0; i < 4; i += 1) {
		assert(error);

		assert(str_equal(error->message, error_messages[i]));
		assert(error->start_line == (uint32_t)(i + 2));
		assert(error->end_line == (uint32_t)(i + 2));

		error = error->next;
	}
}
