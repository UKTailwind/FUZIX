# samples

Programs that need a screen, a keyboard, or both, and so cannot be run
by the gates.

`tests/` is for programs the harness can run to a finish and compare
against a `.expected` file. A program that loops until a key is pressed
has no finish, and one whose whole output is pixels has nothing to
compare — but it is still worth knowing that it TRANSLATES and COMPILES,
which is what `make samples` checks:

    make samples        translate and build every samples/*.bas
                        (never run - there is nothing here to run on)

`fcc/sync-mmbc.sh` copies them onto the SD card image alongside the
tests, so they are on the board to be run by hand.
