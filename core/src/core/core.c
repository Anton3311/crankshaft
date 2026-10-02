#include "core.h"

#include <stdlib.h>
#include <stdio.h>

#include <windows.h>
#include <dbghelp.h>
#include <shlwapi.h> 
#include <pathcch.h>
#include <threads.h>

bool is_debugger_connected() {
	return IsDebuggerPresent();
}

HANDLE _duplicate_process_handle(HANDLE process_handle)
{
	HANDLE process = NULL;
	if (!DuplicateHandle(process_handle, process_handle, process_handle, &process, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
		return NULL;
	}

	return process;
}

bool try_print_stack_trace(size_t skipped_frame_count) {
	// NOTE: Call stack reporting is disabled when running with Tracy.
	// 
	// TODO: Tracy also uses `dbghelp`, and using it here would need syncronization since `dbghelp`
	//       is not thread-safe.
#ifndef FEATURE_PROFILER
	HANDLE process_handle = _duplicate_process_handle(GetCurrentProcess());
	if (process_handle == NULL) {
		return false;
	}

	SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);

	if (!SymInitialize(process_handle, NULL, TRUE)) {
		return false;
	}

#define STACK_TRACE_BUFFER_SIZE 64
	void* stack_trace_buffer[STACK_TRACE_BUFFER_SIZE];

#define MAX_NAME_LENGTH 256
	alignas(8) char symbol_buffer[sizeof(SYMBOL_INFO) + (MAX_NAME_LENGTH - 1) * sizeof(TCHAR)];

	size_t frame_count = CaptureStackBackTrace(
			skipped_frame_count,
			STACK_TRACE_BUFFER_SIZE,
			stack_trace_buffer,
			NULL);

	for (size_t i = 0; i < frame_count; i += 1) {
		size_t frame_index = frame_count - 1 - i;
		void* address = stack_trace_buffer[frame_index];

		PSYMBOL_INFO symbol_info = (PSYMBOL_INFO)symbol_buffer;
		symbol_info->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol_info->MaxNameLen = MAX_NAME_LENGTH;

		DWORD64 displacement = 0;
		if (SymFromAddr(process_handle, (DWORD64)address, &displacement, symbol_info)) {
			String symbol_name = (String) { symbol_info->Name, symbol_info->NameLen };
			printf("\t%zu: %p - %.*s\n", frame_index + 1, address, STR_FMT(symbol_name));
		} else {
			printf("\t%zu: %p\n", frame_index + 1, address);
		}
	}

	if (!SymCleanup(process_handle)) {
		// NOTE: SymCleanup failed, but we got the stack trace
	}

#endif
	return true;
}

void print_assertion_stack_trace() {
	bool result = try_print_stack_trace(2); // skip: `print_assertion_stack_trace` and `try_print_stack_trace`

	if (!result) {
		printf("\033[1;31mFailed to capture stack trace\033[0m");
	}
}

typedef struct {
	size_t page_size;
} SystemMemorysSpec;

static SystemMemorysSpec s_sys_mem_spec;

void _query_system_memory_spec() {
	if (s_sys_mem_spec.page_size != 0) {
		return;
	}

	SYSTEM_INFO sys_info = {};
	GetSystemInfo(&sys_info);

	s_sys_mem_spec.page_size = (size_t)sys_info.dwPageSize;
}

uint64_t hardware_timer_get_frequency() {
	LARGE_INTEGER freq = {};
	QueryPerformanceFrequency(&freq);
	return freq.QuadPart;
}

//
// Profiler
//

#ifdef FEATURE_PROFILER
void profiler_wait_for_connection() {
	while (!TracyCIsConnected) {
		thrd_yield();
	}
}
#endif

//
// Allocator
//

static void* _heap_allocator_procedure(void* allocator_data,
		void* ptr,
		size_t size,
		size_t alignment,
		AllocatorOperation op) {

	switch (op) {
	case ALLOC_OP_ALLOC:
		assert_msg(alignment <= HEAP_ALLOCATOR_MAX_ALIGN, "Cannot guarantee given alignment");
		return malloc(size);
	case ALLOC_OP_FREE:
		free(ptr);
		return NULL;
	}

	unreachable();
	return NULL;
}

