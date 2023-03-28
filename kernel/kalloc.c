// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

#define FREELIST_NUM 4
#define LIST_IDX(pa) ((PGROUNDUP((uint64)(pa)) >> PGSHIFT) % FREELIST_NUM)
struct {
  struct spinlock lock;
  struct run *freelist;
} kmems[FREELIST_NUM];

void
kinit()
{
  int i = 0;
  for (i = 0; i < FREELIST_NUM; ++i) {
    initlock(&kmems[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int list_idx = LIST_IDX(pa);

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmems[list_idx].lock);
  r->next = kmems[list_idx].freelist;
  kmems[list_idx].freelist = r;
  release(&kmems[list_idx].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int cpu_id, i;
  push_off();
  cpu_id = cpuid();
  pop_off();

  for (i = 0; i < FREELIST_NUM; i++) {
    cpu_id = (cpu_id + i) % FREELIST_NUM;
    acquire(&kmems[cpu_id].lock);
    r = kmems[cpu_id].freelist;
    if(r)
      kmems[cpu_id].freelist = r->next;
    release(&kmems[cpu_id].lock);

    if(r) {
      memset((char*)r, 5, PGSIZE); // fill with junk
      return (void*)r;
    }
  }

  return 0;
}
