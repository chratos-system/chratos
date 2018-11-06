#include <chratos/node/node.hpp>

#include <chratos/lib/interface.h>
#include <chratos/lib/utility.hpp>
#include <chratos/node/common.hpp>
#include <chratos/node/rpc.hpp>

#include <algorithm>
#include <cstdlib>
#include <future>
#include <sstream>

#include <boost/polymorphic_cast.hpp>
#include <boost/property_tree/json_parser.hpp>

double constexpr chratos::node::price_max;
double constexpr chratos::node::free_cutoff;
std::chrono::seconds constexpr chratos::node::period;
std::chrono::seconds constexpr chratos::node::cutoff;
std::chrono::seconds constexpr chratos::node::syn_cookie_cutoff;
std::chrono::minutes constexpr chratos::node::backup_interval;
std::chrono::seconds constexpr chratos::node::search_pending_interval;
int constexpr chratos::port_mapping::mapping_timeout;
int constexpr chratos::port_mapping::check_timeout;
unsigned constexpr chratos::active_transactions::announce_interval_ms;
size_t constexpr chratos::block_arrival::arrival_size_min;
std::chrono::seconds constexpr chratos::block_arrival::arrival_time_min;

namespace chratos
{
extern unsigned char chratos_bootstrap_weights[];
extern size_t chratos_bootstrap_weights_size;
}

chratos::network::network (chratos::node & node_a, uint16_t port) :
buffer_container (node_a.stats, chratos::network::buffer_size, 4096), // 2Mb receive buffer
socket (node_a.service, chratos::endpoint (boost::asio::ip::address_v6::any (), port)),
resolver (node_a.service),
node (node_a),
on (true)
{
	boost::thread::attributes attrs;
	chratos::thread_attributes::set (attrs);
	for (size_t i = 0; i < node.config.network_threads; ++i)
	{
		packet_processing_threads.push_back (boost::thread (attrs, [this]() {
			chratos::thread_role::set (chratos::thread_role::name::packet_processing);
			try
			{
				process_packets ();
			}
			catch (boost::system::error_code & ec)
			{
				BOOST_LOG (this->node.log) << FATAL_LOG_PREFIX << ec.message ();
				release_assert (false);
			}
			catch (std::error_code & ec)
			{
				BOOST_LOG (this->node.log) << FATAL_LOG_PREFIX << ec.message ();
				release_assert (false);
			}
			catch (std::runtime_error & err)
			{
				BOOST_LOG (this->node.log) << FATAL_LOG_PREFIX << err.what ();
				release_assert (false);
			}
			catch (...)
			{
				BOOST_LOG (this->node.log) << FATAL_LOG_PREFIX << "Unknown exception";
				release_assert (false);
			}
			if (this->node.config.logging.network_packet_logging ())
			{
				BOOST_LOG (this->node.log) << "Exiting packet processing thread";
			}
		}));
	}
}

chratos::network::~network ()
{
	for (auto & thread : packet_processing_threads)
	{
		thread.join ();
	}
}

void chratos::network::start ()
{
	for (size_t i = 0; i < node.config.io_threads; ++i)
	{
		receive ();
	}
}

void chratos::network::receive ()
{
	if (node.config.logging.network_packet_logging ())
	{
		BOOST_LOG (node.log) << "Receiving packet";
	}
	std::unique_lock<std::mutex> lock (socket_mutex);
	auto data (buffer_container.allocate ());
	socket.async_receive_from (boost::asio::buffer (data->buffer, chratos::network::buffer_size), data->endpoint, [this, data](boost::system::error_code const & error, size_t size_a) {
		if (!error && this->on)
		{
			data->size = size_a;
			this->buffer_container.enqueue (data);
			this->receive ();
		}
		else
		{
			this->buffer_container.release (data);
			if (error)
			{
				if (this->node.config.logging.network_logging ())
				{
					BOOST_LOG (this->node.log) << boost::str (boost::format ("UDP Receive error: %1%") % error.message ());
				}
			}
			if (this->on)
			{
				this->node.alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [this]() { this->receive (); });
			}
		}
	});
}

void chratos::network::process_packets ()
{
	while (on)
	{
		auto data (buffer_container.dequeue ());
		if (data == nullptr)
		{
			break;
		}
		//std::cerr << data->endpoint.address ().to_string ();
		receive_action (data);
		buffer_container.release (data);
	}
}

void chratos::network::stop ()
{
	on = false;
	socket.close ();
	resolver.cancel ();
	buffer_container.stop ();
}

void chratos::network::send_keepalive (chratos::endpoint const & endpoint_a)
{
	assert (endpoint_a.address ().is_v6 ());
	chratos::keepalive message;
	node.peers.random_fill (message.peers);
	std::shared_ptr<std::vector<uint8_t>> bytes (new std::vector<uint8_t>);
	{
		chratos::vectorstream stream (*bytes);
		message.serialize (stream);
	}
	if (node.config.logging.network_keepalive_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Keepalive req sent to %1%") % endpoint_a);
	}
	std::weak_ptr<chratos::node> node_w (node.shared ());
	send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, node_w, endpoint_a](boost::system::error_code const & ec, size_t) {
		if (auto node_l = node_w.lock ())
		{
			if (ec && node_l->config.logging.network_keepalive_logging ())
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending keepalive to %1%: %2%") % endpoint_a % ec.message ());
			}
			else
			{
				node_l->stats.inc (chratos::stat::type::message, chratos::stat::detail::keepalive, chratos::stat::dir::out);
			}
		}
	});
}

void chratos::node::keepalive (std::string const & address_a, uint16_t port_a)
{
	auto node_l (shared_from_this ());
	network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (address_a, std::to_string (port_a)), [node_l, address_a, port_a](boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a) {
		if (!ec)
		{
			for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator {}); i != n; ++i)
			{
				node_l->send_keepalive (chratos::map_endpoint_to_v6 (i->endpoint ()));
			}
		}
		else
		{
			BOOST_LOG (node_l->log) << boost::str (boost::format ("Error resolving address: %1%:%2%: %3%") % address_a % port_a % ec.message ());
		}
	});
}

void chratos::network::send_node_id_handshake (chratos::endpoint const & endpoint_a, boost::optional<chratos::uint256_union> const & query, boost::optional<chratos::uint256_union> const & respond_to)
{
	assert (endpoint_a.address ().is_v6 ());
	boost::optional<std::pair<chratos::account, chratos::signature>> response (boost::none);
	if (respond_to)
	{
		response = std::make_pair (node.node_id.pub, chratos::sign_message (node.node_id.prv, node.node_id.pub, *respond_to));
		assert (!chratos::validate_message (response->first, *respond_to, response->second));
	}
	chratos::node_id_handshake message (query, response);
	std::shared_ptr<std::vector<uint8_t>> bytes (new std::vector<uint8_t>);
	{
		chratos::vectorstream stream (*bytes);
		message.serialize (stream);
	}
	if (node.config.logging.network_node_id_handshake_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Node ID handshake sent with node ID %1% to %2%: query %3%, respond_to %4% (signature %5%)") % node.node_id.pub.to_account () % endpoint_a % (query ? query->to_string () : std::string ("[none]")) % (respond_to ? respond_to->to_string () : std::string ("[none]")) % (response ? response->second.to_string () : std::string ("[none]")));
	}
	node.stats.inc (chratos::stat::type::message, chratos::stat::detail::node_id_handshake, chratos::stat::dir::out);
	std::weak_ptr<chratos::node> node_w (node.shared ());
	send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, node_w, endpoint_a](boost::system::error_code const & ec, size_t) {
		if (auto node_l = node_w.lock ())
		{
			if (ec && node_l->config.logging.network_node_id_handshake_logging ())
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending node ID handshake to %1% %2%") % endpoint_a % ec.message ());
			}
		}
	});
}

void chratos::network::republish (chratos::block_hash const & hash_a, std::shared_ptr<std::vector<uint8_t>> buffer_a, chratos::endpoint endpoint_a)
{
	if (node.config.logging.network_publish_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Publishing %1% to %2%") % hash_a.to_string () % endpoint_a);
	}
	std::weak_ptr<chratos::node> node_w (node.shared ());
	send_buffer (buffer_a->data (), buffer_a->size (), endpoint_a, [buffer_a, node_w, endpoint_a](boost::system::error_code const & ec, size_t size) {
		if (auto node_l = node_w.lock ())
		{
			if (ec && node_l->config.logging.network_logging ())
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending publish to %1%: %2%") % endpoint_a % ec.message ());
			}
			else
			{
				node_l->stats.inc (chratos::stat::type::message, chratos::stat::detail::publish, chratos::stat::dir::out);
			}
		}
	});
}

template <typename T>
bool confirm_block (chratos::transaction const & transaction_a, chratos::node & node_a, T & list_a, std::shared_ptr<chratos::block> block_a)
{
	bool result (false);
	if (node_a.config.enable_voting)
	{
		node_a.wallets.foreach_representative (transaction_a, [&result, &block_a, &list_a, &node_a, &transaction_a](chratos::public_key const & pub_a, chratos::raw_key const & prv_a) {
			result = true;
			auto vote (node_a.store.vote_generate (transaction_a, pub_a, prv_a, block_a));
			chratos::confirm_ack confirm (vote);
			std::shared_ptr<std::vector<uint8_t>> bytes (new std::vector<uint8_t>);
			{
				chratos::vectorstream stream (*bytes);
				confirm.serialize (stream);
			}
			for (auto j (list_a.begin ()), m (list_a.end ()); j != m; ++j)
			{
				node_a.network.confirm_send (confirm, bytes, *j);
			}
		});
	}
	return result;
}

template <>
bool confirm_block (chratos::transaction const & transaction_a, chratos::node & node_a, chratos::endpoint & peer_a, std::shared_ptr<chratos::block> block_a)
{
	std::array<chratos::endpoint, 1> endpoints;
	endpoints[0] = peer_a;
	auto result (confirm_block (transaction_a, node_a, endpoints, std::move (block_a)));
	return result;
}

void chratos::network::republish_block (std::shared_ptr<chratos::block> block)
{
	auto hash (block->hash ());
	auto list (node.peers.list_fanout ());
	chratos::publish message (block);
	std::shared_ptr<std::vector<uint8_t>> bytes (new std::vector<uint8_t>);
	{
		chratos::vectorstream stream (*bytes);
		message.serialize (stream);
	}
	for (auto i (list.begin ()), n (list.end ()); i != n; ++i)
	{
		republish (hash, bytes, *i);
	}
	if (node.config.logging.network_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Block %1% was republished to peers") % hash.to_string ());
	}
}

void chratos::network::republish_block_batch (std::deque<std::shared_ptr<chratos::block>> blocks_a, unsigned delay_a)
{
	auto block (blocks_a.front ());
	blocks_a.pop_front ();
	republish_block (block);
	if (!blocks_a.empty ())
	{
		std::weak_ptr<chratos::node> node_w (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (delay_a), [node_w, blocks_a, delay_a]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.republish_block_batch (blocks_a, delay_a);
			}
		});
	}
}

// In order to rate limit network traffic we republish:
// 1) Only if they are a non-replay vote of a block that's actively settling. Settling blocks are limited by block PoW
// 2) The rep has a weight > Y to prevent creating a lot of small-weight accounts to send out votes
// 3) Only if a vote for this block from this representative hasn't been received in the previous X second.
//    This prevents rapid publishing of votes with increasing sequence numbers.
//
// These rules are implemented by the caller, not this function.
void chratos::network::republish_vote (std::shared_ptr<chratos::vote> vote_a)
{
	chratos::confirm_ack confirm (vote_a);
	std::shared_ptr<std::vector<uint8_t>> bytes (new std::vector<uint8_t>);
	{
		chratos::vectorstream stream (*bytes);
		confirm.serialize (stream);
	}
	auto list (node.peers.list_fanout ());
	for (auto j (list.begin ()), m (list.end ()); j != m; ++j)
	{
		node.network.confirm_send (confirm, bytes, *j);
	}
}

void chratos::network::broadcast_confirm_req (std::shared_ptr<chratos::block> block_a)
{
	auto list (std::make_shared<std::vector<chratos::peer_information>> (node.peers.representatives (std::numeric_limits<size_t>::max ())));
	if (list->empty () || node.peers.total_weight () < node.config.online_weight_minimum.number ())
	{
		// broadcast request to all peers
		list = std::make_shared<std::vector<chratos::peer_information>> (node.peers.list_vector ());
	}

	/*
	 * In either case (broadcasting to all representatives, or broadcasting to
	 * all peers because there are not enough connected representatives),
	 * limit each instance to a single random up-to-32 selection.  The invoker
	 * of "broadcast_confirm_req" will be responsible for calling it again
	 * if the votes for a block have not arrived in time.
	 */
	const size_t max_endpoints = 32;
	std::random_shuffle (list->begin (), list->end ());
	if (list->size () > max_endpoints)
	{
		list->erase (list->begin () + max_endpoints, list->end ());
	}

	broadcast_confirm_req_base (block_a, list, 0);
}

