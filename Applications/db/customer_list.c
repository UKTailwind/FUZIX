#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include "customer_list.h"
#include "db_customer.h"
#include "customer_detail.h"
#include "db_lock.h"
#include "ui.h"
#include "debug.h"

#define MAX_CUSTOMER_ROWS 17    /* Maximum Displayed Customers */
#define MAX_SEARCH_MATCHES 200  /* Maximum Search hits allowed */
#define ROW_NORMAL    0
#define ROW_SELECTED  1

static void rebuild_vis_customers(CustomerList *customer);
static int build_search_results(const char *text);
static void load_search_page(int start_idx, CustomerList *list);
static void rebuild_vis_page(CustomerList *customer);
static int cs_nmatch_disk(const customer_t *c, const char *text);
static const char *strcasestr_simple(const char *haystack, const char *needle);

/* File-local, not global */
static struct {
    char text[CUSTOMER_NAME_MAX + 1];
    int  search_active;      /* User is typing in search text */
    int  filter_active;      /* list is filtered */
} customer_search;

static long next_recno = 0;

/* Search Variables*/
static long matched_recnos[MAX_SEARCH_MATCHES];
static int  match_count = 0;
static int  search_mode = 0;
static int  search_start_idx = 0;

/* Filtered view */
static int visible_indexes[MAX_CUSTOMER_ENTRIES];
static int visible_count;

/* Selection */
static int selected_idx = 0;   /* index into visible_indexes[] */

/* ---------- Customer Screen drawing with dynamic scrolling ---------- */
static void draw_screen(int mode, register const CustomerList *customer, int start_idx, const int *visible_indexes, int visible_count)
{
    char line[100];
    int i, row = 5;
    int max_rows = MAX_CUSTOMER_ROWS;  /* rows available for customer on screen */

    line[0] = '\0';

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));
    ui_attr_rv_off();
    ui_cls();
    ui_puts(1, 1, " Booking Customer Management System                                By D.Pollard");

    ui_puts(3, 1, " ID     Customer Name             Phone 1      Phone 2");
/*
    ui_puts(4, 1, "---------1---------2---------3---------4---------5---------6---------7---------8");
*/
    ui_puts(4, 1, "--------------------------------------------------------------------------------");

    for (i = 0; i < max_rows && start_idx + i < visible_count; i++) {
        int vi = visible_indexes[start_idx + i];
        const customer_list_rec_t *s = &customer->slots[vi];

        if (start_idx + i == selected_idx) {
            ui_attr_rv_on();
        }
        snprintf(line, sizeof(line),
            " %-6.6s %-25.25s %-12.12s %-12.12s",
            s->cs_id,
            s->cs_name,
            s->cs_phone1,
            s->cs_phone2);

        ui_puts(row + i, 1, line);

        if (start_idx + i == selected_idx) {
            ui_attr_rv_off();
        }
    }
    /* Clear remaining rows (important for last page) */
    for (; i < max_rows; i++) {
        ui_puts(row + i, 1, "                                                            ");
    }
    if (mode == CUSTOMER_MODE_SELECT){
        ui_puts(22, 1, "PgUp/PgDn Scroll,  S Search, V View, E Edit, C Create, Enter Select, Esc Exit");
    }
    else
    {
        ui_puts(22, 1, "PgUp/PgDn Scroll,  S Search, V View, E Edit, C Create, Esc Exit");
    }

    ui_puts(UI_COMMAND_ROW, 1, "                                                            "); /* erase old prompt */
    if (customer_search.search_active) {
        ui_puts(UI_COMMAND_ROW, 1, "Command: Search: ");
    } else {
        ui_puts(UI_COMMAND_ROW, 1, "Command: ");
    }
    fflush(stdout);
}

