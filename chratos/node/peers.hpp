#pragma once

#include <boost/asio/ip/address.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/optional.hpp>
#include <chratos/lib/numbers.hpp>
#include <chratos/node/common.hpp>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace chratos
{
chratos::endpoint map_endpoint_to_v6 (chratos::endpoint const &);

/** Multi-index helper */
class peer_by_ip_addr
{
};

/** Multi-index helper */
class peer_attempt
{
public:
	chratos::endpoint endpoint;
	std::chrono::steady_clock::time_point last_attempt;
};

/** Node handshake cookie */
class syn_cookie_info
{
public:
	chratos::uint256_union cookie;
	std::chrono::steady_clock::time_point created_at;
};

/** Collects peer contact information */
class peer_information
{
public:
	peer_information (chratos::endpoint const &, unsigned);
	peer_information (chratos::endpoint const &, std::chrono::steady_clock::time_point const &, std::chrono::steady_clock::time_point const &);
	chratos::endpoint endpoint;
	boost::asio::ip::address ip_address;
	std::chrono::steady_clock::time_point last_contact;
	std::chrono::steady_clock::time_point last_attempt;
	std::chrono::steady_clock::time_point last_bootstrap_attempt;
	std::chrono::steady_clock::time_point last_rep_request;
	std::chrono::steady_clock::time_point last_rep_response;
	chratos::amount rep_weight;
	chratos::account probable_rep_account;
	unsigned network_version;
	boost::optional<chratos::account> node_id;
};

/** Manages a set of disovered peers */
class peer_container
{
public:
	peer_container (chratos::endpoint const &);
	// We were contacted by endpoint, update peers
	// Returns true if a Node ID handshake should begin
	bool contacted (chratos::endpoint const &, unsigned);
	// Unassigned, reserved, self
	bool not_a_peer (chratos::endpoint const &, bool);
	// Returns true if peer was already known
	bool known_peer (chratos::endpoint const &);
	// Notify of peer we received from
	bool insert (chratos::endpoint const &, unsigned);
	std::unordered_set<chratos::endpoint> random_set (size_t);
	void random_fill (std::array<chratos::endpoint, 8> &);
	// Request a list of the top known representatives
	std::vector<peer_information> representatives (size_t);
	// List of all peers
	std::deque<chratos::endpoint> list ();
	std::map<chratos::endpoint, unsigned> list_version ();
	std::vector<peer_information> list_vector ();
	// A list of random peers sized for the configured rebroadcast fanout
	std::deque<chratos::endpoint> list_fanout ();
	// Get the next peer for attempting bootstrap
	chratos::endpoint bootstrap_peer ();
	// Purge any peer where last_contact < time_point and return what was left
	std::vector<chratos::peer_information> purge_list (std::chrono::steady_clock::time_point const &);
	void purge_syn_cookies (std::chrono::steady_clock::time_point const &);
	std::vector<chratos::endpoint> rep_crawl ();
	bool rep_response (chratos::endpoint const &, chratos::account const &, chratos::amount const &);
	void rep_request (chratos::endpoint const &);
	// Should we reach out to this endpoint with a keepalive message
	bool reachout (chratos::endpoint const &);
	// Returns boost::none if the IP is rate capped on syn cookie requests,
	// or if the endpoint already has a syn cookie query
	boost::optional<chratos::uint256_union> assign_syn_cookie (chratos::endpoint const &);
	// Returns false if valid, true if invalid (true on error convention)
	// Also removes the syn cookie from the store if valid
	bool validate_syn_cookie (chratos::endpoint const &, chratos::account, chratos::signature);
	size_t size ();
	size_t size_sqrt ();
	chratos::uint128_t total_weight ();
	chratos::uint128_t online_weight_minimum;
	bool empty ();
	std::mutex mutex;
	chratos::endpoint self;
	boost::multi_index_container<
	peer_information,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<boost::multi_index::member<peer_information, chratos::endpoint, &peer_information::endpoint>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_contact>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_attempt>, std::greater<std::chrono::steady_clock::time_point>>,
	boost::multi_index::random_access<>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_bootstrap_attempt>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_rep_request>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, chratos::amount, &peer_information::rep_weight>, std::greater<chratos::amount>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::tag<peer_by_ip_addr>, boost::multi_index::member<peer_information, boost::asio::ip::address, &peer_information::ip_address>>>>
	peers;
	boost::multi_index_container<
	peer_attempt,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<boost::multi_index::member<peer_attempt, chratos::endpoint, &peer_attempt::endpoint>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_attempt, std::chrono::steady_clock::time_point, &peer_attempt::last_attempt>>>>
	attempts;
	std::mutex syn_cookie_mutex;
	std::unordered_map<chratos::endpoint, syn_cookie_info> syn_cookies;
	std::unordered_map<boost::asio::ip::address, unsigned> syn_cookies_per_ip;
	// Number of peers that don't support node ID
	size_t legacy_peers;
	// Called when a new peer is observed
	std::function<void(chratos::endpoint const &)> peer_observer;
	std::function<void()> disconnect_observer;
	// Number of peers to crawl for being a rep every period
	static size_t constexpr peers_per_crawl = 8;
	// Maximum number of peers per IP (includes legacy peers)
	static size_t constexpr max_peers_per_ip = 10;
	// Maximum number of legacy peers per IP
	static size_t constexpr max_legacy_peers_per_ip = 5;
	// Maximum number of peers that don't support node ID
	static size_t constexpr max_legacy_peers = 500;
};
}
