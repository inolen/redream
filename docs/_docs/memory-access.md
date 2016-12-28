---
title: Memory Access
---

Memory operations are the most frequently occuring type of instruction. From a quick sample of code running during Sonic Adventure gameplay, a whopping 37% of the instructions are memory operations. Due to their high frequency, this is the single most important operation to optimize.

# The beginning

In the beginning, all memory access went through a function call, e.g.:

```c
uint32_t load_i32(uint32_t addr) {
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

```nasm
mov edi, <guest_address>
call load_i32
mov <dst_reg>, eax
```

This is simple and straight forward, but now a single guest instruction has transformed into ~10-20 host instructions: a function call, multiples moves to copy arguments / result, stack push / pop, multiple comparisons, multiple branches and a few arithmetic ops.

With some back-of-the envelope calculations it's evident that trying to run the 200mhz SH4 CPU on a 2ghz host is going to be difficult if 37% of the instructions are exploded by a factor of 10-20.

# The next generation

The next implementation is what most emulators refer to as "fastmem". The general idea is to map the entire 32-bit guest address space into an unused 4 GB area of the 64-bit host address spaace. By doing so, each guest memory access can be performed with some simple pointer arithmetic.

To illustrate with code:

```c
/* memory_base is the base address in the virtual address space where there's
   a 4 GB block of unused pages to map the shared memory into  */
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

```nasm
mov rax, memory_base
mov <dst_reg>, [rax + <guest_address>]
```

In redream's JIT, an extra register is reserved to hold `memory_base`, so the initial mov is avoided:

```nasm
mov <dst_reg>, [<mem_base_reg> + <guest_address>]
```

# Handling MMIO accesses

The above example glosses over the part where this solution doesn't handle MMIO accesses, which still need to call a higher-level callback function on each access.

Initially, it may seem that the only option is to conditionally branch at each memory access:

```nasm
cmp <guest_addr>, 0x08000000
jge .slowmem
# fast path when guest_addr < 0x08000000
mov <dst_reg>, [<mem_base_reg> + <guest_address>]
jmp .end
# slow path when guest_addr >= 0x08000000
.slowmem:
mov rdi, <guest_address>
call load_i32
.end:
```

This isn't _awful_ and is much improved over the original implementation. While there is still a compare and branch, the branch will at least be predicted correctly in most cases due to it now being inlined in the generated code. However, valuable instruction cache space is now being wasted by the same inlining.

Fortunately, we can have our cake and eat it too thanks to segfaults.

## Generating segfaults

The idea with segfaults is to disable access to the pages representing the MMIO region of the guest address space, and _always_ optimistically emit the fastmem code.

Extending on the above `init_memory` function, the code to disable access would look like:

```c
  /* disable access to the mmio region */
  mprotect(memory_base + 0x08000000, 0x04000000, PROT_NONE);
```

Now, when a fastmem-optimized piece of code tries to perform an MMIO access, a segfault will be generated at which point we can either:

 * "backpatch" the original code
 * recompile the code with fastmem optimizations disabled

### Backpatching

This is the easier to implement of the two options, and what redream did originally. The technique involves always writing out the fastmem code with enough padding such that, inside of the signal handler, it can be overwritten with the "slowmem" code.

All memory accesses would by default look like:

```nasm
mov <dst_reg>, [<mem_base_reg> + <guest_addr_reg>]
nop
nop
nop
nop
...
```

Then, when the signal handler is entered, the current PC would be extracted from the thread context and the code would be overwritten with:

```c
mov edi, <guest_addr_reg>
call load_i32
mov <dst_reg>, eax
```

When the signal handler returns, the thread will resume at the same PC that originally generated the segfault, but it will now execute the backpatched slow path. This approach works well, but the `nop` padding can add up depending on the size of the slow path code, which can very negatively impact performance.


### Recompiling

This is what redream currently does. The idea itself is simple: when the signal is raised, recompile the block to not use any fastmem optimizations and execute the recompiled block. However, while this sounds easy, the devil truly is in the details.

For starters, it's not possible to recompile the block inside of the signal handler itself due to the [limitations of what can be done inside of a signal handler](https://www.securecoding.cert.org/confluence/display/c/SIG30-C.+Call+only+asynchronous-safe+functions+within+signal+handlers).

Because of this, the actual recompilation is deferred to a later time, and the MMIO access is handled somewhat-inside of the signal handler itself. I say "somewhat-inside" because, for the same reason the block itself can't be recompiled inside of the signal handler, it's not safe to directly invoke the MMIO callbacks inside of the signal handler. Instead of directly invoking the callback, the signal handler adjusts the program counter to land in a thunk that will invoke the callback when the thread resumes.

This is what the control flow looks like when an MMIO segfault occurs:

![MMIO segfault]({{ site.github.url }}/docs/mmio_segfault.png)

From looking at this diagram it should be apparent that this method of servicing the MMIO request is _extremely_ slow. However, this penalty is only paid once, as the block will be recompiled with all fastmem optimizations disabled before the next run.

The trade off of all this effort is that, now the fastmem route needs no padding, providing non-MMIO guest memory access with the absolute minimum amount of overhead.
