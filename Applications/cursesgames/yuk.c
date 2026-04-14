/* yuk
 * (c) 2017 Niels Sonnich Poulsen (http://nielssp.dk)
 * licensed under the MIT license (see the LICENSE file)
 *
 * Ported to FUZIX.
 * FUZIX-specific changes (c) 2026 Antonio Casado.
 * Some further FUZIX squashing (c) 2026 Alan Cox.
 */

#include <curses.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <time.h>

#include "yuk.h"

#ifdef CURSES_NO_COLOUR
/* FUZIX curses lacks attron */
#define start_color()
#define init_pair(a,b,c)
#define COLOR_PAIR(a) 0
#endif

#define HEART 2
#define DIAMOND 10
#define SPADE 4
#define CLUB 12

#define TABLEAU 9
#define FOUNDATION 17

#define BOTTOM 1
#define RED 2
#define BLACK 4

#define ACE 1
#define JACK 11
#define QUEEN 12
#define KING 13

const char suits[] = {HEART, DIAMOND, SPADE, CLUB};

unsigned int EMPTY_STACK_TOP[7];
unsigned int EMPTY_STACK_MID[7];
unsigned int EMPTY_STACK_BOT[7];

unsigned int CARD_FRONT_TOP[7];
unsigned int CARD_FRONT_MID[7];
unsigned int CARD_FRONT_BOT[7];

unsigned int CARD_BACK_TOP[7];
unsigned int CARD_BACK_MID[7];
unsigned int CARD_BACK_BOT[7];


typedef struct card Card;

struct card {
  Card *prev;
  Card *next;
  char up;
  char suit;
  char rank;
};

int cur_x = 0;
int cur_y = 0;

int max_x = 0;
int max_y = 0;

Card *selection = NULL;
Card *under_cursor = NULL;

void init_templates(void)
{
    EMPTY_STACK_TOP[0] = ACS_ULCORNER;
    EMPTY_STACK_TOP[1] = ACS_HLINE;
    EMPTY_STACK_TOP[2] = ACS_HLINE;
    EMPTY_STACK_TOP[3] = ACS_HLINE;
    EMPTY_STACK_TOP[4] = ACS_HLINE;
    EMPTY_STACK_TOP[5] = ACS_URCORNER;
    EMPTY_STACK_TOP[6] = 0;

    EMPTY_STACK_MID[0] = ACS_VLINE;
    EMPTY_STACK_MID[1] = ' ';
    EMPTY_STACK_MID[2] = ' ';
    EMPTY_STACK_MID[3] = ' ';
    EMPTY_STACK_MID[4] = ' ';
    EMPTY_STACK_MID[5] = ACS_VLINE;
    EMPTY_STACK_MID[6] = 0;

    EMPTY_STACK_BOT[0] = ACS_LLCORNER;
    EMPTY_STACK_BOT[1] = ACS_HLINE;
    EMPTY_STACK_BOT[2] = ACS_HLINE;
    EMPTY_STACK_BOT[3] = ACS_HLINE;
    EMPTY_STACK_BOT[4] = ACS_HLINE;
    EMPTY_STACK_BOT[5] = ACS_LRCORNER;
    EMPTY_STACK_BOT[6] = 0;

    memcpy(CARD_FRONT_TOP, EMPTY_STACK_TOP, sizeof(EMPTY_STACK_TOP));
    memcpy(CARD_FRONT_MID, EMPTY_STACK_MID, sizeof(EMPTY_STACK_MID));
    memcpy(CARD_FRONT_BOT, EMPTY_STACK_BOT, sizeof(EMPTY_STACK_BOT));

    memcpy(CARD_BACK_TOP, EMPTY_STACK_TOP, sizeof(EMPTY_STACK_TOP));
    memcpy(CARD_BACK_MID, EMPTY_STACK_MID, sizeof(EMPTY_STACK_MID));
    memcpy(CARD_BACK_BOT, EMPTY_STACK_BOT, sizeof(EMPTY_STACK_BOT));
}

Card *new_card(char suit, char rank) {
  register Card *card = malloc(sizeof(Card));
  if (card == NULL) {
    fprintf(stderr, "Out of memory.\n");
    exit(1);
  }
  card->prev = NULL;
  card->next = NULL;
  card->up = 1;
  card->suit = suit;
  card->rank = rank;
  return card;
}

Card *new_deck(void) {
  register int suit,rank;
  register Card *deck = new_card(BOTTOM, 0);
  register Card *prev = deck;
  for (suit = 0; suit < 4; suit++) {
    for (rank = 1; rank <= 13; rank++) {
      Card *card = new_card(suits[suit], rank);
      prev->next = card;
      card->prev = prev;
      prev = card;
    }
  }
  return deck;
}