Allocator heap_allocator_new() {
	Allocator allocator = {};
	allocator.allocator_data = NULL;
	allocator.procedure = _heap_allocator_procedure;
	return allocator;
}

//
// Panic Allocator
//

static void* _panic_allocator_procedure(void* allocator_data,
		void* ptr,
		size_t size,
		size_t alignment,
		AllocatorOperation op) {
	panic("Panic allocator called");
	return NULL;
}

Allocator panic_allocator_new() {
	Allocator allocator = {};
	allocator.allocator_data = NULL;
	allocator.procedure = _panic_allocator_procedure;
	return allocator;
}

//
// Arena
//

static void* _arena_allocator_procedure(void* allocator_data,
		void* ptr,
		size_t size,
		size_t alignment,
		AllocatorOperation op) {

	Arena* arena = (Arena*)allocator_data;

	switch (op) {
	case ALLOC_OP_ALLOC:
		return arena_alloc_aligned(arena, size, alignment);
	case ALLOC_OP_FREE:
		panic("`ALLOC_OP_FREE` is not supported by the arena allocator");
	}

	unreachable();
	return NULL;
}

Allocator arena_allocator_new(Arena* arena) {
	Allocator allocator = {};
	allocator.allocator_data = arena;
	allocator.procedure = _arena_allocator_procedure;
	return allocator;
}

// NOTE: Assumes the sys mem spec had already been queried
inline size_t _compute_page_count(size_t bytes) {
	return (bytes + s_sys_mem_spec.page_size - 1) / s_sys_mem_spec.page_size;
}

size_t align_to_page_size(size_t bytes) {
	_query_system_memory_spec();
	return align(bytes, s_sys_mem_spec.page_size);
}

void _arena_reserve(Arena* arena) {
	profile_core_func();

	_query_system_memory_spec();

	arena->base = (uint8_t*)VirtualAlloc(NULL,
			(SIZE_T)align(arena->capacity, s_sys_mem_spec.page_size),
			MEM_RESERVE,
			PAGE_READWRITE);

	assert(arena->base != NULL);

	profile_scope_end();
}

void _arena_commit(Arena* arena, size_t size) {
	profile_core_func();

	size_t page_count = _compute_page_count(size);

	size_t commit_size = page_count * s_sys_mem_spec.page_size;
	if (arena->commited + commit_size > arena->capacity) {
		panic("Out of arena memory");
	}

	void* result = VirtualAlloc(arena->base + arena->commited,
			(SIZE_T)commit_size,
			MEM_COMMIT,
			PAGE_READWRITE);

	assert(result != NULL);

	asan_poison_memory_region(arena->base + arena->commited, commit_size);

	arena->commited += commit_size;

	profile_scope_end();
}

void arena_release(Arena* arena) {
	profile_core_func();
	if (arena->base == NULL) {
		profile_scope_end();
		return;
	}

	BOOL result = VirtualFree(arena->base, 0, MEM_RELEASE);
	assert_msg(result, "Failed to free arena memory");

	arena->base = NULL;
	arena->allocated = 0;
	arena->commited = 0;

	profile_scope_end();
}

void* allocate_executable(size_t size) {
	profile_core_func();
	LPVOID ptr = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	profile_scope_end();
	return ptr;
}

void free_executable(void* ptr, size_t size) {
	profile_core_func();
	VirtualFree(ptr, size, MEM_RELEASE);
	profile_scope_end();
}

//
// String
//

WideString str_to_wstr(String string, Arena* allocator, bool include_null_terminator) {
	profile_core_func();

	// Doesn't include the null-terminator
	int32_t required_size = MultiByteToWideChar(CP_UTF8, 0, string.v, string.length, NULL, 0);

	size_t wide_string_length = required_size;
	if (include_null_terminator) {
		wide_string_length += 1;
	}

	wchar_t* wide_string = arena_alloc_array(allocator, wchar_t, wide_string_length);
	int32_t result = MultiByteToWideChar(CP_UTF8, 0, string.v, string.length, wide_string, required_size);

	if (include_null_terminator) {
		wide_string[required_size] = 0;
	} else {
		assert(wide_string[required_size - 1] == 0);
	}

	assert(result != 0);
	profile_scope_end();
	return (WideString) { .v = wide_string, .length = wide_string_length };
}