static void run_customer_search(int mode, register CustomerList *customer, int *start_idx)
{
    char buf[CUSTOMER_NAME_MAX + 1];
    int start_col = 19;  /* after "Command: Search: " */
    int overflow;
    int rc;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    ui_puts(22, 1, "                                                            ");
    ui_puts(22, 1, "Enter search text, blank line exits search mode");
    fflush(stdout);

    /* Activate search mode and show prompt */
    customer_search.search_active = 1;
    ui_puts(UI_COMMAND_ROW -1, 1, "                                                                               ");
    ui_puts(UI_COMMAND_ROW -1, 1, "Enter search text, <Esc> exits search mode");
    ui_puts(UI_COMMAND_ROW, 1, "                                                            "); /* erase old prompt */
    ui_puts(UI_COMMAND_ROW, 1, "Command: Search: ");
    fflush(stdout);

    rc = ui_read_line(UI_COMMAND_ROW, start_col, buf, sizeof(buf));

    /* Search mode ALWAYS ends after Enter */
    customer_search.search_active = 0;
    *start_idx = 0;

    /* ESC or blank line both exit search */
    if (rc < 0 || buf[0] == '\0') {
        customer_search.filter_active = 0;
        rebuild_vis_customers(customer);

        /* redraw command + status lines */
        ui_puts(UI_COMMAND_ROW, 1, "Command:                                                             ");
        ui_status("Search cancelled");
        return;
    }
    /* Normal search entered */
    strncpy(customer_search.text, buf, CUSTOMER_NAME_MAX);
    customer_search.text[CUSTOMER_NAME_MAX] = '\0';

    overflow = build_search_results(customer_search.text);

    if (match_count == 0) {
        ui_status("No matches found");
       search_mode = 0;
        return;
    }

    if (overflow) {
        ui_status("Too many matches — refine search");
    }

    search_mode = 1;
    search_start_idx = 0;

    /* Load first page of results */
    load_search_page(search_start_idx, customer);

    /* rebuild visible */
    rebuild_vis_page(customer);
    selected_idx = 0;
    *start_idx = 0;
    draw_screen(mode, customer, *start_idx, visible_indexes, visible_count);
}

static int build_search_results(const char *text)
{
    customer_t c;
    long rec = 0;
    match_count = 0;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    while (db_cs_read(rec, &c) == 0) {
        if (c.cs_status[0] != CUSTOMER_STATUS_DELETED &&
            cs_nmatch_disk(&c, text)) {

            if (match_count < MAX_SEARCH_MATCHES) {
                matched_recnos[match_count++] = rec;
            } else {
                return 1; /* overflow */
            }
        }
        rec++;
    }
    return 0;
}

static int cs_nmatch_disk(const customer_t *c, const char *text)
{
    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));
    if (text[0] == '\0')
        return 1;
    return strcasestr_simple(c->cs_name, text) != NULL;
}

static void load_search_page(int start_idx, CustomerList *list)
{
    customer_t c;
    int i;
    long recno;
    register customer_list_rec_t *dst;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));
    list->count = 0;

    for (i = 0; i < CUSTOMER_PAGE_SIZE; i++) {

        int idx = start_idx + i;
        if (idx >= match_count)
            break;

        recno = matched_recnos[idx];

        if (db_cs_read(recno, &c) != 0)
            break;

        dst = &list->slots[list->count++];

        strcpy(dst->cs_id,     c.cs_id);
        strcpy(dst->cs_name,   c.cs_name);
        strcpy(dst->cs_phone1, c.cs_phone1);
        strcpy(dst->cs_phone2, c.cs_phone2);
    }
}

static const char *strcasestr_simple(const char *haystack, const char *needle)
{
    debug_log((DEBUG_TRACE, FUNC_NAME , "Enter:"));
    if (*needle == '\0')
        return haystack;

    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;

        while (*h && *n &&
               tolower((unsigned char)*h) ==
               tolower((unsigned char)*n)) {
            h++;
            n++;
        }

        if (*n == '\0')
            return haystack;
    }

    return NULL;
}

