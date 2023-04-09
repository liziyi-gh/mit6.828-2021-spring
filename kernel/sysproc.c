#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_mmap(void)
{
  uint64 addr;
  int length, prot, flags, fd, offset;
  struct file *file;
  uint64 i;
  struct proc* p = myproc();
  struct vmarea* vma = 0;
  printf("enter sys_mmap\n");

  if ((argaddr(0, &addr) < 0) ||
      (argint(1, &length) < 0) ||
      (argint(2, &prot) < 0) ||
      (argint(3, &flags) < 0) ||
      (argfd(4, &fd, &file) < 0) ||
      (argint(5, &offset) < 0)){
    return 0xffffffffffffffff;
  }

  if ((!file->writable) && (prot & PROT_WRITE) && (flags & MAP_SHARED)) {
    return 0xffffffffffffffff;
  }

  for (i = 0; i < MAXVMA; ++i) {
    if (!p->vmarea[i].valid) {
      vma = &p->vmarea[i];
      break;
    }
  }
  if (i == MAXVMA) {
    return 0xffffffffffffffff;
  }

  vma->length = length;
  vma->prot = prot;
  vma->flags = flags;
  vma->offset = offset;
  vma->file = file;

  // find start addr
  uint64 start_va = find_cons_free_mem(p->pagetable, 1024*PGSIZE, length);
  printf("start_va is %p, length is %d\n", start_va, length);
  if (!start_va) {
    return 0xffffffffffffffff;
  }

  for (i = 0; i < length; i += PGSIZE) {
    if (mappages(p->pagetable, start_va+i, PGSIZE,
                 (uint64)KERNBASE, PTE_MMAP | PTE_U) < 0) {
      printf("map failed\n");
      // TODO: unmap previous pages
      return 0xffffffffffffffff;
    }
  }
  vma->start_addr = start_va;
  vma->valid = 1;
  filedup(file);
  return start_va;
}

static struct vmarea* find_mmap_idx(uint64 va)
{
  int i, ok = 0;
  struct proc *p = myproc();
  struct vmarea* vma;
  for (i = 0; i < MAXVMA; ++i) {
    vma = &p->vmarea[i];
    if ((vma->valid)  &&
        (va >= vma->start_addr) &&
        (va < vma->length + vma->start_addr)) {
      ok = 1;
      break;
    }
  }
  if (!ok) {
    return 0;
  }
  return &p->vmarea[i];
}

uint64 do_munmap(uint64 va, int length)
{
  struct proc *p = myproc();
  struct vmarea* vma;
  pte_t *pte;
  int iaddr;
  vma = find_mmap_idx(va);
  if (!vma) {
    return -1;
  }

  ilock(vma->file->ip);
  for (iaddr = va; iaddr < va + length; iaddr += PGSIZE) {
    pte = walk(p->pagetable, iaddr, 0);
    if ((*pte & PTE_D) && (vma->flags & MAP_SHARED)) {
      int off = (iaddr - iaddr % PGSIZE) - vma->start_addr;
      begin_op();
      if (writei(vma->file->ip, 1, iaddr, off, PGSIZE) <= 0 ) {
        printf("writei failed\n");
      }
      end_op();
      uvmunmap(p->pagetable, iaddr, 1, 1);
    } else {
      uvmunmap(p->pagetable, iaddr, 1, 0);
    }
  }
  iunlock(vma->file->ip);

  //whole region
  if (length >= vma->length) {
    printf("munmap whole region\n");
    fileclose(vma->file);
    vma->valid = 0;
    return 0;
  }
  // at start
  if (vma->start_addr == va) {
    printf("munmap at start\n");
    vma->start_addr += length;
    vma->length -= length;
    return 0;
  }

  // at end
  if (vma->start_addr + vma->length == va + length) {
    printf("munmap at end\n");
    vma->length -= length;
    return 0;
  }

  return 0;
}


uint64
sys_munmap(void)
{
  uint64 va;
  int length;

  if ((argaddr(0, &va) < 0) ||
      (argint(1, &length) < 0)){
    printf("munmap failed");
    return -1;
  }

  return do_munmap(va, length);
}

int handlemmap(pagetable_t ptbl, uint64 va)
{
  struct proc *p = myproc();
  struct vmarea* vma;
  int perm = PTE_U;
  uint offset;
  uint64 pa;
  pte_t *pte;

  printf("handling mmap p is %p\n", va);
  pte = walk(ptbl, va, 0);
  if (!pte) {
    panic("pte 0\n");
  }
  if ((*pte & PTE_MMAP) == 0) {
    printf("not mmap pte\n");
    return -1;
  }

  vma = find_mmap_idx(va);
  if (!vma) {
    return -1;
  }

  perm = PTE_U;
  offset = (va - va % PGSIZE) - vma->start_addr;
  printf("offset is %d\n", offset);
  if (vma->prot & PROT_READ) {
    perm |= PTE_R;
  }
  if (vma->prot & PROT_WRITE) {
    perm |= PTE_W;
  }
  if (vma->prot & PROT_EXEC) {
    perm |= PTE_X;
  }
  pa = (uint64)kalloc();
  memset((void*)pa, 0, PGSIZE);
  if (!pa) {
    printf("no free memory\n");
    return -1;
  }

  uvmunmap(p->pagetable, va, 1, 0);
  mappages(p->pagetable, va, PGSIZE, pa, perm);
  ilock(vma->file->ip);
  if (readi(vma->file->ip, 1, va, offset, PGSIZE) <= 0 ){
    printf("readi failed\n");
    return -1;
  }
  iunlock(vma->file->ip);

  return 0;
}
