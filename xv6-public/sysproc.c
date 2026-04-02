#include "types.h"
#include "x86.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return proc->pid;
}

addr_t
sys_sbrk(void)
{
  addr_t addr;
  addr_t n;

  argaddr(0, &n);
  addr = proc->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(proc->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Halt or reboot the system
// arg0: 0 = shutdown (QEMU ACPI), 1 = reboot (keyboard controller reset)
addr_t
sys_halt(void)
{
  int mode;
  if(argint(0, &mode) < 0)
    return -1;

  cprintf("System %s...\n", mode ? "rebooting" : "shutting down");

  if(mode == 0){
    // QEMU ACPI shutdown via PM1a control register
    outw(0x604, 0x2000);  // SLP_EN | SLP_TYPa
    // If that didn't work, try ISA debug exit
    outb(0x501, 0x31);
    // Last resort: halt CPU
    for(;;)
      asm volatile("hlt");
  } else {
    // Reboot via keyboard controller (8042) pulse reset line
    uchar good = 0x02;
    while(good & 0x02)
      good = inb(0x64);
    outb(0x64, 0xFE);
    // If that didn't work, triple fault
    for(;;)
      asm volatile("hlt");
  }
  return 0;
}
