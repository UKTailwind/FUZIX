#ifndef MMB_GFX_H
#define MMB_GFX_H
/*
 *	The whole drawing library in one include, kept for hand-written C
 *	and for .c files generated before the split.  Translated programs
 *	do not use this: the translator includes the per-primitive headers
 *	- mmb_gfx_circle.h, mmb_gfx_text.h, mmb_gfx_map.h - so a program
 *	pays only for the primitives it names.  Including this file
 *	instead costs whatever the name-count rule cannot drop, which is
 *	most of CIRCLE (it is self-recursive) and its extent tables.
 *	See mmb_gfx_pts.h for the full story.
 */

#include "mmb_gfx_circle.h"
#include "mmb_gfx_text.h"
#include "mmb_gfx_map.h"

#endif /* MMB_GFX_H */
