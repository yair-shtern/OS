#include "uthreads.h"
#include <queue>
#include <cstdlib>
#include <vector>
#include <signal.h>
#include <sys/time.h>
#include <csetjmp>
#include <thread>
#include <iostream>
#include "Uthread.h"

static const char *const SLEEP_ERROR = "thread library error: trying to send to "
                                       "sleep the main thread.";
static const char *const NEGATIVE_QUANTOM_ERROR = "thread library error: "
                                                  "quantum_usecs must be positive integer.";
static const char *const SYS_ERROR_SET_MASK = "system error: unable to set mask to current thread.";
static const char *const SYS_ERROR_HANDLER = "system error: unable to set handler to SIGVTALRM.";
static const char *const SYS_ERROR_VIRTUAL_TIME = "system error: unable to set virtual time.";
static const char *const NULL_SPAWN_ERROR = "thread library error: spawn can't get null entry point.";
static const char *const MAX_THREADS_ERROR = "thread library error: exceeded the max number"
                                             " of allowed threads.";
static const char *const TERMINATE_ERROR = "thread library error: trying to terminate a non "
                                           "valid thread with non-valid id.";
static const char *const BLOCK_ERROR = "thread library error: trying to block thread with non-valid id.";
static const char *const RESUME_ERROR = "thread library error: trying to resume a thread with non-valid id.";
static const char *const QUANTUM_ERROR = "thread library error: trying to get quantums of thread with non-valid id.";
static const int SECONDS = 1000000;

std::vector<Uthread *> ready_vector;
Uthread *uthreads_array[MAX_THREAD_NUM];

bool index_free[MAX_THREAD_NUM] = {true};
int uthread_quantum_usecs = -1;
int num_of_uthread = 0;
Uthread *running_thread;
int quantums;
struct itimerval timer;

void scheduler (int);

int min_free_id ();

void delete_all_thread ();

void erase_from_ready (int tid);

bool invalid_tid (int tid);

void set_clock ();

void block_unblock (int sig);

/**
 * @brief initializes the thread library.
 *
 * You may assume that this function is called before any other thread library function, and that it is called
 * exactly once.
 * The input to the function is the length of a quantums in micro-seconds.
 * It is an error to call this function with non-positive quantum_usecs.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_init (int quantum_usecs)
{
    block_unblock (SIG_SETMASK);
    quantums = 1;
    if (quantum_usecs <= 0)
        {
            std::cerr << NEGATIVE_QUANTOM_ERROR << std::endl;
            return -1;
        }
    running_thread = new Uthread (0, nullptr);
    running_thread->set_uthread_state (RUNNING);
    running_thread->increase_quantum ();
    uthreads_array[0] = running_thread;

    uthread_quantum_usecs = quantum_usecs;
    struct sigaction sa = {nullptr};
    sa.sa_handler = &scheduler;
    if (sigaction (SIGVTALRM, &sa, nullptr) < 0)
        {
            std::cerr << SYS_ERROR_HANDLER << std::endl;
            delete_all_thread();
            exit (1);
        }
    set_clock ();
    for (int i = 0; i < MAX_THREAD_NUM; i++)
        {
            index_free[i] = true;
        }
    index_free[0] = false;
    block_unblock (SIG_UNBLOCK);
    return 0;
}

/**
 * Helper function that block and unblock the SIGVTALRM signal according to the sig parameter.
 * @param sig SIG_SETMASK or SIG_UNBLOCK
 */
void block_unblock (int sig)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGVTALRM);
    if (sigprocmask (sig, &set, nullptr) == -1)
        {
            std::cerr << SYS_ERROR_SET_MASK << std::endl;
            delete_all_thread();
            exit (1);
        }
}

/**
 * Helper function that sets virtual clock
 */
void set_clock ()
{

    int seconds = uthread_quantum_usecs / SECONDS;
    int useconds = uthread_quantum_usecs - seconds * SECONDS;
    timer.it_value.tv_sec = seconds;
    timer.it_value.tv_usec = useconds;
    timer.it_interval.tv_sec = seconds;
    timer.it_interval.tv_usec = useconds;
    if (setitimer (ITIMER_VIRTUAL, &timer, nullptr) == -1)
        {
            std::cerr << SYS_ERROR_VIRTUAL_TIME << std::endl;
            delete_all_thread();
            exit (1);
        }
}

