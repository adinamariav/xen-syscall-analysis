#include "mem_event.h"

static vmi_event_t cr3_event;
static vmi_event_t msr_syscall_event;
static reg_t cr3;
static bool mem_events_registered;

bool register_lstar_mem_event(vmi_instance_t vmi, vmi_event_t *event, event_callback_t cb) {

    addr_t cr3 = event->reg_event.value;
    addr_t phys_lstar = 0;
    addr_t phys_sysenter_ip = 0;
    bool ret = false;
    addr_t phys_vsyscall = 0;

    lstar = event->x86_regs->msr_lstar;
    vmi_pagetable_lookup(vmi, event->x86_regs->cr3, lstar, &phys_lstar);
    printf("Physical LSTAR == %llx\n", (unsigned long long)phys_lstar);

    msr_syscall_event.version = VMI_EVENTS_VERSION;
    msr_syscall_event.type = VMI_EVENT_MEMORY;
    msr_syscall_event.mem_event.gfn = phys_lstar >> 12;
    msr_syscall_event.mem_event.in_access = VMI_MEMACCESS_X;
    msr_syscall_event.callback = cb;

    if ( phys_lstar && VMI_SUCCESS == vmi_register_event(vmi, &msr_syscall_event) )
        ret = true;
    else
        printf("Failed to register memory event on MSR_LSTAR page\n");

    return ret;
}