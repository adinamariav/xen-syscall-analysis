#include <libvmi/libvmi.h>
#include <libvmi/events.h>

static vmi_event_t cr3_event;
static vmi_event_t msr_syscall_event;
static reg_t cr3;
static bool mem_events_registered;
static reg_t lstar;

bool register_lstar_mem_event(vmi_instance_t vmi, vmi_event_t *event, event_callback_t cb);