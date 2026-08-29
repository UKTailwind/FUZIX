# TSCP chess

Tom Kerrigan's Simple Chess Program, in MMBasic. The engine is TSCP
1.81; the MMBasic conversion is Ceptimus's, and the self-play switch is
Volhout's. Tom Kerrigan has agreed to the MMBasic version being
distributed.

    chess.bas          the program
    12piececol.bmp     the pieces: twelve 20x20 sprites in a 240x20 strip
    book.txt           the opening book, 2014 lines of move sequences
    mkspr.bas          makes 12piece.spr out of the sheet, on the board

It must be run FROM THIS DIRECTORY, like `robots` and `retic`: it opens
`12piececol` and `book.txt` by relative name.

    cd /root/MMBasic/chess
    cc chess.bas
    ./chess.bc

Type `help` at the `tscp>` prompt. `on` makes the computer play the side
to move, so typing it at every prompt is a game against itself; the
program has a switch for that - `Const autoplay=1` near the top - which
answers every prompt with `on`.

## What this exercises

Both of the port's sprite loaders, and it is the reason they exist in
their current shape:

* `Sprite Loadbmp i, "12piececol", x, y, 20, 20` cuts a 20x20 piece out
  of the strip - the reference's window form, and the file named without
  its extension, which is MMBasic's `AppendDefaultExtension`.
* `Sprite write s, x, y, &B100` draws one transparently, with no LIFO
  bookkeeping.

`SPRITE LOAD` reads the same pieces from a `.spr` text file instead.
There is no `.spr` in the TSCP package, but the machine can make one:
`mkspr.bas` here cuts the twelve tiles with `SPRITE LOADBMP`, writes
each one opaquely to the screen, reads the pixels back and prints them
as the hex digits `SPRITE LOAD` expects. Swap the body of
`load_sprites` for

    Sprite Load "12piece.spr", 1, 1

and the board is pixel for pixel the same.

## Two notes for anyone reading the BASIC

The piece drawing in `draw_chessboard` was commented out in the file
this came from, because the port could not load the sprites yet. It is
live here.

`book.txt` arrived with CRLF line endings and is stored with LF. The
program reads it with `Input #`, which accepts either.
