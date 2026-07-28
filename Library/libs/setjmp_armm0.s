# 0 "setjmp_armm0.S"
# 0 "<built-in>"
# 0 "<command-line>"
# 1 "setjmp_armm0.S"
 .cpu cortex-m0
 .code 16
    .text
    .align 4
    .global setjmp
 .thumb_func

setjmp:


 stmia r0!, {r4, r5, r6, r7}



 mov r1, r8
 mov r2, r9
 mov r3, r10
 mov r4, fp
 mov r5, sp
 mov r6, lr
 stmia r0!, {r1, r2, r3, r4, r5, r6}
 sub r0, #10*4



 ldmia r0!, {r4, r5, r6, r7}


 mov r0, #0
 bx lr