String str_from_wstr(WideString string, Arena* allocator) {
	profile_core_func();
	
	// Doesn't include the null-terminator
	int32_t required_size = WideCharToMultiByte(CP_UTF8,
			0,             /* dwFlags */
			string.v,      /* lpWideCharStr */
			string.length, /* cchWideChar */
			NULL,          /* lpMultiByteStr */
			0,             /* cbMultiByte */
			NULL,          /* lpDefaultChar */
			NULL           /* lpUsedDefaultChar */);

	char* buffer = arena_alloc_array(allocator, char, required_size);
	int32_t result = WideCharToMultiByte(CP_UTF8,
			0,             /* dwFlags */
			string.v,      /* lpWideCharStr */
			string.length, /* cchWideChar */
			buffer,        /* lpMultiByteStr */
			required_size, /* cbMultiByte */
			NULL,          /* lpDefaultChar */
			NULL           /* lpUsedDefaultChar */);

	assert(result != 0);

	profile_scope_end();
	return (String) { .v = buffer, .length = required_size };
}

String str_format_with_args(Arena* allocator, const char* fmt, va_list args) {
	va_list args_copy;
	va_copy(args_copy, args);

	uint32_t bytes_required = vsnprintf(NULL, 0, fmt, args);

	// +1 for the null terminator
	size_t buffer_capacity = bytes_required + 1;

	char* buffer = arena_alloc_array(allocator, char, buffer_capacity);

	String string = {};
	string.v = buffer;
	string.length = vsnprintf_s(buffer, buffer_capacity, bytes_required, fmt, args_copy);

	allocator->allocated -= buffer_capacity - string.length;
	return string;
}

String str_format(Arena* allocator, const char* fmt, ...) {
	profile_core_func();

	va_list args;
	va_start(args, fmt);

	String string = str_format_with_args(allocator, fmt, args);

	va_end(args);

	profile_scope_end();
	return string;
}

String str_split_next(String* string, char by_char) {
	if (string->length == 0) {
		return (String) {};
	}

	const char* chr_position = memchr(
			string->v,
			by_char,
			string->length);

	if (chr_position) {
		String part = (String) {
			.v = string->v,
			.length = (size_t)(chr_position - string->v)
		};

		string->v = chr_position + 1;
		string->length -= part.length - 1;
		return part;
	}

	String part = *string;
	string->v += string->length;
	string->length = 0;
	return part;
}

//
// String Builder
//

void str_builder_format(StringBuilder* builder, const char* fmt, ...) {
	profile_core_func();

	va_list args;
	va_start(args, fmt);

	String formatted = str_format_with_args(builder->arena, fmt, args);
	if (builder->string.length == 0) {
		builder->string = formatted;
	} else {
		builder->string.length += formatted.length;
	}

	va_end(args);

	profile_scope_end();
}

void str_builder_append_int(StringBuilder* builder, uint64_t value) {
	if (value == 0) {
		str_builder_append_char(builder, '0' + value);
		return;
	}

	char* buffer = arena_alloc_array(builder->arena, char, 0);

	if (!builder->string.v) {
		builder->string.v = buffer;
	}

	char* buffer_write_ptr = buffer;
	while (value) {
		uint32_t digit = value % 10;
		value /= 10;

		arena_alloc(builder->arena, char);

		*buffer_write_ptr = '0' + digit;
		buffer_write_ptr += 1;
	}

	size_t digit_count = buffer_write_ptr - buffer;

	for (size_t i = 0; i < digit_count / 2; i++) {
		char temp = buffer[digit_count - 1 - i];
		buffer[digit_count - 1 - i] = buffer[i];
		buffer[i] = temp;
	}

	builder->string.length += digit_count;
}

//
// Line Iterator
// 

bool line_iterator_next(LineIterator* iter, String* out_line) {
	if (iter->read_position >= iter->source.length) {
		return false;
	}

	size_t line_start = iter->read_position;
	size_t line_end = line_start;
	while (iter->read_position < iter->source.length) {
		char current_char = iter->source.v[iter->read_position];
		line_end = iter->read_position;
		iter->read_position += 1;

		// skip \r\n
		if (current_char == '\r') {
			if (iter->read_position < iter->source.length && iter->source.v[iter->read_position] == '\n') {
				iter->read_position += 1;
			}

			break;
		}

		if (current_char == '\n') {
			break;
		}
	}

	String line = sub_str(iter->source, line_start, line_end - line_start);
	*out_line = line;
	return true;
}

