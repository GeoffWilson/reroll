/* A Windows thread block (TEB) for each thread, reached through fs: as the
 * exe's code expects (its C runtime's structured exception frames use
 * fs:[0]). Linux leaves fs alone on i386 (glibc's thread data is gs), so
 * each thread gets an LDT entry based at its own block, as Wine does. */
#include "nt.h"

#include <asm/ldt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define TEB_SIZE 0x1000

static __thread uint8_t *teb;
static int next_entry;
static uint8_t peb[0x1000];

void nt_teb_init_thread(void)
{
    struct user_desc desc;
    int entry = __atomic_fetch_add(&next_entry, 1, __ATOMIC_SEQ_CST);
    uint16_t selector;
    uint32_t stack_top;

    teb = calloc(1, TEB_SIZE);
    if (!teb)
        nt_fatal("no memory for a thread block");
    stack_top = (uint32_t)(uintptr_t)&stack_top;
    *(uint32_t *)(teb + 0x00) = 0xffffffffu;         /* exception list: empty */
    *(uint32_t *)(teb + 0x04) = (stack_top + 0xfff) & ~0xfffu; /* stack base (approximate) */
    *(uint32_t *)(teb + 0x08) = stack_top - 0x100000; /* stack limit */
    *(uint32_t *)(teb + 0x18) = (uint32_t)(uintptr_t)teb; /* self */
    *(uint32_t *)(teb + 0x20) = (uint32_t)getpid();
    *(uint32_t *)(teb + 0x24) = (uint32_t)syscall(SYS_gettid);
    *(uint32_t *)(teb + 0x30) = (uint32_t)(uintptr_t)peb;

    memset(&desc, 0, sizeof desc);
    desc.entry_number = (unsigned)entry;
    desc.base_addr = (unsigned)(uintptr_t)teb;
    desc.limit = TEB_SIZE - 1;
    desc.seg_32bit = 1;
    desc.contents = 0; /* data */
    desc.useable = 1;
    if (syscall(SYS_modify_ldt, 1, &desc, sizeof desc) != 0)
        nt_fatal("modify_ldt failed for entry %d", entry);
    selector = (uint16_t)(entry << 3 | 7); /* LDT, ring 3 */
    __asm__ volatile("movw %0, %%fs" : : "r"(selector));
}

uint32_t nt_teb_address(void)
{
    return (uint32_t)(uintptr_t)teb;
}
