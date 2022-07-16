#include "MapReduceFramework.h"
#include <pthread.h>
#include "Barrier/Barrier.cpp"
#include <atomic>
#include <algorithm>
#include <semaphore.h>
#include <iostream>

//Error messages//
static const char *error_message_1 = "system error: unable to create semaphore \n";
static const char *error_message_2 = "system error: unable to create pthread \n";
static const char *error_message_3 = "system error: unable to join pthread \n";
static const char *error_message_4 = "system error: unable to lock/ unlock mutex \n";
static const char *error_message_5 = "system error: sem_wait error \n";
static const char *error_message_6 = "system error: unable to destroy semaphore/mutex \n";

/**
 * Job context struct, holding all the relevant fields for each thread.
 * Should be used as a jobHandle, using static cast.
 */
typedef struct {
    int threads_num;
    pthread_t *threads;
    int tid;
    pthread_mutex_t *mutex;
    pthread_mutex_t *mutex_2;
    Barrier *barrier;
    JobState *state;
    std::atomic<uint64_t> *atomic_counter;
    const std::vector<InputPair> *context_input_vec;
    std::vector<IntermediatePair> context_intermediate_vec;
    std::vector<OutputPair> *context_output_vec;
    std::vector<std::vector<IntermediatePair>> *all_intermediate_vec;
    std::vector<std::vector<IntermediatePair>> *shuffled_vector;
    sem_t *sem;
    const MapReduceClient *client;
    bool job_joined;
} JobContext;

void map_sort(JobContext *context);

void shuffle(JobContext *contexts);

void reduce(JobContext *context);

void *operate(void *arg);

void *main_tread_operate(void *arg);

void error_print(const char *message);

bool compare_keys(IntermediatePair pair1, IntermediatePair pair2);

void set_atomic_stage(void *arg, int value);

void set_atomic_processed_pairs(void *arg, int value);

void set_atomic_total_number_of_pairs(void *arg, int value);

int get_atomic_stage(void *arg);

int get_atomic_processed_pairs(void *arg);

int get_atomic_total_num_of_pairs(void *arg);

void update_stage(JobContext *context);

void lock_mutex(pthread_mutex_t *mutex);

void unlock_mutex(pthread_mutex_t *mutex);

K2 *findMaxKey(const JobContext *context, K2 *curr_max_key);

void getMaxKeyFromAllVectors(const JobContext *context, const K2 *curr_max_key, IntermediateVec &temp_vector);

void sort(JobContext *context);

void getReadyToReduce(JobContext *context);

/**
 * Map reduce function that handles the process.
 * Can assume input is valid
 * Need to use c function pthread_create
 * @param client reference to MapReduceClient object
 * @param inputVec reference of input vector ,(K1*,V1*) pairs
 * @param outputVec reference of output vector, (K3*,V3*) pairs
 * @param multiThreadLevel number of worker threads to be used for running
 * the algorithm
 * @return JobHandle (void*) that will be used for monitoring the job
 */
JobHandle startMapReduceJob(const MapReduceClient &client,
                            const InputVec &inputVec, OutputVec &outputVec,
                            int multiThreadLevel) {

    //Fields and variables initialization//
    auto threads = new(std::nothrow) pthread_t[multiThreadLevel];
    auto contexts = new(std::nothrow) JobContext[multiThreadLevel];
    auto mutex = new(std::nothrow) pthread_mutex_t(PTHREAD_MUTEX_INITIALIZER);
    auto mutex_2 = new(std::nothrow) pthread_mutex_t(PTHREAD_MUTEX_INITIALIZER);
    auto barrier = new(std::nothrow) Barrier(multiThreadLevel);
    auto atomic_counter = new(std::nothrow) std::atomic<uint64_t>(0);
    auto shuffled_vector = new(std::nothrow) std::vector<std::vector<IntermediatePair>>;
    auto all_intermediate_vec = new(std::nothrow) std::vector<std::vector<IntermediatePair>>;
    auto *state = new(std::nothrow)JobState{UNDEFINED_STAGE, static_cast<float>(0)};
    auto sem = new sem_t;
    bool job_joined = false;

    if (sem_init(sem, 0, 0) != 0) {
        error_print(error_message_1);
    }

    for (int i = 0; i < multiThreadLevel; ++i) {
        IntermediateVec temp;
        contexts[i] = {multiThreadLevel, threads, i, mutex, mutex_2, barrier, state,
                       atomic_counter, &inputVec, temp,
                       &outputVec, all_intermediate_vec, shuffled_vector, sem, &client, job_joined};
    }

    // update stage
    set_atomic_stage(&contexts[0], MAP_STAGE);
    set_atomic_total_number_of_pairs(&contexts[0], int(contexts[0].context_input_vec->size()));
    update_stage(&contexts[0]);

    // create main thread
    if (pthread_create(&threads[0], nullptr, main_tread_operate, &contexts[0]) != 0) {
        error_print(error_message_2);
    }
    // create threads
    for (int i = 1; i < multiThreadLevel; ++i) {
        if (pthread_create(&threads[i], nullptr, operate, &contexts[i]) != 0) {
            error_print(error_message_2);
        }
    }
    return static_cast<JobHandle> (&contexts[0]);
}

