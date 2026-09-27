__declspec(dllimport) void assert(unsigned long long);

#include <stdio.h>

int main(int argc, char* argv[]) {
	int i = 10;
	while(i > 0) {
		if (argc == 100) {
			break;
		} else {
			i--;
			continue;
		}
	}

	assert(i == 0);
	return 0;
}
