#include <stdio.h>

#ifdef DEFINED_STRING
void func_##DEFINED_STRING(void) {
	puts(#DEFINED_STRING);
}
#endif
