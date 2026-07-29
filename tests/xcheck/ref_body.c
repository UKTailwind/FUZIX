/* ------------------------------------------------------------------
 * IntToStr, IntToStrPad and FloatToStr lifted unchanged from
 * core/MMBasic.c in the PicoMite firmware, purely so that the versions
 * in mmb_runtime.c can be diffed against them.  Not part of the build.
 *
 *   <COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
 *   Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
 *   See ../../LICENSE for the full text and conditions.
 * ------------------------------------------------------------------ */
void IntToStr(char *strr, long long int nbr, unsigned int base)
{
    int i, negative;
    unsigned char digit;
    unsigned long long int sum;
    extern long long int llabs(long long int n);

    unsigned char str[IntToStrBufSize];

    if (nbr < 0 && base == 10)
    { // we can have negative numbers in base 10 only
        nbr = llabs(nbr);
        negative = true;
    }
    else
        negative = false;

    // this generates the digits in reverse order
    sum = (unsigned long long int)nbr;
    i = 0;
    do
    {
        digit = sum % base;
        if (digit < 0xA)
            str[i++] = '0' + digit;
        else
            str[i++] = 'A' + digit - 0xA;
        sum /= base;
    } while (sum && i < IntToStrBufSize);

    if (negative)
        *strr++ = '-';

    // we now need to reverse the digits into their correct order
    for (i--; i >= 0; i--)
        *strr++ = str[i];
    *strr = 0;
}

// convert an integer to a string padded with a leading character
// p is a pointer to the destination
// nbr is the number to convert (can be signed in which case the number is preceeded by '-')
// padch is the leading padding char (usually a space)
// maxch is the desired width of the resultant string (incl padding chars)
// radix is the base of the number.  Base 10 is signed, all others are unsigned
// Special case (used by FloatToStr() only):
//     if padch is negative and nbr is zero prefix the number with the - sign
void IntToStrPad(char *p, long long int nbr, signed char padch, int maxch, int radix)
{
    int j;
    char sign, buf[IntToStrBufSize];

    sign = 0;
    if ((nbr < 0 && radix == 10 && nbr != 0x8000000000000000) || padch < 0)
    {               // if the number is negative or we are forced to use a - symbol
        sign = '-'; // set the sign
        nbr *= -1;  // convert to a positive nbr
        padch = abs(padch);
    }
    else
    {
        if (nbr >= 0 && maxch < 0 && radix == 10) // should we display the + sign?
            sign = '+';
    }

    IntToStr(buf, nbr, radix);
    j = abs(maxch) - strlen(buf); // calc padding required
    if (j <= 0)
        j = 0;
    else
        memset(p, padch, abs(maxch)); // fill the buffer with the padding char
    if (sign != 0)
    { // if we need a sign
        if (j == 0)
            j = 1; // make space if necessary
        if (padch == '0')
            p[0] = sign; // for 0 padding the sign is before the padding
        else
            p[j - 1] = sign; // for anything else the padding is before the sign
    }
    strcpy(&p[j], buf);
}

// convert a float to a string including scientific notation if necessary
// p is the buffer to store the string
// f is the number
// m is the nbr of chars before the decimal point (if negative print the + sign)
// n is the nbr chars after the point
//     if n == STR_AUTO_PRECISION we should automatically determine the precision
//     if n is negative always use exponential format
// ch is the leading pad char
void FloatToStr(char *p, double f, int m, int n, unsigned char ch)
{
    int exp, trim = false, digit;
    double rounding;
    char *pp;
    if (f == INFINITY)
    {
        strcpy(p, "INF");
        return;
    }
    ch &= 0x7f; // make sure that ch is an ASCII char
    if (f == 0)
        exp = 0;
    else
        exp = floor(log10(fabs(f))); // get the exponent part
    if (((fabs(f) < 0.0001 || fabs(f) >= 1000000) && f != 0 && (n == STR_AUTO_PRECISION || n == STR_FLOAT_PRECISION)) || n < 0)
    {
        // we must use scientific notation
        f /= pow(10, exp); // scale the number to 1.2345
        if (f >= 10)
        {
            f /= 10;
            exp++;
        }
        if (n < 0)
            n = -n;                 // negative indicates always use exponantial format
        FloatToStr(p, f, m, n, ch); // recursively call ourself to convert that to a string
        p = p + strlen(p);
        *p++ = 'e'; // add the exponent
        if (exp >= 0)
        {
            *p++ = '+';
            IntToStrPad(p, exp, '0', 2, 10); // add a positive exponent
        }
        else
        {
            *p++ = '-';
            IntToStrPad(p, exp * -1, '0', 2, 10); // add a negative exponent
        }
    }
    else
    {
        // we can treat it as a normal number

        // first figure out how many decimal places we want.
        // n == STR_AUTO_PRECISION means that we should automatically determine the precision
        if (n == STR_AUTO_PRECISION)
        {
            trim = true;
            n = STR_SIG_DIGITS - exp;
            if (n < 0)
                n = 0;
        }
        if (n == STR_FLOAT_PRECISION)
        {
            trim = true;
            n = STR_FLOAT_DIGITS - exp;
            if (n < 0)
                n = 0;
        }

        // calculate rounding to hide the vagaries of floating point
        if (n > 0)
            rounding = 0.5 / pow(10, n);
        else
            rounding = 0.5;
        if (f > 0)
            f += rounding; // add rounding for positive numbers
        if (f < 0)
            f -= rounding; // add rounding for negative numbers

        // convert the digits before the decimal point
        if ((int)f == 0 && f < 0)
            IntToStrPad(p, 0, -ch, m, 10); // convert -0 incl padding if necessary
        else
            IntToStrPad(p, f, ch, m, 10); // convert the integer incl padding if necessary
        p += strlen(p);                   // point to the end of the integer
        pp = p;

        // convert the digits after the decimal point
        if (f < 0)
            f = -f; // make the number positive
        if (n > 0)
        {                  // if we need to have a decimal point and following digits
            *pp++ = '.';   // add the decimal point
            f -= floor(f); // get just the fractional part
            while (n--)
            {
                f *= 10;
                digit = floor(f); // get the next digit for the string
                f -= digit;
                *pp++ = digit + '0';
            }

            // if we do not have a fixed number of decimal places step backwards removing trailing zeros and the decimal point if necessary
            while (trim && pp > p)
            {
                pp--;
                if (*pp == '.')
                    break;
                if (*pp != '0')
                {
                    pp++;
                    break;
                }
            }
        }
        *pp = 0;
    }
}
