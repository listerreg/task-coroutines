	.text
	.globl	sink_asm
	.type	sink_asm, @function
sink_asm:
#	save current registers (RSP+8, RBP, RBX, R12-R15, return address (RSP))
	movq	(%rsp), %rax #return address
	movq	%rax, 64(%rdi)
	leaq	8(%rsp), %rax #RSP+8
	movq	%rax, 72(%rdi)
	movq	%rbp, 80(%rdi)
	movq	%rbx, 88(%rdi)
	movq	%r12, 96(%rdi)
	movq	%r13, 104(%rdi)
	movq	%r14, 112(%rdi)
	movq	%r15, 120(%rdi)

#	returning as the saveandswitch_asm() or the unsink_asm()
	movq	8(%rdi), %rsp
	movq	16(%rdi), %rbp
	movq	24(%rdi), %rbx
	movq	32(%rdi), %r12
	movq	40(%rdi), %r13
	movq	48(%rdi), %r14
	movq	56(%rdi), %r15
	movq	$1, %rax #return value (boolean true)
	jmpq	*(%rdi)
