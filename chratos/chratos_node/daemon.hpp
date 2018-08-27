#include <chratos/node/node.hpp>
#include <chratos/node/rpc.hpp>

namespace chratos_daemon
{
class daemon
{
public:
	void run (boost::filesystem::path const &);
};
class daemon_config
{
public:
	daemon_config (boost::filesystem::path const &);
	bool deserialize_json (bool &, boost::property_tree::ptree &);
	void serialize_json (boost::property_tree::ptree &);
	bool upgrade_json (unsigned, boost::property_tree::ptree &);
	bool rpc_enable;
	chratos::rpc_config rpc;
	chratos::node_config node;
	bool opencl_enable;
	chratos::opencl_config opencl;
};
}
