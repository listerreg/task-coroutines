	.text
	.globl	cleanup_asm
	.hidden	cleanup_asm #hiding from the user code and avoiding PLT indirect calls
	.type	cleanup_asm, @function
cleanup_asm:
	movq	64(%rax), %rdi #functioning as a synchronous flag
	cmp	$0, %rdi
	je	synchronous #user's routine went synchronously

#	asynchronous
#	restore registers saved by the unsink()
	movq	8(%rax), %rsp

#	free old storage
	movq	%rax, %rbx #saving StackState's address in RBX
	movq	128(%rax), %rdi #stack pointer
	callq	free@plt #using unsink's stack or rather the free is called at the same state at what the unsink was called
	movq	%rbx, %rax

	movq	16(%rax), %rbp
	movq	24(%rax), %rbx
	movq	32(%rax), %r12
	movq	40(%rax), %r13
	movq	48(%rax), %r14
	movq	56(%rax), %r15
	movq	%rax, %rcx
	movq	$0, %rax #return value (boolean false)
	jmpq	*(%rcx) #returning as the unsink()

	synchronous:
#	restoring the RSP (that's all what is needed) to the state from the time of calling the saveandswitch_asm
	addq	144(%rax), %rsp

#	free old storage
	subq	$8, %rsp #so the below free won't trash firstLevel's return address still being on the original stack
	movq	128(%rax), %rdi #stack pointer
	callq	free@plt #using the original (restored) stack
	addq	$8, %rsp #setting back correct value for the RSP

	movq	$0, %rax #return value (boolean false)
	jmp	*-8(%rsp) #return address of the firstLevel is still on the original stack
