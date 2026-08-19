# Host build of mmbc.  The board build (arm-none-eabi + Fuzix libc)
# lands with stage 7 in FUZIX Applications/CC.

CC = gcc
CFLAGS = -O2 -g -Wall -Wextra -Wno-unused-parameter

OBJS = mmbc_main.o mmbc_lex.o mmbc_util.o mmbc_tab.o mmbc_sym.o \
       mmbc_expr.o mmbc_builtin.o mmbc_decl.o mmbc_walk.o mmbc_stmt.o \
       mmbc_type.o mmbc_out.o mmbc_font.o

mmbc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(OBJS): mmbc.h

clean:
	rm -f mmbc $(OBJS)
