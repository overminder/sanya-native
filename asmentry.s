
.globl Scheme_asmEntry
# rdi: thisClosure, rsi: function ptr, rdx: heapPtr, rcx: heapLimit,
# r8: threadstate
Scheme_asmEntry:
	push %r12
	push %r13
	push %r14

	mov %rdx, %r12     # set Hp
	mov %rcx, %r13     # set HpLim
	mov %r8,  %r14     # set ThreadState
	mov %rsp, 8(%r14)  # set SpBase

	call *%rsi

	pop %r14
	pop %r13
	pop %r12
	ret
