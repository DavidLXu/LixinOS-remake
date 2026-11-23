/* Encodings for system registers outside the TinyCC assembler vocabulary. */
#define A64_mask_irq ".long 0xd50342df" /* msr daifset,#2 */
#define A64_unmask_irq ".long 0xd50342ff" /* msr daifclr,#2 */
#define A64_stop_timer ".long 0xd51be23f" /* msr cntp_ctl_el0,xzr */
#define A64_invalidate_tlb ".long 0xd508871f" /* tlbi vmalle1 */
#define A64_invalidate_ic ".long 0xd508751f" /* ic iallu */
static unsigned long cpu_ticks(void) {
    unsigned long n;
    __asm__ volatile(".long 0xd53be029\nmov %0,x9":"=r"(n)::"x9");
    return n;
}
static unsigned long cpu_frequency(void) {
    unsigned long n;
    __asm__ volatile(".long 0xd53be009\nmov %0,x9":"=r"(n)::"x9");
    return n;
}
static void cpu_timer(unsigned long deadline) {
    __asm__ volatile("mov x9,%0\n.long 0xd51be249\nmov x9,#1\n.long 0xd51be229\nisb"::"r"(deadline):"x9","memory");
}
