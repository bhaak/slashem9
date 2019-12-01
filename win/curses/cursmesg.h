/* vim:set cin ft=c sw=4 sts=4 ts=8 et ai cino=Ls\:0t0(0 : -*- mode:c;fill-column:80;tab-width:8;c-basic-offset:4;indent-tabs-mode:nil;c-file-style:"k&r" -*-*/

#ifndef CURSMESG_H
#define CURSMESG_H

/* Global declarations */

void curses_message_win_puts(const char *message, bool recursed);
int curses_block(bool require_tab);
int curses_more(void);
void curses_clear_unhighlight_message_window(void);
void curses_message_win_getline(const char *prompt, char *answer, int buffer);
void curses_last_messages(void);
void curses_init_mesg_history(void);
void curses_prev_mesg(void);
void curses_count_window(const char *count_text);

#endif /* CURSMESG_H */
