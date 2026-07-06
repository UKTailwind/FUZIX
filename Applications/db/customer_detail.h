#ifndef CUSTOMER_DETAIL_H
#define CUSTOMER_DETAIL_H

#include "db_customer.h"

/* Public modes */
typedef enum {
    CUST_VIEW,
    CUST_EDIT,
    CUST_CREATE
} cust_mode_t;

typedef enum {
    CUST_DETAIL_NO_CHANGE = 0,
    CUST_DETAIL_SAVED     = 1,
    CUST_DETAIL_CANCELLED = 2
} cust_detail_result_t;

/* Public API */
int run_customer_detail(const char *customer_id, cust_mode_t mode);

#endif
