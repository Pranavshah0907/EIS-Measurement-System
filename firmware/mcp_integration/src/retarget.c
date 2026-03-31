/*
 * retarget.c — semihosting stubs for ADuCM3029 / Keil ARM V5.
 * No printf used in this project — _sys_open is never referenced.
 */

#pragma import(__use_no_semihosting)

struct __FILE { int handle; };

void _sys_exit(int return_code) { (void)return_code; while (1); }
void _ttywrch(int ch)           { (void)ch; }