/**
 * operating function for each thread.
 * @param arg the context for the thread
 * @return nullptr
 */
void *operate(void *arg) {
    auto *context = (JobContext *) arg;
    map_sort(context);
    if (sem_wait(context->sem) != 0) {
        error_print(error_message_5);
    }
    reduce(context);
    return nullptr;
}

/**
 * operating function for the main thread,
 * calls each stage including the shuffle stage.
 * @param arg job context struct of the main thread.
 * @return nullptr
 */
void *main_tread_operate(void *arg) {
    auto *context = (JobContext *) arg;
    map_sort(context);
    shuffle(context);
    reduce(context);

    // wait until all the threads finish
    for (int i = 1; i < context->threads_num; ++i) {
        if (pthread_join(context->threads[i], nullptr) != 0) {
            error_print(error_message_3);
        }
    }
    return nullptr;
}

/**
 * A function that gets JobHandle returned by startMapReduceFramework
 * and waits until it's finished.
 * Should use pthread_join, not more that once from the same process
 * @param job JobHandle (void*)
 */
void waitForJob(JobHandle job) {
    auto *context = (JobContext *) job;
    if (context->job_joined) {
        return;
    }
    if (pthread_join(context->threads[0], nullptr) != 0) {
        error_print(error_message_3);
    }
    context->job_joined = true;
}

/**
 * Function that gets a JobHandle and updates the state of the job
 * into the given JobState struct.
 * @param job JobHandle (void*)
 * @param state State of job struct
 */
void getJobState(JobHandle job, JobState *state) {
    auto *context = (JobContext *) job;
    lock_mutex(context->mutex);
    state->stage = context->state->stage;
    state->percentage = context->state->percentage;
    unlock_mutex(context->mutex);
}

/**
 * Releasing all resources of a job. avoid releasing resources before
 * the job is finished. after this function is called the job handle
 * will be invalid.
 * In case this function is called and the job is not finished yet,
 * wait until the job is finished to close it.
 * Should use the functions pthread_mutex_destroy, sem_destroy to
 * release mutexes and semaphores.
 * @param job obHandle (void*)
 */
void closeJobHandle(JobHandle job) {

    auto *context = (JobContext *) job;
    if (context == nullptr) {
        return;
    }

    // wait for the job to finish before closing it
    waitForJob(job);
    if (pthread_mutex_destroy(context->mutex) != 0) {
        error_print(error_message_6);
    }
    if (pthread_mutex_destroy(context->mutex_2) != 0) {
        error_print(error_message_6);
    }
    if (sem_destroy(context->sem) != 0) {
        error_print(error_message_6);
    }
    delete context->sem;
    delete context->state;
    delete context->all_intermediate_vec;
    delete context->shuffled_vector;
    delete context->atomic_counter;
    delete context->barrier;
    delete context->mutex;
    delete context->mutex_2;
    delete[] context->threads;
    delete[] context;
    context = nullptr;
}

/**
 * Helper function for the map and sort stage,
 * called by each one of the threads.
 */
void map_sort(JobContext *context) {
    while (true) {
        lock_mutex(context->mutex);
        unsigned int old_value = get_atomic_processed_pairs(context);
        if (old_value < context->context_input_vec->size()) {
            set_atomic_processed_pairs(context, 1);
            InputPair pair = context->context_input_vec->at(old_value);
            context->client->map(pair.first, pair.second, context);
            unlock_mutex(context->mutex);
            update_stage(context);
        } else {
            unlock_mutex(context->mutex);
            break;
        }
    }
    sort(context);
    context->barrier->barrier();
}

