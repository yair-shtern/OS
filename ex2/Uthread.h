#ifndef EX2_UTHREAD_H
#define EX2_UTHREAD_H

#include "uthreads.h"
#include <stdio.h>
#include <csetjmp>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdbool.h>

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

enum state {
    READY, RUNNING, BLOCKED, SLEEP
};

/**
 * Class That represents a single thread object
 */
class Uthread {

public:
    /**
     * Constructor
     * @param tid id of thread
     * @param entry_point thread entry point
     */
    Uthread(int tid, thread_entry_point entry_point) :
            tid(tid), quantum(0), uthread_state(READY), is_sleeping(false), num_q_to_sleep(0) {
        address_t sp = (address_t) uthread_stack + STACK_SIZE - sizeof(address_t);
        address_t pc = (address_t) entry_point;
        sigsetjmp(env, 1);
        (env->__jmpbuf)[JB_SP] = translate_address(sp);
        (env->__jmpbuf)[JB_PC] = translate_address(pc);
        sigemptyset(&env->__saved_mask);
    }

    /* A translation is required when using an address of a variable.
    Use this as a black box in your code. */
    address_t translate_address(address_t addr) {
        address_t ret;
        asm volatile("xor    %%fs:0x30,%0\n"
                     "rol    $0x11,%0\n"
        : "=g" (ret)
        : "0" (addr));
        return ret;
    }

    void increase_quantum() {
        Uthread::quantum++;
    }
    void decrease_num_q_to_sleep() {
        Uthread::num_q_to_sleep--;
    }
    void set_num_q_to_sleep(int num) {
        num_q_to_sleep = num;
    }

    void set_uthread_state(state uthreadState) {
        uthread_state = uthreadState;
    }

    int get_tid() const {
        return tid;
    }

    int get_quantum() const {
        return quantum;
    }

    int get_num_q_to_sleep() const {
        return num_q_to_sleep;
    }

    state get_uthread_state() const {
        return uthread_state;
    }

    sigjmp_buf &getEnv() {
        return env;
    }

    void set_is_sleeping(bool is_sleeping) {
        Uthread::is_sleeping = is_sleeping;
    }

    bool get_is_sleeping() const {
        return is_sleeping;
    }


private:

    int tid;
    int quantum;
    char uthread_stack[STACK_SIZE];
    state uthread_state;
    sigjmp_buf env;
    bool is_sleeping;
    int num_q_to_sleep;
};

#endif //EX2_UTHREAD_H
