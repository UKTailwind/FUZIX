/* Sizes scaled by 1000 so they are unmistakable in the object stream:
   32-bit model expects 4000/4000/8000; 16-bit expects 2000/2000/4000. */
char p_int[sizeof(int) * 1000];
char p_ptr[sizeof(char *) * 1000];
char p_arr[sizeof(int[2]) * 1000];
char p_short[sizeof(short) * 1000];
char p_char[sizeof(char) * 1000];

int scale_check(int *p, int n)
{
	return *(p + n);
}
