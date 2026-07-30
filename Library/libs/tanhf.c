/* float tanh via the double one - see tanh.c */
#include <math.h>

float tanhf(float x)
{
	return (float)tanh((double)x);
}
