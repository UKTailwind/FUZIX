/*
* Booking System
*
* Copyright (C) 2026 David Pollard
*
* This software is licensed under the GNU General Public License Version 2
* (GPL v2). See the LICENSE file for the complete license text.
*
* Contributors may add their own copyright notices to their modifications,
* but existing copyright notices must be preserved.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include <unistd.h>
#include "booking_list.h"
#include "customer_list.h"
#include "ui.h"
#include <unistd.h>
#include "keyboard_test.h"
/*#include "ui_common.h"                    Note: ui.h already includes ui_common.h */
#include "term.h"
#include "ui_keyboard_parser.h"

/* main.c - Program entry and menu dispatcher */
/* -------------------------------------------*/

static int first_draw = 1;

static void draw_main_menu(void)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");
    ui_cls();         /* Clear the screen */
    ui_status("");    /* Draw the blank status bar */
/*
                 10        2         3         4         5         6        7          8
                  1234567890123456789012345678901234567890123456789012345678901234567890
*/
    ui_puts(2, 10, "Booking Management System                             By D. Pollard");
    ui_puts(3, 10, "                                                          ver 0.902");
    ui_puts(4, 10, "1.  Bookings");
    ui_puts(5, 10, "2.  Customers");
    ui_puts(6, 10, "3.  Invoices");
    ui_puts(7, 10, "4.  Staff Roster");
    ui_puts(8, 10, "5.  Staff Setup");
    ui_puts(9, 10, "6.  Keyboard Test");
    ui_puts(10, 10, "Esc.  Exit");
    ui_puts(12, 10, "Select option: ");
    fflush(stdout);
}
/* ---------- Main ---------- */

int main(int argc, char *argv[])
{
    int exit_code = 0;
    int menu_choice;

    debug_level_t level = DEBUG_NONE;
    const kb_backend_t *active_backend = &kb_backend_ansi;
    int i;
    for (i = 1; i < argc; i++) {

        if (strcmp(argv[i], "ansi") == 0) {
            active_backend = &kb_backend_ansi;
        }
        else if (strcmp(argv[i], "vt52") == 0) {
            active_backend = &kb_backend_vt52;
        }
        else if (strcmp(argv[i], "error") == 0) {
            level = DEBUG_ERROR;
        }
        else if (strcmp(argv[i], "warn") == 0) {
            level = DEBUG_WARN;
        }
        else if (strcmp(argv[i], "info") == 0) {
            level = DEBUG_INFO;
        }
        else if (strcmp(argv[i], "trace") == 0) {
            level = DEBUG_TRACE;
        }
        else if (strcmp(argv[i], "help") == 0 ||
             strcmp(argv[i], "--help") == 0 ||
             strcmp(argv[i], "-h") == 0) {

            printf("Usage: %s [ansi|vt52] [error|warn|info|trace]\n", argv[0]);
            printf("\n");
            printf("Keyboard types:\n");
            printf("  ansi    ANSI/VT100 compatible terminal (default)\n");
            printf("  vt52    VT52 compatible terminal\n");
            printf("\n");
            printf("Debug levels:\n");
            printf("  error   Errors only\n");
            printf("  warn    Warnings and errors\n");
            printf("  info    Informational messages\n");
            printf("  trace   Verbose tracing\n");
            printf("\n");
            printf("Example: %s vt52 trace\n", argv[0]);            return 0;
        }
        else {
            printf("Unknown option: %s\n", argv[i]);
            printf("Try: %s help\n", argv[0]);
            return 1;
        }
    }

#ifdef DEBUG_ENABLED
    /* Set the debug log folder */
    char log_path[128];
    char *home = getenv("HOME");
    if (!home) {
        home = ".";   /* fallback if HOME not set */
    }
    snprintf(log_path, sizeof(log_path), "%s/booking.log", home);
    debug_init(log_path, level);
#else
    if(level != DEBUG_NONE){
        printf("Error: Debug is not available.  Not Compiled in");
        return(-1);
    }
#endif

    /* If debug enabled always show the Program Start Message */
    debug_log(DEBUG_ERROR, FUNC_NAME, "Enter: ******** Program Start *********");

    /* Set the keyboard type read from the command line */
    ui_set_keyboard_backend(active_backend);

#ifdef DEBUG_ENABLED
    /* Temporary keyboard test */
    printf("Keyboard backend: %s\n", active_backend == &kb_backend_vt52 ? "VT52" : "ANSI");
    sleep(2);
    /* End temporary test */
#endif

    /* Enable raw terminal mode */
    term_raw_on();
    draw_main_menu();
    sleep(2);

    /* Main menu */
    for (;;) {
        debug_log(DEBUG_INFO, FUNC_NAME, "Menu for loop begin");

        if (!first_draw)
        {
            draw_main_menu();
        }
        first_draw=0;

        menu_choice = ui_read_key();

        if (menu_choice == '\n' || menu_choice == '\r')
            continue;

        debug_log(DEBUG_TRACE, FUNC_NAME, "Main menu key=%d '%c'", menu_choice, (menu_choice >= 32 && menu_choice < 127) ? menu_choice : '?');

        if (menu_choice == '1')
        {
            debug_log(DEBUG_TRACE, FUNC_NAME, "About to call booking_run()");
            run_booking_list();
            debug_log(DEBUG_TRACE, FUNC_NAME, "Returned from booking_run()");
        }

        else if (menu_choice == '2')
        {
            debug_log(DEBUG_INFO, FUNC_NAME, "About to call customer_run()");
            run_customer_list(CUSTOMER_MODE_MANAGE, NULL, NULL);
            debug_log(DEBUG_INFO, FUNC_NAME, "Returned from customer_run()");
        }

        else if (menu_choice == '3')
        {
            debug_log(DEBUG_TRACE, FUNC_NAME, "About to call invoice_run()");
            ui_status("TODO");
            sleep(2);
            debug_log(DEBUG_TRACE, FUNC_NAME, "Returned from invoice_run()");
        }
        else if (menu_choice == '4')
        {
            debug_log(DEBUG_TRACE, FUNC_NAME, "About to call staff_roster_run()");
            ui_status("TODO");
            sleep(2);
            debug_log(DEBUG_TRACE, FUNC_NAME, "Returned from staff_roster_run()");
        }

        else if (menu_choice == '5')
        {
            debug_log(DEBUG_TRACE, FUNC_NAME, "About to call staff_setup_run()");
            ui_status("TODO");
            sleep(2);
            debug_log(DEBUG_TRACE, FUNC_NAME, "Returned from staff_setup_run()");
        }
        else if (menu_choice == '6')
        {
            debug_log(DEBUG_INFO, FUNC_NAME, "About to call keyboard_test_run()");
            keyboard_test_run();
            debug_log(DEBUG_INFO, FUNC_NAME, "Returned from keyboard_run()");
        }

        else if (menu_choice == UI_KEY_ESC)
        {
            debug_log(DEBUG_INFO, FUNC_NAME, "Exiting menu loop");
            exit_code = 0;
            break; /* Do cleanup */
        }

        else {
            debug_log(DEBUG_INFO, FUNC_NAME, "Unknown key pressed");
        }
        debug_log(DEBUG_INFO, FUNC_NAME, "Menu for loop end");
    }

    /* cleanup */
    debug_log(DEBUG_INFO, FUNC_NAME, "Cleanup Run");
    term_raw_off();
    ui_cls();
    sleep(1);
    debug_close();
    return exit_code;
}
