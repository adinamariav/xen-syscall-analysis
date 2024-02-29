#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <openssl/md5.h>
#include <libvmi/libvmi.h>
#include <libvmi/events.h>

#include "vmi.h"

static reg_t lstar;
static char saved_opcode = 0;
static vmi_event_t int_event;
static vmi_event_t sstep_event = {0};

struct bp_cb_data {
    reg_t sym_vaddr;
    char saved_opcode;
    uint64_t hit_count;
};

static char BREAKPOINT = 0xcc;

static struct bp_cb_data cb_data;

event_response_t set_lstar_breakpoint(vmi_instance_t vmi);

event_response_t breakpoint_cb(vmi_instance_t vmi, vmi_event_t *event);

event_response_t single_step_cb(vmi_instance_t vmi, vmi_event_t *event);