/**
 * Helper function that sorts the intermediate vector of each
 * thread.
 * @param context the context of the calling thread
 */
void sort(JobContext *context) {
    if (!context->context_intermediate_vec.empty()) {
        std::sort(context->context_intermediate_vec.begin(),
                  context->context_intermediate_vec.end(), compare_keys);
        lock_mutex(context->mutex);
        context->all_intermediate_vec->push_back(context->context_intermediate_vec);
        unlock_mutex(context->mutex);
    }
}

/**
 * Helper function for shuffle part, called only by main thread.
 * @param context main thread context.
 */
void shuffle(JobContext *context) {
    int pairs_num = 0;
    for (auto &i: *context->all_intermediate_vec) {
        pairs_num += int(i.size());
    }
    if (pairs_num == 0) { return; }
    *context->atomic_counter = 0;
    set_atomic_stage(context, SHUFFLE_STAGE);
    set_atomic_total_number_of_pairs(context, pairs_num);
    update_stage(context);

    while (true) {
        K2 *curr_max_key = nullptr;
        curr_max_key = findMaxKey(context, curr_max_key);
        if (curr_max_key == nullptr) {
            break;
        }
        // shuffle
        IntermediateVec temp_vector;
        getMaxKeyFromAllVectors(context, curr_max_key, temp_vector);
        context->shuffled_vector->push_back(temp_vector);
        set_atomic_processed_pairs(context, int(temp_vector.size()));
        update_stage(context);
    }
    getReadyToReduce(context);

}

/**
 * Helper function for the reduce stage.
 * updates the job stage and releases all
 * waiting threads.
 * @param context context of main thread.
 */
void getReadyToReduce(JobContext *context) {// Updates stage
    *context->atomic_counter = 0;
    set_atomic_stage(context, REDUCE_STAGE);
    set_atomic_total_number_of_pairs(context, int(context->shuffled_vector->size()));
    update_stage(context);

    // release all sleeping threads
    for (int i = 0; i < context->threads_num; i++) {
        if (sem_post(context->sem) != 0) {
            error_print(error_message_2);
        }
    }
}

/**
 * Helper function used in the shuffle stage,
 * gets the pair with max key from intermediate vector.
 * @param context main thread context
 * @param curr_max_key current max key
 * @param temp_vector temp vector
 */
void getMaxKeyFromAllVectors(const JobContext *context,
                             const K2 *curr_max_key, IntermediateVec &temp_vector) {
    for (auto &vec: *context->all_intermediate_vec) {
        while (!vec.empty()) {
            K2 *key = vec.back().first;
            if (!(*curr_max_key < *key) && !(*key < *curr_max_key)) {
                temp_vector.push_back(vec.back());
                vec.pop_back();
            } else {
                break;
            }
        }
    }
}

/**
 * Helper function for the shuffle stage.
 * finds the max key from the intermediate vector.
 * @param context main thread context
 * @param curr_max_key current max key
 * @return new max key
 */
K2 *findMaxKey(const JobContext *context, K2 *curr_max_key) {
    for (auto &vec: *context->all_intermediate_vec) {
        if (vec.empty()) {
            continue;
        } else {
            K2 *key = vec.back().first;
            if (curr_max_key == nullptr || *curr_max_key < *key) {
                curr_max_key = key;
            }
        }
    }
    return curr_max_key;
}

/**
 * Helper function for the reduce stage.
 * called by each one of the threads and
 * performs reduce on the shuffled vector.
 * @param context context of the calling thread
 */
void reduce(JobContext *context) {
    while (true) {
        lock_mutex(context->mutex_2);
        if (!context->shuffled_vector->empty()) {
            IntermediateVec pairs_vector = context->shuffled_vector->back();
            context->shuffled_vector->pop_back();
            unlock_mutex(context->mutex_2);

            lock_mutex(context->mutex);
            context->client->reduce(&pairs_vector, context);
            unlock_mutex(context->mutex);
            set_atomic_processed_pairs(context, 1);
            update_stage(context);
        } else {
            unlock_mutex(context->mutex_2);
            break;
        }

    }
}

