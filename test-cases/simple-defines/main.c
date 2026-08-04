#include <stdio.h>

#ifdef ENABLE_A
void a_enabled_function(void) {
	puts("function defined only if ENABLE_A defined");
}
#else
void no_a_enabled_function(void) {
	puts("function defined only if ENABLE_A not defined");
}
#endif

#ifdef ENABLE_B
void b_enabled_function(void) {
	puts("function defined only if ENABLE_B defined");
}
#else
void no_b_enabled_function(void) {
	puts("function defined only if ENABLE_B not defined");
}
#endif

int main(int argc, char** argv) {
#ifdef ENABLE_A
	a_enabled_function();
#else
	no_a_enabled_function();
#endif

#ifdef ENABLE_B
	b_enabled_function();
#else
	no_b_enabled_function();
#endif
}
