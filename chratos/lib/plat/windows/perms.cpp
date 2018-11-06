#include <assert.h>
#include <boost/filesystem.hpp>
#include <chratos/lib/utility.hpp>

#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>

void chratos::set_umask ()
{
	int oldMode;

	auto result (_umask_s (_S_IWRITE | _S_IREAD, &oldMode));
	assert (result == 0);
}

void chratos::set_secure_perm_directory (boost::filesystem::path const & path)
{
	boost::filesystem::permissions (path, boost::filesystem::owner_all);
}

void chratos::set_secure_perm_directory (boost::filesystem::path const & path, boost::system::error_code & ec)
{
	boost::filesystem::permissions (path, boost::filesystem::owner_all, ec);
}

void chratos::set_secure_perm_file (boost::filesystem::path const & path)
{
	boost::filesystem::permissions (path, boost::filesystem::owner_read | boost::filesystem::owner_write);
}

void chratos::set_secure_perm_file (boost::filesystem::path const & path, boost::system::error_code & ec)
{
	boost::filesystem::permissions (path, boost::filesystem::owner_read | boost::filesystem::owner_write, ec);
}
