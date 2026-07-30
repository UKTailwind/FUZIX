#!/bin/sh
#
# Regenerate the DCP double-precision support from pico-sdk.  The
# instruction and canned-sequence includes are copied verbatim;
# dcp_double.S is the SDK's double_aeabi_dcp.S bodies under our own
# small macro prelude (kept in this script's here-doc via
# /tmp/dcp-prelude.S historically - now inline below), with sqrt
# renamed to __dcp_sqrt so musl's sqrt keeps its name.

SDK=${SDK:-/home/peter/src/micropython/lib/pico-sdk}
D=$(cd "$(dirname "$0")" && pwd)
HW=$SDK/src/rp2_common/hardware_dcp/include/hardware
PD=$SDK/src/rp2_common/pico_double/double_aeabi_dcp.S

cp "$HW/dcp_canned.inc.S" "$D/dcp_canned.inc.S"

# One binutils accommodation: our GAS refuses apsr_nzcv as an mrc2
# destination (the SDK's accepts it).  PCMP with that operand becomes
# the raw encoding - mrc2 p4,#0,r15,c0,c0,#1.
sed '/^.macro PCMP rt$/,/^.endm$/c\
.macro PCMP rt\
.ifc \\rt,apsr_nzcv\
 .inst 0xfe10f430\
.else\
 mrc2 p4,#0,\\rt,c0,c0,#1\
.endif\
.endm' "$HW/dcp_instr.inc.S" > "$D/dcp_instr.inc.S"

{
	cat "$D/dcp_prelude.inc.S"
	# body: everything after the SDK's own macro framework, minus
	# the trailing #endif of its HAS_DOUBLE_COPROCESSOR guard
	sed -n '31,454p' "$PD" | sed \
		-e 's/^double_wrapper_section sqrt$/double_section __dcp_sqrt/' \
		-e 's/^saving_func wrapper sqrt$/saving_func regular __dcp_sqrt/'
	# __aeabi_f2d, the one float-side entry the double set needs:
	# libgcc bundles f2d in the same object as dadd/i2d/l2d, so any
	# unresolved member would drag all its soft doubles back in and
	# collide.  From the SDK's float_aeabi_dcp.S, same macros.
	cat <<'EOF'

double_section __aeabi_f2d
saving_func wrapper __aeabi_f2d
  dcp_float2double_m r0,r1,r0
  saving_func_return
EOF
} > "$D/dcp_double.S"
echo "regenerated dcp_double.S ($(wc -l < "$D/dcp_double.S") lines)"

# The 64-bit integer <-> double conversions (__aeabi_l2d/ul2d/d2lz/
# d2ulz): pure M33 integer code from the SDK, same reason as f2d -
# they share libgcc's bundled object with the doubles.
PC=$SDK/src/rp2_common/pico_double/double_conv_m33.S
{
	cat "$D/dcp_prelude.inc.S"
	sed -n '/^double_wrapper_section conv_tod/,$p' "$PC"
} > "$D/dcp_conv_m33.S"
echo "regenerated dcp_conv_m33.S ($(wc -l < "$D/dcp_conv_m33.S") lines)"
