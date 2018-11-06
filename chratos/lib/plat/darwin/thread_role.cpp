#include <chratos/lib/utility.hpp>
#include <pthread.h>

void chratos::thread_role::set_name (std::string thread_name)
{
	pthread_setname_np (thread_name.c_str ());
}