void chratos::network::broadcast_confirm_req_base (std::shared_ptr<chratos::block> block_a, std::shared_ptr<std::vector<chratos::peer_information>> endpoints_a, unsigned delay_a, bool resumption)
{
	const size_t max_reps = 10;
	if (!resumption && node.config.logging.network_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Broadcasting confirm req for block %1% to %2% representatives") % block_a->hash ().to_string () % endpoints_a->size ());
	}
	auto count (0);
	while (!endpoints_a->empty () && count < max_reps)
	{
		send_confirm_req (endpoints_a->back ().endpoint, block_a);
		endpoints_a->pop_back ();
		count++;
	}
	if (!endpoints_a->empty ())
	{
		delay_a += 50;
		delay_a += std::rand () % 50;

		std::weak_ptr<chratos::node> node_w (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (delay_a), [node_w, block_a, endpoints_a, delay_a]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.broadcast_confirm_req_base (block_a, endpoints_a, delay_a, true);
			}
		});
	}
}

void chratos::network::send_confirm_req (chratos::endpoint const & endpoint_a, std::shared_ptr<chratos::block> block)
{
	chratos::confirm_req message (block);
	std::shared_ptr<std::vector<uint8_t>> bytes (new std::vector<uint8_t>);
	{
		chratos::vectorstream stream (*bytes);
		message.serialize (stream);
	}
	if (node.config.logging.network_message_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Sending confirm req to %1%") % endpoint_a);
	}
	std::weak_ptr<chratos::node> node_w (node.shared ());
	node.stats.inc (chratos::stat::type::message, chratos::stat::detail::confirm_req, chratos::stat::dir::out);
	send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, node_w](boost::system::error_code const & ec, size_t size) {
		if (auto node_l = node_w.lock ())
		{
			if (ec && node_l->config.logging.network_logging ())
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending confirm request: %1%") % ec.message ());
			}
		}
	});
}

template <typename T>
void rep_query (chratos::node & node_a, T const & peers_a)
{
	auto transaction (node_a.store.tx_begin_read ());
	std::shared_ptr<chratos::block> block (node_a.store.block_random (transaction));
	auto hash (block->hash ());
	node_a.rep_crawler.add (hash);
	for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
	{
		node_a.peers.rep_request (*i);
		node_a.network.send_confirm_req (*i, block);
	}
	std::weak_ptr<chratos::node> node_w (node_a.shared ());
	node_a.alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w, hash]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->rep_crawler.remove (hash);
		}
	});
}

template <>
void rep_query (chratos::node & node_a, chratos::endpoint const & peers_a)
{
	std::array<chratos::endpoint, 1> peers;
	peers[0] = peers_a;
	rep_query (node_a, peers);
}

namespace
{
class network_message_visitor : public chratos::message_visitor
{
public:
	network_message_visitor (chratos::node & node_a, chratos::endpoint const & sender_a) :
	node (node_a),
	sender (sender_a)
	{
	}
	virtual ~network_message_visitor () = default;
	void keepalive (chratos::keepalive const & message_a) override
	{
		if (node.config.logging.network_keepalive_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Received keepalive message from %1%") % sender);
		}
		node.stats.inc (chratos::stat::type::message, chratos::stat::detail::keepalive, chratos::stat::dir::in);
		if (node.peers.contacted (sender, message_a.header.version_using))
		{
			auto endpoint_l (chratos::map_endpoint_to_v6 (sender));
			auto cookie (node.peers.assign_syn_cookie (endpoint_l));
			if (cookie)
			{
				node.network.send_node_id_handshake (endpoint_l, *cookie, boost::none);
			}
		}
		node.network.merge_peers (message_a.peers);
	}
	void publish (chratos::publish const & message_a) override
	{
		if (node.config.logging.network_message_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Publish message from %1% for %2%") % sender % message_a.block->hash ().to_string ());
		}
		node.stats.inc (chratos::stat::type::message, chratos::stat::detail::publish, chratos::stat::dir::in);
		node.peers.contacted (sender, message_a.header.version_using);
		node.process_active (message_a.block);
		node.active.publish (message_a.block);
	}
	void confirm_req (chratos::confirm_req const & message_a) override
	{
		if (node.config.logging.network_message_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Confirm_req message from %1% for %2%") % sender % message_a.block->hash ().to_string ());
		}
		node.stats.inc (chratos::stat::type::message, chratos::stat::detail::confirm_req, chratos::stat::dir::in);
		node.peers.contacted (sender, message_a.header.version_using);
		// Don't load nodes with disabled voting
		if (node.config.enable_voting)
		{
			auto transaction (node.store.tx_begin_read ());
			auto successor (node.ledger.successor (transaction, message_a.block->root ()));
			if (successor != nullptr)
			{
				confirm_block (transaction, node, sender, std::move (successor));
			}
		}
	}
	void confirm_ack (chratos::confirm_ack const & message_a) override
	{
		if (node.config.logging.network_message_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Received confirm_ack message from %1% for %2%sequence %3%") % sender % message_a.vote->hashes_string () % std::to_string (message_a.vote->sequence));
		}
		node.stats.inc (chratos::stat::type::message, chratos::stat::detail::confirm_ack, chratos::stat::dir::in);
		node.peers.contacted (sender, message_a.header.version_using);
		for (auto & vote_block : message_a.vote->blocks)
		{
			if (!vote_block.which ())
			{
				auto block (boost::get<std::shared_ptr<chratos::block>> (vote_block));
				node.process_active (block);
				node.active.publish (block);
			}
		}
		node.vote_processor.vote (message_a.vote, sender);
	}
	void bulk_pull (chratos::bulk_pull const &) override
	{
		assert (false);
	}
	void bulk_pull_account (chratos::bulk_pull_account const &) override
	{
		assert (false);
	}
	void bulk_pull_blocks (chratos::bulk_pull_blocks const &) override
	{
		assert (false);
	}
	void bulk_push (chratos::bulk_push const &) override
	{
		assert (false);
	}
	void frontier_req (chratos::frontier_req const &) override
	{
		assert (false);
	}
	void node_id_handshake (chratos::node_id_handshake const & message_a) override
	{
		if (node.config.logging.network_node_id_handshake_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Received node_id_handshake message from %1% with query %2% and response account %3%") % sender % (message_a.query ? message_a.query->to_string () : std::string ("[none]")) % (message_a.response ? message_a.response->first.to_account () : std::string ("[none]")));
		}
		node.stats.inc (chratos::stat::type::message, chratos::stat::detail::node_id_handshake, chratos::stat::dir::in);
		auto endpoint_l (chratos::map_endpoint_to_v6 (sender));
		boost::optional<chratos::uint256_union> out_query;
		boost::optional<chratos::uint256_union> out_respond_to;
		if (message_a.query)
		{
			out_respond_to = message_a.query;
		}
		auto validated_response (false);
		if (message_a.response)
		{
			if (!node.peers.validate_syn_cookie (endpoint_l, message_a.response->first, message_a.response->second))
			{
				validated_response = true;
				if (message_a.response->first != node.node_id.pub)
				{
					node.peers.insert (endpoint_l, message_a.header.version_using);
				}
			}
			else if (node.config.logging.network_node_id_handshake_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Failed to validate syn cookie signature %1% by %2%") % message_a.response->second.to_string () % message_a.response->first.to_account ());
			}
		}
		if (!validated_response && !node.peers.known_peer (endpoint_l))
		{
			out_query = node.peers.assign_syn_cookie (endpoint_l);
		}
		if (out_query || out_respond_to)
		{
			node.network.send_node_id_handshake (sender, out_query, out_respond_to);
		}
	}
	chratos::node & node;
	chratos::endpoint sender;
};
}

void chratos::network::receive_action (chratos::udp_data * data_a)
{
	if (!chratos::reserved_address (data_a->endpoint, false) && data_a->endpoint != endpoint ())
	{
		network_message_visitor visitor (node, data_a->endpoint);
		chratos::message_parser parser (visitor, node.work);
		parser.deserialize_buffer (data_a->buffer, data_a->size);
		if (parser.status != chratos::message_parser::parse_status::success)
		{
			node.stats.inc (chratos::stat::type::error);

			switch (parser.status)
			{
				case chratos::message_parser::parse_status::insufficient_work:
					// We've already increment error count, update detail only
					node.stats.inc_detail_only (chratos::stat::type::error, chratos::stat::detail::insufficient_work);
					break;
				case chratos::message_parser::parse_status::invalid_magic:
					node.stats.inc (chratos::stat::type::udp, chratos::stat::detail::invalid_magic);
					break;
				case chratos::message_parser::parse_status::invalid_network:
					node.stats.inc (chratos::stat::type::udp, chratos::stat::detail::invalid_network);
					break;
				case chratos::message_parser::parse_status::invalid_header:
					node.stats.inc (chratos::stat::type::udp, chratos::stat::detail::invalid_header);
					break;
				case chratos::message_parser::parse_status::invalid_message_type:
					node.stats.inc (chratos::stat::type::udp, chratos::stat::detail::invalid_message_type);
					break;
				case chratos::message_parser::parse_status::invalid_keepalive_message:
					node.stats.inc (chratos::stat::type::udp, chratos::stat::detail::invalid_keepalive_message);
					break;
				case chratos::message_parser::parse_status::invalid_publish_message:
					node.stats.inc (chratos::stat::type::udp, chratos::stat::detail::invalid_publish_message);
					break;
				case chratos::message_parser::parse_status::invalid_confirm_req_message:
					node.stats.inc (chratos::stat::type::udp, chratos::stat::detail::invalid_confirm_req_message);
					break;
				case chratos::message_parser::parse_status::invalid_confirm_ack_message:
					node.stats.inc (chratos::stat::type::udp, chratos::stat::detail::invalid_confirm_ack_message);
					break;
				case chratos::message_parser::parse_status::invalid_node_id_handshake_message:
					node.stats.inc (chratos::stat::type::udp, chratos::stat::detail::invalid_node_id_handshake_message);
					break;
				case chratos::message_parser::parse_status::outdated_version:
					node.stats.inc (chratos::stat::type::udp, chratos::stat::detail::outdated_version);
					break;
				case chratos::message_parser::parse_status::success:
					/* Already checked, unreachable */
					break;
			}

			if (node.config.logging.network_logging ())
			{
				BOOST_LOG (node.log) << "Could not parse message.  Error: " << parser.status_string ();
			}
		}
		else
		{
			node.stats.add (chratos::stat::type::traffic, chratos::stat::dir::in, data_a->size);
		}
	}
	else
	{
		if (node.config.logging.network_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Reserved sender %1%") % data_a->endpoint.address ().to_string ());
		}

		node.stats.inc_detail_only (chratos::stat::type::error, chratos::stat::detail::bad_sender);
	}
}

// Send keepalives to all the peers we've been notified of
void chratos::network::merge_peers (std::array<chratos::endpoint, 8> const & peers_a)
{
	for (auto i (peers_a.begin ()), j (peers_a.end ()); i != j; ++i)
	{
		if (!node.peers.reachout (*i))
		{
			send_keepalive (*i);
		}
	}
}

bool chratos::operation::operator> (chratos::operation const & other_a) const
{
	return wakeup > other_a.wakeup;
}

chratos::alarm::alarm (boost::asio::io_service & service_a) :
service (service_a),
thread ([this]() {
	chratos::thread_role::set (chratos::thread_role::name::alarm);
	run ();
})
{
}

chratos::alarm::~alarm ()
{
	add (std::chrono::steady_clock::now (), nullptr);
	thread.join ();
}

