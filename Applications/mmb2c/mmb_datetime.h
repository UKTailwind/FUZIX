#ifndef MMB_DATETIME_H
#define MMB_DATETIME_H
/*
 *	DATE$, TIME$, DATETIME$(), EPOCH(), DAY$() - and the DATE$= /
 *	TIME$= writers, which no translator emits yet but which belong
 *	with the clock offset they adjust.
 *
 *	WHY A HEADER rather than the runtime - the mmb_math.h bargain:
 *	2K of calendar arithmetic carried by every program on the
 *	machine whether or not it ever asks the date.  Moved out of
 *	mmb_runtime.c on 2026-08-21, byte for byte.  The calendar is
 *	done here rather than through gmtime() so the answers are
 *	identical on every platform - and the only OS fact needed is
 *	time(), which is already a bcrun libcall.
 *
 *	mm_clock_off is the DATE$=/TIME$= adjustment.  As a program
 *	static it is per process, exactly as it was as a runtime static
 *	- bcrun's statics are per process too - so nothing observable
 *	moves.  mm_int_to_str_pad stays in the runtime: every PRINT of
 *	a number already pays for it.
 */

#include <time.h>

#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

static MMINTEGER mm_clock_off;          /* DATE$= / TIME$= adjustment */

MMG_FN MMINTEGER mm_epoch_now(void)
{
    return (MMINTEGER)time(NULL) + mm_clock_off;
}

/* civil from days (Howard Hinnant's algorithm) - the inverse of
 * mm_days_from_civil below. */
MMG_FN void mm_civil_from_days(long long z, int *y, int *m, int *d)
{
    long long era, doe, yoe, doy, mp, yy;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    yy = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp + (mp < 10 ? 3 : -9));
    *y = (int)(yy + (*m <= 2));
}

MMG_FN void mm_break_epoch(MMINTEGER e, int *Y, int *M, int *D,
                           int *h, int *m, int *s, int *wd)
{
    long long days = (e >= 0 ? e : e - 86399) / 86400;
    long long sec = e - days * 86400;
    mm_civil_from_days(days, Y, M, D);
    *h = (int)(sec / 3600);
    *m = (int)((sec / 60) % 60);
    *s = (int)(sec % 60);
    /* 1970-01-01 was a Thursday; 1 = Monday .. 7 = Sunday */
    *wd = (int)(((days + 3) % 7 + 7) % 7) + 1;
}

