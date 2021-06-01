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

struct
{
  int pa_ref[TOTALPAGE];
  struct spinlock lock;
} ref_cnt;

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;


int getRefCnt(void* pa){
  uint64 addr = (uint64)pa;
  return ref_cnt.pa_ref[PAID(addr)];
}

void setRefCnt(void* pa, int diff){
  uint64 addr = (uint64)pa;
  acquire(&ref_cnt.lock);
  int id = PAID(addr);
  ref_cnt.pa_ref[id] += diff;
  release(&ref_cnt.lock);
}

// atomic free a ref
int freeRefCnt(void* pa){
  uint64 addr = (uint64)pa;
  acquire(&ref_cnt.lock);
  int id = PAID(addr);
  ref_cnt.pa_ref[id] -= 1;
  int res = ref_cnt.pa_ref[id];
  release(&ref_cnt.lock);
  return res;
}

void refCntDebug(){
  int total = 0;
  int page = 0;
  for(int i=0; i<TOTALPAGE; i++){
    if(ref_cnt.pa_ref[i] < 0)
      printf("unexpected negative cnt\n");
    else if(ref_cnt.pa_ref[i] > 0)
      page += 1;
    total += ref_cnt.pa_ref[i];   
  }
  int left = 0;
  struct run* tmp = kmem.freelist;
  while(tmp){
    left += 1;
    tmp = tmp->next;
  }
  printf("total page: %d, total ref: %d, left: %d\n",page ,total, left);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref_cnt.lock, "refcnt");
  freerange(end, (void*)PHYSTOP);
  memset(ref_cnt.pa_ref, 0, TOTALPAGE*sizeof(int));
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

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  
  acquire(&kmem.lock);
  if(freeRefCnt(pa) > 0){
    release(&kmem.lock);
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    setRefCnt(r, +1);
  }

  if(!r){
    printf("kalloc fail: ");
    refCntDebug();
  }
    
  return (void*)r;
}