void chratos::alarm::run ()
{
	std::unique_lock<std::mutex> lock (mutex);
	auto done (false);
	while (!done)
	{
		if (!operations.empty ())
		{
			auto & operation (operations.top ());
			if (operation.function)
			{
				if (operation.wakeup <= std::chrono::steady_clock::now ())
				{
					service.post (operation.function);
					operations.pop ();
				}
				else
				{
					auto wakeup (operation.wakeup);
					condition.wait_until (lock, wakeup);
				}
			}
			else
			{
				done = true;
			}
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void chratos::alarm::add (std::chrono::steady_clock::time_point const & wakeup_a, std::function<void()> const & operation)
{
	std::lock_guard<std::mutex> lock (mutex);
	operations.push (chratos::operation ({ wakeup_a, operation }));
	condition.notify_all ();
}

chratos::node_init::node_init () :
block_store_init (false),
wallet_init (false)
{
}

bool chratos::node_init::error ()
{
	return block_store_init || wallet_init;
}

chratos::vote_processor::vote_processor (chratos::node & node_a) :
node (node_a),
started (false),
stopped (false),
active (false),
thread ([this]() {
	chratos::thread_role::set (chratos::thread_role::name::vote_processing);
	process_loop ();
})
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!started)
	{
		condition.wait (lock);
	}
}

void chratos::vote_processor::process_loop ()
{
	std::unique_lock<std::mutex> lock (mutex);
	started = true;
	condition.notify_all ();
	while (!stopped)
	{
		if (!votes.empty ())
		{
			std::deque<std::pair<std::shared_ptr<chratos::vote>, chratos::endpoint>> votes_l;
			votes_l.swap (votes);
			active = true;
			lock.unlock ();
			{
				auto transaction (node.store.tx_begin_read ());
				for (auto & i : votes_l)
				{
					vote_blocking (transaction, i.first, i.second);
				}
			}
			lock.lock ();
			active = false;
			condition.notify_all ();
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void chratos::vote_processor::vote (std::shared_ptr<chratos::vote> vote_a, chratos::endpoint endpoint_a)
{
	assert (endpoint_a.address ().is_v6 ());
	std::lock_guard<std::mutex> lock (mutex);
	if (!stopped)
	{
		votes.push_back (std::make_pair (vote_a, endpoint_a));
		condition.notify_all ();
	}
}

chratos::vote_code chratos::vote_processor::vote_blocking (chratos::transaction const & transaction_a, std::shared_ptr<chratos::vote> vote_a, chratos::endpoint endpoint_a)
{
	assert (endpoint_a.address ().is_v6 ());
	auto result (chratos::vote_code::invalid);
	if (!vote_a->validate ())
	{
		auto max_vote (node.store.vote_max (transaction_a, vote_a));
		result = chratos::vote_code::replay;
		if (!node.active.vote (vote_a))
		{
			result = chratos::vote_code::vote;
		}
		switch (result)
		{
			case chratos::vote_code::vote:
				node.observers.vote.notify (transaction_a, vote_a, endpoint_a);
			case chratos::vote_code::replay:
				// This tries to assist rep nodes that have lost track of their highest sequence number by replaying our highest known vote back to them
				// Only do this if the sequence number is significantly different to account for network reordering
				// Amplify attack considerations: We're sending out a confirm_ack in response to a confirm_ack for no net traffic increase
				if (max_vote->sequence > vote_a->sequence + 10000)
				{
					chratos::confirm_ack confirm (max_vote);
					std::shared_ptr<std::vector<uint8_t>> bytes (new std::vector<uint8_t>);
					{
						chratos::vectorstream stream (*bytes);
						confirm.serialize (stream);
					}
					node.network.confirm_send (confirm, bytes, endpoint_a);
				}
				break;
			case chratos::vote_code::invalid:
				assert (false);
				break;
		}
	}
	std::string status;
	switch (result)
	{
		case chratos::vote_code::invalid:
			status = "Invalid";
			node.stats.inc (chratos::stat::type::vote, chratos::stat::detail::vote_invalid);
			break;
		case chratos::vote_code::replay:
			status = "Replay";
			node.stats.inc (chratos::stat::type::vote, chratos::stat::detail::vote_replay);
			break;
		case chratos::vote_code::vote:
			status = "Vote";
			node.stats.inc (chratos::stat::type::vote, chratos::stat::detail::vote_valid);
			break;
	}
	if (node.config.logging.vote_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Vote from: %1% sequence: %2% block(s): %3%status: %4%") % vote_a->account.to_account () % std::to_string (vote_a->sequence) % vote_a->hashes_string () % status);
	}
	return result;
}

void chratos::vote_processor::stop ()
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		stopped = true;
		condition.notify_all ();
	}
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void chratos::vote_processor::flush ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (active || !votes.empty ())
	{
		condition.wait (lock);
	}
}

void chratos::rep_crawler::add (chratos::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	active.insert (hash_a);
}

void chratos::rep_crawler::remove (chratos::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	active.erase (hash_a);
}

bool chratos::rep_crawler::exists (chratos::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	return active.count (hash_a) != 0;
}

chratos::block_processor::block_processor (chratos::node & node_a) :
stopped (false),
active (false),
next_log (std::chrono::steady_clock::now ()),
node (node_a),
generator (node_a, chratos::chratos_network == chratos::chratos_networks::chratos_test_network ? std::chrono::milliseconds (10) : std::chrono::milliseconds (500))
{
}

chratos::block_processor::~block_processor ()
{
	stop ();
}

void chratos::block_processor::stop ()
{
	generator.stop ();
	std::lock_guard<std::mutex> lock (mutex);
	stopped = true;
	condition.notify_all ();
}

void chratos::block_processor::flush ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped && (have_blocks () || active))
	{
		condition.wait (lock);
	}
}

bool chratos::block_processor::full ()
{
	std::unique_lock<std::mutex> lock (mutex);
	return blocks.size () > 16384;
}

void chratos::block_processor::add (std::shared_ptr<chratos::block> block_a, std::chrono::steady_clock::time_point origination)
{
	if (!chratos::work_validate (block_a->root (), block_a->block_work ()))
	{
		std::lock_guard<std::mutex> lock (mutex);
		if (blocks_hashes.find (block_a->hash ()) == blocks_hashes.end ())
		{
			if (block_a->type () == chratos::block_type::state && !node.ledger.is_epoch_link (block_a->link ()))
			{
				state_blocks.push_back (std::make_pair (block_a, origination));
			}
			else
			{
				blocks.push_back (std::make_pair (block_a, origination));
			}
			condition.notify_all ();
		}
	}
	else
	{
		BOOST_LOG (node.log) << "chratos::block_processor::add called for hash " << block_a->hash ().to_string () << " with invalid work " << chratos::to_string_hex (block_a->block_work ());
		assert (false && "chratos::block_processor::add called with invalid work");
	}
}

void chratos::block_processor::force (std::shared_ptr<chratos::block> block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	forced.push_back (block_a);
	condition.notify_all ();
}

void chratos::block_processor::process_blocks ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped)
	{
		if (have_blocks ())
		{
			active = true;
			lock.unlock ();
			process_receive_many (lock);
			lock.lock ();
			active = false;
		}
		else
		{
			condition.notify_all ();
			condition.wait (lock);
		}
	}
}

bool chratos::block_processor::should_log ()
{
	auto result (false);
	auto now (std::chrono::steady_clock::now ());
	if (next_log < now)
	{
		next_log = now + std::chrono::seconds (15);
		result = true;
	}
	return result;
}

bool chratos::block_processor::have_blocks ()
{
	assert (!mutex.try_lock ());
	return !blocks.empty () || !forced.empty () || !state_blocks.empty ();
}

void chratos::block_processor::verify_state_blocks (std::unique_lock<std::mutex> & lock_a)
{
	lock_a.lock ();
	std::deque<std::pair<std::shared_ptr<chratos::block>, std::chrono::steady_clock::time_point>> items;
	items.swap (state_blocks);
	lock_a.unlock ();
	auto size (items.size ());
	std::vector<chratos::uint256_union> hashes;
	hashes.reserve (size);
	std::vector<unsigned char const *> messages;
	messages.reserve (size);
	std::vector<size_t> lengths;
	lengths.reserve (size);
	std::vector<unsigned char const *> pub_keys;
	pub_keys.reserve (size);
	std::vector<unsigned char const *> signatures;
	signatures.reserve (size);
	std::vector<int> verifications;
	verifications.resize (size);
	for (auto i (0); i < size; ++i)
	{
		auto & block (static_cast<chratos::state_block &> (*items[i].first));
		hashes.push_back (block.hash ());
		messages.push_back (hashes.back ().bytes.data ());
		lengths.push_back (sizeof (decltype (hashes)::value_type));
		pub_keys.push_back (block.hashables.account.bytes.data ());
		signatures.push_back (block.signature.bytes.data ());
	}
	/* Verifications is vector if signatures check results
	validate_message_batch returing "true" if there are at least 1 invalid signature */
	auto code (chratos::validate_message_batch (messages.data (), lengths.data (), pub_keys.data (), signatures.data (), size, verifications.data ()));
	(void)code;
	lock_a.lock ();
	for (auto i (0); i < size; ++i)
	{
		assert (verifications[i] == 1 || verifications[i] == 0);
		if (verifications[i] == 1)
		{
			blocks.push_back (items.front ());
		}
		items.pop_front ();
	}
	lock_a.unlock ();
}

void chratos::block_processor::process_receive_many (std::unique_lock<std::mutex> & lock_a)
{
	verify_state_blocks (lock_a);
	auto transaction (node.store.tx_begin_write ());
	auto start_time (std::chrono::steady_clock::now ());
	lock_a.lock ();
	// Processing blocks
	while ((!blocks.empty () || !forced.empty ()) && std::chrono::steady_clock::now () - start_time < node.config.block_processor_batch_max_time)
	{
		if (blocks.size () > 64 && should_log ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("%1% blocks in processing queue") % blocks.size ());
		}
		std::pair<std::shared_ptr<chratos::block>, std::chrono::steady_clock::time_point> block;
		bool force (false);
		if (forced.empty ())
		{
			block = blocks.front ();
			blocks.pop_front ();
			blocks_hashes.erase (block.first->hash ());
		}
		else
		{
			block = std::make_pair (forced.front (), std::chrono::steady_clock::now ());
			forced.pop_front ();
			force = true;
		}
		lock_a.unlock ();
		auto hash (block.first->hash ());
		if (force)
		{
			auto successor (node.ledger.successor (transaction, block.first->root ()));
			if (successor != nullptr && successor->hash () != hash)
			{
				// Replace our block with the winner and roll back any dependent blocks
				BOOST_LOG (node.log) << boost::str (boost::format ("Rolling back %1% and replacing with %2%") % successor->hash ().to_string () % hash.to_string ());
				node.ledger.rollback (transaction, successor->hash ());
			}
		}
		/* Forced state blocks are not validated in verify_state_blocks () function
		Because of that we should set set validated_state_block as "false" for forced state blocks (!force) */
		bool validated_state_block (!force && block.first->type () == chratos::block_type::state);
		auto process_result (process_receive_one (transaction, block.first, block.second, validated_state_block));
		(void)process_result;
		lock_a.lock ();
	}
	lock_a.unlock ();
}

chratos::process_return chratos::block_processor::process_receive_one (chratos::transaction const & transaction_a, std::shared_ptr<chratos::block> block_a, std::chrono::steady_clock::time_point origination, bool validated_state_block)
{
	chratos::process_return result;
	auto hash (block_a->hash ());
	result = node.ledger.process (transaction_a, *block_a, validated_state_block);
	switch (result.code)
	{
		case chratos::process_result::progress:
		{
			if (node.config.logging.ledger_logging ())
			{
				std::string block;
				block_a->serialize_json (block);
				BOOST_LOG (node.log) << boost::str (boost::format ("Processing block %1%: %2%") % hash.to_string () % block);
			}
			if (node.block_arrival.recent (hash))
			{
				node.active.start (block_a);
				if (node.config.enable_voting)
				{
					generator.add (hash);
				}
			}
			queue_unchecked (transaction_a, hash);
			break;
		}
		case chratos::process_result::gap_previous:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Gap previous for: %1%") % hash.to_string ());
			}
			node.store.unchecked_put (transaction_a, block_a->previous (), block_a);
			node.gap_cache.add (transaction_a, block_a);
			break;
		}
		case chratos::process_result::gap_source:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Gap source for: %1%") % hash.to_string ());
			}
			node.store.unchecked_put (transaction_a, node.ledger.block_source (transaction_a, *block_a), block_a);
			node.gap_cache.add (transaction_a, block_a);
			break;
		}
		case chratos::process_result::old:
		{
			if (node.config.logging.ledger_duplicate_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Old for: %1%") % block_a->hash ().to_string ());
			}
			queue_unchecked (transaction_a, hash);
			break;
		}
		case chratos::process_result::bad_signature:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Bad signature for: %1%") % hash.to_string ());
			}
			break;
		}
		case chratos::process_result::negative_spend:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Negative spend for: %1%") % hash.to_string ());
			}
			break;
		}
		case chratos::process_result::unreceivable:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Unreceivable for: %1%") % hash.to_string ());
			}
			break;
		}
		case chratos::process_result::fork:
		{
			if (origination < std::chrono::steady_clock::now () - std::chrono::seconds (15))
			{
				// Only let the bootstrap attempt know about forked blocks that not originate recently.
				node.process_fork (transaction_a, block_a);
			}
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Fork for: %1% root: %2%") % hash.to_string () % block_a->root ().to_string ());
			}
			break;
		}
		case chratos::process_result::opened_burn_account:
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("*** Rejecting open block for burn account ***: %1%") % hash.to_string ());
			break;
		}
		case chratos::process_result::balance_mismatch:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Balance mismatch for: %1%") % hash.to_string ());
			}
			break;
		}
		case chratos::process_result::representative_mismatch:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Representative mismatch for: %1%") % hash.to_string ());
			}
			break;
		}
		case chratos::process_result::block_position:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Block %1% cannot follow predecessor %2%") % hash.to_string () % block_a->previous ().to_string ());
			}
			break;
		}
		case chratos::process_result::outstanding_pendings:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Dividend %1% has outstanding pendings") % block_a->dividend ().to_string ());
			}
			break;
		}
		case chratos::process_result::dividend_too_small:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Dividend %1% is too small to be accepted") % hash.to_string ());
			}
			break;
		}
		case chratos::process_result::incorrect_dividend:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Block %1% cannot be sent without the account claiming for the dividend first") % hash.to_string ());
			}
			node.store.unchecked_put (transaction_a, block_a->dividend (), block_a);
			break;
		}
		case chratos::process_result::dividend_fork:
		{
			if (origination < std::chrono::steady_clock::now () - std::chrono::seconds (15))
			{
				// Only let the bootstrap attempt know about forked blocks that not originate recently.
				node.process_dividend_fork (transaction_a, block_a);
			}
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Fork for: %1% root: %2%") % hash.to_string () % block_a->root ().to_string ());
			}
			break;
		}
		case chratos::process_result::invalid_dividend_account:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Account %1% cannot create a dividend") % block_a->source ().to_account ());
			}
			break;
		}
	}
	return result;
}

void chratos::block_processor::queue_unchecked (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a)
{
	auto cached (node.store.unchecked_get (transaction_a, hash_a));
	for (auto i (cached.begin ()), n (cached.end ()); i != n; ++i)
	{
		node.store.unchecked_del (transaction_a, hash_a, *i);
		add (*i, std::chrono::steady_clock::time_point ());
	}
	std::lock_guard<std::mutex> lock (node.gap_cache.mutex);
	node.gap_cache.blocks.get<1> ().erase (hash_a);
}

