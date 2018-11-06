#include <chratos/node/nodeconfig.hpp>
// NOTE: to reduce compile times, this include can be replaced by more narrow includes
// once chratos::network is factored out of node.{c|h}pp
#include <chratos/node/node.hpp>

chratos::node_config::node_config () :
node_config (chratos::network::node_port, chratos::logging ())
{
}

chratos::node_config::node_config (uint16_t peering_port_a, chratos::logging const & logging_a) :
peering_port (peering_port_a),
logging (logging_a),
bootstrap_fraction_numerator (1),
receive_minimum (chratos::chr_ratio),
dividend_minimum (chratos::minimum_dividend_amount),
online_weight_minimum (60000 * chratos::Mchr_ratio),
online_weight_quorum (50),
password_fanout (1024),
io_threads (std::max<unsigned> (4, boost::thread::hardware_concurrency ())),
network_threads (std::max<unsigned> (4, boost::thread::hardware_concurrency ())),
work_threads (std::max<unsigned> (4, boost::thread::hardware_concurrency ())),
enable_voting (true),
bootstrap_connections (4),
bootstrap_connections_max (64),
callback_port (0),
lmdb_max_dbs (128),
block_processor_batch_max_time (std::chrono::milliseconds (5000))
{
	const char * epoch_message ("epoch v1 block");
	strncpy ((char *)epoch_block_link.bytes.data (), epoch_message, epoch_block_link.bytes.size ());
	epoch_block_signer = chratos::genesis_account;
	switch (chratos::chratos_network)
	{
		case chratos::chratos_networks::chratos_test_network:
			preconfigured_representatives.push_back (chratos::genesis_account);
			break;
		case chratos::chratos_networks::chratos_beta_network:
			preconfigured_representatives.push_back (chratos::genesis_account);
			preconfigured_peers.push_back ("chratos-beta.vidaru.org");
			break;
		case chratos::chratos_networks::chratos_live_network:
			preconfigured_representatives.push_back (chratos::genesis_account);
			preconfigured_peers.push_back ("chratos.seeds.vidaru.org");
			break;
		default:
			assert (false);
			break;
	}
}

void chratos::node_config::serialize_json (boost::property_tree::ptree & tree_a) const
{
	tree_a.put ("version", "15");
	tree_a.put ("peering_port", std::to_string (peering_port));
	tree_a.put ("bootstrap_fraction_numerator", std::to_string (bootstrap_fraction_numerator));
	tree_a.put ("receive_minimum", receive_minimum.to_string_dec ());
	boost::property_tree::ptree logging_l;
	logging.serialize_json (logging_l);
	tree_a.add_child ("logging", logging_l);
	boost::property_tree::ptree work_peers_l;
	for (auto i (work_peers.begin ()), n (work_peers.end ()); i != n; ++i)
	{
		boost::property_tree::ptree entry;
		entry.put ("", boost::str (boost::format ("%1%:%2%") % i->first % i->second));
		work_peers_l.push_back (std::make_pair ("", entry));
	}
	tree_a.add_child ("work_peers", work_peers_l);
	boost::property_tree::ptree preconfigured_peers_l;
	for (auto i (preconfigured_peers.begin ()), n (preconfigured_peers.end ()); i != n; ++i)
	{
		boost::property_tree::ptree entry;
		entry.put ("", *i);
		preconfigured_peers_l.push_back (std::make_pair ("", entry));
	}
	tree_a.add_child ("preconfigured_peers", preconfigured_peers_l);
	boost::property_tree::ptree preconfigured_representatives_l;
	for (auto i (preconfigured_representatives.begin ()), n (preconfigured_representatives.end ()); i != n; ++i)
	{
		boost::property_tree::ptree entry;
		entry.put ("", i->to_account ());
		preconfigured_representatives_l.push_back (std::make_pair ("", entry));
	}
	tree_a.add_child ("preconfigured_representatives", preconfigured_representatives_l);
	tree_a.put ("online_weight_minimum", online_weight_minimum.to_string_dec ());
	tree_a.put ("online_weight_quorum", std::to_string (online_weight_quorum));
	tree_a.put ("password_fanout", std::to_string (password_fanout));
	tree_a.put ("io_threads", std::to_string (io_threads));
	tree_a.put ("network_threads", std::to_string (network_threads));
	tree_a.put ("work_threads", std::to_string (work_threads));
	tree_a.put ("enable_voting", enable_voting);
	tree_a.put ("bootstrap_connections", bootstrap_connections);
	tree_a.put ("bootstrap_connections_max", bootstrap_connections_max);
	tree_a.put ("callback_address", callback_address);
	tree_a.put ("callback_port", std::to_string (callback_port));
	tree_a.put ("callback_target", callback_target);
	tree_a.put ("lmdb_max_dbs", lmdb_max_dbs);
	tree_a.put ("block_processor_batch_max_time", block_processor_batch_max_time.count ());
}

