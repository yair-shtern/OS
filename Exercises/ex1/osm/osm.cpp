#include <sys/time.h>
#include <utility>
#include "osm.h"

#define SEC_TO_NANO 1000000000
#define MIK_TO_NANO 1000
#define FAIL -1
#define UNROLLING_FACTOR 5


/**
 * Enum for assigning different operation
 */
enum Operation {
    ARITHMETIC, FUNCTION, TRAP
};

/**
 * Empty function
 */
void empty_func_call ()
{}

/**
 * Helper function that runs the actual operation defined by the user
 * @param operation Enum for the specific operation
 * @param iterations Number of iterations
 * @return A double that has no real significance
 */
double make_operations (Operation operation, unsigned int iterations)
{
  switch (operation)
    {
      case ARITHMETIC:
        {
          int x = 0, y = 0, z = 0, w = 1, t = 1, k = 1;
          for (unsigned int i = iterations; i > 0; i -= UNROLLING_FACTOR)
            {
              x = x + y + z + w + t + k;
            }
          return x;
        }
      case FUNCTION:
        {
          for (unsigned int i = iterations; i > 0; i -= UNROLLING_FACTOR)
            {
              empty_func_call ();
              empty_func_call ();
              empty_func_call ();
              empty_func_call ();
              empty_func_call ();
            }
          return 0;
        }
      case TRAP:
        {
          for (unsigned int i = iterations; i > 0; i -= UNROLLING_FACTOR)
            {
              OSM_NULLSYSCALL;
              OSM_NULLSYSCALL;
              OSM_NULLSYSCALL;
              OSM_NULLSYSCALL;
              OSM_NULLSYSCALL;
            }
        }
      break;
    }
  return 0;
}

/**
 * Helper function that operates the time counting of the operation
 * @param op The specified operation Enum
 * @param iterations Number of iterations
 * @return The measured time of the operation in Nano-seconds in case of
 * success, -1 otherwise.
 */
double get_run_time (Operation op, unsigned int iterations)
{
  if (iterations <= 0)
    {
      return FAIL;
    }
  std::pair<int, int> times;
  struct timeval start{}, end{};
  times.first = gettimeofday (&start, nullptr);
  make_operations (op, iterations);
  times.second = gettimeofday (&end, nullptr);
  if (times.first == FAIL || times.second == FAIL)
    {
      return FAIL;
    }
  return (double) ((end.tv_sec * SEC_TO_NANO + (end.tv_usec) * MIK_TO_NANO)
                   - (start.tv_sec * SEC_TO_NANO
                      + start.tv_usec * MIK_TO_NANO)) / iterations;
}

/**
 * implementation of the arithmetic operation function
 * @param iterations Number of iterations
 * @return The measured time of the operation in Nano-seconds in case of
 * success, -1 otherwise. */
double osm_operation_time (unsigned int iterations)
{
  return get_run_time (ARITHMETIC, iterations);
}

/**
 * implementation of the empty function operation
 * @param iterations Number of iterations
 * @return The measured time of the operation in Nano-seconds in case of
 * success, -1 otherwise. */
double osm_function_time (unsigned int iterations)
{
  return get_run_time (FUNCTION, iterations);
}

/**
 * implementation of the syscall operation function
 * @param iterations Number of iterations
 * @return The measured time of the operation in Nano-seconds in case of
 * success, -1 otherwise. */
double osm_syscall_time (unsigned int iterations)
{
  return get_run_time (TRAP, iterations);
}

