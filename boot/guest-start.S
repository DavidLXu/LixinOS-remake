/* AArch64 reset, traps and context handoff. Fixed SYS and SIMD encodings
   retain their mnemonics for the TinyCC A64 assembler subset. */
.text
.global _start
_start:
    .long 0xd5034fdf /* msr daifset,#15 */
    .long 0xd2a83fe1 /* movz x1,#0x41ff,lsl #16 */
    mov sp,x1
    bl boot_platform
halt_reset:
    .long 0xd503205f /* wfe */
    b halt_reset
.global install_mmu
install_mmu:
    .long 0xd518c001 /* msr vbar_el1,x1 */
    .long 0xd5381041 /* mrs x1,cpacr_el1 */
    .long 0xb26c0421 /* orr x1,x1,#(3<<20) */
    .long 0xd5181041 /* msr cpacr_el1,x1 */
    .long 0xd5182000 /* msr ttbr0_el1,x0 */
    .long 0xd2860321 /* movz x1,#0x3019 */
    .long 0xf2a01001 /* movk x1,#0x80,lsl #16 */
    .long 0xd5182041 /* msr tcr_el1,x1 */
    .long 0xd2888001 /* mov x1,#0x4400 */
    .long 0xd518a201 /* msr mair_el1,x1 */
    .long 0xd5033f9f /* dsb sy */
    .long 0xd508871f /* tlbi vmalle1 */
    .long 0xd5033f9f /* dsb sy */
    .long 0xd5033fdf /* isb */
    .long 0xd5381001 /* mrs x1,sctlr_el1 */
    .long 0x927ef821 /* bic x1,x1,#2 */
    .long 0xb2400021 /* orr x1,x1,#1 */
    .long 0xd5181001 /* msr sctlr_el1,x1 */
    .long 0xd5033fdf /* isb */
    ret
.global psci_poweroff
psci_poweroff:
    .long 0xd2800100 /* movz x0,#8 */
    .long 0xf2b08000 /* movk x0,#0x8400,lsl #16 */
    .long 0xd4000002 /* hvc #0 */
    ret
.global transfer
transfer:
    .long 0xd2a83fe2 /* movz x2,#0x41ff,lsl #16 */
    mov sp,x2
    .long 0xd508751f /* ic iallu */
    .long 0xd5033f9f /* dsb sy */
    .long 0xd5033fdf /* isb */
    br x1
.global exception
.global user_sync
.global user_timeout
exception:
    .long 0xd5034fdf /* msr daifset,#15 */
    .long 0x58000040 /* ldr x0, .+8 */
    .long 0x14000003 /* b .+12 */
    .quad trial_armed
    .long 0xb9400000 /* ldr w0, [x0] */
    cbz w0, halt_fault
    .long 0xd2800120 /* movz x0, #9 */
    .long 0xf2b08000 /* movk x0, #33792, lsl #16 */
    .long 0xd4000002 /* hvc #0 */
halt_fault:
    .long 0xd503205f /* wfe */
    b halt_fault

/* Resume this EL1 stack on exit or fault. EL0 has its own stack. */
.global enter_user
enter_user:
    .long 0xa9bf53f3 /* stp x19,x20,[sp,#-16]! */
    .long 0xa9bf5bf5 /* stp x21,x22,[sp,#-16]! */
    .long 0xa9bf63f7 /* stp x23,x24,[sp,#-16]! */
    .long 0xa9bf6bf9 /* stp x25,x26,[sp,#-16]! */
    .long 0xa9bf73fb /* stp x27,x28,[sp,#-16]! */
    .long 0xa9bf7bfd /* stp x29,x30,[sp,#-16]! */
    .long 0xd5381043 /* mrs x3,cpacr_el1 */
    .long 0xd53b4224 /* mrs x4,daif */
    .long 0xa9bf13e3 /* stp x3,x4,[sp,#-16]! */
    .long 0x926af863 /* bic x3,x3,#(2<<20) */
    .long 0xd5181043 /* msr cpacr_el1,x3 */
    .long 0xd5184020 /* msr elr_el1,x0 */
    .long 0xd2806803 /* mov x3,#0x340 */
    .long 0xd5184003 /* msr spsr_el1,x3 */
    .long 0xd2800003 /* movz x3, #0 */
    .long 0xf2a8c403 /* movk x3, #17952, lsl #16 */
    .long 0xd5184103 /* msr sp_el0,x3 */
    mov x0,x1
    mov x1,x2
    mov x2,xzr
    mov x3,xzr
    mov x4,xzr
    mov x5,xzr
    mov x6,xzr
    mov x7,xzr
    mov x8,xzr
    mov x9,xzr
    mov x10,xzr
    mov x11,xzr
    mov x12,xzr
    mov x13,xzr
    mov x14,xzr
    mov x15,xzr
    mov x16,xzr
    mov x17,xzr
    mov x18,xzr
    mov x19,xzr
    mov x20,xzr
    mov x21,xzr
    mov x22,xzr
    mov x23,xzr
    mov x24,xzr
    mov x25,xzr
    mov x26,xzr
    mov x27,xzr
    mov x28,xzr
    mov x29,xzr
    mov x30,xzr
    .long 0xd5033fdf /* isb */
    .long 0xd69f03e0 /* eret */
user_sync:
    .long 0xd5385202 /* mrs x2,esr_el1 */
    .long 0xd2800003 /* movz x3, #0 */
    .long 0xf2aac003 /* movk x3, #22016, lsl #16 */
    .long 0xeb03005f /* cmp x2,x3 */
    .long 0x1a9f07e0 /* cset w0,ne */
    b user_done
user_timeout:
    .long 0x58000041 /* ldr x1, .+8 */
    .long 0x14000003 /* b .+12 */
    .quad trial_armed
    .long 0xb9400021 /* ldr w1,[x1] */
    cbnz w1,exception
    .long 0xd51be23f /* msr cntp_ctl_el0,xzr */
    .long 0x52800040 /* mov w0,#2 */
user_done:
    .long 0xa8c113e3 /* ldp x3,x4,[sp],#16 */
    .long 0xd5181043 /* msr cpacr_el1,x3 */
    .long 0xd5033fdf /* isb */
    .long 0xa8c17bfd /* ldp x29,x30,[sp],#16 */
    .long 0xa8c173fb /* ldp x27,x28,[sp],#16 */
    .long 0xa8c16bf9 /* ldp x25,x26,[sp],#16 */
    .long 0xa8c163f7 /* ldp x23,x24,[sp],#16 */
    .long 0xa8c15bf5 /* ldp x21,x22,[sp],#16 */
    .long 0xa8c153f3 /* ldp x19,x20,[sp],#16 */
    .long 0xd51b4224 /* msr daif,x4 */
    ret