/* days from civil (Howard Hinnant's algorithm) - avoids timegm */
MMG_FN long long mm_days_from_civil(int y, int m, int d)
{
    long long yy = y;
    long long era, yoe, doy, doe;
    yy -= m <= 2;
    era = (yy >= 0 ? yy : yy - 399) / 400;
    yoe = yy - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* "h:m[:s]" -> fields; how many were parsed.  Replaces sscanf, which
 * neither MSVC (deprecation) nor the Fuzix bytecode runtime is happy
 * with. */
MMG_FN int mm_parse_hms(const char *q, int *h, int *m, int *s)
{
    int n = 0;
    int *out[3];
    out[0] = h; out[1] = m; out[2] = s;
    while (n < 3) {
        int v = 0, got = 0;
        while (*q == ' ') q++;
        while (*q >= '0' && *q <= '9') { v = v * 10 + (*q++ - '0'); got = 1; }
        if (!got)
            break;
        *out[n++] = v;
        if (*q == ':') q++;
        else break;
    }
    return n;
}

/* accepts "dd-mm-yyyy[ hh:mm:ss]", "dd-mm-yy...", "yyyy-mm-dd..."  and
 * '/' as an alternative separator */
MMG_FN MMINTEGER mm_epoch_str(const char *ds)
{
    const char *p = mm_cstr(ds);
    int a = 0, b = 0, c = 0, h = 0, mi = 0, se = 0, d, m, y;
    int n = 0;
    const char *q = p;
    int vals[3];
    while (*q == ' ') q++;
    for (n = 0; n < 3; n++) {
        int v = 0, got = 0;
        while (*q >= '0' && *q <= '9') { v = v * 10 + (*q++ - '0'); got = 1; }
        if (!got) MM_RAISEV("Invalid date", 0);
        vals[n] = v;
        if (n < 2) { if (*q == '-' || *q == '/') q++;
                     else MM_RAISEV("Invalid date", 0); }
    }
    a = vals[0]; b = vals[1]; c = vals[2];
    while (*q == ' ') q++;
    if (*q) {
        if (mm_parse_hms(q, &h, &mi, &se) < 2) { h = mi = se = 0; }
    }
    if (a > 1000) { y = a; m = b; d = c; }
    else          { d = a; m = b; y = c; }
    if (y >= 0 && y < 100) y += 2000;
    if (d < 1 || d > 31 || m < 1 || m > 12 || y < 1902 || y > 2999)
        mm_error("Invalid date");
    return (MMINTEGER)(mm_days_from_civil(y, m, d) * 86400LL
                       + h * 3600LL + mi * 60LL + se);
}

MMG_FN char *mm_datetime(MMINTEGER e)
{
    char b[32];
    char *t = mm_tmp();
    int Y, M, D, h, m, s, wd;
    mm_break_epoch(e, &Y, &M, &D, &h, &m, &s, &wd);
    mm_int_to_str_pad(b, D, '0', 2, 10);      b[2]  = '-';
    mm_int_to_str_pad(b + 3, M, '0', 2, 10);  b[5]  = '-';
    mm_int_to_str_pad(b + 6, Y, '0', 4, 10);  b[10] = ' ';
    mm_int_to_str_pad(b + 11, h, '0', 2, 10); b[13] = ':';
    mm_int_to_str_pad(b + 14, m, '0', 2, 10); b[16] = ':';
    mm_int_to_str_pad(b + 17, s, '0', 2, 10);
    b[19] = 0;
    mm_ssetc(t, b);
    return t;
}

MMG_FN char *mm_time_str(void)
{
    char b[16];
    char *t = mm_tmp();
    int Y, M, D, h, m, s, wd;
    mm_break_epoch(mm_epoch_now(), &Y, &M, &D, &h, &m, &s, &wd);
    mm_int_to_str_pad(b, h, '0', 2, 10);      b[2] = ':';
    mm_int_to_str_pad(b + 3, m, '0', 2, 10);  b[5] = ':';
    mm_int_to_str_pad(b + 6, s, '0', 2, 10);  b[8] = 0;
    mm_ssetc(t, b);
    return t;
}

MMG_FN char *mm_date_str(void)
{
    char b[16];
    char *t = mm_tmp();
    int Y, M, D, h, m, s, wd;
    mm_break_epoch(mm_epoch_now(), &Y, &M, &D, &h, &m, &s, &wd);
    mm_int_to_str_pad(b, D, '0', 2, 10);      b[2] = '-';
    mm_int_to_str_pad(b + 3, M, '0', 2, 10);  b[5] = '-';
    mm_int_to_str_pad(b + 6, Y, '0', 4, 10);  b[10] = 0;
    mm_ssetc(t, b);
    return t;
}

MMG_FN char *mm_day(MMINTEGER e)
{
    static const char *mm_daynames[8] = { "", "Monday", "Tuesday",
                                          "Wednesday", "Thursday", "Friday",
                                          "Saturday", "Sunday" };
    char *t = mm_tmp();
    int Y, M, D, h, m, s, wd;
    mm_break_epoch(e, &Y, &M, &D, &h, &m, &s, &wd);
    mm_ssetc(t, mm_daynames[wd]);
    return t;
}

MMG_FN void mm_set_date(const char *d)
{
    /* keep the current time of day, move the date */
    MMINTEGER now = mm_epoch_now();
    MMINTEGER tod = now % 86400;
    char buf[MM_STRSZ];
    mm_sset(buf, d);
    mm_clock_off += (mm_epoch_str(buf) + tod) - now;
}

MMG_FN void mm_set_time(const char *t)
{
    MMINTEGER now = mm_epoch_now();
    MMINTEGER day = now - (now % 86400);
    int h = 0, m = 0, sec = 0;
    mm_parse_hms(mm_cstr(t), &h, &m, &sec);
    mm_clock_off += (day + h * 3600 + m * 60 + sec) - now;
}

#endif /* MMB_DATETIME_H */