StringArray string_to_lines(String string, Arena* allocator) {
	profile_core_func();
	StringArray array = { .values = arena_alloc_array(allocator, String, 0), .count = 0 };

	LineIterator iter = { .source = string };
	String line = {};
	while (line_iterator_next(&iter, &line)) {
		*arena_alloc(allocator, String) = line;
		array.count += 1;
	}

	profile_scope_end();
	return array;
}

//
// Environment
//

String env_get(const char* var_name, Arena* allocator) {
	profile_core_func();

	DWORD buffer_size = GetEnvironmentVariable(var_name, NULL, 0);
	if (buffer_size == 0) {
		profile_scope_end();
		return (String) {};
	}

	ArenaRegion temp = arena_begin_temp(allocator);

	char* buffer = arena_alloc_array(allocator, char, buffer_size);
	DWORD result = GetEnvironmentVariable(var_name, buffer, buffer_size);
	if (result + 1 != buffer_size) {
		arena_end_temp(temp);
		profile_scope_end();
		return (String) {};
	}

	// buffer_size includes null-terminator
	profile_scope_end();
	return (String) { .v = buffer, .length = buffer_size - 1 };
}

//
// File System
//

String read_entire_file_to_str(const char* file_path, Arena* arena) {
	profile_core_func();

	FILE* f = NULL;
	errno_t error = fopen_s(&f, file_path, "rb");

	if (!f || error == EINVAL) {
		// Failed to open
		profile_scope_end();
		return (String) {};
	}

	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);

	char* string = arena_alloc_array(arena, char, size + 1);
	string[size] = 0;
	fread(string, 1, size, f);

	fclose(f);

	profile_scope_end();
	return (String) { .v = string, .length = size };
}

bool write_str_to_file(const char* file_path, String string) {
	profile_core_func();

	FILE* f = NULL;
	errno_t error = fopen_s(&f, file_path, "w");

	if (!f || error == EINVAL) {
		// Failed to open
		profile_scope_end();
		return false;
	}

	fwrite(string.v, 1, string.length, f);
	fclose(f);

	profile_scope_end();
	return true;
}

StringArray fs_enumerate_files_in_directory(String directory_path, Arena* file_path_allocator, Arena* temp_arena) {
	return fs_enumerate_entries_in_directory(directory_path, FS_ENTRY_FILE, file_path_allocator, temp_arena);
}

StringArray fs_enumerate_entries_in_directory(String directory_path,
		FsEntryType mask,
		Arena* file_path_allocator,
		Arena* temp_arena) {

	profile_core_func();
	ArenaRegion temp = arena_begin_temp(temp_arena);

	DWORD file_attributes = 0;
	if (has_flag(mask, FS_ENTRY_FILE)) {
		file_attributes |= UINT32_MAX & (~FILE_ATTRIBUTE_DIRECTORY);
	}

	if (has_flag(mask, FS_ENTRY_DIRECTORY)) {
		file_attributes |= FILE_ATTRIBUTE_DIRECTORY;
	}

	StringBuilder builder = { .arena = temp_arena };
	str_builder_append(&builder, directory_path);
	str_builder_append(&builder, STR_LIT("\\*"));

	const char* filter_string = str_builder_to_cstr(&builder);

	WIN32_FIND_DATA find_data = {};
	HANDLE search_handle = FindFirstFileA(filter_string, &find_data);

	if (search_handle == INVALID_HANDLE_VALUE) {
		arena_end_temp(temp);
		return (StringArray) {};
	}


	String* file_path_array = arena_alloc_array(temp_arena, String, 0);
	size_t file_count = 0;

	while (true) {
		if ((find_data.dwFileAttributes & file_attributes) != 0) {
			bool skip = false;
			if (find_data.cFileName[0] == '.' && find_data.cFileName[1] == '.' && find_data.cFileName[2] == 0) {
				skip = true;
			} else if (find_data.cFileName[0] == '.' && find_data.cFileName[1] == 0) {
				skip = true;
			}

			if (!skip) {
				String file_path = str_duplicate_from_cstr(find_data.cFileName, file_path_allocator);
				*arena_alloc(temp_arena, String) = file_path;
				file_count += 1;
			}
		}

		if (FindNextFile(search_handle, &find_data) == 0) {
			break;
		}
	}

	FindClose(search_handle);

	StringArray file_paths = {};
	file_paths.values = arena_alloc_array(file_path_allocator, String, file_count);
	file_paths.count = file_count;
	memcpy(file_paths.values, file_path_array, sizeof(String) * file_count);

	arena_end_temp(temp);
	profile_scope_end();
	return file_paths;
}

