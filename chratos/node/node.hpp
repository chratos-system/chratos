#pragma once

#include <chratos/lib/work.hpp>
#include <chratos/node/bootstrap.hpp>
#include <chratos/node/stats.hpp>
#include <chratos/node/wallet.hpp>
#include <chratos/secure/ledger.hpp>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>

#include <boost/asio.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/log/trivial.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <miniupnpc.h>

namespace boost
{
namespace program_options
{
	class options_description;
	class variables_map;
}
}

namespace chratos
{
chratos::endpoint map_endpoint_to_v6 (chratos::endpoint const &);
class node;
class election_status
{
public:
	std::shared_ptr<chratos::block> winner;
	chratos::amount tally;
};
class vote_info
{
public:
	std::chrono::steady_clock::time_point time;
	uint64_t sequence;
	chratos::block_hash hash;
};
class election_vote_result
{
public:
	election_vote_result ();
	election_vote_result (bool, bool);
	bool replay;
	bool processed;
};
class election : public std::enable_shared_from_this<chratos::election>
{
	std::function<void(std::shared_ptr<chratos::block>)> confirmation_action;
	void confirm_once (MDB_txn *);

public:
	election (chratos::node &, std::shared_ptr<chratos::block>, std::function<void(std::shared_ptr<chratos::block>)> const &);
	chratos::election_vote_result vote (chratos::account, uint64_t, chratos::block_hash);
	chratos::tally_t tally (MDB_txn *);
	// Check if we have vote quorum
	bool have_quorum (chratos::tally_t const &);
	// Change our winner to agree with the network
	void compute_rep_votes (MDB_txn *);
	// Confirm this block if quorum is met
	void confirm_if_quorum (MDB_txn *);
	void log_votes (chratos::tally_t const &);
	bool publish (std::shared_ptr<chratos::block> block_a);
	void abort ();
	chratos::node & node;
	std::unordered_map<chratos::account, chratos::vote_info> last_votes;
	std::unordered_map<chratos::block_hash, std::shared_ptr<chratos::block>> blocks;
	chratos::block_hash root;
	chratos::election_status status;
	std::atomic<bool> confirmed;
	bool aborted;
	std::unordered_map<chratos::block_hash, chratos::uint128_t> last_tally;
};
class conflict_info
{
public:
	chratos::block_hash root;
	std::shared_ptr<chratos::election> election;
	// Number of announcements in a row for this fork
	unsigned announcements;
	std::pair<std::shared_ptr<chratos::block>, std::shared_ptr<chratos::block>> confirm_req_options;
};
// Core class for determining consensus
// Holds all active blocks i.e. recently added blocks that need confirmation
class active_transactions
{
public:
	active_transactions (chratos::node &);
	~active_transactions ();
	// Start an election for a block
	// Call action with confirmed block, may be different than what we started with
	bool start (std::shared_ptr<chratos::block>, std::function<void(std::shared_ptr<chratos::block>)> const & = [](std::shared_ptr<chratos::block>) {});
	// Also supply alternatives to block, to confirm_req reps with if the boolean argument is true
	// Should only be used for old elections
	// The first block should be the one in the ledger
	bool start (std::pair<std::shared_ptr<chratos::block>, std::shared_ptr<chratos::block>>, std::function<void(std::shared_ptr<chratos::block>)> const & = [](std::shared_ptr<chratos::block>) {});
	// If this returns true, the vote is a replay
	// If this returns false, the vote may or may not be a replay
	bool vote (std::shared_ptr<chratos::vote>);
	// Is the root of this block in the roots container
	bool active (chratos::block const &);
	std::deque<std::shared_ptr<chratos::block>> list_blocks ();
	void erase (chratos::block const &);
	void stop ();
	bool publish (std::shared_ptr<chratos::block> block_a);
	boost::multi_index_container<
	chratos::conflict_info,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<boost::multi_index::member<chratos::conflict_info, chratos::block_hash, &chratos::conflict_info::root>>>>
	roots;
	std::unordered_map<chratos::block_hash, std::shared_ptr<chratos::election>> successors;
	std::deque<chratos::election_status> confirmed;
	chratos::node & node;
	std::mutex mutex;
	// Maximum number of conflicts to vote on per interval, lowest root hash first
	static unsigned constexpr announcements_per_interval = 32;
	// Minimum number of block announcements
	static unsigned constexpr announcement_min = 2;
	// Threshold to start logging blocks haven't yet been confirmed
	static unsigned constexpr announcement_long = 20;
	static unsigned constexpr announce_interval_ms = (chratos::chratos_network == chratos::chratos_networks::chratos_test_network) ? 10 : 16000;
	static size_t constexpr election_history_size = 2048;

private:
	void announce_loop ();
	void announce_votes ();
	std::condition_variable condition;
	bool started;
	bool stopped;
	std::thread thread;
};
class operation
{
public:
	bool operator> (chratos::operation const &) const;
	std::chrono::steady_clock::time_point wakeup;
	std::function<void()> function;
};
class alarm
{
public:
	alarm (boost::asio::io_service &);
	~alarm ();
	void add (std::chrono::steady_clock::time_point const &, std::function<void()> const &);
	void run ();
	boost::asio::io_service & service;
	std::mutex mutex;
	std::condition_variable condition;
	std::priority_queue<operation, std::vector<operation>, std::greater<operation>> operations;
	std::thread thread;
};
class gap_information
{
public:
	std::chrono::steady_clock::time_point arrival;
	chratos::block_hash hash;
	std::unordered_set<chratos::account> voters;
};
class gap_cache
{
public:
	gap_cache (chratos::node &);
	void add (MDB_txn *, std::shared_ptr<chratos::block>);
	void vote (std::shared_ptr<chratos::vote>);
	chratos::uint128_t bootstrap_threshold (MDB_txn *);
	void purge_old ();
	boost::multi_index_container<
	chratos::gap_information,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<gap_information, std::chrono::steady_clock::time_point, &gap_information::arrival>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<gap_information, chratos::block_hash, &gap_information::hash>>>>
	blocks;
	size_t const max = 256;
	std::mutex mutex;
	chratos::node & node;
};
class work_pool;
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
class peer_attempt
{
public:
	chratos::endpoint endpoint;
	std::chrono::steady_clock::time_point last_attempt;
};
class syn_cookie_info
{
public:
	chratos::uint256_union cookie;
	std::chrono::steady_clock::time_point created_at;
};
class peer_by_ip_addr
{
};
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
class send_info
{
public:
	uint8_t const * data;
	size_t size;
	chratos::endpoint endpoint;
	std::function<void(boost::system::error_code const &, size_t)> callback;
};
class mapping_protocol
{
public:
	char const * name;
	int remaining;
	boost::asio::ip::address_v4 external_address;
	uint16_t external_port;
};
// These APIs aren't easy to understand so comments are verbose
class port_mapping
{
public:
	port_mapping (chratos::node &);
	void start ();
	void stop ();
	void refresh_devices ();
	// Refresh when the lease ends
	void refresh_mapping ();
	// Refresh occasionally in case router loses mapping
	void check_mapping_loop ();
	int check_mapping ();
	bool has_address ();
	std::mutex mutex;
	chratos::node & node;
	UPNPDev * devices; // List of all UPnP devices
	UPNPUrls urls; // Something for UPnP
	IGDdatas data; // Some other UPnP thing
	// Primes so they infrequently happen at the same time
	static int constexpr mapping_timeout = chratos::chratos_network == chratos::chratos_networks::chratos_test_network ? 53 : 3593;
	static int constexpr check_timeout = chratos::chratos_network == chratos::chratos_networks::chratos_test_network ? 17 : 53;
	boost::asio::ip::address_v4 address;
	std::array<mapping_protocol, 2> protocols;
	uint64_t check_count;
	bool on;
};
class block_arrival_info
{
public:
	std::chrono::steady_clock::time_point arrival;
	chratos::block_hash hash;
};
// This class tracks blocks that are probably live because they arrived in a UDP packet
// This gives a fairly reliable way to differentiate between blocks being inserted via bootstrap or new, live blocks.
class block_arrival
{
public:
	// Return `true' to indicated an error if the block has already been inserted
	bool add (chratos::block_hash const &);
	bool recent (chratos::block_hash const &);
	boost::multi_index_container<
	chratos::block_arrival_info,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<chratos::block_arrival_info, std::chrono::steady_clock::time_point, &chratos::block_arrival_info::arrival>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<chratos::block_arrival_info, chratos::block_hash, &chratos::block_arrival_info::hash>>>>
	arrival;
	std::mutex mutex;
	static size_t constexpr arrival_size_min = 8 * 1024;
	static std::chrono::seconds constexpr arrival_time_min = std::chrono::seconds (300);
};
class rep_last_heard_info
{
public:
	std::chrono::steady_clock::time_point last_heard;
	chratos::account representative;
};
class online_reps
{
public:
	online_reps (chratos::node &);
	void vote (std::shared_ptr<chratos::vote> const &);
	void recalculate_stake ();
	chratos::uint128_t online_stake ();
	std::deque<chratos::account> list ();
	boost::multi_index_container<
	chratos::rep_last_heard_info,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<chratos::rep_last_heard_info, std::chrono::steady_clock::time_point, &chratos::rep_last_heard_info::last_heard>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<chratos::rep_last_heard_info, chratos::account, &chratos::rep_last_heard_info::representative>>>>
	reps;

private:
	chratos::uint128_t online_stake_total;
	std::mutex mutex;
	chratos::node & node;
};
class network
{
public:
	network (chratos::node &, uint16_t);
	void receive ();
	void stop ();
	void receive_action (boost::system::error_code const &, size_t);
	void rpc_action (boost::system::error_code const &, size_t);
	void republish_vote (std::shared_ptr<chratos::vote>);
	void republish_block (MDB_txn *, std::shared_ptr<chratos::block>, bool = true);
	void republish (chratos::block_hash const &, std::shared_ptr<std::vector<uint8_t>>, chratos::endpoint);
	void publish_broadcast (std::vector<chratos::peer_information> &, std::unique_ptr<chratos::block>);
	void confirm_send (chratos::confirm_ack const &, std::shared_ptr<std::vector<uint8_t>>, chratos::endpoint const &);
	void merge_peers (std::array<chratos::endpoint, 8> const &);
	void send_keepalive (chratos::endpoint const &);
	void send_node_id_handshake (chratos::endpoint const &, boost::optional<chratos::uint256_union> const & query, boost::optional<chratos::uint256_union> const & respond_to);
	void broadcast_confirm_req (std::shared_ptr<chratos::block>);
	void broadcast_confirm_req_base (std::shared_ptr<chratos::block>, std::shared_ptr<std::vector<chratos::peer_information>>, unsigned);
	void send_confirm_req (chratos::endpoint const &, std::shared_ptr<chratos::block>);
	void send_buffer (uint8_t const *, size_t, chratos::endpoint const &, std::function<void(boost::system::error_code const &, size_t)>);
	chratos::endpoint endpoint ();
	chratos::endpoint remote;
	std::array<uint8_t, 512> buffer;
	boost::asio::ip::udp::socket socket;
	std::mutex socket_mutex;
	boost::asio::ip::udp::resolver resolver;
	chratos::node & node;
	bool on;
	static uint16_t const node_port = chratos::chratos_network == chratos::chratos_networks::chratos_live_network ? 9125 : 44000;
};
class logging
{
public:
	logging ();
	void serialize_json (boost::property_tree::ptree &) const;
	bool deserialize_json (bool &, boost::property_tree::ptree &);
	bool upgrade_json (unsigned, boost::property_tree::ptree &);
	bool ledger_logging () const;
	bool ledger_duplicate_logging () const;
	bool vote_logging () const;
	bool network_logging () const;
	bool network_message_logging () const;
	bool network_publish_logging () const;
	bool network_packet_logging () const;
	bool network_keepalive_logging () const;
	bool network_node_id_handshake_logging () const;
	bool node_lifetime_tracing () const;
	bool insufficient_work_logging () const;
	bool log_rpc () const;
	bool bulk_pull_logging () const;
	bool callback_logging () const;
	bool work_generation_time () const;
	bool log_to_cerr () const;
	void init (boost::filesystem::path const &);

