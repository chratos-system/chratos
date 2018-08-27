#include <chratos/node/working.hpp>

#include <pwd.h>
#include <sys/types.h>

namespace chratos
{
boost::filesystem::path app_path ()
{
	auto entry (getpwuid (getuid ()));
	assert (entry != nullptr);
	boost::filesystem::path result (entry->pw_dir);
	return result;
}
}
