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
    .quad 0x401, 0x40000705
    .zero 4080

.section .text,"ax"
.balign 2048
vectors:
.rept 16
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
