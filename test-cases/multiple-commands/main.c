#include <stdio.h>

#ifndef LENGTH
#error LENGTH must be defined!
#endif

void use_length_macro(void) {
	int len = LENGTH;
	printf("length=%d\n", len);
}

int main(int argc, char** argv) {
	print_buffer_with_length();
	return 0;
}
