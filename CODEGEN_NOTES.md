S2S (Scheme to scheme) calling convention
-----------------------------------------

### Stack after call instr
  [...]
  retAddr <- %rsp
  r10 = frameDescr
  rdi = thisClosure
  rsi = arg0
  rdx = arg1
  rcx = arg2

### prologue
  push r10
  push rdi
  push rsi
  push rdx
  push rcx

### (define a val)
  [code for val]

### (func a1 a2 a3)
  [code for func]
  pop %rdi
  [code for a1]
  pop %rsi
  [code for a2]
  pop %rdx
  [code for a3]
  pop %rcx
  mov frameDescr, %r10
  [test and extract codeptr to %rax]
  call %rax
  push %rax

### return x (frameSize = args + locals + thisClosure + frameDescr)
  [code for x]
  pop %rax
  add $8 * frameSize, %rsp
  ret

### tailcall (func a1 a2 a3)
  [code for func]
  pop %rdi
  [code for a1]
  pop %rsi
  [code for a2]
  pop %rdx
  [code for a3]
  pop %rcx
  add $8, frameSize
  [test and extract codeptr to %rax]
  jmp %rax

### Runtime GC call
  mov allocSize, %rdi
  mov %rsp, %rsi
  mov frameDescr, %rdx
  mov threadState, %rcx
  call collectAndAlloc