bool fs_create_directory(String directory_path, Arena* temp_allocator) {
	profile_core_func();
	ArenaRegion temp = arena_begin_temp(temp_allocator);

	const char* directory_path_cstr = str_to_cstr(directory_path, temp_allocator);
	BOOL result = CreateDirectoryA(directory_path_cstr, NULL);

	arena_end_temp(temp);
	profile_scope_end();
	return result == TRUE;
}

//
// BitArray
//

bool bit_array_and(const BitArray* a, const BitArray* b, BitArray* out) {
	assert(a->bit_count == b->bit_count && a->bit_count == out->bit_count);

	size_t full_byte_count = a->bit_count / 8;
	size_t total_byte_count = (a->bit_count + 7) / 8;

	uint8_t change_mask = 0;
	for (size_t i = 0; i < full_byte_count; i++) {
		uint8_t new_byte = a->values[i] & b->values[i];
		uint8_t changes = new_byte ^ out->values[i];
		
		change_mask |= changes;

		out->values[i] = new_byte;
	}

	size_t rest_bit_count = a->bit_count - full_byte_count * 8;
	if (rest_bit_count > 0) {
		size_t last_index = total_byte_count - 1;
		
		uint8_t new_byte = a->values[last_index] & b->values[last_index];
		uint8_t old_byte = out->values[last_index];

		change_mask |= (new_byte ^ old_byte);
		out->values[last_index] = new_byte;
	}

	return change_mask != 0;
}

bool bit_array_or(const BitArray* a, const BitArray* b, BitArray* out) {
	assert(a->bit_count == b->bit_count && a->bit_count == out->bit_count);

	size_t full_byte_count = a->bit_count / 8;
	size_t total_byte_count = (a->bit_count + 7) / 8;

	uint8_t change_mask = 0;
	for (size_t i = 0; i < full_byte_count; i++) {
		uint8_t new_byte = a->values[i] | b->values[i];
		uint8_t changes = new_byte ^ out->values[i];
		
		change_mask |= changes;

		out->values[i] = new_byte;
	}

	size_t rest_bit_count = a->bit_count - full_byte_count * 8;
	if (rest_bit_count > 0) {
		size_t last_index = total_byte_count - 1;
		
		uint8_t new_byte = a->values[last_index] | b->values[last_index];
		uint8_t old_byte = out->values[last_index];

		change_mask |= (new_byte ^ old_byte);
		out->values[last_index] = new_byte;
	}

	return change_mask != 0;
}

bool bit_array_equal(const BitArray* a, const BitArray* b) {
	if (a == b) {
		return true;
	}

	if (a->bit_count != b->bit_count) {
		return false;
	}

	size_t full_element_count = a->bit_count / 8;
	if (memcmp(a->values, b->values, full_element_count) != 0) {
		return false;
	}

	if (a->bit_count % 8 == 0) {
		return true;
	}

	uint8_t mask = (1 << (a->bit_count & 7)) - 1;
	uint8_t a_last = a->values[full_element_count] & mask;
	uint8_t b_last = b->values[full_element_count] & mask;
	return a_last == b_last;
}

//
// Registry
//