chratos::node::node (chratos::node_init & init_a, boost::asio::io_service & service_a, uint16_t peering_port_a, boost::filesystem::path const & application_path_a, chratos::alarm & alarm_a, chratos::logging const & logging_a, chratos::work_pool & work_a) :
node (init_a, service_a, application_path_a, alarm_a, chratos::node_config (peering_port_a, logging_a), work_a)
{
}

chratos::node::node (chratos::node_init & init_a, boost::asio::io_service & service_a, boost::filesystem::path const & application_path_a, chratos::alarm & alarm_a, chratos::node_config const & config_a, chratos::work_pool & work_a) :
service (service_a),
config (config_a),
alarm (alarm_a),
work (work_a),
store_impl (std::make_unique<chratos::mdb_store> (init_a.block_store_init, application_path_a / "data.ldb", config_a.lmdb_max_dbs)),
store (*store_impl),
gap_cache (*this),
ledger (store, stats, config.epoch_block_link, config.epoch_block_signer),
active (*this),
network (*this, config.peering_port),
bootstrap_initiator (*this),
bootstrap (service_a, config.peering_port, *this),
peers (network.endpoint ()),
application_path (application_path_a),
wallets (init_a.block_store_init, *this),
port_mapping (*this),
vote_processor (*this),
warmed_up (0),
block_processor (*this),
block_processor_thread ([this]() {
	chratos::thread_role::set (chratos::thread_role::name::block_processing);
	this->block_processor.process_blocks ();
}),
online_reps (*this),
stats (config.stat_config)
{
	wallets.observer = [this](bool active) {
		observers.wallet.notify (active);
	};
	peers.peer_observer = [this](chratos::endpoint const & endpoint_a) {
		observers.endpoint.notify (endpoint_a);
	};
	peers.disconnect_observer = [this]() {
		observers.disconnect.notify ();
	};
	observers.blocks.add ([this](std::shared_ptr<chratos::block> block_a, chratos::account const & account_a, chratos::amount const & amount_a, bool is_state_send_a) {
		if (this->block_arrival.recent (block_a->hash ()))
		{
			auto node_l (shared_from_this ());
			background ([node_l, block_a, account_a, amount_a, is_state_send_a]() {
				if (!node_l->config.callback_address.empty ())
				{
					boost::property_tree::ptree event;
					event.add ("account", account_a.to_account ());
					event.add ("hash", block_a->hash ().to_string ());
					std::string block_text;
					block_a->serialize_json (block_text);
					event.add ("block", block_text);
					event.add ("amount", amount_a.to_string_dec ());
					if (is_state_send_a)
					{
						event.add ("is_send", is_state_send_a);
					}
					std::stringstream ostream;
					boost::property_tree::write_json (ostream, event);
					ostream.flush ();
					auto body (std::make_shared<std::string> (ostream.str ()));
					auto address (node_l->config.callback_address);
					auto port (node_l->config.callback_port);
					auto target (std::make_shared<std::string> (node_l->config.callback_target));
					auto resolver (std::make_shared<boost::asio::ip::tcp::resolver> (node_l->service));
					resolver->async_resolve (boost::asio::ip::tcp::resolver::query (address, std::to_string (port)), [node_l, address, port, target, body, resolver](boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::iterator i_a) {
						if (!ec)
						{
							for (auto i (i_a), n (boost::asio::ip::tcp::resolver::iterator {}); i != n; ++i)
							{
								auto sock (std::make_shared<boost::asio::ip::tcp::socket> (node_l->service));
								sock->async_connect (i->endpoint (), [node_l, target, body, sock, address, port](boost::system::error_code const & ec) {
									if (!ec)
									{
										auto req (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
										req->method (boost::beast::http::verb::post);
										req->target (*target);
										req->version (11);
										req->insert (boost::beast::http::field::host, address);
										req->insert (boost::beast::http::field::content_type, "application/json");
										req->body () = *body;
										//req->prepare (*req);
										//boost::beast::http::prepare(req);
										req->prepare_payload ();
										boost::beast::http::async_write (*sock, *req, [node_l, sock, address, port, req](boost::system::error_code const & ec, size_t bytes_transferred) {
											if (!ec)
											{
												auto sb (std::make_shared<boost::beast::flat_buffer> ());
												auto resp (std::make_shared<boost::beast::http::response<boost::beast::http::string_body>> ());
												boost::beast::http::async_read (*sock, *sb, *resp, [node_l, sb, resp, sock, address, port](boost::system::error_code const & ec, size_t bytes_transferred) {
													if (!ec)
													{
														if (resp->result () == boost::beast::http::status::ok)
														{
															node_l->stats.inc (chratos::stat::type::http_callback, chratos::stat::detail::initiate, chratos::stat::dir::out);
														}
														else
														{
															if (node_l->config.logging.callback_logging ())
															{
																BOOST_LOG (node_l->log) << boost::str (boost::format ("Callback to %1%:%2% failed with status: %3%") % address % port % resp->result ());
															}
															node_l->stats.inc (chratos::stat::type::error, chratos::stat::detail::http_callback, chratos::stat::dir::out);
														}
													}
													else
													{
														if (node_l->config.logging.callback_logging ())
														{
															BOOST_LOG (node_l->log) << boost::str (boost::format ("Unable complete callback: %1%:%2%: %3%") % address % port % ec.message ());
														}
														node_l->stats.inc (chratos::stat::type::error, chratos::stat::detail::http_callback, chratos::stat::dir::out);
													};
												});
											}
											else
											{
												if (node_l->config.logging.callback_logging ())
												{
													BOOST_LOG (node_l->log) << boost::str (boost::format ("Unable to send callback: %1%:%2%: %3%") % address % port % ec.message ());
												}
												node_l->stats.inc (chratos::stat::type::error, chratos::stat::detail::http_callback, chratos::stat::dir::out);
											}
										});
									}
									else
									{
										if (node_l->config.logging.callback_logging ())
										{
											BOOST_LOG (node_l->log) << boost::str (boost::format ("Unable to connect to callback address: %1%:%2%: %3%") % address % port % ec.message ());
										}
										node_l->stats.inc (chratos::stat::type::error, chratos::stat::detail::http_callback, chratos::stat::dir::out);
									}
								});
							}
						}
						else
						{
							if (node_l->config.logging.callback_logging ())
							{
								BOOST_LOG (node_l->log) << boost::str (boost::format ("Error resolving callback: %1%:%2%: %3%") % address % port % ec.message ());
							}
							node_l->stats.inc (chratos::stat::type::error, chratos::stat::detail::http_callback, chratos::stat::dir::out);
						}
					});
				}
			});
		}
	});
	observers.endpoint.add ([this](chratos::endpoint const & endpoint_a) {
		this->network.send_keepalive (endpoint_a);
		rep_query (*this, endpoint_a);
	});
	observers.vote.add ([this](chratos::transaction const & transaction, std::shared_ptr<chratos::vote> vote_a, chratos::endpoint const & endpoint_a) {
		assert (endpoint_a.address ().is_v6 ());
		this->gap_cache.vote (vote_a);
		this->online_reps.vote (vote_a);
		chratos::uint128_t rep_weight;
		chratos::uint128_t min_rep_weight;
		{
			rep_weight = ledger.weight (transaction, vote_a->account);
			min_rep_weight = online_reps.online_stake () / 1000;
		}
		if (rep_weight > min_rep_weight)
		{
			bool rep_crawler_exists (false);
			for (auto hash : *vote_a)
			{
				if (this->rep_crawler.exists (hash))
				{
					rep_crawler_exists = true;
					break;
				}
			}
			if (rep_crawler_exists)
			{
				// We see a valid non-replay vote for a block we requested, this node is probably a representative
				if (this->peers.rep_response (endpoint_a, vote_a->account, rep_weight))
				{
					BOOST_LOG (log) << boost::str (boost::format ("Found a representative at %1%") % endpoint_a);
					// Rebroadcasting all active votes to new representative
					auto blocks (this->active.list_blocks ());
					for (auto i (blocks.begin ()), n (blocks.end ()); i != n; ++i)
					{
						if (*i != nullptr)
						{
							this->network.send_confirm_req (endpoint_a, *i);
						}
					}
				}
			}
		}
	});
	BOOST_LOG (log) << "Node starting, version: " << RAIBLOCKS_VERSION_MAJOR << "." << RAIBLOCKS_VERSION_MINOR;
	BOOST_LOG (log) << boost::str (boost::format ("Work pool running %1% threads") % work.threads.size ());
	if (!init_a.error ())
	{
		if (config.logging.node_lifetime_tracing ())
		{
			BOOST_LOG (log) << "Constructing node";
		}
		chratos::genesis genesis;
		auto transaction (store.tx_begin_write ());
		if (store.latest_begin (transaction) == store.latest_end ())
		{
			// Store was empty meaning we just created it, add the genesis block
			store.initialize (transaction, genesis);
		}
		if (!store.block_exists (transaction, genesis.hash ()))
		{
			BOOST_LOG (log) << "Genesis block not found. Make sure the node network ID is correct.";
			std::exit (1);
		}

		node_id = chratos::keypair (store.get_node_id (transaction));
		BOOST_LOG (log) << "Node ID: " << node_id.pub.to_account ();
	}
	peers.online_weight_minimum = config.online_weight_minimum.number ();
	if (chratos::chratos_network == chratos::chratos_networks::chratos_live_network || chratos::chratos_network == chratos::chratos_networks::chratos_beta_network)
	{
		chratos::bufferstream weight_stream ((const uint8_t *)chratos_bootstrap_weights, chratos_bootstrap_weights_size);
		chratos::uint128_union block_height;
		if (!chratos::read (weight_stream, block_height))
		{
			auto max_blocks = (uint64_t)block_height.number ();
			auto transaction (store.tx_begin_read ());
			if (ledger.store.block_count (transaction).sum () < max_blocks)
			{
				ledger.bootstrap_weight_max_blocks = max_blocks;
				while (true)
				{
					chratos::account account;
					if (chratos::read (weight_stream, account.bytes))
					{
						break;
					}
					chratos::amount weight;
					if (chratos::read (weight_stream, weight.bytes))
					{
						break;
					}
					BOOST_LOG (log) << "Using bootstrap rep weight: " << account.to_account () << " -> " << weight.format_balance (Mchr_ratio, 0, true) << " CHR";
					ledger.bootstrap_weights[account] = weight.number ();
				}
			}
		}
	}
}

chratos::node::~node ()
{
	if (config.logging.node_lifetime_tracing ())
	{
		BOOST_LOG (log) << "Destructing node";
	}
	stop ();
}

bool chratos::node::copy_with_compaction (boost::filesystem::path const & destination_file)
{
	return !mdb_env_copy2 (boost::polymorphic_downcast<chratos::mdb_store *> (store_impl.get ())->env.environment, destination_file.string ().c_str (), MDB_CP_COMPACT);
}

void chratos::node::send_keepalive (chratos::endpoint const & endpoint_a)
{
	network.send_keepalive (chratos::map_endpoint_to_v6 (endpoint_a));
}

void chratos::node::process_fork (chratos::transaction const & transaction_a, std::shared_ptr<chratos::block> block_a)
{
	auto root (block_a->root ());
	if (!store.block_exists (transaction_a, block_a->hash ()) && store.root_exists (transaction_a, block_a->root ()))
	{
		std::shared_ptr<chratos::block> ledger_block (ledger.forked_block (transaction_a, *block_a));
		if (ledger_block)
		{
			std::weak_ptr<chratos::node> this_w (shared_from_this ());
			if (!active.start (std::make_pair (ledger_block, block_a), [this_w, root](std::shared_ptr<chratos::block>) {
				    if (auto this_l = this_w.lock ())
				    {
					    auto attempt (this_l->bootstrap_initiator.current_attempt ());
					    if (attempt)
					    {
						    auto transaction (this_l->store.tx_begin_read ());
						    auto account (this_l->ledger.store.frontier_get (transaction, root));
						    if (!account.is_zero ())
						    {
							    attempt->requeue_pull (chratos::pull_info (account, root, root));
						    }
						    else if (this_l->ledger.store.account_exists (transaction, root))
						    {
							    attempt->requeue_pull (chratos::pull_info (root, chratos::block_hash (0), chratos::block_hash (0)));
						    }
					    }
				    }
			    }))
			{
				BOOST_LOG (log) << boost::str (boost::format ("Resolving fork between our block: %1% and block %2% both with root %3%") % ledger_block->hash ().to_string () % block_a->hash ().to_string () % block_a->root ().to_string ());
				network.broadcast_confirm_req (ledger_block);
			}
		}
	}
}

void chratos::node::process_dividend_fork (chratos::transaction const & transaction_a, std::shared_ptr<chratos::block> block_a)
{
	// TODO - Handle dividend forks explicitly
}

chratos::gap_cache::gap_cache (chratos::node & node_a) :
node (node_a)
{
}

void chratos::gap_cache::add (chratos::transaction const & transaction_a, std::shared_ptr<chratos::block> block_a)
{
	auto hash (block_a->hash ());
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (blocks.get<1> ().find (hash));
	if (existing != blocks.get<1> ().end ())
	{
		blocks.get<1> ().modify (existing, [](chratos::gap_information & info) {
			info.arrival = std::chrono::steady_clock::now ();
		});
	}
	else
	{
		blocks.insert ({ std::chrono::steady_clock::now (), hash, std::unordered_set<chratos::account> () });
		if (blocks.size () > max)
		{
			blocks.get<0> ().erase (blocks.get<0> ().begin ());
		}
	}
}

void chratos::gap_cache::vote (std::shared_ptr<chratos::vote> vote_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto transaction (node.store.tx_begin_read ());
	for (auto hash : *vote_a)
	{
		auto existing (blocks.get<1> ().find (hash));
		if (existing != blocks.get<1> ().end ())
		{
			auto is_new (false);
			blocks.get<1> ().modify (existing, [&](chratos::gap_information & info) { is_new = info.voters.insert (vote_a->account).second; });
			if (is_new)
			{
				uint128_t tally;
				for (auto & voter : existing->voters)
				{
					tally += node.ledger.weight (transaction, voter);
				}
				if (tally > bootstrap_threshold (transaction))
				{
					auto node_l (node.shared ());
					auto now (std::chrono::steady_clock::now ());
					node.alarm.add (chratos::chratos_network == chratos::chratos_networks::chratos_test_network ? now + std::chrono::milliseconds (5) : now + std::chrono::seconds (5), [node_l, hash]() {
						auto transaction (node_l->store.tx_begin_read ());
						if (!node_l->store.block_exists (transaction, hash))
						{
							if (!node_l->bootstrap_initiator.in_progress ())
							{
								BOOST_LOG (node_l->log) << boost::str (boost::format ("Missing block %1% which has enough votes to warrant bootstrapping it") % hash.to_string ());
							}
							node_l->bootstrap_initiator.bootstrap ();
						}
					});
				}
			}
		}
	}
}

chratos::uint128_t chratos::gap_cache::bootstrap_threshold (chratos::transaction const & transaction_a)
{
	auto result ((node.online_reps.online_stake () / 256) * node.config.bootstrap_fraction_numerator);
	return result;
}

void chratos::network::confirm_send (chratos::confirm_ack const & confirm_a, std::shared_ptr<std::vector<uint8_t>> bytes_a, chratos::endpoint const & endpoint_a)
{
	if (node.config.logging.network_publish_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Sending confirm_ack for block(s) %1%to %2% sequence %3%") % confirm_a.vote->hashes_string () % endpoint_a % std::to_string (confirm_a.vote->sequence));
	}
	std::weak_ptr<chratos::node> node_w (node.shared ());
	node.network.send_buffer (bytes_a->data (), bytes_a->size (), endpoint_a, [bytes_a, node_w, endpoint_a](boost::system::error_code const & ec, size_t size_a) {
		if (auto node_l = node_w.lock ())
		{
			if (ec && node_l->config.logging.network_logging ())
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Error broadcasting confirm_ack to %1%: %2%") % endpoint_a % ec.message ());
			}
			else
			{
				node_l->stats.inc (chratos::stat::type::message, chratos::stat::detail::confirm_ack, chratos::stat::dir::out);
			}
		}
	});
}

