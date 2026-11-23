/* musl AArch64 setjmp/longjmp, see /src/musl/COPYRIGHT. */
.text
.global setjmp
setjmp:
    .long 0xa9005013  /* stp	x19, x20, [x0] */
    .long 0xa9015815  /* stp	x21, x22, [x0, #16] */
    .long 0xa9026017  /* stp	x23, x24, [x0, #32] */
    .long 0xa9036819  /* stp	x25, x26, [x0, #48] */
    .long 0xa904701b  /* stp	x27, x28, [x0, #64] */
    .long 0xa905781d  /* stp	x29, x30, [x0, #80] */
    .long 0x910003e2  /* mov	x2, sp */
    .long 0xf9003402  /* str	x2, [x0, #104] */
    .long 0x6d072408  /* stp	d8, d9, [x0, #112] */
    .long 0x6d082c0a  /* stp	d10, d11, [x0, #128] */
    .long 0x6d09340c  /* stp	d12, d13, [x0, #144] */
    .long 0x6d0a3c0e  /* stp	d14, d15, [x0, #160] */
    .long 0xd2800000  /* mov	x0, #0x0                   	// #0 */
    .long 0xd65f03c0  /* ret */
.global longjmp
longjmp:
    .long 0xa9405013  /* ldp	x19, x20, [x0] */
    .long 0xa9415815  /* ldp	x21, x22, [x0, #16] */
    .long 0xa9426017  /* ldp	x23, x24, [x0, #32] */
    .long 0xa9436819  /* ldp	x25, x26, [x0, #48] */
    .long 0xa944701b  /* ldp	x27, x28, [x0, #64] */
    .long 0xa945781d  /* ldp	x29, x30, [x0, #80] */
    .long 0xf9403402  /* ldr	x2, [x0, #104] */
    .long 0x9100005f  /* mov	sp, x2 */
    .long 0x6d472408  /* ldp	d8, d9, [x0, #112] */
    .long 0x6d482c0a  /* ldp	d10, d11, [x0, #128] */
    .long 0x6d49340c  /* ldp	d12, d13, [x0, #144] */
    .long 0x6d4a3c0e  /* ldp	d14, d15, [x0, #160] */
    .long 0x7100003f  /* cmp	w1, #0x0 */
    .long 0x1a9f1420  /* csinc	w0, w1, wzr, ne	// ne = any */
    .long 0xd61f03c0  /* br	x30 */