bool win_sdk_get_install_path(Arena* allocator, String* out_path) {
	profile_core_func();

	HKEY key = {};
	LSTATUS result = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
			"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots",
			0,
			KEY_QUERY_VALUE,
			&key);

	if (result != ERROR_SUCCESS) {
		return false;
	}

	DWORD value_type = 0;
	DWORD data_size = 0;
	LSTATUS status = RegGetValueA(key /* hkey */,
			NULL /* lpSubKey */,
			"KitsRoot10" /* lpValue */,
			RRF_RT_REG_SZ, /* dwFlags */
			&value_type, /* pdwType */
			NULL, /* pvDAta */
			&data_size /* pcbData */);

	if (status != ERROR_SUCCESS) {
		RegCloseKey(key);
		profile_scope_end();
		return false;
	}

	assert(value_type == REG_SZ);

	ArenaRegion temp = arena_begin_temp(allocator);

	char* data_buffer = arena_alloc_array(allocator, char, (size_t)data_size);
	status = RegGetValueA(key /* hkey */,
			NULL /* lpSubKey */,
			"KitsRoot10" /* lpValue */,
			RRF_RT_ANY /* dwFlags */,
			&value_type /* pdwType */,
			data_buffer, /* pvDAta */
			&data_size /* pcbData */);

	if (status != ERROR_SUCCESS) {
		arena_end_temp(temp);
		RegCloseKey(key);

		profile_scope_end();
		return false;
	}

	RegCloseKey(key);

	*out_path = str_from_cstr(data_buffer);
	profile_scope_end();
	return true;
}


//
// Path
// 

String get_current_directory(Arena* allocator) {
	profile_core_func();
	// Includes null terminator
	DWORD buffer_size = GetCurrentDirectory(0, NULL);

	ArenaRegion temp = arena_begin_temp(allocator);

	char* buffer = arena_alloc_array(allocator, char, (size_t)buffer_size);
	DWORD result = GetCurrentDirectory(buffer_size, buffer);

	if (!result) {
		arena_end_temp(temp);
		profile_scope_end();
		return (String) {};
	}

	assert(buffer[buffer_size - 1] == 0);
	profile_scope_end();
	return (String) { .v = buffer, .length = buffer_size - 1 };
}

String path_to_absolute(Arena* allocator, String path) {
	String current_dir = get_current_directory(allocator);

	StringBuilder builder = { .arena = allocator };
	str_builder_append(&builder, current_dir);
	str_builder_append_char(&builder, '/');
	str_builder_append(&builder, path);

	return builder.string;
}

bool path_exists(Arena* temp_allocator, String path) {
	profile_core_func();
	ArenaRegion temp = arena_begin_temp(temp_allocator);

	const char* path_cstr = str_to_cstr(path, temp_allocator);
	bool exists = PathFileExistsA(path_cstr);

	arena_end_temp(temp);
	profile_scope_end();
	return exists;
}

inline static DWORD _get_file_attributes(String path, Arena* temp_allocator) {
	profile_core_func();
	ArenaRegion temp = arena_begin_temp(temp_allocator);

	const char* path_cstr = str_to_cstr(path, temp_allocator);
	DWORD attributes = GetFileAttributesA(path_cstr);

	arena_end_temp(temp);
	profile_scope_end();
	return attributes;
}

bool path_is_file(Arena* temp_allocator, String path) {
	DWORD attributes = _get_file_attributes(path, temp_allocator);
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		return false;
	}

	return !has_flag(attributes, FILE_ATTRIBUTE_DIRECTORY);
}

bool path_is_directory(Arena* temp_allocator, String path) {
	DWORD attributes = _get_file_attributes(path, temp_allocator);
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		return false;
	}

	return has_flag(attributes, FILE_ATTRIBUTE_DIRECTORY);
}

WideString _path_canonicalize_to_wide_string(String path, Arena* allocator) {
	profile_core_func();
	WideString wide_path_string = str_to_wstr(path, allocator, true);

	wchar_t* wide_canonical_path = arena_alloc_array(allocator, wchar_t, wide_path_string.length);
	HRESULT result = PathCchCanonicalize(wide_canonical_path,
			wide_path_string.length,
			wide_path_string.v);

	assert(result == S_OK);
	
	size_t canonical_path_length = wcslen(wide_canonical_path);
	for (size_t i = 0; i < canonical_path_length; i += 1) {
		if (wide_canonical_path[i] == '\\') {
			wide_canonical_path[i] = '/';
		}
	}

	profile_scope_end();
	return (WideString) { .v = wide_canonical_path, .length = canonical_path_length };
}

