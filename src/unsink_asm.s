	.text
	.globl	unsink_asm
	.type	unsink_asm, @function
unsink_asm:
#	save current registers (RSP, RBP, RBX, R12-R15) plus the return address - sink() will use them the same way as during the first call
	movq	(%rsp), %rax #return address
	movq	%rax, (%rdi)
	addq	$8, %rsp #correct value of the RSP if execution would return from this function (we can safely trash the RSP)
	movq	%rsp, 8(%rdi)
	movq	%rbp, 16(%rdi)
	movq	%rbx, 24(%rdi)
	movq	%r12, 32(%rdi)
	movq	%r13, 40(%rdi)
	movq	%r14, 48(%rdi)
	movq	%r15, 56(%rdi)

#	return as the sink_asm()
	movq	72(%rdi), %rsp
	movq	80(%rdi), %rbp
	movq	88(%rdi), %rbx
	movq	96(%rdi), %r12
	movq	104(%rdi), %r13
	movq	112(%rdi), %r14
	movq	120(%rdi), %r15
	jmpq	*64(%rdi)
