#include <chratos/secure/utility.hpp>

#include <chratos/lib/interface.h>
#include <chratos/node/working.hpp>

static std::vector<boost::filesystem::path> all_unique_paths;

boost::filesystem::path chratos::working_path ()
{
	auto result (chratos::app_path ());
	switch (chratos::chratos_network)
	{
		case chratos::chratos_networks::chratos_test_network:
			result /= "ChratosTest";
			break;
		case chratos::chratos_networks::chratos_beta_network:
			result /= "ChratosBeta";
			break;
		case chratos::chratos_networks::chratos_live_network:
			result /= "Chratos";
			break;
	}
	return result;
}

boost::filesystem::path chratos::unique_path ()
{
	auto result (working_path () / boost::filesystem::unique_path ());
	all_unique_paths.push_back (result);
	return result;
}

std::vector<boost::filesystem::path> chratos::remove_temporary_directories ()
{
	for (auto & path : all_unique_paths)
	{
		boost::system::error_code ec;
		boost::filesystem::remove_all (path, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary directory: " << ec.message () << std::endl;
		}

		// lmdb creates a -lock suffixed file for its MDB_NOSUBDIR databases
		auto lockfile = path;
		lockfile += "-lock";
		boost::filesystem::remove (lockfile, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary lock file: " << ec.message () << std::endl;
		}
	}
	return all_unique_paths;
}

void chratos::open_or_create (std::fstream & stream_a, std::string const & path_a)
{
	stream_a.open (path_a, std::ios_base::in);
	if (stream_a.fail ())
	{
		stream_a.open (path_a, std::ios_base::out);
	}
	stream_a.close ();
	stream_a.open (path_a, std::ios_base::in | std::ios_base::out);
}
