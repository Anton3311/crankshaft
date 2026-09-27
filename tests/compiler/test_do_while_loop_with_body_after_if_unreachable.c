__declspec(dllimport) void assert(unsigned long long);

int main(int argc, char* argv[]) {
	int i = 10;
	do {
		if (i == 100) {
			break;
		} else {
			i -= 1;
			continue;
		}
	} while (i > 0);

	assert(i == 0);
	return 0;
}