void chratos::node::process_active (std::shared_ptr<chratos::block> incoming)
{
	if (!block_arrival.add (incoming->hash ()))
	{
		block_processor.add (incoming, std::chrono::steady_clock::now ());
	}
}

chratos::process_return chratos::node::process (chratos::block const & block_a)
{
	auto transaction (store.tx_begin_write ());
	auto result (ledger.process (transaction, block_a));
	return result;
}

void chratos::node::start ()
{
	network.start ();
	ongoing_keepalive ();
	ongoing_syn_cookie_cleanup ();
	ongoing_bootstrap ();
	ongoing_store_flush ();
	ongoing_rep_crawl ();
	bootstrap.start ();
	backup_wallet ();
	search_pending ();
	online_reps.recalculate_stake ();
	port_mapping.start ();
	add_initial_peers ();
	observers.started.notify ();
}

void chratos::node::stop ()
{
	BOOST_LOG (log) << "Node stopping";
	block_processor.stop ();
	if (block_processor_thread.joinable ())
	{
		block_processor_thread.join ();
	}
	active.stop ();
	network.stop ();
	bootstrap_initiator.stop ();
	bootstrap.stop ();
	port_mapping.stop ();
	vote_processor.stop ();
	wallets.stop ();
}

void chratos::node::keepalive_preconfigured (std::vector<std::string> const & peers_a)
{
	for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
	{
		keepalive (*i, chratos::network::node_port);
	}
}

chratos::block_hash chratos::node::latest (chratos::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.latest (transaction, account_a);
}

chratos::uint128_t chratos::node::balance (chratos::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.account_balance (transaction, account_a);
}

std::unique_ptr<chratos::block> chratos::node::block (chratos::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	return store.block_get (transaction, hash_a);
}

std::pair<chratos::uint128_t, chratos::uint128_t> chratos::node::balance_pending (chratos::account const & account_a)
{
	std::pair<chratos::uint128_t, chratos::uint128_t> result;
	auto transaction (store.tx_begin_read ());
	result.first = ledger.account_balance (transaction, account_a);
	result.second = ledger.account_pending (transaction, account_a);
	return result;
}

chratos::uint128_t chratos::node::weight (chratos::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.weight (transaction, account_a);
}

chratos::account chratos::node::representative (chratos::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	chratos::account_info info;
	chratos::account result (0);
	if (!store.account_get (transaction, account_a, info))
	{
		result = info.rep_block;
	}
	return result;
}

void chratos::node::ongoing_keepalive ()
{
	keepalive_preconfigured (config.preconfigured_peers);
	auto peers_l (peers.purge_list (std::chrono::steady_clock::now () - cutoff));
	for (auto i (peers_l.begin ()), j (peers_l.end ()); i != j && std::chrono::steady_clock::now () - i->last_attempt > period; ++i)
	{
		network.send_keepalive (i->endpoint);
	}
	std::weak_ptr<chratos::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + period, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_keepalive ();
		}
	});
}

void chratos::node::ongoing_syn_cookie_cleanup ()
{
	peers.purge_syn_cookies (std::chrono::steady_clock::now () - syn_cookie_cutoff);
	std::weak_ptr<chratos::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + (syn_cookie_cutoff * 2), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_syn_cookie_cleanup ();
		}
	});
}

void chratos::node::ongoing_rep_crawl ()
{
	auto now (std::chrono::steady_clock::now ());
	auto peers_l (peers.rep_crawl ());
	rep_query (*this, peers_l);
	if (network.on)
	{
		std::weak_ptr<chratos::node> node_w (shared_from_this ());
		alarm.add (now + std::chrono::seconds (4), [node_w]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->ongoing_rep_crawl ();
			}
		});
	}
}

void chratos::node::ongoing_bootstrap ()
{
	auto next_wakeup (300);
	if (warmed_up < 3)
	{
		// Re-attempt bootstrapping more aggressively on startup
		next_wakeup = 5;
		if (!bootstrap_initiator.in_progress () && !peers.empty ())
		{
			++warmed_up;
		}
	}
	bootstrap_initiator.bootstrap ();
	std::weak_ptr<chratos::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (next_wakeup), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_bootstrap ();
		}
	});
}

void chratos::node::ongoing_store_flush ()
{
	{
		auto transaction (store.tx_begin_write ());
		store.flush (transaction);
	}
	std::weak_ptr<chratos::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_store_flush ();
		}
	});
}

void chratos::node::backup_wallet ()
{
	auto transaction (store.tx_begin_read ());
	for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n; ++i)
	{
		boost::system::error_code error_chmod;
		auto backup_path (application_path / "backup");

		boost::filesystem::create_directories (backup_path);
		chratos::set_secure_perm_directory (backup_path, error_chmod);
		i->second->store.write_backup (transaction, backup_path / (i->first.to_string () + ".json"));
	}
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + backup_interval, [this_l]() {
		this_l->backup_wallet ();
	});
}

void chratos::node::search_pending ()
{
	wallets.search_pending_all ();
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + search_pending_interval, [this_l]() {
		this_l->search_pending ();
	});
}

int chratos::node::price (chratos::uint128_t const & balance_a, int amount_a)
{
	assert (balance_a >= amount_a * chratos::Gchr_ratio);
	auto balance_l (balance_a);
	double result (0.0);
	for (auto i (0); i < amount_a; ++i)
	{
		balance_l -= chratos::Gchr_ratio;
		auto balance_scaled ((balance_l / chratos::Mchr_ratio).convert_to<double> ());
		auto units (balance_scaled / 1000.0);
		auto unit_price (((free_cutoff - units) / free_cutoff) * price_max);
		result += std::min (std::max (0.0, unit_price), price_max);
	}
	return static_cast<int> (result * 100.0);
}

namespace
{
class work_request
{
public:
	work_request (boost::asio::io_service & service_a, boost::asio::ip::address address_a, uint16_t port_a) :
	address (address_a),
	port (port_a),
	socket (service_a)
	{
	}
	boost::asio::ip::address address;
	uint16_t port;
	boost::beast::flat_buffer buffer;
	boost::beast::http::response<boost::beast::http::string_body> response;
	boost::asio::ip::tcp::socket socket;
};
class distributed_work : public std::enable_shared_from_this<distributed_work>
{
public:
	distributed_work (std::shared_ptr<chratos::node> const & node_a, chratos::block_hash const & root_a, std::function<void(uint64_t)> callback_a) :
	distributed_work (1, node_a, root_a, callback_a)
	{
		assert (node_a != nullptr);
	}
	distributed_work (unsigned int backoff_a, std::shared_ptr<chratos::node> const & node_a, chratos::block_hash const & root_a, std::function<void(uint64_t)> callback_a) :
	callback (callback_a),
	backoff (backoff_a),
	node (node_a),
	root (root_a),
	need_resolve (node_a->config.work_peers)
	{
		assert (node_a != nullptr);
		completed.clear ();
	}
	void start ()
	{
		if (need_resolve.empty ())
		{
			start_work ();
		}
		else
		{
			auto current (need_resolve.back ());
			need_resolve.pop_back ();
			auto this_l (shared_from_this ());
			boost::system::error_code ec;
			auto parsed_address (boost::asio::ip::address_v6::from_string (current.first, ec));
			if (!ec)
			{
				outstanding[parsed_address] = current.second;
				start ();
			}
			else
			{
				node->network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (current.first, std::to_string (current.second)), [current, this_l](boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a) {
					if (!ec)
					{
						for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator {}); i != n; ++i)
						{
							auto endpoint (i->endpoint ());
							this_l->outstanding[endpoint.address ()] = endpoint.port ();
						}
					}
					else
					{
						BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Error resolving work peer: %1%:%2%: %3%") % current.first % current.second % ec.message ());
					}
					this_l->start ();
				});
			}
		}
	}
	void start_work ()
	{
		if (!outstanding.empty ())
		{
			auto this_l (shared_from_this ());
			std::lock_guard<std::mutex> lock (mutex);
			for (auto const & i : outstanding)
			{
				auto host (i.first);
				auto service (i.second);
				node->background ([this_l, host, service]() {
					auto connection (std::make_shared<work_request> (this_l->node->service, host, service));
					connection->socket.async_connect (chratos::tcp_endpoint (host, service), [this_l, connection](boost::system::error_code const & ec) {
						if (!ec)
						{
							std::string request_string;
							{
								boost::property_tree::ptree request;
								request.put ("action", "work_generate");
								request.put ("hash", this_l->root.to_string ());
								std::stringstream ostream;
								boost::property_tree::write_json (ostream, request);
								request_string = ostream.str ();
							}
							auto request (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
							request->method (boost::beast::http::verb::post);
							request->target ("/");
							request->version (11);
							request->body () = request_string;
							request->prepare_payload ();
							boost::beast::http::async_write (connection->socket, *request, [this_l, connection, request](boost::system::error_code const & ec, size_t bytes_transferred) {
								if (!ec)
								{
									boost::beast::http::async_read (connection->socket, connection->buffer, connection->response, [this_l, connection](boost::system::error_code const & ec, size_t bytes_transferred) {
										if (!ec)
										{
											if (connection->response.result () == boost::beast::http::status::ok)
											{
												this_l->success (connection->response.body (), connection->address);
											}
											else
											{
												BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Work peer responded with an error %1% %2%: %3%") % connection->address % connection->port % connection->response.result ());
												this_l->failure (connection->address);
											}
										}
										else
										{
											BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Unable to read from work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ());
											this_l->failure (connection->address);
										}
									});
								}
								else
								{
									BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Unable to write to work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ());
									this_l->failure (connection->address);
								}
							});
						}
						else
						{
							BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Unable to connect to work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ());
							this_l->failure (connection->address);
						}
					});
				});
			}
		}
		else
		{
			handle_failure (true);
		}
	}
	void stop ()
	{
		auto this_l (shared_from_this ());
		std::lock_guard<std::mutex> lock (mutex);
		for (auto const & i : outstanding)
		{
			auto host (i.first);
			node->background ([this_l, host]() {
				std::string request_string;
				{
					boost::property_tree::ptree request;
					request.put ("action", "work_cancel");
					request.put ("hash", this_l->root.to_string ());
					std::stringstream ostream;
					boost::property_tree::write_json (ostream, request);
					request_string = ostream.str ();
				}
				boost::beast::http::request<boost::beast::http::string_body> request;
				request.method (boost::beast::http::verb::post);
				request.target ("/");
				request.version (11);
				request.body () = request_string;
				request.prepare_payload ();
				auto socket (std::make_shared<boost::asio::ip::tcp::socket> (this_l->node->service));
				boost::beast::http::async_write (*socket, request, [socket](boost::system::error_code const & ec, size_t bytes_transferred) {
				});
			});
		}
		outstanding.clear ();
	}
	void success (std::string const & body_a, boost::asio::ip::address const & address)
	{
		auto last (remove (address));
		std::stringstream istream (body_a);
		try
		{
			boost::property_tree::ptree result;
			boost::property_tree::read_json (istream, result);
			auto work_text (result.get<std::string> ("work"));
			uint64_t work;
			if (!chratos::from_string_hex (work_text, work))
			{
				if (!chratos::work_validate (root, work))
				{
					set_once (work);
					stop ();
				}
				else
				{
					BOOST_LOG (node->log) << boost::str (boost::format ("Incorrect work response from %1% for root %2%: %3%") % address % root.to_string () % work_text);
					handle_failure (last);
				}
			}
			else
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Work response from %1% wasn't a number: %2%") % address % work_text);
				handle_failure (last);
			}
		}
		catch (...)
		{
			BOOST_LOG (node->log) << boost::str (boost::format ("Work response from %1% wasn't parsable: %2%") % address % body_a);
			handle_failure (last);
		}
	}
	void set_once (uint64_t work_a)
	{
		if (!completed.test_and_set ())
		{
			callback (work_a);
		}
	}
	void failure (boost::asio::ip::address const & address)
	{
		auto last (remove (address));
		handle_failure (last);
	}
	void handle_failure (bool last)
	{
		if (last)
		{
			if (!completed.test_and_set ())
			{
				if (node->config.work_threads != 0 || node->work.opencl)
				{
					auto callback_l (callback);
					node->work.generate (root, [callback_l](boost::optional<uint64_t> const & work_a) {
						callback_l (work_a.value ());
					});
				}
				else
				{
					if (backoff == 1 && node->config.logging.work_generation_time ())
					{
						BOOST_LOG (node->log) << "Work peer(s) failed to generate work for root " << root.to_string () << ", retrying...";
					}
					auto now (std::chrono::steady_clock::now ());
					auto root_l (root);
					auto callback_l (callback);
					std::weak_ptr<chratos::node> node_w (node);
					auto next_backoff (std::min (backoff * 2, (unsigned int)60 * 5));
					node->alarm.add (now + std::chrono::seconds (backoff), [node_w, root_l, callback_l, next_backoff] {
						if (auto node_l = node_w.lock ())
						{
							auto work_generation (std::make_shared<distributed_work> (next_backoff, node_l, root_l, callback_l));
							work_generation->start ();
						}
					});
				}
			}
		}
	}
	bool remove (boost::asio::ip::address const & address)
	{
		std::lock_guard<std::mutex> lock (mutex);
		outstanding.erase (address);
		return outstanding.empty ();
	}
	std::function<void(uint64_t)> callback;
	unsigned int backoff; // in seconds
	std::shared_ptr<chratos::node> node;
	chratos::block_hash root;
	std::mutex mutex;
	std::map<boost::asio::ip::address, uint16_t> outstanding;
	std::vector<std::pair<std::string, uint16_t>> need_resolve;
	std::atomic_flag completed;
};
}

