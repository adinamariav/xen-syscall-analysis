#include "bp_event.h"

event_response_t single_step_cb(vmi_instance_t vmi, vmi_event_t *event)
{
    (void)vmi;

    if (!event->data) {
        fprintf(stderr, "Empty event data in singlestep callback !\n");
        interrupted = true;

        return VMI_EVENT_RESPONSE_NONE;
    }

    struct bp_cb_data *cb_data = (struct bp_cb_data*)event->data;

    if (VMI_FAILURE == vmi_write_va(vmi, cb_data->sym_vaddr, 0, sizeof(BREAKPOINT), &BREAKPOINT, NULL)) {
        fprintf(stderr, "Failed to write breakpoint at %lx\n",event->x86_regs->rip);
        interrupted = true;
        
        return VMI_EVENT_RESPONSE_NONE;
    }

    return VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP;
}


event_response_t set_lstar_breakpoint(vmi_instance_t vmi) {

if (VMI_FAILURE == vmi_get_vcpureg(vmi, &lstar, MSR_LSTAR, 0)) {
        fprintf(stderr, "Failed to get current RIP\n");
        return 1;
    }

    printf("Pause VM\n");
    if (VMI_FAILURE == vmi_pause_vm(vmi)) {
        fprintf(stderr, "Failed to pause VM\n");
        return VMI_FAILURE;
    }

    printf("Save opcode\n");
    if (VMI_FAILURE == vmi_read_va(vmi, lstar, 0, sizeof(BREAKPOINT), &saved_opcode, NULL)) {
        fprintf(stderr, "Failed to read opcode\n");
        return VMI_FAILURE;
    }

    printf("Write breakpoint at %lx\n", lstar);
    if (VMI_FAILURE == vmi_write_va(vmi, lstar, 0, sizeof(BREAKPOINT), &BREAKPOINT, NULL)) {
        fprintf(stderr, "Failed to write breakpoint\n");
        return VMI_FAILURE;
    }


    memset(&int_event, 0, sizeof(vmi_event_t));
    int_event.version = VMI_EVENTS_VERSION;
    int_event.type = VMI_EVENT_INTERRUPT;
    int_event.interrupt_event.intr = INT3;
    int_event.callback = breakpoint_cb;

    cb_data.sym_vaddr = lstar;
    cb_data.saved_opcode = saved_opcode;
    cb_data.hit_count = 0;

    int_event.data = (void*)&cb_data;

    printf("Register interrupt event\n");
    if (VMI_FAILURE == vmi_register_event(vmi, &int_event)) {
        fprintf(stderr, "Failed to register interrupt event\n");
        return VMI_FAILURE;
    }

    unsigned int num_vcpus = vmi_get_num_vcpus(vmi);

    sstep_event.version = VMI_EVENTS_VERSION;
    sstep_event.type = VMI_EVENT_SINGLESTEP;
    sstep_event.callback = single_step_cb;
    sstep_event.ss_event.enable = false;

    for (unsigned int vcpu=0; vcpu < num_vcpus; vcpu++)
        SET_VCPU_SINGLESTEP(sstep_event.ss_event, vcpu);

    sstep_event.data = (void*)&cb_data;

    printf("Register singlestep event\n");
    if (VMI_FAILURE == vmi_register_event(vmi, &sstep_event)) {
        fprintf(stderr, "Failed to register singlestep event\n");
        return VMI_FAILURE;
    }

    printf("Resume VM\n");
    if (VMI_FAILURE == vmi_resume_vm(vmi)) {
        fprintf(stderr, "Failed to resume VM\n");
        return VMI_FAILURE;
    }
}