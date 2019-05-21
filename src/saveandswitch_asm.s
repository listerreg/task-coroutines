	.text #alloc exec progbits alignment: 16
	.globl	saveandswitch_asm
	#.hidden	saveandswitch_asm #since all the code is in the header anyway (due to templates) it cannot have visibility HIDDEN
	#.extern malloc #not necessary: gas treats every symbol used es external (GLOBAL)
	.type	saveandswitch_asm, @function #public (not hidden) functions from shared libraries use PLT in a PIC code so the assembler needs to know it's a function to generate an appropriate relocation entry
saveandswitch_asm:
/*here we are after the call to the PLT and the resolver. Both are using the stack for their purposes but after jumping here there is no additional call frame above. It is just like the PLT was never used*/
#	save current registers (RSP+8, RBP, RBX, R12-R15, return address (RSP))
	movq	(%rsp), %rax #return address
	movq	%rax, (%rdi)
	leaq	8(%rsp), %rax #RSP+8
	movq	%rax, 8(%rdi)
	movq	%rbp, 16(%rdi)
	movq	%rbx, 24(%rdi)
	movq	%r12, 32(%rdi)
	movq	%r13, 40(%rdi)
	movq	%r14, 48(%rdi)
	movq	%r15, 56(%rdi)

#	set flag to synchronous
	movq	$0, 64(%rdi)

#	copy the stack (the little part of it)
	movq	%rdi, %r12 #saving StackState's address
	movq	$8388608, %rdi
	call	malloc@plt #RAX holds pointer to the allocated memory
	cmp	$0, %rax
	je	retOne #malloc did not succeed
	movq	%rax, 128(%r12) #storing a pointer to the allocated memory
	leaq	16(%rbp), %rdx #exclusive (including the return address of the firstLevel)
	addq	$8388608, %rax #storage pointer + 8MB = just above
	movq	%rdx, %rcx
	subq	%rax, %rcx #RCX = stack offset
	movq	%rcx, 144(%r12) #storing offset
	subq	%rsp, %rdx #RDX = 16(RBP) - (RSP) = stack length

	movq	%rax, %rdi
	subq	$8, %rdi #return address pointer #XXX if it works then combine arithmetic instructions
	leaq	cleanup_asm(%rip), %rsi #RIP-relative: REX.W + 8D /r, ModR/M: 00(no meaning) 110(rsi register) 101(rip + disp32), relocation type: R_X86_64_PC32 which is resolved during building the library not when it is loaded (used). This is valid within a shared library because the cleanup_asm is declared as HIDDEN thus it is not subjected to a further preemtion. A PUBLIC symbol is subjected to preemption and needs the load-time resolving. It even could be of the type R_X86_64_PC32 (load-time relocation) but in the small code model a 32-bit range could be not enough. An aside note: PROTECTED regarding its definition should be ok as well but using the .protected directive didn't work with gas and ld
	movq	%rsi, (%rdi) #replace a return address on the alternate stack
	movq	%rdi, 136(%r12) #storing the return address pointer

	subq	%rdx, %rax #...minus stack length => RAX = current (new) stack pointer
	movq	%rsp, %rsi #source
	movq	%rax, %rsp #RSP will be valid for the ret instruction already
	#subq	%rcx, %rsp #those should be equivalent
	subq	%rcx, %rbp

	movq	%rax, %rdi #destination
	#rdx #length
	subq	$8, %rdx #we don't need to copy to the new stack firstLevel's return address
	/*the two following instructions do not use GOT (nor PLT)*/
	leaq	memcpyret(%rip), %rcx #RIP-relative: REX.W + 8D /r, ModR/M: 00(no meaning) 001(rcx register) 101(rip + disp32), const. offset (here 0x5) thus no relocation
	jmp	mymemcpy #RIP-relative: E9 cd, JMP rel32
	memcpyret:

	movq	%r12, %rdi #restoring RDI (StackState's address)
	movq	32(%rdi), %r12 #restoring trashed r12
	movq	$0, %rax
	retq

	retOne:
	movq	%r12, %rdi #restoring RDI (StackState's address)
	movq	32(%rdi), %r12 #restoring trashed r12
	movq	$2, %rax #returning error = 2
	retq
	#.size	saveandswitch_asm, .-saveandswitch_asm #not necessary - let me know if it is useful for a linker somehow