static int cs_nmatch(const customer_list_rec_t *c, const char *search)
{
    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));
    if (search[0] == '\0')
        return 1;

    return strcasestr_simple(c->cs_name, search) != NULL;
}

static void rebuild_vis_customers(CustomerList *customer)
{
    int i;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    visible_count = 0;

    for (i = 0; i < customer->count; i++) {
        if (!customer_search.filter_active ||
            cs_nmatch(&customer->slots[i],
                                  customer_search.text)) {

            visible_indexes[visible_count++] = i;
        }
    }

    /* This prevents crashes when:  */
    /*   A search reduces the list  */
    /*   B The filter is cleared    */
    if (visible_count == 0) {
        selected_idx = 0;
    } else if (selected_idx >= visible_count) {
        selected_idx = visible_count - 1;
    }
}

static void rebuild_vis_page(CustomerList *customer)
{
    int i;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));
    visible_count = customer->count;

    for (i = 0; i < visible_count; i++) {
        visible_indexes[i] = i;
    }
}

static void draw_customer_row(const CustomerList *customer, int screen_row, int visible_idx, int is_selected)
{
    char line[100];
    int vi = visible_indexes[visible_idx];
    register const customer_list_rec_t *s = &customer->slots[vi];

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    if (is_selected)
        ui_attr_rv_on();

    snprintf(line, sizeof(line),
        " %-6.6s %-25.25s %-12.12s %-12.12s",
        s->cs_id,
        s->cs_name,
        s->cs_phone1,
        s->cs_phone2);

    ui_puts(screen_row, 1, line);

    if (is_selected)
        ui_attr_rv_off();
}

static void apply_loaded_page(CustomerList *customer, int *start_idx, int *selected_idx, int *page_changed, int new_selected_idx)
{
    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));
    rebuild_vis_page(customer);

    if (new_selected_idx < 0) {
        *selected_idx = visible_count - 1;
    } else {
        *selected_idx = new_selected_idx;
    }

    if (*selected_idx < 0)
        *selected_idx = 0;
    if (*selected_idx >= visible_count)
        *selected_idx = visible_count - 1;

    *start_idx = 0;
    *page_changed = 1;
}