/**
 * Helper function that updates the num of quantums for each
 * sleeping thread
 */
void update_sleeping_threads ()
{
    for (int i = 0; i < MAX_THREAD_NUM; i++)
        {
            if (!index_free[i])
                {
                    if (uthreads_array[i]->get_is_sleeping ())
                        {
                            uthreads_array[i]->decrease_num_q_to_sleep ();
                            if (uthreads_array[i]->get_num_q_to_sleep () == 0)
                                {
                                    uthreads_array[i]->set_is_sleeping (false);
                                    if (uthreads_array[i]->get_uthread_state () != BLOCKED)
                                        {
                                            uthread_resume (i);
                                        }
                                }
                        }
                }
        }
}

/**
 * Function that manages the current thread switch,
 * receives SIGVTALRM
 */
void scheduler (int)
{
    block_unblock (SIG_SETMASK);
    quantums++;
    update_sleeping_threads ();
    if (running_thread != nullptr)
        {
            if (sigsetjmp (running_thread->getEnv (), 1) == 1)
                {
                    return;
                }
            if (running_thread->get_uthread_state () != BLOCKED && running_thread->get_uthread_state () != SLEEP)
                {
                    running_thread->set_uthread_state (READY);
                    ready_vector.push_back (running_thread);
                }
        }
    running_thread = ready_vector.front ();
    ready_vector.erase (ready_vector.begin ());
    running_thread->set_uthread_state (RUNNING);
    running_thread->increase_quantum ();
    block_unblock (SIG_UNBLOCK);
    siglongjmp (running_thread->getEnv (), 1);
}

/**
 * @brief Creates a new thread, whose entry point is the function entry_point with the signature
 * void entry_point(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 *
 * @return On success, return the ID of the created thread. On failure, return -1.
*/
int uthread_spawn (thread_entry_point entry_point)
{
    block_unblock (SIG_SETMASK);
    if (entry_point == nullptr)
        {
            std::cerr << NULL_SPAWN_ERROR << std::endl;
            block_unblock (SIG_UNBLOCK);
            return -1;
        }
    int free_tid = min_free_id ();
    if (free_tid == -1)
        {
            std::cerr << MAX_THREADS_ERROR << std::endl;
            block_unblock (SIG_UNBLOCK);
            return -1;
        }
    Uthread *new_thread = new Uthread (free_tid, entry_point);
    int tid = new_thread->get_tid ();
    uthreads_array[tid] = new_thread;
    ready_vector.push_back (new_thread);
    num_of_uthread++;
    block_unblock (SIG_UNBLOCK);
    return tid;
}

/**
 * Helper function tha finds and returns the minimal free thread id.
 * @return the free id if such exists, otherwise -1.
 */
int min_free_id ()
{
    for (int i = 1; i < MAX_THREAD_NUM; ++i)
        {
            if (index_free[i])
                {
                    index_free[i] = false;
                    return i;
                }
        }
    return -1;
}

/**
 * @brief Terminates the thread with ID tid and deletes it from all relevant control structures.
 *
 * All the resources allocated by the library for this thread should be released. If no thread with ID tid exists it
 * is considered an error. Terminating the main thread (tid == 0) will result in the termination of the entire
 * process using exit(0) (after releasing the assigned library memory).
 *
 * @return The function returns 0 if the thread was successfully terminated and -1 otherwise. If a thread terminates
 * itself or the main thread is terminated, the function does not return.
*/
int uthread_terminate (int tid)
{
    block_unblock (SIG_SETMASK);
    if (tid == 0)
        {
            delete_all_thread ();
            exit (0);
        }
    if (invalid_tid (tid))
        {
            std::cerr << TERMINATE_ERROR << std::endl;
            block_unblock (SIG_UNBLOCK);
            return -1;
        }
    int running_thread_tid = running_thread->get_tid ();
    if (running_thread_tid == tid)
        {
            running_thread = nullptr;
            set_clock ();
            scheduler (SIGVTALRM);
        }
    erase_from_ready (tid);
    delete (uthreads_array[tid]);
    uthreads_array[tid] = nullptr;
    index_free[tid] = true;
    num_of_uthread--;
    block_unblock (SIG_UNBLOCK);
    return 0;
}