void chratos::node::work_generate_blocking (chratos::block & block_a)
{
	block_a.block_work_set (work_generate_blocking (block_a.root ()));
}

void chratos::node::work_generate (chratos::uint256_union const & hash_a, std::function<void(uint64_t)> callback_a)
{
	auto work_generation (std::make_shared<distributed_work> (shared (), hash_a, callback_a));
	work_generation->start ();
}

uint64_t chratos::node::work_generate_blocking (chratos::uint256_union const & hash_a)
{
	std::promise<uint64_t> promise;
	work_generate (hash_a, [&promise](uint64_t work_a) {
		promise.set_value (work_a);
	});
	return promise.get_future ().get ();
}

void chratos::node::add_initial_peers ()
{
}

void chratos::node::block_confirm (std::shared_ptr<chratos::block> block_a)
{
	active.start (block_a);
	network.broadcast_confirm_req (block_a);
}

chratos::uint128_t chratos::node::delta ()
{
	auto result ((online_reps.online_stake () / 100) * config.online_weight_quorum);
	return result;
}

namespace
{
class confirmed_visitor : public chratos::block_visitor
{
public:
	confirmed_visitor (chratos::transaction const & transaction_a, chratos::node & node_a, std::shared_ptr<chratos::block> block_a, chratos::block_hash const & hash_a, chratos::block_hash const & dividend_a) :
	transaction (transaction_a),
	node (node_a),
	block (block_a),
	hash (hash_a),
	dividend (dividend_a)
	{
	}
	virtual ~confirmed_visitor () = default;
	void scan_receivable (chratos::account const & account_a)
	{
		for (auto i (node.wallets.items.begin ()), n (node.wallets.items.end ()); i != n; ++i)
		{
			auto wallet (i->second);
			if (wallet->store.exists (transaction, account_a))
			{
				chratos::account representative;
				chratos::pending_info pending;
				representative = wallet->store.representative (transaction);
				auto error (node.store.pending_get (transaction, chratos::pending_key (account_a, hash), pending));
				if (!error)
				{
					auto node_l (node.shared ());
					auto amount (pending.amount.number ());
					wallet->receive_async (block, representative, amount, [](std::shared_ptr<chratos::block>) {});
				}
				else
				{
					if (!node.store.block_exists (transaction, hash))
					{
						BOOST_LOG (node.log) << boost::str (boost::format ("Confirmed block is missing:  %1%") % hash.to_string ());
						assert (false && "Confirmed block is missing");
					}
					else
					{
						BOOST_LOG (node.log) << boost::str (boost::format ("Block %1% has already been received") % hash.to_string ());
					}
				}
			}
		}
	}
	void claim_dividend (chratos::dividend_block const & block_a)
	{
		std::shared_ptr<chratos::block> dividend_l = node.store.block_get (transaction, block_a.hash ());
		for (auto i (node.wallets.items.begin ()), n (node.wallets.items.end ()); i != n; ++i)
		{
			auto wallet (i->second);
			auto accounts = wallet->search_unclaimed (block_a.hash ());
			chratos::account representative;
			representative = wallet->store.representative (transaction);

			for (auto & account : accounts)
			{
				// Check pending and claim outstanding
				receive_outstanding_pendings (wallet, account, block_a.hash ());
				// Check dividend points to the account's last claimed
				chratos::account_info info;
				node.store.account_get (transaction, account, info);
				if (info.dividend_block == dividend_l->dividend ())
				{
					// Claim dividends
					wallet->claim_dividend_async (dividend_l, account, representative, [](std::shared_ptr<chratos::block>) {});
				}
				else
				{
					auto prev_hash = dividend_l->dividend ();
					std::shared_ptr<chratos::block> previous = node.store.block_get (transaction, prev_hash);
					chratos::dividend_block const * prev_dividend (dynamic_cast<chratos::dividend_block const *> (previous.get ()));
					claim_dividend (*prev_dividend);
					claim_dividend (block_a);
				}
			}
		}
	}

	void receive_outstanding_pendings (std::shared_ptr<chratos::wallet> wallet, chratos::account const & account_a, chratos::block_hash const & dividend_a)
	{
		wallet->receive_outstanding_pendings_sync (transaction, account_a, dividend_a);
	}
	void state_block (chratos::state_block const & block_a) override
	{
		scan_receivable (block_a.hashables.link);
	}
	void dividend_block (chratos::dividend_block const & block_a) override
	{
		claim_dividend (block_a);
	}
	void claim_block (chratos::claim_block const &) override
	{
	}
	chratos::transaction const & transaction;
	chratos::node & node;
	std::shared_ptr<chratos::block> block;
	chratos::block_hash const & hash;
	chratos::block_hash const & dividend;
};
}

void chratos::node::process_confirmed (std::shared_ptr<chratos::block> block_a)
{
	auto hash (block_a->hash ());
	bool exists (ledger.block_exists (hash));
	// Attempt to process confirmed block if it's not in ledger yet
	if (!exists)
	{
		auto transaction (store.tx_begin_write ());
		block_processor.process_receive_one (transaction, block_a);
		exists = store.block_exists (transaction, hash);
	}
	if (exists)
	{
		auto dividend (block_a->dividend ());
		auto transaction (store.tx_begin_read ());
		confirmed_visitor visitor (transaction, *this, block_a, hash, dividend);
		block_a->visit (visitor);
		auto account (ledger.account (transaction, hash));
		auto amount (ledger.amount (transaction, hash));
		bool is_state_send (false);
		chratos::account pending_account (0);
		if (auto state = dynamic_cast<chratos::state_block *> (block_a.get ()))
		{
			is_state_send = ledger.is_send (transaction, *state);
			pending_account = state->hashables.link;
		}

		observers.blocks.notify (block_a, account, amount, is_state_send);
		if (amount > 0)
		{
			observers.account_balance.notify (account, false);
			if (!pending_account.is_zero ())
			{
				observers.account_balance.notify (pending_account, true);
			}
		}
	}
}

void chratos::node::process_message (chratos::message & message_a, chratos::endpoint const & sender_a)
{
	network_message_visitor visitor (*this, sender_a);
	message_a.visit (visitor);
}

chratos::endpoint chratos::network::endpoint ()
{
	boost::system::error_code ec;
	auto port (socket.local_endpoint (ec).port ());
	if (ec)
	{
		BOOST_LOG (node.log) << "Unable to retrieve port: " << ec.message ();
	}
	return chratos::endpoint (boost::asio::ip::address_v6::loopback (), port);
}

bool chratos::block_arrival::add (chratos::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	auto inserted (arrival.insert (chratos::block_arrival_info { now, hash_a }));
	auto result (!inserted.second);
	return result;
}

bool chratos::block_arrival::recent (chratos::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	while (arrival.size () > arrival_size_min && arrival.begin ()->arrival + arrival_time_min < now)
	{
		arrival.erase (arrival.begin ());
	}
	return arrival.get<1> ().find (hash_a) != arrival.get<1> ().end ();
}

chratos::online_reps::online_reps (chratos::node & node) :
node (node)
{
}

void chratos::online_reps::vote (std::shared_ptr<chratos::vote> const & vote_a)
{
	auto rep (vote_a->account);
	std::lock_guard<std::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	auto transaction (node.store.tx_begin_read ());
	auto current (reps.begin ());
	while (current != reps.end () && current->last_heard + std::chrono::seconds (chratos::node::cutoff) < now)
	{
		auto old_stake (online_stake_total);
		online_stake_total -= node.ledger.weight (transaction, current->representative);
		if (online_stake_total > old_stake)
		{
			// underflow
			online_stake_total = 0;
		}
		current = reps.erase (current);
	}
	auto rep_it (reps.get<1> ().find (rep));
	auto info (chratos::rep_last_heard_info { now, rep });
	if (rep_it == reps.get<1> ().end ())
	{
		auto old_stake (online_stake_total);
		online_stake_total += node.ledger.weight (transaction, rep);
		if (online_stake_total < old_stake)
		{
			// overflow
			online_stake_total = std::numeric_limits<chratos::uint128_t>::max ();
		}
		reps.insert (info);
	}
	else
	{
		reps.get<1> ().replace (rep_it, info);
	}
}

void chratos::online_reps::recalculate_stake ()
{
	std::lock_guard<std::mutex> lock (mutex);
	online_stake_total = 0;
	auto transaction (node.store.tx_begin_read ());
	for (auto it : reps)
	{
		online_stake_total += node.ledger.weight (transaction, it.representative);
	}
	auto now (std::chrono::steady_clock::now ());
	std::weak_ptr<chratos::node> node_w (node.shared ());
	node.alarm.add (now + std::chrono::minutes (5), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->online_reps.recalculate_stake ();
		}
	});
}

chratos::uint128_t chratos::online_reps::online_stake ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return std::max (online_stake_total, node.config.online_weight_minimum.number ());
}

std::vector<chratos::account> chratos::online_reps::list ()
{
	std::vector<chratos::account> result;
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (reps.begin ()), n (reps.end ()); i != n; ++i)
	{
		result.push_back (i->representative);
	}
	return result;
}

namespace
{
boost::asio::ip::address_v6 mapped_from_v4_bytes (unsigned long address_a)
{
	return boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (address_a));
}
}

