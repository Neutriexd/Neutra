#include "scheduler.h"
#include "kernel.h"

static Scheduler sched = {0};
static SchedulerTableEntry scheduler_table[MAX_SCHEDULER_TABLE];
static int scheduler_table_count = 0;
void scheduler_init(void) {
    memset(&sched, 0, sizeof(Scheduler));
    sched.head = 0;
    sched.tail = 0;
    sched.count = 0;
    sched.current_task_id = -1;
    sched.total_time_ms = 0;
}

void scheduler_add_task(const char* name, void (*entry)(void), int32_t time_ms) {
    if (sched.count >= MAX_TASKS) {
        return;
    }
    
    int idx = sched.tail;
    memset(&sched.queue[idx], 0, sizeof(Task));
    
    sched.queue[idx].id = sched.count;
    sched.queue[idx].name = name;
    sched.queue[idx].entry = entry;
    sched.queue[idx].remaining_time_ms = time_ms;
    sched.queue[idx].state = TASK_READY;
    sched.queue[idx].sp = (uint32_t)(&sched.queue[idx].stack[TASK_STACK_SIZE - 1]);
    
    sched.tail = (sched.tail + 1) % MAX_TASKS;
    sched.count++;
}

void scheduler_add_table(
    const char* name,
    void (*entry)(void),
    int32_t time_ms
) {
    if (scheduler_table_count >= MAX_SCHEDULER_TABLE) {
        return;
    }

    scheduler_table[scheduler_table_count].name = name;
    scheduler_table[scheduler_table_count].entry = entry;
    scheduler_table[scheduler_table_count].time_ms = time_ms;

    scheduler_table_count++;
}

void scheduler_check_table(const char* name)
{
    for (int i = 0; i < scheduler_table_count; i++) {

        if (strcmp(name, scheduler_table[i].name) == 0) {

            scheduler_add_task(
                scheduler_table[i].name,
                scheduler_table[i].entry,
                scheduler_table[i].time_ms
            );

            return;
        }
    }
}

void scheduler_add_task_priority(const char* name, void (*entry)(void), int32_t time_ms, int priority) {
    if (sched.count >= MAX_TASKS) {
        return;
    }
    
    int idx = sched.tail;
    memset(&sched.queue[idx], 0, sizeof(Task));
    
    sched.queue[idx].id = sched.count;
    sched.queue[idx].name = name;
    sched.queue[idx].entry = entry;
    sched.queue[idx].remaining_time_ms = time_ms;
    sched.queue[idx].state = TASK_READY;
    sched.queue[idx].priority = priority;
    sched.queue[idx].sp = (uint32_t)(&sched.queue[idx].stack[TASK_STACK_SIZE - 1]);
    
    sched.tail = (sched.tail + 1) % MAX_TASKS;
    sched.count++;
}

void scheduler_run(void) {
    while (sched.count > 0) {
        Task* current = &sched.queue[sched.head];
        sched.current_task_id = current->id;
        
        current->state = TASK_RUNNING;
        current->entry();
        
        current->remaining_time_ms -= TIME_SLICE_MS;
        sched.total_time_ms += TIME_SLICE_MS;
        
        sched.head = (sched.head + 1) % MAX_TASKS;
        
        if (current->remaining_time_ms > 0) {
            current->state = TASK_READY;
            sched.queue[sched.tail] = *current;
            sched.tail = (sched.tail + 1) % MAX_TASKS;
        } else {
            current->state = TASK_DEAD;
            sched.count--;
        }
    }
}

int scheduler_step(void) {
    if (sched.count <= 0) return 0;
    
    Task* current = &sched.queue[sched.head];
    sched.current_task_id = current->id;
    
    current->state = TASK_RUNNING;
    current->entry();
    
    current->remaining_time_ms -= TIME_SLICE_MS;
    sched.total_time_ms += TIME_SLICE_MS;
    
    sched.head = (sched.head + 1) % MAX_TASKS;
    
    if (current->remaining_time_ms > 0) {
        current->state = TASK_READY;
        sched.queue[sched.tail] = *current;
        sched.tail = (sched.tail + 1) % MAX_TASKS;
    } else {
        current->state = TASK_DEAD;
        sched.count--;
    }
    
    return sched.count;
}

void scheduler_print_stats(void) {
    vga_write("\ntotal runtime: \n", MAKE_COLOR(COLOR_BLACK, COLOR_CYAN));
    vga_print_int(sched.total_time_ms, MAKE_COLOR(COLOR_BLACK, COLOR_CYAN));
    vga_write("ms\n", MAKE_COLOR(COLOR_BLACK, COLOR_CYAN));
}

Task* scheduler_current_task(void) {
    if (sched.current_task_id >= 0 && sched.current_task_id < MAX_TASKS) {
        return &sched.queue[sched.current_task_id];
    }
    return 0;
}

int scheduler_task_count(void) {
    return sched.count;
}

unsigned int scheduler_total_time(void) {
    return sched.total_time_ms;
}

void scheduler_reset(void) {
    memset(&sched, 0, sizeof(Scheduler));
    sched.head = 0;
    sched.tail = 0;
    sched.count = 0;
    sched.current_task_id = -1;
    sched.total_time_ms = 0;
}