/**
 * Helper function that erases the given id thread from list of ready
 * threads.
 * @param tid thread id.
 */
void erase_from_ready (int tid)
{

    for (long unsigned int i = 0; i < ready_vector.size (); ++i)
        {
            if (ready_vector.at (i)->get_tid () == tid)
                {
                    ready_vector.erase (ready_vector.begin () + i);
                }
        }
}

/**
 * Deletes all threads.
 */
void delete_all_thread ()
{
    for (auto thread: uthreads_array)
        {
            delete (thread);
        }
}

/**
 * @brief Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision should be made. Blocking a thread in
 * BLOCKED state has no effect and is not considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_block (int tid)
{
    block_unblock (SIG_SETMASK);
    if (tid == 0 || invalid_tid (tid))
        {
            std::cerr << BLOCK_ERROR << std::endl;
            block_unblock (SIG_UNBLOCK);
            return -1;
        }
    uthreads_array[tid]->set_uthread_state (BLOCKED);
    erase_from_ready (tid);
    if (running_thread->get_tid () == tid)
        {
            set_clock ();
            scheduler (SIGVTALRM);
        }
    block_unblock (SIG_UNBLOCK);
    return 0;
}

/**
 * @brief Resumes a blocked thread with ID tid and moves it to the READY state.
 *
 * Resuming a thread in a RUNNING or READY state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_resume (int tid)
{
    block_unblock (SIG_SETMASK);
    if (invalid_tid (tid))
        {
            std::cerr << RESUME_ERROR << std::endl;
            block_unblock (SIG_UNBLOCK);
            return -1;
        }
    Uthread *thread = uthreads_array[tid];
    if (!thread->get_is_sleeping ())
        {
            if (thread->get_uthread_state () == BLOCKED || thread->get_uthread_state () == SLEEP)
                {
                    ready_vector.push_back (thread);
                    thread->set_uthread_state (READY);
                }
        }
    block_unblock (SIG_UNBLOCK);
    return 0;
}

/**
 * @brief Blocks the RUNNING thread for num_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY threads list.
 * The number of quantums refers to the number of times a new quantums starts, regardless of the reason. Specifically,
 * the quantums of the thread which has made the call to uthread_sleep isn’t counted.
 * It is considered an error if the main thread (tid==0) calls this function.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_sleep (int num_quantums)
{
    block_unblock (SIG_SETMASK);
    if (running_thread->get_tid () == 0)
        {
            std::cerr << SLEEP_ERROR << std::endl;
            block_unblock (SIG_UNBLOCK);
            return -1;
        }
    running_thread->set_uthread_state (SLEEP);
    running_thread->set_is_sleeping (true);
    running_thread->set_num_q_to_sleep (num_quantums + 1); // decrease 1 quantum in scheduler
    set_clock ();
    scheduler (SIGVTALRM);
    block_unblock (SIG_UNBLOCK);
    return 0;
}

/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid ()
{
    return running_thread->get_tid ();
}

/**
 * @brief Returns the total number of quantums since the library was initialized, including the current quantums.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantums starts, regardless of the reason, this number should be increased by 1.
 *
 * @return The total number of quantums.
*/
int uthread_get_total_quantums ()
{
    return quantums;
}

/**
 * @brief Returns the number of quantums the thread with ID tid was in RUNNING state.
 *
 * On the first time a thread runs, the function should return 1. Every additional quantums
 * that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state when this function is called, include
 * also the current quantums). If no thread with ID tid exists it is considered an error.
 *
 * @return On success, return the number of quantums of the thread with ID tid. On failure, return -1.
*/
int uthread_get_quantums (int tid)
{
    if (invalid_tid (tid))
        {
            std::cerr << QUANTUM_ERROR << std::endl;
            return -1;
        }
    return uthreads_array[tid]->get_quantum ();
}

/**
 * Helper function that checks if the given tid is the id of an existing
 * thread and if its invalid number.
 * @param tid the id to be checked
 * @return true if invalid, false otherwise.
 */
bool invalid_tid (int tid)
{
    return (tid < 0 || tid >= MAX_THREAD_NUM || index_free[tid]);
}