/**
 * Helper function that unlocks a given
 * mutex.
 * @param mutex the mutex to be unlocked.
 */
void unlock_mutex(pthread_mutex_t *mutex) {
    if (pthread_mutex_unlock(mutex) != 0) {
        error_print(error_message_4);
    }
}

/**
 * Helper function that locks a given
 * mutex.
 * @param mutex the mutex to be locked.
 */
void lock_mutex(pthread_mutex_t *mutex) {
    if (pthread_mutex_lock(mutex) != 0) {
        error_print(error_message_4);
    }
}

/**
 * Helper function that compares two given intermediate
 * pairs, using their compare operator.
 * @param pair1 first pair
 * @param pair2 second pair
 * @return true if first is smaller than second, false otherwise.
 */
bool compare_keys(const IntermediatePair pair1, const IntermediatePair pair2) {
    return *pair1.first < *pair2.first;
}

/**
 * emit2 function, receives intermediate key and value
 * and creates a new intermediate pair from them,
 * and adds it to the thread intermediate vector.
 * @param key key
 * @param value value
 * @param context calling thread context
 */
void emit2(K2 *key, V2 *value, void *context) {
    auto temp_context = (JobContext *) context;
    IntermediatePair temp_pair(key, value);
    temp_context->context_intermediate_vec.push_back(temp_pair);
}

/**
 * emit3 function, receives output key and value
 * and creates a new output pair from them,
 * and adds it to the thread output vector.
 * @param key key
 * @param value value
 * @param context calling thread context
 */
void emit3(K3 *key, V3 *value, void *context) {
    auto *temp_context = (JobContext *) context;
    OutputPair temp_pair(key, value);
    temp_context->context_output_vec->push_back(temp_pair);
}

/**
 * Helper function that updates the stage of the
 * process.
 * @param context calling thread context
 */
void update_stage(JobContext *context) {
    lock_mutex(context->mutex);
    int processed_pairs = get_atomic_processed_pairs(context);
    int total_pairs = get_atomic_total_num_of_pairs(context);
    context->state->stage = static_cast<stage_t>(get_atomic_stage(context));
    context->state->percentage = 100 * (float(processed_pairs)) / float(total_pairs);
    unlock_mutex(context->mutex);
}

/**
 * Helper function that prints a an informative error message
 * and exits the program.
 */
void error_print(const char *message) {
    std::cout << message;
    exit(1);
}

/**
 * set the current stage, represented by last two bits.
 * @param value value representing the stage between 0-3
 */
void set_atomic_stage(void *arg, int value) {

    auto *temp_context = (JobContext *) arg;
    (*(temp_context->atomic_counter)) += static_cast<unsigned long> (value % 4) << 62;
}

/**
 * increase by one the number of finished pairs from current stage,
 * represented by 31 Middle bits
 */
void set_atomic_processed_pairs(void *arg, int value) {
    auto *temp_context = (JobContext *) arg;
    (*(temp_context->atomic_counter)) += static_cast<unsigned long> (value) << 31;
}

/**
 * 31 last bits representing the total number of pairs to be processed
 * @param value total number of pairs to be updated
 */
void set_atomic_total_number_of_pairs(void *arg, int value) {
    auto *temp_context = (JobContext *) arg;
    (*(temp_context->atomic_counter)) += static_cast<unsigned long> (value);
}

/**
 * get the current stage, represented by last two bits.
 * @return The stage we're at between 0-3
 */
int get_atomic_stage(void *arg) {
    auto *temp_context = (JobContext *) arg;
    return static_cast<int> ((*(temp_context->atomic_counter)).load() >> 62);
}

/**
 * get the number of processed pairs from current stage,
 * represented by 31 Middle bits
 * @return number of processed pairs from current stage
 */
int get_atomic_processed_pairs(void *arg) {
    auto *temp_context = (JobContext *) arg;
    return static_cast<int> ((*(temp_context->atomic_counter)).load() >> 31 & (0xfffffffU));
}

/**
 * get 31 last bits representing the total number of pairs to be processed
 * @return total number of pairs to be processed
 */
int get_atomic_total_num_of_pairs(void *arg) {
    auto *temp_context = (JobContext *) arg;
    return static_cast<int> ((*(temp_context->atomic_counter)).load() & (0xfffffffU));
}