/*================ Customer list screen main loop ======================*/
int run_customer_list(int mode, const char *initial_customer_id, char *selected_customer_id)
{
    static CustomerList customer_storage;
    register CustomerList *customer = &customer_storage;
    int selection_moved = 0;
    int start_idx = 0;
    int page_changed = 0;
    long start_recno = 0;  /* which page we are on in file */
    int running;
    int rc;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));

    customer->count = 0;
    selected_idx = 0;

    if (selected_customer_id)
        selected_customer_id[0] = '\0';

    /* NOTE customer.c owns customer_fd
    * Do not return from this function once the file is open.
    */
    rc = db_cs_op_read();

    if (rc < 0) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "ERROR: Unable to open Customer database"));
        ui_status("Unable to open Customer database");
        sleep(4);
        goto cleanup;
    }

    /* Load one screens worth of customers */
    if (db_cs_load_page(start_recno, customer, &next_recno) < 0) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "ERROR: Failed to load customers"));
        ui_status("Failed to load customers");
        sleep(4);
        goto cleanup;
    }
    rebuild_vis_page(customer);

    /* Position list on requested customer if provided */
    if (initial_customer_id && initial_customer_id[0] != '\0') {
        int i;
        debug_log((DEBUG_INFO, FUNC_NAME, "Position list on requested customer"));
        for (i = 0; i < visible_count; i++) {
            int idx = visible_indexes[i];

            if (strncmp(customer->slots[idx].cs_id, initial_customer_id, 6) == 0)
            {
                selected_idx = i;
                /* Ensure selected row is visible */
                if (selected_idx >= MAX_CUSTOMER_ROWS)
                    start_idx = selected_idx - MAX_CUSTOMER_ROWS + 1;
                else
                    start_idx = 0;
                break;
            }
        }
    }

    running = 1;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Before draw_screen: customer->count=%d", customer->count));
    draw_screen(mode, customer, start_idx, visible_indexes, visible_count);

    while (running) {
        int ch;
        int prev_selected_idx = selected_idx;
        char status[80];

        selection_moved = 0;

        /* Status bar (depends on what is visible right now) */
        if (customer->count == 0)
        {
            snprintf(status, sizeof(status), "No Customers Found");
        }
        else
        {
            int from,to;
            int shown = visible_count - start_idx;
            if (shown > MAX_CUSTOMER_ROWS)
            {
                shown = MAX_CUSTOMER_ROWS;
            }
            from = start_recno + 1;
            to   = start_recno + customer->count;
            snprintf(status, sizeof(status), "Showing records %d to %d", from, to);
        }
        ui_status(status);
        ch = ui_read_key();

        debug_log((DEBUG_TRACE, FUNC_NAME, "key=%d '%c'", ch, (ch >= 32 && ch < 127) ? ch : '?'));
        debug_log((DEBUG_TRACE, FUNC_NAME, "RAW key value=%d", ch));
        if (ch == EOF)
            break;

        switch (ch) {

            case UI_KEY_ESC:
                debug_log((DEBUG_TRACE, FUNC_NAME, "ESC pressed"));
                debug_log((DEBUG_TRACE, FUNC_NAME, "ESC handler triggered by key=%d", ch));

                if (search_mode) {
                    /* Exit search mode ONLY */
                    search_mode = 0;
                    customer_search.filter_active = 0;
                    db_cs_load_page(start_recno, customer, &next_recno);
                    apply_loaded_page(customer, &start_idx, &selected_idx, &page_changed, 0);
                    selection_moved = 1;
                    break;  /* Do NOT exit screen */
                }
                /* Normal ESC behaviour */
                if (selected_customer_id){
                    selected_customer_id[0] = '\0';
                }
                running = 0;
                break;

            case UI_KEY_ENTER:
                 debug_log((DEBUG_INFO, FUNC_NAME, "Pressed UI_KEY_ENTER (loop 2)"));
                if (mode == CUSTOMER_MODE_SELECT)
                {
                    debug_log((DEBUG_INFO, FUNC_NAME, "Mode = CUSTOMER_MODE_SELECT (loop 2)"));
                    /* Copy the selected ID to the shared memory location */
                    if (selected_customer_id){
                        strncpy(selected_customer_id, customer->slots[visible_indexes[selected_idx]].cs_id, 6);
                        selected_customer_id[CUSTOMER_ID_MAX] = '\0';
                        debug_log((DEBUG_INFO, FUNC_NAME, "Customer selected id=%s", selected_customer_id));
                    }
                    running = 0;  /* exit the list loop */
                }
                break;

            case UI_KEY_PGDN:
            {
                debug_log((DEBUG_TRACE, FUNC_NAME, "Pressed UI_KEY_PGDN"));
                if (search_mode) {
                    int new_start = search_start_idx + CUSTOMER_PAGE_SIZE;
                    if (new_start < match_count) {
                        search_start_idx = new_start;
                        load_search_page(search_start_idx, customer);
                        apply_loaded_page(customer, &start_idx, &selected_idx, &page_changed, 0);
                        selection_moved = 1;
                    }
                } else {
                    /* existing normal paging */
                    CustomerList temp;
                    long temp_next;

                    db_cs_load_page(next_recno, &temp, &temp_next);

                    if (temp.count > 0) {
                        memcpy(customer, &temp, sizeof(temp));
                        start_recno = next_recno;
                        next_recno = temp_next;
                        apply_loaded_page(customer, &start_idx, &selected_idx, &page_changed, 0);
                        selection_moved = 1;
                    }
                    /* else: do NOTHING (stay exactly where we are) */
                }
                break;
            }
            case UI_KEY_PGUP:
            {
                CustomerList temp;
                long temp_next;

                debug_log((DEBUG_TRACE, FUNC_NAME, "Pressed UI_KEY_PGUP"));

                if (search_mode) {
                    int new_start = search_start_idx - CUSTOMER_PAGE_SIZE;
                    if (new_start < 0)
                        new_start = 0;
                    if (new_start != search_start_idx) {
                        search_start_idx = new_start;
                        load_search_page(search_start_idx, customer);
                        apply_loaded_page(customer, &start_idx, &selected_idx, &page_changed, 0);
                        selection_moved = 1;
                    }
                } else {
                    long new_start;
                    if (start_recno >= CUSTOMER_PAGE_SIZE)
                        new_start = start_recno - CUSTOMER_PAGE_SIZE;
                    else
                        new_start = 0;
                    db_cs_load_page(new_start, &temp, &temp_next);

                    if (temp.count > 0) {
                        memcpy(customer, &temp, sizeof(temp));
                        start_recno = new_start;
                        next_recno = temp_next;
                        apply_loaded_page(customer, &start_idx, &selected_idx, &page_changed, 0);
                        selection_moved = 1;
                    }
                }
                break;
            }

            case 's':
            case 'S':      /* Search Customer */
            {
                debug_log((DEBUG_TRACE, FUNC_NAME, "Pressed S for Search"));
                run_customer_search(mode, customer, &start_idx);
                break;
            }

            case UI_KEY_DOWN:
                debug_log((DEBUG_TRACE, FUNC_NAME, "Pressed UI_KEY_DOWN"));
                if (selected_idx + 1 < visible_count) {
                    selected_idx++;
                    selection_moved = 1;
                } else {
                    if (search_mode) {
                        int new_start = search_start_idx + CUSTOMER_PAGE_SIZE;
                        if (new_start < match_count) {
                            search_start_idx = new_start;
                            load_search_page(search_start_idx, customer);
                            apply_loaded_page(customer, &start_idx, &selected_idx, &page_changed, 0);
                            selection_moved = 1;
                        }
                        /* else: do nothing (end of search results) */
                    } else {
                        /* existing normal paging logic */
                        CustomerList temp;
                        long temp_next;
                        db_cs_load_page(next_recno, &temp, &temp_next);
                        if (temp.count > 0) {
                            memcpy(customer, &temp, sizeof(temp));
                            start_recno = next_recno;
                            next_recno = temp_next;
                            apply_loaded_page(customer, &start_idx, &selected_idx, &page_changed, 0);
                            selection_moved = 1;
                        }
                    }
                }
                break;

            case UI_KEY_UP:
                debug_log((DEBUG_TRACE, FUNC_NAME, "Pressed UI_KEY_UP"));
                if (selected_idx > 0) {
                    selected_idx--;
                    selection_moved = 1;
                } else {
                    if (search_mode) {
                        int new_start = search_start_idx - CUSTOMER_PAGE_SIZE;
                        if (new_start < 0)
                            new_start = 0;

                        if (new_start != search_start_idx) {
                            search_start_idx = new_start;
                            load_search_page(search_start_idx, customer);
                            apply_loaded_page(customer, &start_idx, &selected_idx, &page_changed, -1);
                            selection_moved = 1;
                        }
                    } else {
                        long new_start;
                        CustomerList temp;
                        long temp_next;

                        if(start_recno == 0) {
                            break;         /* We are already on page one so no up from here. */
                        }
                        /* existing normal paging logic (page up) */
                        if (start_recno >= CUSTOMER_PAGE_SIZE)
                            new_start = start_recno - CUSTOMER_PAGE_SIZE;
                        else
                            new_start = 0;
                        db_cs_load_page(new_start, &temp, &temp_next);
                        if (temp.count > 0) {
                            memcpy(customer, &temp, sizeof(temp));
                            start_recno = new_start;
                            next_recno = temp_next;
                            apply_loaded_page(customer, &start_idx, &selected_idx, &page_changed, -1);
                            selection_moved = 1;
                        }
                    }
                }
                break;

            case 'v':
            case 'V':   /* View selected customer */
                debug_log((DEBUG_TRACE, FUNC_NAME, "Pressed V for View Mode"));
                debug_log((DEBUG_TRACE, FUNC_NAME, "Before customer_detail screen View"));
                rc = run_customer_detail(customer->slots[visible_indexes[selected_idx]].cs_id, CUST_VIEW);
                draw_screen(mode, customer, start_idx, visible_indexes, visible_count);
                debug_log((DEBUG_TRACE, FUNC_NAME, "After customer_detail screen View"));
                break;

            case 'e':
            case 'E':   /* Edit selected customer */
                 debug_log((DEBUG_TRACE, FUNC_NAME, "Pressed E for Edit Mode"));
                debug_log((DEBUG_TRACE, FUNC_NAME, "Before customer_detail screen Edit"));
                rc = run_customer_detail(customer->slots[visible_indexes[selected_idx]].cs_id, CUST_EDIT);

                if(rc==CUST_DETAIL_SAVED){
                    char id[7];
                    int i;
                    strncpy(id, customer->slots[visible_indexes[selected_idx]].cs_id, 6);
                    id[6] = '\0';

                    db_cs_load_page(start_recno, customer, &next_recno);
                    customer_search.filter_active = 0;

                    /* paging mode → no filtering */
                    rebuild_vis_page(customer);

                    selected_idx = 0;
                    start_idx = 0;

                    /* reselect the same customer */
                    for (i = 0; i < visible_count; i++) {
                        int idx = visible_indexes[i];
                        if (strncmp(customer->slots[idx].cs_id, id, 6) == 0) {
                          selected_idx = i;
                          break;
                        }
                   }
                    if (selected_idx >= MAX_CUSTOMER_ROWS)
                        start_idx = selected_idx - MAX_CUSTOMER_ROWS + 1;
                    else
                        start_idx = 0;
                }
                draw_screen(mode, customer, start_idx, visible_indexes, visible_count);
                debug_log((DEBUG_TRACE, FUNC_NAME, "After customer_detail screen Edit"));
                break;

            case 'c':
            case 'C':   /* Create new customer */
                debug_log((DEBUG_TRACE, FUNC_NAME, "Pressed C for Create Mode"));
                rc = run_customer_detail(NULL, CUST_CREATE);
                if (rc == CUST_DETAIL_SAVED) {
                    db_cs_load_page(start_recno, customer, &next_recno);
                    customer_search.filter_active = 0;
                }
                rebuild_vis_page(customer);
                selected_idx = 0;
                start_idx = 0;
                draw_screen(mode, customer, start_idx, visible_indexes, visible_count);
                debug_log((DEBUG_TRACE, FUNC_NAME, "After customer_detail screen New"));
                break;

            default:
                break;
        }

        if (selection_moved) {
            if (page_changed) {
               draw_screen(mode, customer, start_idx, visible_indexes, visible_count);
                page_changed = 0;
            }
            else {
                /* Only update two rows */
                int base_row = 5;

                /* redraw old row (remove highlight) */
                draw_customer_row(customer, base_row + (prev_selected_idx - start_idx), prev_selected_idx, ROW_NORMAL);

                /*redraw new row (add highlight) */
                draw_customer_row(customer, base_row + (selected_idx - start_idx), selected_idx, ROW_SELECTED);
                fflush(stdout);
            }
        }
    }

cleanup:
    db_cs_cl_read();

    if (selected_customer_id && selected_customer_id[0] != '\0'){
        return 0;
    }

    return -1;      /* no selection */
}