unsigned char card_suit(register Card *card) {
    switch (card->suit) {
    case HEART:
        return S_HEART;
    case SPADE:
        return S_SPADE;
    case DIAMOND:
        return S_DIAMOND;
    case CLUB:
        return S_CLUB;
    }
    return ' ';
}

static const char *card_str(register Card *card) {
    static char buf[6];
    switch (card->rank) {
    case ACE:
        return "A";
    case KING:
        return "K";
    case QUEEN:
        return "Q";
    case JACK:
        return "J";
    default:
        snprintf(buf, 6, "%d", card->rank);
        return buf;
    }
}

void prn_card_name_l(int y, int x, Card *card) {
    mvprintw(y, x, card_str(card));
}

void prn_card_name_r(int y, int x, register Card *card) {
    move(y, x - 1 - (card->rank == 10));
    addch(card_suit(card));
    printw("%s", card_str(card));
}

Card *shuffle_stack(register Card *stack) {
  int n = 2;
  register Card *new;
  Card *next;
  if (!stack->next) {
    return stack;
  }
  new = stack;
  next = stack->next;
  new->next = NULL;
  new->prev = NULL;
  while (next) {
    Card *next_next = next->next;
    int i = rand() % n;
    if (i == 0) {
      new->prev = next;
      next->prev = NULL;
      next->next = new;
      new = next;
    } else {
      Card *c = new;
      int j;
      for (j = 1; j < i; j++) {
        c = c->next;
      }
      if (c->next) {
        c->next->prev = next;
      }
      next->next = c->next;
      c->next = next;
      next->prev = c;
    }
    n++;
    next = next_next;
  }
  return new;
}

Card *take_card(register Card *card) {
  if (card->prev) {
    card->prev->next = card->next;
  }
  if (card->next) {
    card->next->prev = card->prev;
  }
  card->prev = NULL;
  card->next = NULL;
  return card;
}

Card *take_stack(register Card *stack) {
  if (stack->prev) {
    stack->prev->next = NULL;
  }
  stack->prev = NULL;
  return stack;
}

void move_stack(register Card *dest, register Card *src) {
  if (dest->next) {
    move_stack(dest->next, src);
  } else {
    dest->next = src;
    if (src->prev) src->prev->next = NULL;
    src->prev = dest;
  }
}

Card *get_bottom(register Card *stack) {
  if (stack->prev)
    return get_bottom(stack->prev);
  return stack;
}

char get_stack_type(Card *stack) {
  return get_bottom(stack)->suit;
}

int move_to_tableau(register Card *dest, register Card *src) {
  if (dest->next)
    return move_to_tableau(dest->next, src);
  if ((dest->suit & BOTTOM && src->rank == KING) || ((dest->suit & RED) != (src->suit & RED) && src->rank == dest->rank - 1)) {
    dest->next = src;
    if (src->prev) src->prev->next = NULL;
    src->prev = dest;
    return 1;
  }
  return 0;
}

int move_to_foundation(register Card *dest, register Card *src) {
  if (src->next)
    return 0;
  if (dest->next)
    return move_to_foundation(dest->next, src);
  if ((dest->suit & BOTTOM && src->rank == ACE) || (dest->suit == src->suit && src->rank == dest->rank + 1)) {
    dest->next = src;
    if (src->prev) src->prev->next = NULL;
    src->prev = dest;
    return 1;
  }
  return 0;
}

int legal_move_stack(register Card *dest, register Card *src) {
  if (get_bottom(src) == get_bottom(dest)) {
    return 0;
  }
  switch (get_stack_type(dest)) {
    case TABLEAU:
      return move_to_tableau(dest, src);
    case FOUNDATION:
      return move_to_foundation(dest, src);
  }
  return 0;
}
static void draw_tpl(int y, int x, const unsigned int *tpl) {
    move(y, x);
    while(*tpl)
        addch(*tpl++);
}

static void draw_selected_mark(int y, int x)
{
    mvaddch(y, x - 1, ACS_RARROW);
    mvaddch(y, x + 6, ACS_LARROW);
}