String path_canonicalize(String path, Arena* allocator, Arena* temp_allocator) {
	profile_core_func();
	ArenaRegion temp = arena_begin_temp(temp_allocator);

	WideString wide_canonical_path_string = _path_canonicalize_to_wide_string(path, temp_allocator);
	String canonical_path = str_from_wstr(wide_canonical_path_string, allocator);

	arena_end_temp(temp);

	profile_scope_end();
	return canonical_path;
}

size_t path_get_file_name_start(String path) {
	for (size_t i = path.length; i > 0; i -= 1) {
		char c = path.v[i - 1];
		if (c == '/' || c == '\\') {
			return i;
		}
	}

	return 0;
}

uint64_t path_get_last_write_time(String path, Arena* temp_allocator) {
	profile_core_func();

	ArenaRegion temp = arena_begin_temp(temp_allocator);

	const char* path_cstr = str_to_cstr(path, temp_allocator);

	HANDLE file_handle = CreateFile(path_cstr,
			GENERIC_READ,
			0,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL);

	if (file_handle == INVALID_HANDLE_VALUE) {
		arena_end_temp(temp);
		profile_scope_end();
		return 0;
	}

	FILETIME write_time;
	if (!GetFileTime(file_handle, NULL, NULL, &write_time)) {
		CloseHandle(file_handle);
		arena_end_temp(temp);
		profile_scope_end();
		return 0;
	}

	uint64_t last_write_time = ((uint64_t)write_time.dwHighDateTime << 32)
		| ((uint64_t)write_time.dwLowDateTime);

	CloseHandle(file_handle);
	arena_end_temp(temp);

	profile_scope_end();
	return last_write_time;
}

String path_get_parent(String path) {
	size_t file_name_start = path_get_file_name_start(path);
	size_t parent_path_end = file_name_start;

	for (size_t i = file_name_start; i > 0; i -= 1) {
		char c = path.v[i - 1];
		if (c == '/' || c == '\\') {
			continue;
		} else {
			parent_path_end = i;
			break;
		}
	}

	String parent_path = sub_str(path, 0, parent_path_end);
	if (parent_path.length == 0) {
		return STR_LIT("./");
	}

	return parent_path;
}

String path_append(String parent, String path, Arena* allocator) {
	StringBuilder builder = { .arena = allocator };

	str_builder_append(&builder, path_trim_trailing_slash(parent));
	str_builder_append_char(&builder, '/');
	str_builder_append(&builder, path_trim_trailing_slash(path));

	return builder.string;
}


//
// Process
//

ProcessRunResult process_run(String executable_path,
		String working_directory,
		String arguments,
		int32_t* out_exit_code,
		Arena* temp_allocator) {
	profile_core_func();

	if (!path_is_file(temp_allocator, executable_path)) {
		profile_scope_end();
		return PROCESS_RUN_INVALID_EXE_PATH;
	}

	if (!path_is_directory(temp_allocator, working_directory)) {
		profile_scope_end();
		return PROCESS_RUN_INVALID_WORKING_DIR_PATH;
	}

	ArenaRegion temp = arena_begin_temp(temp_allocator);

	const char* executable_path_cstr = str_to_cstr(executable_path, temp_allocator);
	const char* working_directory_cstr = str_to_cstr(working_directory, temp_allocator);
	char* arguments_cstr = str_to_cstr(arguments, temp_allocator);

	PROCESS_INFORMATION process_info = {};
	STARTUPINFOA startup_info = {};

	ZeroMemory(&process_info, sizeof(process_info));
	ZeroMemory(&startup_info, sizeof(startup_info));

	startup_info.cb = sizeof(startup_info);

	BOOL success = CreateProcessA(executable_path_cstr,
			arguments_cstr,
			NULL,
			NULL,
			TRUE,
			0,
			NULL,
			working_directory_cstr,
			&startup_info,
			&process_info);

	arena_end_temp(temp);

	if (!success) {
		profile_scope_end();
		return PROCESS_RUN_ERROR;
	}

	WaitForSingleObject(process_info.hProcess, INFINITE);

	if (out_exit_code) {
		DWORD code = 0;
		GetExitCodeProcess(process_info.hProcess, &code);
		*out_exit_code = (int32_t)code;
	}

	CloseHandle(process_info.hProcess);
	CloseHandle(process_info.hThread);
	profile_scope_end();
	return PROCESS_RUN_OK;
}

