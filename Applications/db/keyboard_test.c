#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "debug.h"
#include "ui.h"
#include "ui_common.h"
#include "ui_keyboard_parser.h"

extern const kb_backend_t kb_backend_ansi;

static void run_parser_tests(void);
static void run_test_case(const char *name, const char *input, int expected, const kb_backend_t *backend);
static void draw_keyboard_test_screen(void);

/* ---------- Main ---------- */

int keyboard_test_run(void)
{
    int exit_code = 0;
    int key;
    int y = 13;
    int x = 10;
    char buf[64];

    debug_log(DEBUG_INFO, FUNC_NAME, "Enter: ");

    draw_keyboard_test_screen();

    /*  Main menu */
    for (;;) {

        key = ui_read_key();
        if (key == UI_KEY_NONE)
            continue;

        if (key == 0x19) {   /* Ctrl-Y */
            run_parser_tests();
            continue;
        }

        if (key == 0x18) {   /* Ctrl-X */
            debug_log(DEBUG_INFO, FUNC_NAME, "Exit Keyboard Test");
            exit_code = 0;
            goto cleanup;
        }

        /* Clear previous key display line */
        ui_puts(y, x, "                              ");

        /* Display detected key */
        if (key >= 32 && key < 127) {
            snprintf(buf, sizeof(buf), "ASCII '%c' (0x%02X)", key, key);
            ui_puts(y, x, buf);
        }
        else {
            switch (key) {
            case UI_KEY_UP:
                ui_puts(y, x, "UI_KEY_UP");
                break;
            case UI_KEY_DOWN:
                ui_puts(y, x, "UI_KEY_DOWN");
                break;
            case UI_KEY_LEFT:
                ui_puts(y, x, "UI_KEY_LEFT");
                break;
            case UI_KEY_RIGHT:
                ui_puts(y, x, "UI_KEY_RIGHT");
                break;
            case UI_KEY_PGUP:
                ui_puts(y, x, "UI_KEY_PGUP");
                break;
            case UI_KEY_PGDN:
                ui_puts(y, x, "UI_KEY_PGDN");
                break;
            case UI_KEY_SHIFT_LEFT:
                ui_puts(y, x, "UI_KEY_SHIFT_LEFT");
                break;
            case UI_KEY_SHIFT_RIGHT:
                ui_puts(y, x, "UI_KEY_SHIFT_RIGHT");
                break;
            case UI_KEY_SHIFT_DELETE:
                ui_puts(y, x, "UI_KEY_SHIFT_DELETE");
                break;
            case UI_KEY_ESC:
                ui_puts(y, x, "UI_KEY_ESC");
                break;
            case UI_KEY_INSERT:
                ui_puts(y, x, "UI_KEY_INSERT");
                break;
            case UI_KEY_DELETE:
                ui_puts(y, x, "UI_KEY_DELETE");
                break;
            case UI_KEY_HOME:
                ui_puts(y, x, "UI_KEY_HOME");
                break;
            case UI_KEY_END:
                ui_puts(y, x, "UI_KEY_END");
                break;
            case UI_KEY_ENTER:
                ui_puts(y, x, "UI_KEY_ENTER");
                break;
            case UI_KEY_TAB:
                ui_puts(y, x, "UI_KEY_TAB");
                break;
            case UI_KEY_BACKSPACE:
                ui_puts(y, x, "UI_KEY_BACKSPACE");
                break;
            case UI_KEY_F1:
                ui_puts(y, x, "UI_KEY_F1");
                break;
            case UI_KEY_F2:
                ui_puts(y, x, "UI_KEY_F2");
                break;
            case UI_KEY_F3:
                ui_puts(y, x, "UI_KEY_F3");
                break;
            case UI_KEY_F4:
                ui_puts(y, x, "UI_KEY_F4");
                break;
            case UI_KEY_F5:
                ui_puts(y, x, "UI_KEY_F5");
                break;
            case UI_KEY_F6:
                ui_puts(y, x, "UI_KEY_F6");
                break;
            case UI_KEY_F7:
                ui_puts(y, x, "UI_KEY_F7");
                break;
            case UI_KEY_F8:
                ui_puts(y, x, "UI_KEY_F8");
                break;
            case UI_KEY_F9:
                ui_puts(y, x, "UI_KEY_F9");
                break;
            case UI_KEY_F10:
                ui_puts(y, x, "UI_KEY_F10");
                break;
            case UI_KEY_F11:
                ui_puts(y, x, "UI_KEY_F11");
                break;
            case UI_KEY_F12:
                ui_puts(y, x, "UI_KEY_F12");
                break;

            default:
                snprintf(buf, sizeof(buf), "UNKNOWN key: %d (0x%04X)", key, key);
                ui_puts(y, x, buf);
                break;
            }
        }
    }
cleanup:
    debug_close();
    return exit_code;

}

static void draw_keyboard_test_screen(void)
{
    ui_cls();

    ui_puts(2,  10, "Keyboard Test System                                  By D. Pollard");
    ui_puts(6,  10, "Press any key test");
    ui_puts(7,  10, "Press  Ctrl-X to exit");
    ui_puts(8,  10, "Press  Ctrl-Y to run automated test");
    ui_puts(12, 10, "Last key pressed was:");
}