void print_card(int y, int x, register Card *card, int full) {
  register int i;
  int y2 = y + full * (CARD_HEIGHT - 1);
  if (y2 > max_y) max_y = y2;
  if (x > max_x) max_x = x;
  if (y <= cur_y && y2 >= cur_y && x == cur_x) under_cursor = card;
  y = 1 + y;
  x = 2 + x * 8;
  if (card == selection) {
    draw_selected_mark(y, x);
  }
  if (card->suit & BOTTOM) {
    attron(COLOR_PAIR(1));
    draw_tpl(y, x, EMPTY_STACK_TOP);
    if (full && CARD_HEIGHT > 1) {
      for (i = 1; i < CARD_HEIGHT - 1; i++) {
        draw_tpl(y + i, x, EMPTY_STACK_MID);
      }
      draw_tpl(y + CARD_HEIGHT - 1, x, EMPTY_STACK_BOT);
    }
    if (card->rank > 0) {
      prn_card_name_l(y, x + 1, card);
    }
  } else if (!card->up) {
    attron(COLOR_PAIR(2));
    draw_tpl(y, x, CARD_BACK_TOP);
    if (full && CARD_HEIGHT > 1) {
      for (i = 1; i < CARD_HEIGHT - 1; i++) {
        draw_tpl(y + i, x, CARD_BACK_MID);
      }
      draw_tpl(y + CARD_HEIGHT - 1, x, CARD_BACK_BOT);
    }
  } else {
    if (card->suit & RED) {
      attron(COLOR_PAIR(3));
    } else {
      attron(COLOR_PAIR(4));
    }
    draw_tpl(y, x, CARD_FRONT_TOP);
    if (full && CARD_HEIGHT > 1) {
      for (i = 1; i < CARD_HEIGHT - 1; i++) {
        draw_tpl(y + i, x, CARD_FRONT_MID);
      }
      draw_tpl(y + CARD_HEIGHT - 1, x, CARD_FRONT_BOT);
      prn_card_name_r(y + CARD_HEIGHT - 1, x + 4, card);
    }
    prn_card_name_l(y, x + 1, card);
  }

}

void print_card_top(int y, int x, Card *card) {
  print_card(y, x, card, 0);
}

void print_card_full(int y, int x, Card *card) {
  print_card(y, x, card, 1);
}

void print_stack(int y, int x, register Card *bottom) {
  if (bottom->next) {
    print_stack(y, x, bottom->next);
  } else {
    print_card_full(y, x, bottom);
  }
}

void print_tableau(int y, int x, register Card *bottom) {
  if (bottom->next && bottom->suit & BOTTOM) {
    print_tableau(y, x, bottom->next);
  } else {
    if (bottom->next) {
      print_card_top(y, x, bottom);
      print_tableau(y + 1, x, bottom->next);
    } else {
      print_card_full(y, x, bottom);
    }
  }
}
static void scan_card(int y, int x, Card *card, int full)
{
    register int y2 = y + full * (CARD_HEIGHT - 1);

    if (y2 > max_y)
        max_y = y2;
    if (x > max_x)
        max_x = x;
    if (y <= cur_y && y2 >= cur_y && x == cur_x)
        under_cursor = card;
}

static void scan_stack(int y, int x, register Card *bottom)
{
    if (bottom->next)
        scan_stack(y, x, bottom->next);
    else
        scan_card(y, x, bottom, 1);
}

static void scan_tableau(int y, int x, register Card *bottom)
{
    if (bottom->next && (bottom->suit & BOTTOM)) {
        scan_tableau(y, x, bottom->next);
    } else {
        if (bottom->next) {
            scan_card(y, x, bottom, 0);
            scan_tableau(y + 1, x, bottom->next);
        } else {
            scan_card(y, x, bottom, 1);
        }
    }
}

static void update_cursor_state(Card *foundations[4], Card *tableaux[7])
{
    register int i;

    under_cursor = NULL;
    max_x = max_y = 0;

    for (i = 0; i < 4; i++)
        scan_stack(0, 3 + i, foundations[i]);

    for (i = 0; i < 7; i++)
        scan_tableau(1 + CARD_HEIGHT, i, tableaux[i]);
}

static void redraw_board(Card *foundations[4], Card *tableaux[7])
{
    register int i;

    clear();
    under_cursor = NULL;
    max_x = max_y = 0;

    for (i = 0; i < 4; i++)
        print_stack(0, 3 + i, foundations[i]);

    for (i = 0; i < 7; i++)
        print_tableau(1 + CARD_HEIGHT, i, tableaux[i]);

    move(1 + cur_y, 2 + cur_x * 8);
    refresh();
}

static void cleanup(void)
{
  endwin();
}

