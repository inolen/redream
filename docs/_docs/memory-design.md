---
title: Memory Design
---

Memory operations are the most frequently occuring type of instruction. From a quick sample of code running during Sonic Adventure gameplay, a whopping 37% of the instructions were memory operations. Due to their high frequency, this is the single most important operation to optimize.

# The beginning

In the beginning, all memory operations resulted in a function call, for example:

```
uint32_t load_i8(uint32_t addr) {
  if (addr < 0x04000000) {
    return system_ram[addr];
  } else if (addr < 0x08000000) {
    return video_ram[addr - 0x04000000];
  } else {
    mmio_callback_t cb = mmio_callbacks[addr - 0x08000000];
    return cb();
  }
}
```

This function would be called by the compiled x64 code like so:

```
mov rax, <guest_address>
call load_i8
mov <dst_reg>, rax
```

This is simple and straight forward, but now a single guest instruction has transformed into ~10-20 host instructions: a function call, multiples moves to copy arguments / result, stack push / pop, multiple comparisons, multiple branches and a few arithmetic ops.

With some back-of-the envelope calculations it's evident that trying to run the 200mhz SH4 CPU on a 2ghz host is going to be difficult if 37% of the instructions are exploded by a factor of 10-20.

# The next generation

The next implementation is what most emulators refer to as "fastmem". The general idea is to map the entire 32-bit guest address space into an unused 4 GB area of the 64-bit host address spaace. By doing so, each guest memory access can be performed with some simple pointer arithmetic.

To illustrate with code:

```c
/* memory_base is the base address in the virtual address space where there's
   a 4 GB block of unreserved pages to map the shared memory object into  */
void init_memory(void *memory_base) {
  /* create the shared memory object representing the guest address space */
  int handle = shm_open("/redream_memory", O_RDWR | O_CREAT | O_EXCL, S_IREAD | S_IWRITE);

  /* resize shared memory object to 4 GB */
  ftruncate(handle, 0x100000000);

  /* map shared memory object into host address space */
  mmap(memory_base, 0xffffffff, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, handle, 0x0);
}
```

With the entire guest address space being mapped into the host address space relative to `memory_base`, each memory lookup now looks like:

```
mov rax, memory_base
mov <dst_reg>, [rax + <guest_address>]
```

In redream's JIT, an extra register is reserved to hold `memory_base`, so the initial mov is avoided:

```
mov <dst_reg>, [<mem_base_reg> + <guest_address>]
```

# MMIO access

The above example glosses over the part where this design doesn't at all work for MMIO accesses, which still need to call a higher-level callback function on each access.

Initially, it may seem that the only option is to conditionally branch at each memory access:

```
cmp <guest_addr>, 0x08000000
jge .slowmem
# fastmem path when guest_addr < 0x08000000
mov <dst_reg>, [<mem_base_reg> + <guest_address>]
jmp .end
# slowmem path when guest_addr >= 0x08000000
.slowmem:
sub <guest_addr>, 0x08000000
call [mmio_callbacks + <guest_addr>]
.end:
```

This isn't _awful_ and is much improved over the original implementation. While there is still a compare and branch, the branch will at least be predicted correctly in most cases due to it now being inlined in the generated code. However, valuable instruction cache space is now being wasted by the same inlining.

Fortunately, we can have our cake and eat it too thanks to segfaults.

## Abusing segfaults

The idea with segfaults is to disable access to the pages representing the MMIO region of the guest address space, and _always_ optimistically emit the fastmem code.

Extending on the above `init_memory` function, this would look like:

```
  /* disable access to the mmio region*/
  mprotect(memory_base + 0x08000000, 0x04000000, PROT_NONE);
```

Now, when a fastmem piece of code tries to perform an MMIO access, a `SIGSEGV` signal will be raised at which point we can either:

 * "backpatch" the original code
 * recompile the original code with fastmem optimizations disabled

### Backpatching

This is the easier to implement of the two options, and what redream did originally. The technique involves always writing out the fastmem code with enough padding such that, inside of the signal handler, it can be overwritten with the slowmem code.

All memory accesses would by default look like:

```
mov <dst_reg>, [<mem_base_reg> + <guest_addr_reg>]
nop
nop
nop
nop
...
```

Then, when the signal handler is ran, the current pc would be extracted from the signal handler and the code would be overwritten with:

```
sub <guest_addr_reg>, 0x08000000
call [mmio_callbacks + <guest_addr_reg>]
```

When the signal handler returns, the program would now resume at the `sub` instruction and all would be well. This approach works well, but the added `nop` instructions can add up depending on how your MMIO callbacks are invoked, which can very negatively impact performance.


### Recompiling

This is what redream currently does. The idea is simple: when the signal is raised, recompile the block to not use any fastmem optimizations and execute the recompiled block. However, while this sounds easy, the devil truly is in the details.

For starters, it's not possible to recompile the block inside of the signal handler itself due to the [limitations of what can be done inside of a signal handler](https://www.securecoding.cert.org/confluence/display/c/SIG30-C.+Call+only+asynchronous-safe+functions+within+signal+handlers).

Because of this, the actual recompilation is deferred to a later time (on the next access to the block in fact), and the MMIO access is handled somewhat-inside of the signal handler itself. I say "somewhat-inside" because, for the same reason the block itself can't be recompiled inside of the signal handler, it's not safe to directly invoke the MMIO callbacks inside of the signal handler. Instead of directly invoking the callback, the signal handler adjusts the program counter to land in a thunk that will invoke the callback when the signal handler resumes. This portion of the code is rather involved, but fairly well documented inside of [x64_backend_handle_exception](https://github.com/inolen/redream/blob/master/src/jit/backend/x64/x64_backend.cc#L406).

TODO add diagram show how this function works
