#include <pthread.h>
#include <chratos/lib/utility.hpp>

void chratos::thread_role::set_name (std::string thread_name)
{
	pthread_setname_np (pthread_self (), thread_name.c_str ());
}
