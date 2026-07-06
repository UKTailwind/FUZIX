#ifndef DATE_H
#define DATE_H

void date_today_str(char *datestr);
const char *day_name(int yyyymmdd);
int add_days(int yyyymmdd, int delta);

#endif