bool chratos::reserved_address (chratos::endpoint const & endpoint_a, bool blacklist_loopback)
{
	assert (endpoint_a.address ().is_v6 ());
	auto bytes (endpoint_a.address ().to_v6 ());
	auto result (false);
	static auto const rfc1700_min (mapped_from_v4_bytes (0x00000000ul));
	static auto const rfc1700_max (mapped_from_v4_bytes (0x00fffffful));
	static auto const ipv4_loopback_min (mapped_from_v4_bytes (0x7f000000ul));
	static auto const ipv4_loopback_max (mapped_from_v4_bytes (0x7ffffffful));
	static auto const rfc1918_1_min (mapped_from_v4_bytes (0x0a000000ul));
	static auto const rfc1918_1_max (mapped_from_v4_bytes (0x0afffffful));
	static auto const rfc1918_2_min (mapped_from_v4_bytes (0xac100000ul));
	static auto const rfc1918_2_max (mapped_from_v4_bytes (0xac1ffffful));
	static auto const rfc1918_3_min (mapped_from_v4_bytes (0xc0a80000ul));
	static auto const rfc1918_3_max (mapped_from_v4_bytes (0xc0a8fffful));
	static auto const rfc6598_min (mapped_from_v4_bytes (0x64400000ul));
	static auto const rfc6598_max (mapped_from_v4_bytes (0x647ffffful));
	static auto const rfc5737_1_min (mapped_from_v4_bytes (0xc0000200ul));
	static auto const rfc5737_1_max (mapped_from_v4_bytes (0xc00002fful));
	static auto const rfc5737_2_min (mapped_from_v4_bytes (0xc6336400ul));
	static auto const rfc5737_2_max (mapped_from_v4_bytes (0xc63364fful));
	static auto const rfc5737_3_min (mapped_from_v4_bytes (0xcb007100ul));
	static auto const rfc5737_3_max (mapped_from_v4_bytes (0xcb0071fful));
	static auto const ipv4_multicast_min (mapped_from_v4_bytes (0xe0000000ul));
	static auto const ipv4_multicast_max (mapped_from_v4_bytes (0xeffffffful));
	static auto const rfc6890_min (mapped_from_v4_bytes (0xf0000000ul));
	static auto const rfc6890_max (mapped_from_v4_bytes (0xfffffffful));
	static auto const rfc6666_min (boost::asio::ip::address_v6::from_string ("100::"));
	static auto const rfc6666_max (boost::asio::ip::address_v6::from_string ("100::ffff:ffff:ffff:ffff"));
	static auto const rfc3849_min (boost::asio::ip::address_v6::from_string ("2001:db8::"));
	static auto const rfc3849_max (boost::asio::ip::address_v6::from_string ("2001:db8:ffff:ffff:ffff:ffff:ffff:ffff"));
	static auto const rfc4193_min (boost::asio::ip::address_v6::from_string ("fc00::"));
	static auto const rfc4193_max (boost::asio::ip::address_v6::from_string ("fd00:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
	static auto const ipv6_multicast_min (boost::asio::ip::address_v6::from_string ("ff00::"));
	static auto const ipv6_multicast_max (boost::asio::ip::address_v6::from_string ("ff00:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
	if (bytes >= rfc1700_min && bytes <= rfc1700_max)
	{
		result = true;
	}
	else if (bytes >= rfc5737_1_min && bytes <= rfc5737_1_max)
	{
		result = true;
	}
	else if (bytes >= rfc5737_2_min && bytes <= rfc5737_2_max)
	{
		result = true;
	}
	else if (bytes >= rfc5737_3_min && bytes <= rfc5737_3_max)
	{
		result = true;
	}
	else if (bytes >= ipv4_multicast_min && bytes <= ipv4_multicast_max)
	{
		result = true;
	}
	else if (bytes >= rfc6890_min && bytes <= rfc6890_max)
	{
		result = true;
	}
	else if (bytes >= rfc6666_min && bytes <= rfc6666_max)
	{
		result = true;
	}
	else if (bytes >= rfc3849_min && bytes <= rfc3849_max)
	{
		result = true;
	}
	else if (bytes >= ipv6_multicast_min && bytes <= ipv6_multicast_max)
	{
		result = true;
	}
	else if (blacklist_loopback && bytes.is_loopback ())
	{
		result = true;
	}
	else if (blacklist_loopback && bytes >= ipv4_loopback_min && bytes <= ipv4_loopback_max)
	{
		result = true;
	}
	else if (chratos::chratos_network == chratos::chratos_networks::chratos_live_network)
	{
		if (bytes >= rfc1918_1_min && bytes <= rfc1918_1_max)
		{
			result = true;
		}
		else if (bytes >= rfc1918_2_min && bytes <= rfc1918_2_max)
		{
			result = true;
		}
		else if (bytes >= rfc1918_3_min && bytes <= rfc1918_3_max)
		{
			result = true;
		}
		else if (bytes >= rfc6598_min && bytes <= rfc6598_max)
		{
			result = true;
		}
		else if (bytes >= rfc4193_min && bytes <= rfc4193_max)
		{
			result = true;
		}
	}
	return result;
}

void chratos::network::send_buffer (uint8_t const * data_a, size_t size_a, chratos::endpoint const & endpoint_a, std::function<void(boost::system::error_code const &, size_t)> callback_a)
{
	std::unique_lock<std::mutex> lock (socket_mutex);
	if (node.config.logging.network_packet_logging ())
	{
		BOOST_LOG (node.log) << "Sending packet";
	}
	socket.async_send_to (boost::asio::buffer (data_a, size_a), endpoint_a, [this, callback_a](boost::system::error_code const & ec, size_t size_a) {
		callback_a (ec, size_a);
		this->node.stats.add (chratos::stat::type::traffic, chratos::stat::dir::out, size_a);
		if (ec == boost::system::errc::host_unreachable)
		{
			this->node.stats.inc (chratos::stat::type::error, chratos::stat::detail::unreachable_host, chratos::stat::dir::out);
		}
		if (this->node.config.logging.network_packet_logging ())
		{
			BOOST_LOG (this->node.log) << "Packet send complete";
		}
	});
}

std::shared_ptr<chratos::node> chratos::node::shared ()
{
	return shared_from_this ();
}

chratos::election_vote_result::election_vote_result () :
replay (false),
processed (false)
{
}

chratos::election_vote_result::election_vote_result (bool replay_a, bool processed_a)
{
	replay = replay_a;
	processed = processed_a;
}

chratos::election::election (chratos::node & node_a, std::shared_ptr<chratos::block> block_a, std::function<void(std::shared_ptr<chratos::block>)> const & confirmation_action_a) :
confirmation_action (confirmation_action_a),
node (node_a),
root (block_a->root ()),
status ({ block_a, 0 }),
confirmed (false),
stopped (false)
{
	last_votes.insert (std::make_pair (chratos::not_an_account, chratos::vote_info { std::chrono::steady_clock::now (), 0, block_a->hash () }));
	blocks.insert (std::make_pair (block_a->hash (), block_a));
}

void chratos::election::compute_rep_votes (chratos::transaction const & transaction_a)
{
	if (node.config.enable_voting)
	{
		node.wallets.foreach_representative (transaction_a, [this, &transaction_a](chratos::public_key const & pub_a, chratos::raw_key const & prv_a) {
			auto vote (this->node.store.vote_generate (transaction_a, pub_a, prv_a, status.winner));
			this->node.vote_processor.vote (vote, this->node.network.endpoint ());
		});
	}
}

void chratos::election::confirm_once (chratos::transaction const & transaction_a)
{
	if (!confirmed.exchange (true))
	{
		auto winner_l (status.winner);
		auto node_l (node.shared ());
		auto confirmation_action_l (confirmation_action);
		node.background ([node_l, winner_l, confirmation_action_l]() {
			node_l->process_confirmed (winner_l);
			confirmation_action_l (winner_l);
		});
	}
}

void chratos::election::stop ()
{
	stopped = true;
}

bool chratos::election::have_quorum (chratos::tally_t const & tally_a, chratos::uint128_t tally_sum)
{
	bool result = false;
	if (tally_sum >= node.config.online_weight_minimum.number ())
	{
		auto i (tally_a.begin ());
		auto first (i->first);
		++i;
		auto second (i != tally_a.end () ? i->first : 0);
		auto delta_l (node.delta ());
		result = tally_a.begin ()->first > (second + delta_l);
	}
	return result;
}

chratos::tally_t chratos::election::tally (chratos::transaction const & transaction_a)
{
	std::unordered_map<chratos::block_hash, chratos::uint128_t> block_weights;
	for (auto vote_info : last_votes)
	{
		block_weights[vote_info.second.hash] += node.ledger.weight (transaction_a, vote_info.first);
	}
	last_tally = block_weights;
	chratos::tally_t result;
	for (auto item : block_weights)
	{
		auto block (blocks.find (item.first));
		if (block != blocks.end ())
		{
			result.insert (std::make_pair (item.second, block->second));
		}
	}
	return result;
}

void chratos::election::confirm_if_quorum (chratos::transaction const & transaction_a)
{
	auto tally_l (tally (transaction_a));
	assert (tally_l.size () > 0);
	auto winner (tally_l.begin ());
	auto block_l (winner->second);
	status.tally = winner->first;
	chratos::uint128_t sum (0);
	for (auto & i : tally_l)
	{
		sum += i.first;
	}
	if (sum >= node.config.online_weight_minimum.number () && block_l->hash () != status.winner->hash ())
	{
		auto node_l (node.shared ());
		node_l->block_processor.force (block_l);
		status.winner = block_l;
	}
	if (have_quorum (tally_l, sum))
	{
		if (node.config.logging.vote_logging () || blocks.size () > 1)
		{
			log_votes (tally_l);
		}
		confirm_once (transaction_a);
	}
}

void chratos::election::log_votes (chratos::tally_t const & tally_a)
{
	std::stringstream tally;
	tally << boost::str (boost::format ("\nVote tally for root %1%") % status.winner->root ().to_string ());
	for (auto i (tally_a.begin ()), n (tally_a.end ()); i != n; ++i)
	{
		tally << boost::str (boost::format ("\nBlock %1% weight %2%") % i->second->hash ().to_string () % i->first.convert_to<std::string> ());
	}
	for (auto i (last_votes.begin ()), n (last_votes.end ()); i != n; ++i)
	{
		tally << boost::str (boost::format ("\n%1% %2%") % i->first.to_account () % i->second.hash.to_string ());
	}
	BOOST_LOG (node.log) << tally.str ();
}

chratos::election_vote_result chratos::election::vote (chratos::account rep, uint64_t sequence, chratos::block_hash block_hash)
{
	// see republish_vote documentation for an explanation of these rules
	auto transaction (node.store.tx_begin_read ());
	auto replay (false);
	auto supply (node.online_reps.online_stake ());
	auto weight (node.ledger.weight (transaction, rep));
	auto should_process (false);
	if (chratos::chratos_network == chratos::chratos_networks::chratos_test_network || weight > supply / 1000) // 0.1% or above
	{
		unsigned int cooldown;
		if (weight < supply / 100) // 0.1% to 1%
		{
			cooldown = 15;
		}
		else if (weight < supply / 20) // 1% to 5%
		{
			cooldown = 5;
		}
		else // 5% or above
		{
			cooldown = 1;
		}
		auto last_vote_it (last_votes.find (rep));
		if (last_vote_it == last_votes.end ())
		{
			should_process = true;
		}
		else
		{
			auto last_vote (last_vote_it->second);
			if (last_vote.sequence < sequence || (last_vote.sequence == sequence && last_vote.hash < block_hash))
			{
				if (last_vote.time <= std::chrono::steady_clock::now () - std::chrono::seconds (cooldown))
				{
					should_process = true;
				}
			}
			else
			{
				replay = true;
			}
		}
		if (should_process)
		{
			last_votes[rep] = { std::chrono::steady_clock::now (), sequence, block_hash };
			if (!confirmed)
			{
				confirm_if_quorum (transaction);
			}
		}
	}
	return chratos::election_vote_result (replay, should_process);
}

bool chratos::node::validate_block_by_previous (chratos::transaction const & transaction, std::shared_ptr<chratos::block> block_a)
{
	bool result (false);
	chratos::account account;
	if (!block_a->previous ().is_zero ())
	{
		if (store.block_exists (transaction, block_a->previous ()))
		{
			account = ledger.account (transaction, block_a->previous ());
		}
		else
		{
			result = true;
		}
	}
	else
	{
		account = block_a->root ();
	}
	if (!result && block_a->type () == chratos::block_type::state)
	{
		std::shared_ptr<chratos::state_block> block_l (std::static_pointer_cast<chratos::state_block> (block_a));
		chratos::amount prev_balance (0);
		if (!block_l->hashables.previous.is_zero ())
		{
			if (store.block_exists (transaction, block_l->hashables.previous))
			{
				prev_balance = ledger.balance (transaction, block_l->hashables.previous);
			}
			else
			{
				result = true;
			}
		}
		if (!result)
		{
			if (block_l->hashables.balance == prev_balance && !ledger.epoch_link.is_zero () && ledger.is_epoch_link (block_l->hashables.link))
			{
				account = ledger.epoch_signer;
			}
		}
	}
	if (!result && (account.is_zero () || chratos::validate_message (account, block_a->hash (), block_a->block_signature ())))
	{
		result = true;
	}
	return result;
}

bool chratos::election::publish (std::shared_ptr<chratos::block> block_a)
{
	auto result (false);
	if (blocks.size () >= 10)
	{
		if (last_tally[block_a->hash ()] < node.online_reps.online_stake () / 10)
		{
			result = true;
		}
	}
	if (!result)
	{
		auto transaction (node.store.tx_begin_read ());
		result = node.validate_block_by_previous (transaction, block_a);
		if (!result)
		{
			if (blocks.find (block_a->hash ()) == blocks.end ())
			{
				blocks.insert (std::make_pair (block_a->hash (), block_a));
				confirm_if_quorum (transaction);
				node.network.republish_block (block_a);
			}
		}
	}
	return result;
}

void chratos::active_transactions::announce_votes ()
{
	std::unordered_set<chratos::block_hash> inactive;
	auto transaction (node.store.tx_begin_read ());
	unsigned unconfirmed_count (0);
	unsigned unconfirmed_announcements (0);
	unsigned mass_request_count (0);
	std::deque<std::shared_ptr<chratos::block>> rebroadcast_bundle;

	for (auto i (roots.begin ()), n (roots.end ()); i != n; ++i)
	{
		auto election_l (i->election);
		if ((election_l->confirmed || election_l->stopped) && i->announcements >= announcement_min - 1)
		{
			if (election_l->confirmed)
			{
				confirmed.push_back (i->election->status);
				if (confirmed.size () > election_history_size)
				{
					confirmed.pop_front ();
				}
			}
			inactive.insert (election_l->root);
		}
		else
		{
			if (i->announcements > announcement_long)
			{
				++unconfirmed_count;
				unconfirmed_announcements += i->announcements;
				// Log votes for very long unconfirmed elections
				if (i->announcements % 50 == 1)
				{
					auto tally_l (election_l->tally (transaction));
					election_l->log_votes (tally_l);
				}
				/* Escalation for long unconfirmed elections
				Start new elections for previous block & source
				if there are less than 100 active elections */
				if (i->announcements % announcement_long == 1 && roots.size () < 100)
				{
					std::unique_ptr<chratos::block> previous (nullptr);
					auto previous_hash (election_l->status.winner->previous ());
					if (!previous_hash.is_zero ())
					{
						previous = node.store.block_get (transaction, previous_hash);
						if (previous != nullptr)
						{
							add (std::make_pair (std::move (previous), nullptr));
						}
					}
					/* If previous block not existing/not commited yet, block_source can cause segfault for state blocks
					So source check can be done only if previous != nullptr or previous is 0 (open account) */
					if (previous_hash.is_zero () || previous != nullptr)
					{
						auto source_hash (node.ledger.block_source (transaction, *election_l->status.winner));
						if (!source_hash.is_zero ())
						{
							auto source (node.store.block_get (transaction, source_hash));
							if (source != nullptr)
							{
								add (std::make_pair (std::move (source), nullptr));
							}
						}
					}
				}
			}
			if (i->announcements < announcement_long || i->announcements % announcement_long == 1)
			{
				if (node.ledger.could_fit (transaction, *election_l->status.winner))
				{
					// Broadcast winner
					rebroadcast_bundle.push_back (election_l->status.winner);
				}
				else
				{
					if (i->announcements != 0)
					{
						election_l->stop ();
					}
				}
			}
			if (i->announcements % 4 == 1)
			{
				auto reps (std::make_shared<std::vector<chratos::peer_information>> (node.peers.representatives (std::numeric_limits<size_t>::max ())));
				std::unordered_set<chratos::account> probable_reps;
				chratos::uint128_t total_weight (0);
				for (auto j (reps->begin ()), m (reps->end ()); j != m;)
				{
					auto & rep_votes (i->election->last_votes);
					auto rep_acct (j->probable_rep_account);
					// Calculate if representative isn't recorded for several IP addresses
					if (probable_reps.find (rep_acct) == probable_reps.end ())
					{
						total_weight = total_weight + j->rep_weight.number ();
						probable_reps.insert (rep_acct);
					}
					if (rep_votes.find (rep_acct) != rep_votes.end ())
					{
						std::swap (*j, reps->back ());
						reps->pop_back ();
						m = reps->end ();
					}
					else
					{
						++j;
						if (node.config.logging.vote_logging ())
						{
							BOOST_LOG (node.log) << "Representative did not respond to confirm_req, retrying: " << rep_acct.to_account ();
						}
					}
				}
				if (!reps->empty () && (total_weight > node.config.online_weight_minimum.number () || mass_request_count > 20))
				{
					// broadcast_confirm_req_base modifies reps, so we clone it once to avoid aliasing
					node.network.broadcast_confirm_req_base (i->confirm_req_options.first, std::make_shared<std::vector<chratos::peer_information>> (*reps), 0);
				}
				else
				{
					// broadcast request to all peers
					node.network.broadcast_confirm_req_base (i->confirm_req_options.first, std::make_shared<std::vector<chratos::peer_information>> (node.peers.list_vector ()), 0);
					++mass_request_count;
				}
			}
		}
		roots.modify (i, [](chratos::conflict_info & info_a) {
			++info_a.announcements;
		});
	}
	// Rebroadcast unconfirmed blocks
	if (!rebroadcast_bundle.empty ())
	{
		node.network.republish_block_batch (rebroadcast_bundle);
	}
	for (auto i (inactive.begin ()), n (inactive.end ()); i != n; ++i)
	{
		auto root_it (roots.find (*i));
		assert (root_it != roots.end ());
		for (auto successor : root_it->election->blocks)
		{
			auto successor_it (successors.find (successor.first));
			if (successor_it != successors.end ())
			{
				assert (successor_it->second == root_it->election);
				successors.erase (successor_it);
			}
			else
			{
				assert (false && "election successor not in active_transactions blocks table");
			}
		}
		roots.erase (root_it);
	}
	if (unconfirmed_count > 0)
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("%1% blocks have been unconfirmed averaging %2% announcements") % unconfirmed_count % (unconfirmed_announcements / unconfirmed_count));
	}
}

void chratos::active_transactions::announce_loop ()
{
	std::unique_lock<std::mutex> lock (mutex);
	started = true;
	condition.notify_all ();
	while (!stopped)
	{
		announce_votes ();
		condition.wait_for (lock, std::chrono::milliseconds (announce_interval_ms + roots.size () * node.network.broadcast_interval_ms));
	}
}

void chratos::active_transactions::stop ()
{
	{
		std::unique_lock<std::mutex> lock (mutex);
		while (!started)
		{
			condition.wait (lock);
		}
		stopped = true;
		roots.clear ();
		condition.notify_all ();
	}
	if (thread.joinable ())
	{
		thread.join ();
	}
}

bool chratos::active_transactions::start (std::shared_ptr<chratos::block> block_a, std::function<void(std::shared_ptr<chratos::block>)> const & confirmation_action_a)
{
	return start (std::make_pair (block_a, nullptr), confirmation_action_a);
}

bool chratos::active_transactions::start (std::pair<std::shared_ptr<chratos::block>, std::shared_ptr<chratos::block>> blocks_a, std::function<void(std::shared_ptr<chratos::block>)> const & confirmation_action_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	return add (blocks_a, confirmation_action_a);
}

bool chratos::active_transactions::add (std::pair<std::shared_ptr<chratos::block>, std::shared_ptr<chratos::block>> blocks_a, std::function<void(std::shared_ptr<chratos::block>)> const & confirmation_action_a)
{
	assert (blocks_a.first != nullptr);
	auto error (true);
	if (!stopped)
	{
		auto primary_block (blocks_a.first);
		auto root (primary_block->root ());
		auto existing (roots.find (root));
		if (existing == roots.end ())
		{
			auto election (std::make_shared<chratos::election> (node, primary_block, confirmation_action_a));
			roots.insert (chratos::conflict_info { root, election, 0, blocks_a });
			successors.insert (std::make_pair (primary_block->hash (), election));
		}
		error = existing != roots.end ();
	}
	return error;
}

// Validate a vote and apply it to the current election if one exists
bool chratos::active_transactions::vote (std::shared_ptr<chratos::vote> vote_a)
{
	std::shared_ptr<chratos::election> election;
	bool replay (false);
	bool processed (false);
	{
		std::lock_guard<std::mutex> lock (mutex);
		for (auto vote_block : vote_a->blocks)
		{
			chratos::election_vote_result result;
			if (vote_block.which ())
			{
				auto block_hash (boost::get<chratos::block_hash> (vote_block));
				auto existing (successors.find (block_hash));
				if (existing != successors.end ())
				{
					result = existing->second->vote (vote_a->account, vote_a->sequence, block_hash);
				}
			}
			else
			{
				auto block (boost::get<std::shared_ptr<chratos::block>> (vote_block));
				auto existing (roots.find (block->root ()));
				if (existing != roots.end ())
				{
					result = existing->election->vote (vote_a->account, vote_a->sequence, block->hash ());
				}
			}
			replay = replay || result.replay;
			processed = processed || result.processed;
		}
	}
	if (processed)
	{
		node.network.republish_vote (vote_a);
	}
	return replay;
}

bool chratos::active_transactions::active (chratos::block const & block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	return roots.find (block_a.root ()) != roots.end ();
}

// List of active blocks in elections
std::deque<std::shared_ptr<chratos::block>> chratos::active_transactions::list_blocks ()
{
	std::deque<std::shared_ptr<chratos::block>> result;
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (roots.begin ()), n (roots.end ()); i != n; ++i)
	{
		result.push_back (i->election->status.winner);
	}
	return result;
}

void chratos::active_transactions::erase (chratos::block const & block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	if (roots.find (block_a.root ()) != roots.end ())
	{
		roots.erase (block_a.root ());
		BOOST_LOG (node.log) << boost::str (boost::format ("Election erased for block block %1% root %2%") % block_a.hash ().to_string () % block_a.root ().to_string ());
	}
}

chratos::active_transactions::active_transactions (chratos::node & node_a) :
node (node_a),
started (false),
stopped (false),
thread ([this]() {
	chratos::thread_role::set (chratos::thread_role::name::announce_loop);
	announce_loop ();
})
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!started)
	{
		condition.wait (lock);
	}
}

