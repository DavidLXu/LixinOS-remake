.section .text.start,"ax"
.global boot_entry
boot_entry:
    msr daifset, #15
    ldr x1, =vectors
    msr vbar_el1,x1
    isb
    ldr x1, =0x41ff0000
    mov sp, x1
    mrs x1, cpacr_el1
    orr x1, x1, #(3 << 20)
    msr cpacr_el1, x1
    isb
    /* Normal non-cacheable RAM permits unaligned C/newlib accesses.
       The lower GiB remains device memory for PL011 and VirtIO. */
    ldr x1, =identity_map
    msr ttbr0_el1, x1
    ldr x1, =0x803019
    msr tcr_el1, x1
    mov x1, #0x4400
    msr mair_el1, x1
    dsb sy
    tlbi vmalle1
    dsb sy
    isb
    mrs x1, sctlr_el1
    bic x1, x1, #2
    orr x1, x1, #1
    msr sctlr_el1, x1
    isb
    ldr x1, =__bss_start
    ldr x2, =__bss_end
1:  cmp x1, x2
    b.hs 2f
    str xzr, [x1], #8
    b 1b
2:  bl boot_main
3:  wfe
    b 3b

.global psci_poweroff
psci_poweroff:
    ldr x0, =0x84000008
    hvc #0
    ret

.global transfer
transfer:
    ldr x2, =0x41ff0000
    mov sp, x2
    ic iallu
    dsb sy
    isb
    br x1

.section .rodata,"a"
.balign 4096
identity_map:
    .quad 0x401, ram_blocks + 3
    .zero 4080

/* One 2 MiB region gets page permissions for the foreground user program. */
.balign 4096
ram_blocks:
.set block,0x40000000
.rept 512
.if block == 0x46000000
    .quad user_pages + 3
.else
    .quad block + 0x705
.endif
.set block,block+0x200000
.endr
.section .data,"aw"
.balign 4096
.global user_pages
user_pages:
.set page,0x46000000
.rept 512
    .quad page + 0x60000000000707
.set page,page+4096
.endr

.section .text,"ax"
.balign 2048
vectors:
.rept 8
    b exception
    .balign 128
.endr
    b user_sync
    .balign 128
    b user_timeout
    .balign 128
.rept 6
    b exception
    .balign 128
.endr
exception:
    msr daifset,#15
    ldr x0, =trial_armed
    ldr w0, [x0]
    cbz w0, halt_fault
    ldr x0, =0x84000009
    hvc #0
halt_fault:
    wfe
    b halt_fault

/* Resume this EL1 stack on exit or fault. EL0 has its own stack. */
.global enter_user
enter_user:
    stp x19,x20,[sp,#-16]!
    stp x21,x22,[sp,#-16]!
    stp x23,x24,[sp,#-16]!
    stp x25,x26,[sp,#-16]!
    stp x27,x28,[sp,#-16]!
    stp x29,x30,[sp,#-16]!
    mrs x3,cpacr_el1
    mrs x4,daif
    stp x3,x4,[sp,#-16]!
    bic x3,x3,#(2<<20)
    msr cpacr_el1,x3
    msr elr_el1,x0
    mov x3,#0x340
    msr spsr_el1,x3
    ldr x3,=0x46200000
    msr sp_el0,x3
    mov x0,x1
    mov x1,x2
.irp reg,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
    mov x\reg,xzr
.endr
    isb
    eret
user_sync:
    mrs x2,esr_el1
    ldr x3,=0x56000000
    cmp x2,x3
    cset w0,ne
    b user_done
user_timeout:
    ldr x1,=trial_armed
    ldr w1,[x1]
    cbnz w1,exception
    msr cntp_ctl_el0,xzr
    mov w0,#2
user_done:
    ldp x3,x4,[sp],#16
    msr cpacr_el1,x3
    isb
    ldp x29,x30,[sp],#16
    ldp x27,x28,[sp],#16
    ldp x25,x26,[sp],#16
    ldp x23,x24,[sp],#16
    ldp x21,x22,[sp],#16
    ldp x19,x20,[sp],#16
    msr daif,x4
    ret
