#include <iostream>
#include "osm.h"

int main ()
{
  double op = osm_operation_time (200000);

  double func = osm_function_time (200000);

//  double syscall = osm_syscall_time (200000);

  std::cout << "simple_arithmetic: " << op << "\n" <<
            "function: " << func << "\n" << "syscall: "
            << std::endl;

  return 0;
}
