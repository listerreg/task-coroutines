	.text
	.globl	mymemcpy
	.hidden	mymemcpy #hiding from the user code and avoiding PLT indirect calls
mymemcpy:
#	arguments in:
#	RDI = destination ptr
#	RSI = source ptr
#	RDX = length in bytes
#	RCX = return address

	movq	%rcx, %r8
	xorq	%rcx, %rcx #RCX = 0
	movq	%rdx, %rax
	andq	$3, %rax #RAX = last three bits of the length
	cmp	$0, %rax
	jne	loopNotAligned #stack is not aligned to 8 bytes
	shrq	$3, %rdx #RDX = stack length in quad words
	loopAligned:
	movq	(%rsi, %rcx, 8), %r9
	movq	%r9, (%rdi, %rcx, 8)
	addq	$1, %rcx
	cmp	%rdx, %rcx #i < (stackLength/8)
	jl	loopAligned
	jmpq	*%r8

	loopNotAligned:
	movq	(%rsi, %rcx), %r9
	movq	%r9, (%rdi, %rcx)
	addq	$1, %rcx
	cmp	%rdx, %rcx #i < stack length
	jl	loopNotAligned
	jmpq	*%r8