static void run_parser_tests(void)
{
    ui_cls();

    printf("Parser Test Suite\n\n");
    printf("\nVT52 Tests\n");
    run_test_case("VT52 ESC",   "\x1B"      ,  UI_KEY_ESC,       &kb_backend_vt52);
    run_test_case("VT52 UP",    "\x1B" "A"  ,  UI_KEY_UP,        &kb_backend_vt52);
    run_test_case("VT52 DOWN",  "\x1B" "B"  ,  UI_KEY_DOWN,      &kb_backend_vt52);
    run_test_case("VT52 RIGHT", "\x1B" "C"  ,  UI_KEY_RIGHT,     &kb_backend_vt52);
    run_test_case("VT52 LEFT",  "\x1B" "D"  ,  UI_KEY_LEFT,      &kb_backend_vt52);
    run_test_case("VT52 HOME",  "\x1B" "H"  ,  UI_KEY_HOME,      &kb_backend_vt52);
    run_test_case("VT52 ENTER", "\r"        ,  UI_KEY_ENTER,     &kb_backend_vt52);
    run_test_case("VT52 BACKSPACE", "\x7F"  ,  UI_KEY_BACKSPACE, &kb_backend_vt52);

    run_test_case("VT52 F2 (unsupported)",           "\x1BP",  UI_KEY_INVALID,  &kb_backend_vt52);
    run_test_case("VT52 SHIFT LEFT (unsupported)",   "\x1B<",  UI_KEY_INVALID,  &kb_backend_vt52);
    run_test_case("VT52 SHIFT RIGHT (unsupported)",  "\x1B>",  UI_KEY_INVALID,  &kb_backend_vt52);
    run_test_case("VT52 SHIFT DELETE unsupported)", "\x1B?",  UI_KEY_INVALID,  &kb_backend_vt52);

    printf("\nPress any key to continue with ANSI tests...");
    fflush(stdout);
    ui_read_key();

    printf("\nANSI Tests\n");

    run_test_case("ANSI ESC",       "\x1B",      UI_KEY_ESC,       &kb_backend_ansi);
    run_test_case("ANSI UP",        "\x1B[A",    UI_KEY_UP,        &kb_backend_ansi);
    run_test_case("ANSI DOWN",      "\x1B[B",    UI_KEY_DOWN,      &kb_backend_ansi);
    run_test_case("ANSI RIGHT",     "\x1B[C",    UI_KEY_RIGHT,     &kb_backend_ansi);
    run_test_case("ANSI LEFT",      "\x1B[D",    UI_KEY_LEFT,      &kb_backend_ansi);
    run_test_case("ANSI HOME",      "\x1B[H",    UI_KEY_HOME,      &kb_backend_ansi);
    run_test_case("ANSI END",       "\x1B[F",    UI_KEY_END,       &kb_backend_ansi);
    run_test_case("ANSI PGUP",      "\x1B[5~",   UI_KEY_PGUP,      &kb_backend_ansi);
    run_test_case("ANSI PGDN",      "\x1B[6~",   UI_KEY_PGDN,      &kb_backend_ansi);
    run_test_case("ANSI INSERT",    "\x1B[2~",   UI_KEY_INSERT,    &kb_backend_ansi);
    run_test_case("ANSI DELETE",    "\x1B[3~",   UI_KEY_DELETE,    &kb_backend_ansi);
    run_test_case("ANSI F1",        "\x1BOP",    UI_KEY_F1,        &kb_backend_ansi);
    run_test_case("ANSI F2",        "\x1BOQ",    UI_KEY_F2,        &kb_backend_ansi);
    run_test_case("ANSI F3",        "\x1BOR",    UI_KEY_F3,        &kb_backend_ansi);
    run_test_case("ANSI F4",        "\x1BOS",    UI_KEY_F4,        &kb_backend_ansi);
    run_test_case("ANSI F5",        "\x1B[15~",  UI_KEY_F5,        &kb_backend_ansi);
    run_test_case("ANSI F6",        "\x1B[17~",  UI_KEY_F6,        &kb_backend_ansi);
    run_test_case("ANSI F7",        "\x1B[18~",  UI_KEY_F7,        &kb_backend_ansi);
    run_test_case("ANSI F8",        "\x1B[19~",  UI_KEY_F8,        &kb_backend_ansi);
    run_test_case("ANSI F9",        "\x1B[20~",  UI_KEY_F9,        &kb_backend_ansi);
    run_test_case("ANSI F10",       "\x1B[21~",  UI_KEY_F10,       &kb_backend_ansi);
    run_test_case("ANSI ENTER",     "\r",        UI_KEY_ENTER,     &kb_backend_ansi);
    run_test_case("ANSI BACKSPACE", "\x7F",      UI_KEY_BACKSPACE, &kb_backend_ansi);

    printf("\nPress any key to return to Keyboard Test Menu...");
    fflush(stdout);
    ui_read_key();
    ui_cls();
    draw_keyboard_test_screen();
}

/*static void run_test_case(const char *name, const char *input, int expected) */
static void run_test_case(const char *name, const char *input, int expected, const kb_backend_t *backend)
{
    kb_parser_t kb;
    int i;
    int key;
    int ticks = 0;

    kb_init(&kb, backend);

    key = 0;

    for (i = 0; input[i] != '\0'; i++) {

        key = kb_feed(&kb, (uint8_t)input[i]);

        if (key > 0)
            break;
    }

    while (key == 0 && ticks < kb.backend->esc_timeout + 1) {
        key = kb_tick(&kb);
        ticks++;
    }

    printf("TEST: %s\n", name);
    printf("EXPECTED: %d\n", expected);
    printf("ACTUAL:   %d\n", key);

    if (key == expected)
        printf("RESULT: PASS\n\n");
    else
        printf("RESULT: FAIL\n\n");
}
