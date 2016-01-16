# the memory thunks exist as a mechanism to service dynamic memory handlers
# AFTER exiting the exception handler. invoking the handlers from inside the
# exception handler imposes many restrictions:
# https://www.securecoding.cert.org/confluence/display/c/SIG30-C.+Call+only+
# asynchronous-safe+functions+within+signal+handlers
#
# instead, the handler preps the stack and registers for the handler, sets rip
# to the thunk address, and once the execption handler exits the thunk will be
# invoked, the handler will be called, and the stack will be cleaned back up

.section  __TEXT,__text

.macro load_thunk r
  .globl _load_thunk_\r

  _load_thunk_\r:
    callq *%rax
    movq %rax, %\r
    pop %rdx
    pop %rsi
    pop %rdi
    retq
.endm

load_thunk rax
load_thunk rcx
load_thunk rdx
load_thunk rbx
load_thunk rsp
load_thunk rbp
load_thunk rsi
load_thunk rdi
load_thunk r8
load_thunk r9
load_thunk r10
load_thunk r11
load_thunk r12
load_thunk r13
load_thunk r14
load_thunk r15

.globl _store_thunk

_store_thunk:
  callq *%rax
  pop %rdx
  pop %rsi
  pop %rdi
  retq
