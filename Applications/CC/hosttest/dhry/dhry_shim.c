/* time_us() for the native build of the Dhrystone port; the bytecode
   build binds the interpreter's libcall of the same name instead. */
#include <sys/time.h>

long time_us(void)
{
	struct timeval tv;
	gettimeofday(&tv, 0);
	return (long)(tv.tv_sec * 1000000L + tv.tv_usec);
}