	bool ledger_logging_value;
	bool ledger_duplicate_logging_value;
	bool vote_logging_value;
	bool network_logging_value;
	bool network_message_logging_value;
	bool network_publish_logging_value;
	bool network_packet_logging_value;
	bool network_keepalive_logging_value;
	bool network_node_id_handshake_logging_value;
	bool node_lifetime_tracing_value;
	bool insufficient_work_logging_value;
	bool log_rpc_value;
	bool bulk_pull_logging_value;
	bool work_generation_time_value;
	bool log_to_cerr_value;
	bool flush;
	uintmax_t max_size;
	uintmax_t rotation_size;
	boost::log::sources::logger_mt log;
};
class node_init
{
public:
	node_init ();
	bool error ();
	bool block_store_init;
	bool wallet_init;
};
class node_config
{
public:
	node_config ();
	node_config (uint16_t, chratos::logging const &);
	void serialize_json (boost::property_tree::ptree &) const;
	bool deserialize_json (bool &, boost::property_tree::ptree &);
	bool upgrade_json (unsigned, boost::property_tree::ptree &);
	chratos::account random_representative ();
	uint16_t peering_port;
	chratos::logging logging;
	std::vector<std::pair<std::string, uint16_t>> work_peers;
	std::vector<std::string> preconfigured_peers;
	std::vector<chratos::account> preconfigured_representatives;
	unsigned bootstrap_fraction_numerator;
	chratos::amount receive_minimum;
	chratos::amount online_weight_minimum;
  chratos::amount dividend_minimum;
	unsigned online_weight_quorum;
	unsigned password_fanout;
	unsigned io_threads;
	unsigned work_threads;
	bool enable_voting;
	unsigned bootstrap_connections;
	unsigned bootstrap_connections_max;
	std::string callback_address;
	uint16_t callback_port;
	std::string callback_target;
	int lmdb_max_dbs;
	chratos::stat_config stat_config;
	chratos::uint256_union epoch_block_link;
	chratos::account epoch_block_signer;
	std::chrono::system_clock::time_point generate_hash_votes_at;
	static std::chrono::seconds constexpr keepalive_period = std::chrono::seconds (60);
	static std::chrono::seconds constexpr keepalive_cutoff = keepalive_period * 5;
	static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
};
class node_observers
{
public:
	chratos::observer_set<std::shared_ptr<chratos::block>, chratos::account const &, chratos::uint128_t const &, bool> blocks;
	chratos::observer_set<bool> wallet;
	chratos::observer_set<std::shared_ptr<chratos::vote>, chratos::endpoint const &> vote;
	chratos::observer_set<chratos::account const &, bool> account_balance;
	chratos::observer_set<chratos::endpoint const &> endpoint;
	chratos::observer_set<> disconnect;
	chratos::observer_set<> started;
};
class vote_processor
{
public:
	vote_processor (chratos::node &);
	void vote (std::shared_ptr<chratos::vote>, chratos::endpoint);
	chratos::vote_code vote_blocking (MDB_txn *, std::shared_ptr<chratos::vote>, chratos::endpoint);
	void flush ();
	chratos::node & node;
	void stop ();

private:
	void process_loop ();
	std::deque<std::pair<std::shared_ptr<chratos::vote>, chratos::endpoint>> votes;
	std::condition_variable condition;
	std::mutex mutex;
	bool started;
	bool stopped;
	bool active;
	std::thread thread;
};
// The network is crawled for representatives by occasionally sending a unicast confirm_req for a specific block and watching to see if it's acknowledged with a vote.
class rep_crawler
{
public:
	void add (chratos::block_hash const &);
	void remove (chratos::block_hash const &);
	bool exists (chratos::block_hash const &);
	std::mutex mutex;
	std::unordered_set<chratos::block_hash> active;
};
// Processing blocks is a potentially long IO operation
// This class isolates block insertion from other operations like servicing network operations
class block_processor
{
public:
	block_processor (chratos::node &);
	~block_processor ();
	void stop ();
	void flush ();
	bool full ();
	void add (std::shared_ptr<chratos::block>, std::chrono::steady_clock::time_point);
	void force (std::shared_ptr<chratos::block>);
	bool should_log ();
	bool have_blocks ();
	void process_blocks ();
	chratos::process_return process_receive_one (MDB_txn *, std::shared_ptr<chratos::block>, std::chrono::steady_clock::time_point = std::chrono::steady_clock::now ());

private:
	void queue_unchecked (MDB_txn *, chratos::block_hash const &);
	void process_receive_many (std::unique_lock<std::mutex> &);
	bool stopped;
	bool active;
	std::chrono::steady_clock::time_point next_log;
	std::deque<std::pair<std::shared_ptr<chratos::block>, std::chrono::steady_clock::time_point>> blocks;
	std::unordered_set<chratos::block_hash> blocks_hashes;
	std::deque<std::shared_ptr<chratos::block>> forced;
	std::condition_variable condition;
	chratos::node & node;
	std::mutex mutex;
};
class node : public std::enable_shared_from_this<chratos::node>
{
public:
	node (chratos::node_init &, boost::asio::io_service &, uint16_t, boost::filesystem::path const &, chratos::alarm &, chratos::logging const &, chratos::work_pool &);
	node (chratos::node_init &, boost::asio::io_service &, boost::filesystem::path const &, chratos::alarm &, chratos::node_config const &, chratos::work_pool &);
	~node ();
	template <typename T>
	void background (T action_a)
	{
		alarm.service.post (action_a);
	}
	void send_keepalive (chratos::endpoint const &);
	bool copy_with_compaction (boost::filesystem::path const &);
	void keepalive (std::string const &, uint16_t);
	void start ();
	void stop ();
	std::shared_ptr<chratos::node> shared ();
	int store_version ();
	void process_confirmed (std::shared_ptr<chratos::block>);
	void process_message (chratos::message &, chratos::endpoint const &);
	void process_active (std::shared_ptr<chratos::block>);
	chratos::process_return process (chratos::block const &);
	void keepalive_preconfigured (std::vector<std::string> const &);
	chratos::block_hash latest (chratos::account const &);
	chratos::uint128_t balance (chratos::account const &);
	std::unique_ptr<chratos::block> block (chratos::block_hash const &);
	std::pair<chratos::uint128_t, chratos::uint128_t> balance_pending (chratos::account const &);
	chratos::uint128_t weight (chratos::account const &);
	chratos::account representative (chratos::account const &);
	void ongoing_keepalive ();
	void ongoing_syn_cookie_cleanup ();
	void ongoing_rep_crawl ();
	void ongoing_bootstrap ();
	void ongoing_store_flush ();
	void backup_wallet ();
	int price (chratos::uint128_t const &, int);
	void work_generate_blocking (chratos::block &);
	uint64_t work_generate_blocking (chratos::uint256_union const &);
	void work_generate (chratos::uint256_union const &, std::function<void(uint64_t)>);
	void add_initial_peers ();
	void block_confirm (std::shared_ptr<chratos::block>);
	void process_fork (MDB_txn *, std::shared_ptr<chratos::block>);
	chratos::uint128_t delta ();
	boost::asio::io_service & service;
	chratos::node_config config;
	chratos::alarm & alarm;
	chratos::work_pool & work;
	boost::log::sources::logger_mt log;
	chratos::block_store store;
	chratos::gap_cache gap_cache;
	chratos::ledger ledger;
	chratos::active_transactions active;
	chratos::network network;
	chratos::bootstrap_initiator bootstrap_initiator;
	chratos::bootstrap_listener bootstrap;
	chratos::peer_container peers;
	boost::filesystem::path application_path;
	chratos::node_observers observers;
	chratos::wallets wallets;
	chratos::port_mapping port_mapping;
	chratos::vote_processor vote_processor;
	chratos::rep_crawler rep_crawler;
	unsigned warmed_up;
	chratos::block_processor block_processor;
	std::thread block_processor_thread;
	chratos::block_arrival block_arrival;
	chratos::online_reps online_reps;
	chratos::stat stats;
	chratos::keypair node_id;
	static double constexpr price_max = 16.0;
	static double constexpr free_cutoff = 1024.0;
	static std::chrono::seconds constexpr period = std::chrono::seconds (60);
	static std::chrono::seconds constexpr cutoff = period * 5;
	static std::chrono::seconds constexpr syn_cookie_cutoff = std::chrono::seconds (5);
	static std::chrono::minutes constexpr backup_interval = std::chrono::minutes (5);
};
class thread_runner
{
public:
	thread_runner (boost::asio::io_service &, unsigned);
	~thread_runner ();
	void join ();
	std::vector<std::thread> threads;
};
class inactive_node
{
public:
	inactive_node (boost::filesystem::path const & path = chratos::working_path ());
	~inactive_node ();
	boost::filesystem::path path;
	boost::shared_ptr<boost::asio::io_service> service;
	chratos::alarm alarm;
	chratos::logging logging;
	chratos::node_init init;
	chratos::work_pool work;
	std::shared_ptr<chratos::node> node;
};
}