typedef struct {
	HANDLE read;
	HANDLE write;
} StdoutPipe;

static bool _stdout_pipe_create(StdoutPipe* pipe) {
	profile_core_func();

	HANDLE stdout_read = NULL;
	HANDLE stdout_write = NULL;

	SECURITY_ATTRIBUTES security_attributes = {};
	security_attributes.nLength = sizeof(security_attributes);
	security_attributes.bInheritHandle = TRUE;
	security_attributes.lpSecurityDescriptor = NULL;
	if (!CreatePipe(&stdout_read, &stdout_write, &security_attributes, 0)) {
		profile_scope_end();
		return false;
	}

	if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0)) {
		panic("SetHandleInformation failed");
	}

	*pipe = (StdoutPipe) {
		.read = stdout_read,
		.write = stdout_write,
	};

	profile_scope_end();
	return true;
}

static void _stdout_pipe_release(StdoutPipe* pipe) {
	profile_core_func();

	CloseHandle(pipe->read);
	CloseHandle(pipe->write);
	*pipe = (StdoutPipe) {};

	profile_scope_end();
}

ProcessRunResult process_capture_stdout(String executable_path,
		String working_directory,
		String arguments,
		int32_t* out_exit_code,
		String* out_stdout,
		Arena* allocator,
		Arena* temp_allocator) {
	if (!path_is_file(temp_allocator, executable_path)) {
		return PROCESS_RUN_INVALID_EXE_PATH;
	}

	if (!path_is_directory(temp_allocator, working_directory)) {
		return PROCESS_RUN_INVALID_WORKING_DIR_PATH;
	}

	ArenaRegion temp = arena_begin_temp(temp_allocator);

	const char* executable_path_cstr = str_to_cstr(executable_path, temp_allocator);
	const char* working_directory_cstr = str_to_cstr(working_directory, temp_allocator);
	char* arguments_cstr = str_to_cstr(arguments, temp_allocator);

	StdoutPipe pipe = {};
	if (!_stdout_pipe_create(&pipe)) {
		return PROCESS_RUN_ERROR;
	}

	PROCESS_INFORMATION process_info = {};
	STARTUPINFOA startup_info = {};

	ZeroMemory(&process_info, sizeof(process_info));
	ZeroMemory(&startup_info, sizeof(startup_info));

	startup_info.cb = sizeof(startup_info);
	startup_info.hStdError = pipe.write;
	startup_info.hStdOutput = pipe.write;
	startup_info.dwFlags |= STARTF_USESTDHANDLES;

	BOOL success = CreateProcessA(executable_path_cstr,
			arguments_cstr,
			NULL,
			NULL,
			TRUE,
			0,
			NULL,
			working_directory_cstr,
			&startup_info,
			&process_info);

	arena_end_temp(temp);

	if (!success) {
		_stdout_pipe_release(&pipe);
		return PROCESS_RUN_ERROR;
	}

	CloseHandle(process_info.hThread);

	// Close the write handle, so that it is possible to detect
	// whenever the child process has terminated
	CloseHandle(pipe.write);

	String stdout_string = { .v = arena_alloc_array(allocator, char, 0), .length = 0 };
	size_t string_allocation_base = allocator->allocated;
	size_t buffer_size = 4096;

	while (true) {
		DWORD read_count = 0;

		char* buffer = arena_alloc_array(allocator, char, buffer_size);

		bool success = ReadFile(pipe.read, buffer, buffer_size, &read_count, NULL);
		if (!success || read_count == 0) {
			break;
		}

		stdout_string.length += (size_t)read_count;
		allocator->allocated = string_allocation_base + stdout_string.length;
	}

	allocator->allocated = string_allocation_base + stdout_string.length;
	*out_stdout = stdout_string;

	WaitForSingleObject(process_info.hProcess, INFINITE);

	if (out_exit_code) {
		DWORD code = 0;
		GetExitCodeProcess(process_info.hProcess, &code);
		*out_exit_code = (int32_t)code;
	}

	CloseHandle(process_info.hProcess);
	CloseHandle(pipe.read);

	return PROCESS_RUN_OK;
}
