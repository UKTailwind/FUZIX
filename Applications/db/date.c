#include <time.h>
#include <stdio.h>
#include "date.h"

/* Return todays's date as a YYYYMMDD string */
void date_today_str(char *datestr)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);

    unsigned year  = (unsigned)(tm->tm_year + 1900) % 10000;
    unsigned month = (unsigned)(tm->tm_mon + 1);
    unsigned day   = (unsigned)tm->tm_mday;

    snprintf(datestr, 9, "%04u%02u%02u", year, month, day);
}


/* Return 0=Sunday ... 6=Saturday */
static int weekday(int y, int m, int d)
{
    if (m < 3) {
        m += 12;
        y -= 1;
    }
    int k = y % 100;
    int j = y / 100;
    int w = (d + 13*(m + 1)/5 + k + k/4 + j/4 + 5*j) % 7;
    return ((w + 6) % 7);  /* adjust to 0=Sunday */
}

/* Return Day Name from YYYYMMDD int */
const char *day_name(int yyyymmdd)
{
    static const char *names[] = {
        "Sunday","Monday","Tuesday","Wednesday",
        "Thursday","Friday","Saturday"
    };

    int y = yyyymmdd / 10000;
    int m = (yyyymmdd / 100) % 100;
    int d = yyyymmdd % 100;

    int w = weekday(y, m, d);
    return names[w];
}

/* Return 1 if leap year */
static int is_leap(int y) {
    return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

/* Add delta days to YYYYMMDD */
int add_days(int yyyymmdd, int delta)
{
    static const int month_days[12] = 
        {31,28,31,30,31,30,31,31,30,31,30,31};

    int y = yyyymmdd / 10000;
    int m = (yyyymmdd / 100) % 100;
    int d = yyyymmdd % 100;

    d += delta;

    while (1) {
        int dim = month_days[m-1];
        if (m == 2 && is_leap(y)) dim++;  /* Feb in leap year */

        if (d > dim) {
            d -= dim;
            m++;
            if (m > 12) { m = 1; y++; }
        } else if (d < 1) {
            m--;
            if (m < 1) { m = 12; y--; }
            dim = month_days[m-1];
            if (m == 2 && is_leap(y)) dim++;
            d += dim;
        } else {
            break;
        }
    }

    return y*10000 + m*100 + d;
}

