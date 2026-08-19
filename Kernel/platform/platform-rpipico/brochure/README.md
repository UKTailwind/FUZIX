# The Fuzix / Pico Computer 3 brochure

A three-page A4 brochure for promoting the port: cover, what it does,
and the specification with the links.

    fuzix-pc3-brochure.pdf   the thing to hand out or attach
    Main.dc.html             page 1 - cover
    Inside.dc.html           page 2 - what it does
    Specs.dc.html            page 3 - the machine
    canvas.json              artboard layout
    board.jpg                the board photograph
    makepdf.sh               rebuilds the PDF from the three pages

The pages are edited on a Claude Design canvas, which is where the
layout, type and colour are changed by hand; the `.dc.html` files here
are what that canvas holds. To rebuild the PDF after a change, run
`./makepdf.sh`.

Every figure on the pages comes from `FUZIX-PC3-MANUAL.md` - the
Dhrystone rate, the KnivD score, the eclipse timings, the memory and
process numbers, the MMBasic keyword coverage. **When those move, they
move here too**, and the version in the top-right corner of page 1 with
them.
