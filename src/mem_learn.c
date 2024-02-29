#include "vmi.h"
#include "client.h"
#include "mem_event.h"

#include <fcntl.h>
#include <signal.h>
#include <time.h>

void process_syscall(vmi_instance_t vmi, vmi_event_t* event) {
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

event_response_t syscall_cb(vmi_instance_t vmi, vmi_event_t *event) {
    if ( event->mem_event.offset != (VMI_BIT_MASK(0,11) & event->x86_regs->msr_lstar) ) {
        vmi_clear_event(vmi, event, NULL);
        vmi_step_event(vmi, event, event->vcpu_id, 1, NULL);
        return 0;
    }
    
    process_syscall(vmi, event);

    vmi_clear_event(vmi, event, NULL);
    vmi_step_event(vmi, event, event->vcpu_id, 1, NULL);

    return 0;
}

event_response_t cr3_register_learn_callback(vmi_instance_t vmi, vmi_event_t *event) {
    if ( !mem_events_registered )
        mem_events_registered = register_lstar_mem_event(vmi, event, syscall_cb);
    vmi_clear_event(vmi, &cr3_event, NULL);
    return 0;
}


event_response_t register_mem_events_learn(vmi_instance_t vmi) {
    memset(&cr3_event, 0, sizeof(vmi_event_t));
    cr3_event.version = VMI_EVENTS_VERSION;
    cr3_event.type = VMI_EVENT_REGISTER;
    cr3_event.callback = cr3_register_learn_callback;
    cr3_event.reg_event.reg = CR3;
    cr3_event.reg_event.in_access = VMI_REGACCESS_W;

    return vmi_register_event(vmi, &cr3_event);
}

int mem_learn (char *name, int set_time) {
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

    if ( VMI_SUCCESS == register_mem_events_learn(vmi) ) {
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
    if ( vmi_are_events_pending(vmi) > 0 )
        vmi_events_listen(vmi, 0);

    vmi_clear_event(vmi, &cr3_event, NULL);
    vmi_clear_event(vmi, &msr_syscall_event, NULL);

    vmi_destroy(vmi);

    return 0;
}