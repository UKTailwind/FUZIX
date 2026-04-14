/* yuk configuration */

/* suits - here we use the Amstrad CPC glyphs, change for other platforms*/
#if 0
#define S_CLUB '\342'
#define S_DIAMOND '\343'
#define S_HEART '\344'
#define S_SPADE '\345'
#else
#define S_CLUB		'C'
#define S_DIAMOND	'D'
#define S_HEART		'H'
#define S_SPADE		'S'
#endif

#define CARD_HEIGHT 4

#ifndef NCURSES_VERSION
#define CURSES_NO_COLOUR
#endif
