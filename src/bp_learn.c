#include "client.h"
#include "bp_event.h"

#include <fcntl.h>
#include <signal.h>
#include <time.h>

void process_syscall_learn(vmi_instance_t vmi, vmi_event_t* event) {
    reg_t rdi, rax, cr3;
    vmi_pid_t pid = -1;
    FILE* fp = NULL;

    fp = fopen("syscall-trace.csv", "a");

    vmi_get_vcpureg(vmi, &rax, RAX, event->vcpu_id);
    vmi_get_vcpureg(vmi, &rdi, RDI, event->vcpu_id);
    vmi_get_vcpureg(vmi, &cr3, CR3, event->vcpu_id);

    vmi_dtb_to_pid(vmi, cr3, &pid);

    uint16_t _index = (uint16_t)rax;

    char* output = (char*)malloc (100 * sizeof(char));
    char** args = (char**)malloc(7 * sizeof (char*));

    for (int i = 0; i < 7; i++) {
        args[i] = NULL;
    }

    if (_index >= num_sys) {
        sprintf(output, "Process[%d]: unknown syscall id: %d ", pid, _index);

        fprintf(fp, "%d, %d, , ", pid, _index);
    }
    else {
        sprintf(output, "PID[%d]: %s with args", pid, sys_index[_index]);
        fprintf(fp, "%d, %d, %s, ", pid, _index, sys_index[_index]);
    }

    args[0] = output;
    get_args(vmi, event, pid, _index, fp, LEARN_MODE, args);

    for (int i = 0; i < 7; i++) {
        if (args[i] != NULL) {
            printf("%s ", args[i]);
            free(args[i]);
        }
    }

    free(args);
    printf("\n");
    
    fclose(fp);
}

event_response_t breakpoint_cb(vmi_instance_t vmi, vmi_event_t *event)
{   
    (void)vmi;

    if (!event->data) {
        fprintf(stderr, "Empty event data in breakpoint callback !\n");
        interrupted = true;
        return VMI_EVENT_RESPONSE_NONE;
    }
    struct bp_cb_data *cb_data = (struct bp_cb_data*)event->data;
    event->interrupt_event.reinject = 1;
   
    if ( !event->interrupt_event.insn_length )
        event->interrupt_event.insn_length = 1;

    if (event->x86_regs->rip != cb_data->sym_vaddr) {
        printf("Not our breakpoint. Reinjecting INT3\n");
        return VMI_EVENT_RESPONSE_NONE;
    } else {
        event->interrupt_event.reinject = 0;
       
        process_syscall_learn(vmi, event);

        cb_data->hit_count++;

        if (VMI_FAILURE == vmi_write_va(vmi, event->x86_regs->rip, 0, sizeof(BREAKPOINT), &cb_data->saved_opcode, NULL)) {
            fprintf(stderr, "Failed to write back original opcode at 0x%" PRIx64 "\n", event->x86_regs->rip);
            interrupted = true;
            return VMI_EVENT_RESPONSE_NONE;
        }
        
        return VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP;
    }
}

int bp_learn (char *name, int set_time) {
    vmi_instance_t vmi = NULL;
    status_t status = VMI_SUCCESS;
    struct sigaction act;
    vmi_init_data_t *init_data = NULL;
    act.sa_handler = close_handler;
    act.sa_flags = 0;

    read_syscall_table();

    sigemptyset(&act.sa_mask);
    sigaction(SIGHUP,  &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT,  &act, NULL);
    sigaction(SIGALRM, &act, NULL);

    vmi_mode_t mode;
    if (VMI_FAILURE == vmi_get_access_mode(NULL, name, VMI_INIT_DOMAINNAME, init_data, &mode)) {
        printf("Failed to find a supported hypervisor with LibVMI\n");
        return 1;
    }

    if (VMI_FAILURE == vmi_init(&vmi, mode, name, VMI_INIT_DOMAINNAME | VMI_INIT_EVENTS, init_data, NULL)) {
        printf("Failed to init LibVMI library.\n");
        return 1;
    }

    if ( VMI_PM_UNKNOWN == vmi_init_paging(vmi, 0) ) {
        printf("Failed to init determine paging.\n");
        vmi_destroy(vmi);
        return 1;
    }

    if ( VMI_OS_UNKNOWN == vmi_init_os(vmi, VMI_CONFIG_GLOBAL_FILE_ENTRY, NULL, NULL) ) {
        printf("Failed to init os.\n");
        vmi_destroy(vmi);
        return 1;
    }

    printf("LibVMI init succeeded!\n");

    create_csv_file();

    time_t start_time;
    float f_set_time = set_time * 60;
    time(&start_time);

    if ( VMI_SUCCESS == set_lstar_breakpoint(vmi) ) {
        while (!interrupted) {
            status = vmi_events_listen(vmi,500);

            if (status != VMI_SUCCESS) {
                printf("Error waiting for events, quitting...\n");
                interrupted = -1;
            }

            if (set_time > 0) {
                if (difftime(time(NULL), start_time) > f_set_time)
                    interrupted = -1;
            }
        }
    }

error_exit:
    vmi_pause_vm(vmi);
    if (saved_opcode) {
        printf("Restore previous opcode at 0x%" PRIx64 "\n", lstar);
        vmi_write_va(vmi, lstar, 0, sizeof(BREAKPOINT), &saved_opcode, NULL);
    }

    if (vmi_are_events_pending(vmi))
        vmi_events_listen(vmi, 0);

    vmi_clear_event(vmi, &int_event, NULL);
    vmi_clear_event(vmi, &sstep_event, NULL);

    vmi_resume_vm(vmi);
    vmi_destroy(vmi);

    return 0;
}