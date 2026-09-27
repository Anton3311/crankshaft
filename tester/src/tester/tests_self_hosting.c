#include "tests_self_hosting.h"

#include "parser/tokenizer.h"

void test_self_hosted_tokenizer(TestContext* context, String test_file_path) {
	String args = str_format(context->temp_arena, "bin\\c.exe "
				".\\tester\\src\\tester\\self_hosting\\self_hosted_tokenizer_driver.c "
				"-Istdx -Icore/src/ -Iparser/src .\\parser\\src\\parser\\tokenizer.c "
				"--no-report-exit-code -- c.exe %.*s", STR_FMT(test_file_path));

	String output;
	int32_t exit_code;
	ProcessRunResult process_result = process_capture_stdout(
			STR_LIT("bin\\c.exe"),
			STR_LIT("./"),
			args,
			&exit_code,
			&output,
			context->arena,
			context->temp_arena);

	if (process_result != PROCESS_RUN_OK || exit_code != 0) {
		printf("%.*s\n", STR_FMT(output));
		panic("Failed to compile");
	}

	String source_code = read_entire_file_to_str(
			str_to_cstr(test_file_path, context->temp_arena),
			context->temp_arena);

	Tokenizer tokenizer = { .source_code = source_code };

	LineIterator iterator = { output };
	String actual_line;

	while (line_iterator_next(&iterator, &actual_line)) {
		ArenaRegion temp = arena_begin_temp(context->temp_arena);

		Token expected_token = tokenizer_next_token(&tokenizer);
		String expected_line = str_format(context->temp_arena,
				"%u %zu %zu %.*s",
				expected_token.kind,
				expected_token.source_range.start,
				expected_token.source_range.end,
				STR_FMT(expected_token.string));

		if (!str_equal(actual_line, expected_line)) {
			printf("expected: %.*s\n", STR_FMT(expected_line));
			printf("  actual: %.*s\n", STR_FMT(actual_line));
			crash();
		}
		
		arena_end_temp(temp);
	}
}