chratos::active_transactions::~active_transactions ()
{
	stop ();
}

bool chratos::active_transactions::publish (std::shared_ptr<chratos::block> block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (roots.find (block_a->root ()));
	auto result (true);
	if (existing != roots.end ())
	{
		result = existing->election->publish (block_a);
		if (!result)
		{
			successors.insert (std::make_pair (block_a->hash (), existing->election));
		}
	}
	return result;
}

int chratos::node::store_version ()
{
	auto transaction (store.tx_begin_read ());
	return store.version_get (transaction);
}

chratos::thread_runner::thread_runner (boost::asio::io_service & service_a, unsigned service_threads_a)
{
	boost::thread::attributes attrs;
	chratos::thread_attributes::set (attrs);
	for (auto i (0); i < service_threads_a; ++i)
	{
		threads.push_back (boost::thread (attrs, [&service_a]() {
			chratos::thread_role::set (chratos::thread_role::name::io);
			try
			{
				service_a.run ();
			}
			catch (...)
			{
#ifndef NDEBUG
				/*
         * In a release build, catch and swallow the
         * service exception, in debug mode pass it
         * on
         */
				throw;
#endif
			}
		}));
	}
}

chratos::thread_runner::~thread_runner ()
{
	join ();
}

void chratos::thread_runner::join ()
{
	for (auto & i : threads)
	{
		if (i.joinable ())
		{
			i.join ();
		}
	}
}

chratos::inactive_node::inactive_node (boost::filesystem::path const & path) :
path (path),
service (std::make_shared<boost::asio::io_service> ()),
alarm (*service),
work (1, nullptr)
{
	boost::system::error_code error_chmod;

	/*
	 * @warning May throw a filesystem exception
	 */
	boost::filesystem::create_directories (path);
	chratos::set_secure_perm_directory (path, error_chmod);
	logging.max_size = std::numeric_limits<std::uintmax_t>::max ();
	logging.init (path);
	node = std::make_shared<chratos::node> (init, *service, 24000, path, alarm, logging, work);
}

chratos::inactive_node::~inactive_node ()
{
	node->stop ();
}

chratos::udp_buffer::udp_buffer (chratos::stat & stats, size_t size, size_t count) :
stats (stats),
free (count),
full (count),
slab (size * count),
entries (count),
stopped (false)
{
	assert (count > 0);
	assert (size > 0);
	auto slab_data (slab.data ());
	auto entry_data (entries.data ());
	for (auto i (0); i < count; ++i, ++entry_data)
	{
		*entry_data = { slab_data + i * size, 0, chratos::endpoint () };
		free.push_back (entry_data);
	}
}
chratos::udp_data * chratos::udp_buffer::allocate ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped && free.empty () && full.empty ())
	{
		stats.inc (chratos::stat::type::udp, chratos::stat::detail::blocking, chratos::stat::dir::in);
		condition.wait (lock);
	}
	chratos::udp_data * result (nullptr);
	if (!free.empty ())
	{
		result = free.front ();
		free.pop_front ();
	}
	if (result == nullptr)
	{
		result = full.front ();
		full.pop_front ();
		stats.inc (chratos::stat::type::udp, chratos::stat::detail::overflow, chratos::stat::dir::in);
	}
	return result;
}
void chratos::udp_buffer::enqueue (chratos::udp_data * data_a)
{
	assert (data_a != nullptr);
	std::lock_guard<std::mutex> lock (mutex);
	full.push_back (data_a);
	condition.notify_one ();
}
chratos::udp_data * chratos::udp_buffer::dequeue ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped && full.empty ())
	{
		condition.wait (lock);
	}
	chratos::udp_data * result (nullptr);
	if (!full.empty ())
	{
		result = full.front ();
		full.pop_front ();
	}
	return result;
}
void chratos::udp_buffer::release (chratos::udp_data * data_a)
{
	assert (data_a != nullptr);
	std::lock_guard<std::mutex> lock (mutex);
	free.push_back (data_a);
	condition.notify_one ();
}
void chratos::udp_buffer::stop ()
{
	std::lock_guard<std::mutex> lock (mutex);
	stopped = true;
	condition.notify_all ();
}
