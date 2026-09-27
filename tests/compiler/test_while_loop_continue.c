#include <stdint.h>

#include <stdio.h>

__declspec(dllimport) void* malloc(size_t count);
__declspec(dllimport) void free(void* ptr);
__declspec(dllimport) void panic(const char*);
__declspec(dllimport) void assert(uint64_t);

int main(int argc, char* argv[]) {
	int* array = malloc(20);
	array[0] = 89;
	array[1] = 34;
	array[2] = -12;
	array[3] = -9;
	array[4] = 7;

	int i = 0;
	int sum = 0;
	while (i < 5) {
		if (array[i] < 0) {
			i = i + 1;
			continue;
		}

		sum = sum + array[i];
		i = i + 1;
	}

	free(array);

	assert(sum == 130);
	return 0;
}