int main(int argc, char *argv[]) {
  int ch;
  register uint_fast8_t i;
  uint_fast8_t j, run = 1, enable_colors = 1, dirty = 1;
  int x,y;
  Card *deck;
  Card *foundations[4];
  Card *tableaux[7];
  foundations[0] = NULL;
  foundations[1] = NULL;
  foundations[2] = NULL;
  foundations[3] = NULL;
  tableaux[0] = NULL;
  tableaux[1] = NULL;
  tableaux[2] = NULL;
  tableaux[3] = NULL;
  tableaux[4] = NULL;
  tableaux[5] = NULL;
  tableaux[6] = NULL;
  if (argc > 1 && argv[1][0] == '-') {
    switch (argv[1][1]) {
      case 'm':
        enable_colors = 0;
        break;
      case 'v':
        printf("yuk 1.0-fz1\n");
        return 0;
      default:
        printf("usage: %s [option]\n", argv[0]);
        printf("options:\n");
        printf("  -v Version\n");
        printf("  -h Usage\n");
#ifndef CURSES_NO_COLOR
        printf("  -m Disable colors\n");
#endif        
        printf("keys:\n");
        printf("  hjkl  Move cursor\n");
        printf("  space Select card\n");
        printf("  m     Move selection\n");
        printf("  a     Move to foundation\n");
        return 0;
    }
  }
  setlocale(LC_ALL, "");
  srand(time(NULL));
  if (initscr() == NULL) {
    fprintf(stderr, "%s: unable to init curses.\n", argv[0]);
    exit(1);
  }
  atexit(cleanup);

  getmaxyx(stdscr, y, x);
  if (y < 20 || x < 64) {
    fprintf(stderr, "%s: screen size of %u x %u is too small.\n",
      argv[0], y, x);
    exit(1);
  }
  init_templates();
  if (enable_colors) {
    start_color();
    init_pair(1, 7, 0);
    init_pair(2, 7, 4);
    init_pair(3, 1, 7);
    init_pair(4, 0, 7);
  }
  raw();
  clear();
  curs_set(1);
  keypad(stdscr, 1);
  noecho();

  deck = new_deck();
  move_stack(deck, shuffle_stack(take_stack(deck->next)));

  for (i = 0; i < 4; i++) {
    foundations[i] = new_card(FOUNDATION, ACE);
  }

  for (i = 0; i < 7; i++) {
    tableaux[i] = new_card(TABLEAU, KING);
  }

  move_stack(tableaux[0], take_card(deck->next));

  for (i = 1; i < 7; i++) {
    for (j = 0; j < i; j++) {
      Card *card = take_card(deck->next);
      card->up = 0;
      move_stack(tableaux[i], card);
    }
  }
  for (i = 1; i < 7; i++) {
    for (j = 0; j < 5; j++) {
      move_stack(tableaux[i], take_card(deck->next));
    }
  }

  while (run) {
    if (dirty) {
        redraw_board(foundations, tableaux);
        dirty = 0;
    } else {
        update_cursor_state(foundations, tableaux);
        move(1 + cur_y, 2 + cur_x * 8);
        refresh();
    }

    ch = getch();

    switch (ch) {
        case 'h':
        case 260:
            cur_x--;
            if (cur_x < 0)
                cur_x = 0;
            break;

        case 'j':
        case 258:
            cur_y++;
            if (cur_y > max_y)
                cur_y = max_y;
            break;

        case 'k':
        case 259:
            cur_y--;
            if (cur_y < 0)
                cur_y = 0;
            break;

        case 'l':
        case 261:
            cur_x++;
            if (cur_x > max_x)
                cur_x = max_x;
            break;

        case 'm':
            if (selection && under_cursor && !(selection->suit & BOTTOM)) {
                if (legal_move_stack(under_cursor, selection)) {
                    selection = NULL;
                    dirty = 1;
                }
            }
            break;

        case 10:
        case 'f':
            if (under_cursor && under_cursor->up && !under_cursor->next) {
                for (i = 0; i < 4; i++) {
                    if (legal_move_stack(foundations[i], under_cursor)) {
                        selection = NULL;
                        dirty = 1;
                        break;
                    }
                }
            }
            break;

        case 'a':
            for (i = 0; i < 7; i++) {
                Card *c = tableaux[i];
                while (c->next)
                    c = c->next;
                if (!(c->suit & BOTTOM)) {
                    if (!c->up) {
                        c->up = 1;
                        dirty = 1;
                        break;
                    } else {
                        int legal = 0;
                        for (j = 0; j < 4; j++) {
                            if (legal_move_stack(foundations[j], c)) {
                                selection = NULL;
                                dirty = 1;
                                legal = 1;
                                break;
                            }
                        }
                        if (legal)
                            break;
                    }
                }
            }
            break;

        case ' ':
            if (under_cursor) {
                if (!under_cursor->up) {
                    if (!under_cursor->next) {
                        under_cursor->up = 1;
                        dirty = 1;
                    }
                } else if (!(under_cursor->suit & BOTTOM)) {
                    if (selection == under_cursor && !under_cursor->next) {
                        for (i = 0; i < 4; i++) {
                            if (legal_move_stack(foundations[i], under_cursor)) {
                                selection = NULL;
                                dirty = 1;
                                break;
                            }
                        }
                        if (!selection)
                            break;
                    }

                    if (selection != under_cursor) {
                        selection = under_cursor;
                        dirty = 1;
                    }
                }
            }
            break;

        case 27:
            if (selection) {
                selection = NULL;
                dirty = 1;
            }
            break;

        case 'q':
            run = 0;
            break;

        default:
/*            mvprintw(0, 0, "%d", ch); */
            break;
    }
  }
  exit(0);
}