bool chratos::node_config::upgrade_json (unsigned version, boost::property_tree::ptree & tree_a)
{
	auto result (false);
	switch (version)
	{
		case 1:
		{
			auto reps_l (tree_a.get_child ("preconfigured_representatives"));
			boost::property_tree::ptree reps;
			for (auto i (reps_l.begin ()), n (reps_l.end ()); i != n; ++i)
			{
				chratos::uint256_union account;
				account.decode_account (i->second.get<std::string> (""));
				boost::property_tree::ptree entry;
				entry.put ("", account.to_account ());
				reps.push_back (std::make_pair ("", entry));
			}
			tree_a.erase ("preconfigured_representatives");
			tree_a.add_child ("preconfigured_representatives", reps);
			tree_a.erase ("version");
			tree_a.put ("version", "2");
			result = true;
		}
		case 2:
		{
			tree_a.put ("inactive_supply", chratos::uint128_union (0).to_string_dec ());
			tree_a.put ("password_fanout", std::to_string (1024));
			tree_a.put ("io_threads", std::to_string (io_threads));
			tree_a.put ("work_threads", std::to_string (work_threads));
			tree_a.erase ("version");
			tree_a.put ("version", "3");
			result = true;
		}
		case 3:
			tree_a.erase ("receive_minimum");
			tree_a.put ("receive_minimum", chratos::chr_ratio.convert_to<std::string> ());
			tree_a.erase ("version");
			tree_a.put ("version", "4");
			result = true;
		case 4:
			tree_a.erase ("receive_minimum");
			tree_a.put ("receive_minimum", chratos::chr_ratio.convert_to<std::string> ());
			tree_a.erase ("version");
			tree_a.put ("version", "5");
			result = true;
		case 5:
			tree_a.put ("enable_voting", enable_voting);
			tree_a.erase ("packet_delay_microseconds");
			tree_a.erase ("rebroadcast_delay");
			tree_a.erase ("creation_rebroadcast");
			tree_a.erase ("version");
			tree_a.put ("version", "6");
			result = true;
		case 6:
			tree_a.put ("bootstrap_connections", 16);
			tree_a.put ("callback_address", "");
			tree_a.put ("callback_port", "0");
			tree_a.put ("callback_target", "");
			tree_a.erase ("version");
			tree_a.put ("version", "7");
			result = true;
		case 7:
			tree_a.put ("lmdb_max_dbs", "128");
			tree_a.erase ("version");
			tree_a.put ("version", "8");
			result = true;
		case 8:
			tree_a.put ("bootstrap_connections_max", "64");
			tree_a.erase ("version");
			tree_a.put ("version", "9");
			result = true;
		case 9:
			tree_a.put ("state_block_parse_canary", chratos::block_hash (0).to_string ());
			tree_a.put ("state_block_generate_canary", chratos::block_hash (0).to_string ());
			tree_a.erase ("version");
			tree_a.put ("version", "10");
			result = true;
		case 10:
			tree_a.put ("online_weight_minimum", online_weight_minimum.to_string_dec ());
			tree_a.put ("online_weight_quorom", std::to_string (online_weight_quorum));
			tree_a.erase ("inactive_supply");
			tree_a.erase ("version");
			tree_a.put ("version", "11");
			result = true;
		case 11:
		{
			auto online_weight_quorum_l (tree_a.get<std::string> ("online_weight_quorom"));
			tree_a.erase ("online_weight_quorom");
			tree_a.put ("online_weight_quorum", online_weight_quorum_l);
			tree_a.erase ("version");
			tree_a.put ("version", "12");
			result = true;
		}
		case 12:
			tree_a.erase ("state_block_parse_canary");
			tree_a.erase ("state_block_generate_canary");
			tree_a.erase ("version");
			tree_a.put ("version", "13");
			result = true;
		case 13:
			tree_a.put ("generate_hash_votes_at", "0");
			tree_a.erase ("version");
			tree_a.put ("version", "14");
			result = true;
		case 14:
			tree_a.put ("network_threads", std::to_string (network_threads));
			tree_a.erase ("generate_hash_votes_at");
			tree_a.put ("block_processor_batch_max_time", block_processor_batch_max_time.count ());
			tree_a.erase ("version");
			tree_a.put ("version", "15");
			result = true;
		case 15:
			break;
		default:
			throw std::runtime_error ("Unknown node_config version");
	}
	return result;
}

