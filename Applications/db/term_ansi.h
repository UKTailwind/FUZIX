#ifndef TERM_ANSI_H
#define TERM_ANSI_H

#define ANSI_SAVE_CURSOR    "\033[s"
#define ANSI_RESTORE_CURSOR "\033[u"
#define ANSI_REVERSE_ON     "\033[7m"
#define ANSI_REVERSE_OFF    "\033[27m"
#define ANSI_RESET_ATTRS    "\033[0m"
#define ANSI_CLEAR_SCREEN   "\033[2J\033[H"

#endif   /* TERM_ANSI_H */
