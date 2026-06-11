#include <stdio.h>

int return_a(void) {
	return A;
}

int main(int argc, char** argv) {
	int n = return_a();
	printf("n = %d\n", n);
	return 0;
}