bool chratos::node_config::deserialize_json (bool & upgraded_a, boost::property_tree::ptree & tree_a)
{
	auto result (false);
	try
	{
		auto version_l (tree_a.get_optional<std::string> ("version"));
		if (!version_l)
		{
			tree_a.put ("version", "1");
			version_l = "1";
			auto work_peers_l (tree_a.get_child_optional ("work_peers"));
			if (!work_peers_l)
			{
				tree_a.add_child ("work_peers", boost::property_tree::ptree ());
			}
			upgraded_a = true;
		}
		upgraded_a |= upgrade_json (std::stoull (version_l.get ()), tree_a);
		auto peering_port_l (tree_a.get<std::string> ("peering_port"));
		auto bootstrap_fraction_numerator_l (tree_a.get<std::string> ("bootstrap_fraction_numerator"));
		auto receive_minimum_l (tree_a.get<std::string> ("receive_minimum"));
		auto & logging_l (tree_a.get_child ("logging"));
		work_peers.clear ();
		auto work_peers_l (tree_a.get_child ("work_peers"));
		for (auto i (work_peers_l.begin ()), n (work_peers_l.end ()); i != n; ++i)
		{
			auto work_peer (i->second.get<std::string> (""));
			auto port_position (work_peer.rfind (':'));
			result |= port_position == -1;
			if (!result)
			{
				auto port_str (work_peer.substr (port_position + 1));
				uint16_t port;
				result |= parse_port (port_str, port);
				if (!result)
				{
					auto address (work_peer.substr (0, port_position));
					work_peers.push_back (std::make_pair (address, port));
				}
			}
		}
		auto preconfigured_peers_l (tree_a.get_child ("preconfigured_peers"));
		preconfigured_peers.clear ();
		for (auto i (preconfigured_peers_l.begin ()), n (preconfigured_peers_l.end ()); i != n; ++i)
		{
			auto bootstrap_peer (i->second.get<std::string> (""));
			preconfigured_peers.push_back (bootstrap_peer);
		}
		auto preconfigured_representatives_l (tree_a.get_child ("preconfigured_representatives"));
		preconfigured_representatives.clear ();
		for (auto i (preconfigured_representatives_l.begin ()), n (preconfigured_representatives_l.end ()); i != n; ++i)
		{
			chratos::account representative (0);
			result = result || representative.decode_account (i->second.get<std::string> (""));
			preconfigured_representatives.push_back (representative);
		}
		if (preconfigured_representatives.empty ())
		{
			result = true;
		}
		auto stat_config_l (tree_a.get_child_optional ("statistics"));
		if (stat_config_l)
		{
			result |= stat_config.deserialize_json (stat_config_l.get ());
		}
		auto online_weight_minimum_l (tree_a.get<std::string> ("online_weight_minimum"));
		auto online_weight_quorum_l (tree_a.get<std::string> ("online_weight_quorum"));
		auto password_fanout_l (tree_a.get<std::string> ("password_fanout"));
		auto io_threads_l (tree_a.get<std::string> ("io_threads"));
		auto work_threads_l (tree_a.get<std::string> ("work_threads"));
		enable_voting = tree_a.get<bool> ("enable_voting");
		auto bootstrap_connections_l (tree_a.get<std::string> ("bootstrap_connections"));
		auto bootstrap_connections_max_l (tree_a.get<std::string> ("bootstrap_connections_max"));
		callback_address = tree_a.get<std::string> ("callback_address");
		auto callback_port_l (tree_a.get<std::string> ("callback_port"));
		callback_target = tree_a.get<std::string> ("callback_target");
		auto lmdb_max_dbs_l = tree_a.get<std::string> ("lmdb_max_dbs");
		result |= parse_port (callback_port_l, callback_port);
		auto block_processor_batch_max_time_l = tree_a.get<std::string> ("block_processor_batch_max_time");
		try
		{
			peering_port = std::stoul (peering_port_l);
			bootstrap_fraction_numerator = std::stoul (bootstrap_fraction_numerator_l);
			password_fanout = std::stoul (password_fanout_l);
			io_threads = std::stoul (io_threads_l);
			network_threads = tree_a.get<unsigned> ("network_threads", network_threads);
			work_threads = std::stoul (work_threads_l);
			bootstrap_connections = std::stoul (bootstrap_connections_l);
			bootstrap_connections_max = std::stoul (bootstrap_connections_max_l);
			lmdb_max_dbs = std::stoi (lmdb_max_dbs_l);
			online_weight_quorum = std::stoul (online_weight_quorum_l);
			block_processor_batch_max_time = std::chrono::milliseconds (std::stoul (block_processor_batch_max_time_l));
			result |= peering_port > std::numeric_limits<uint16_t>::max ();
			result |= logging.deserialize_json (upgraded_a, logging_l);
			result |= receive_minimum.decode_dec (receive_minimum_l);
			result |= online_weight_minimum.decode_dec (online_weight_minimum_l);
			result |= online_weight_quorum > 100;
			result |= password_fanout < 16;
			result |= password_fanout > 1024 * 1024;
			result |= io_threads == 0;
		}
		catch (std::logic_error const &)
		{
			result = true;
		}
	}
	catch (std::runtime_error const &)
	{
		result = true;
	}
	return result;
}

chratos::account chratos::node_config::random_representative ()
{
	assert (preconfigured_representatives.size () > 0);
	size_t index (chratos::random_pool.GenerateWord32 (0, preconfigured_representatives.size () - 1));
	auto result (preconfigured_representatives[index]);
	return result;
}
