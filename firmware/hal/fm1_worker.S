/* SPDX-License-Identifier: GPL-3.0-only */


	.section .text.cpu1,"ax",@progbits
	.balign 4
	.balign 4
	.globl fm1_c1_entry
fm1_c1_entry:
	cli
	r1 = fm1_c1_trace	
	r2 = 0xC1000001
	[r1 + 0] = r2
	icfg = 0
	usp = _c1_ustack_top
	sp = _c1_sstack_top
	ssp = _c1_sstack_top
	reti = fm1_c1_main
	rti

	.section .worker_stack,"aw",@nobits
	.balign 8
	.globl _c1_ustack
_c1_ustack:			
	.space 20480
_c1_ustack_top:
_c1_sstack:			
	.space 256
	.globl _c1_sstack_top
_c1_sstack_top:
