/* usleep.c
 *
 * _pause() timeouts are in deciseconds (the kernel timer wheel runs
 * once per decisecond on every platform).  Round UP: the old code
 * rounded down, so any period below 100ms became _pause(0), which is
 * "pause until a signal" - i.e. sleep forever.
 */
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <syscalls.h>

int usleep(useconds_t us)
{
	if (us == 0)
		return 0;
	return _pause((us + 99999UL) / 100000UL);
}
