typedef struct {
	size_t a;
	size_t b;
	int c;
} Type;

int main(int argc, char* argv[]) {
	Type t = {};

	int a = 10;
	do {
		a--;
	} while (a);

	return t.a;
}
