/*
 *	__aeabi_d* double-precision arithmetic on the RP2350's DCP
 *	coprocessor.
 *
 *	The function bodies below are pico-sdk's double_aeabi_dcp.S
 *	(BSD-3-Clause, Copyright (c) 2024 Raspberry Pi (Trading) Ltd),
 *	taken verbatim - regenerate with Library/libs/dcp-sync.sh rather
 *	than editing them.  Only this prelude is ours: it replaces the
 *	SDK's asm_helper macro framework, and the sqrt wrapper is
 *	exported as __dcp_sqrt so musl's sqrt in this library keeps its
 *	name (swapping libm over is a separate step).
 *
 *	These definitions override libgcc's soft-float versions simply
 *	by link order: libc is searched first.  Every entry is a
 *	"saving func": it tests the DCP engaged flag and, if another
 *	context - an interrupted process, since Fuzix preempts - was
 *	mid-operation, saves and restores that context's 24 bytes of
 *	coprocessor state around its own use.  No kernel context-switch
 *	support is needed; the kernel only has to grant CP4 access
 *	(CPACR), which platform-rpipico does at boot.
 */

.syntax unified
.cpu cortex-m33
.thumb

#include "dcp_instr.inc.S"
#include "dcp_canned.inc.S"

.macro double_section name
.section .text.dcp_\name, "ax"
.endm

.macro double_wrapper_section func
double_section \func
.endm

.macro wrapper_func name
.global \name
.thumb_func
.type \name, %function
\name:
.endm

.macro regular_func name
.global \name
.thumb_func
.type \name, %function
\name:
.endm

.macro local_func name
.thumb_func
.type \name, %function
\name:
.endm

