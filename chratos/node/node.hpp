#pragma once

#include <chratos/lib/work.hpp>
#include <chratos/node/bootstrap.hpp>
#include <chratos/node/logging.hpp>
#include <chratos/node/nodeconfig.hpp>
#include <chratos/node/peers.hpp>
#include <chratos/node/portmapping.hpp>
#include <chratos/node/stats.hpp>
#include <chratos/node/voting.hpp>
#include <chratos/node/wallet.hpp>
#include <chratos/secure/ledger.hpp>

#include <condition_variable>

#include <boost/iostreams/device/array.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread/thread.hpp>

namespace chratos
{
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
	void confirm_once (chratos::transaction const &);

public:
	election (chratos::node &, std::shared_ptr<chratos::block>, std::function<void(std::shared_ptr<chratos::block>)> const &);
	chratos::election_vote_result vote (chratos::account, uint64_t, chratos::block_hash);
	chratos::tally_t tally (chratos::transaction const &);
	// Check if we have vote quorum
	bool have_quorum (chratos::tally_t const &, chratos::uint128_t);
	// Change our winner to agree with the network
	void compute_rep_votes (chratos::transaction const &);
	// Confirm this block if quorum is met
	void confirm_if_quorum (chratos::transaction const &);
	void log_votes (chratos::tally_t const &);
	bool publish (std::shared_ptr<chratos::block> block_a);
	void stop ();
	chratos::node & node;
	std::unordered_map<chratos::account, chratos::vote_info> last_votes;
	std::unordered_map<chratos::block_hash, std::shared_ptr<chratos::block>> blocks;
	chratos::block_hash root;
	chratos::election_status status;
	std::atomic<bool> confirmed;
	bool stopped;
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
	bool add (std::pair<std::shared_ptr<chratos::block>, std::shared_ptr<chratos::block>>, std::function<void(std::shared_ptr<chratos::block>)> const & = [](std::shared_ptr<chratos::block>) {});
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
	boost::thread thread;
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
	boost::thread thread;
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
	void add (chratos::transaction const &, std::shared_ptr<chratos::block>);
	void vote (std::shared_ptr<chratos::vote>);
	chratos::uint128_t bootstrap_threshold (chratos::transaction const &);
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
class send_info
{
public:
	uint8_t const * data;
	size_t size;
	chratos::endpoint endpoint;
	std::function<void(boost::system::error_code const &, size_t)> callback;
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
	chratos::uint128_t online_stake_total;
	std::vector<chratos::account> list ();
	boost::multi_index_container<
	chratos::rep_last_heard_info,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<chratos::rep_last_heard_info, std::chrono::steady_clock::time_point, &chratos::rep_last_heard_info::last_heard>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<chratos::rep_last_heard_info, chratos::account, &chratos::rep_last_heard_info::representative>>>>
	reps;

private:
	std::mutex mutex;
	chratos::node & node;
};
class udp_data
{
public:
	uint8_t * buffer;
	size_t size;
	chratos::endpoint endpoint;
};
/**
  * A circular buffer for servicing UDP datagrams. This container follows a producer/consumer model where the operating system is producing data in to buffers which are serviced by internal threads.
  * If buffers are not serviced fast enough they're internally dropped.
  * This container has a maximum space to hold N buffers of M size and will allocate them in round-robin order.
  * All public methods are thread-safe
*/
class udp_buffer
{
public:
	// Size - Size of each individual buffer
	// Count - Number of buffers to allocate
	// Stats - Statistics
	udp_buffer (chratos::stat & stats, size_t, size_t);
	// Return a buffer where UDP data can be put
	// Method will attempt to return the first free buffer
	// If there are no free buffers, an unserviced buffer will be dequeued and returned
	// Function will block if there are no free or unserviced buffers
	// Return nullptr if the container has stopped
	chratos::udp_data * allocate ();
	// Queue a buffer that has been filled with UDP data and notify servicing threads
	void enqueue (chratos::udp_data *);
	// Return a buffer that has been filled with UDP data
	// Function will block until a buffer has been added
	// Return nullptr if the container has stopped
	chratos::udp_data * dequeue ();
	// Return a buffer to the freelist after is has been serviced
	void release (chratos::udp_data *);
	// Stop container and notify waiting threads
	void stop ();

private:
	chratos::stat & stats;
	std::mutex mutex;
	std::condition_variable condition;
	boost::circular_buffer<chratos::udp_data *> free;
	boost::circular_buffer<chratos::udp_data *> full;
	std::vector<uint8_t> slab;
	std::vector<chratos::udp_data> entries;
	bool stopped;
};
class network
{
public:
	network (chratos::node &, uint16_t);
	~network ();
	void receive ();
	void process_packets ();
	void start ();
	void stop ();
	void receive_action (chratos::udp_data *);
	void rpc_action (boost::system::error_code const &, size_t);
	void republish_vote (std::shared_ptr<chratos::vote>);
	void republish_block (std::shared_ptr<chratos::block>);
	static unsigned const broadcast_interval_ms = (chratos::chratos_network == chratos::chratos_networks::chratos_test_network) ? 10 : 50;
	void republish_block_batch (std::deque<std::shared_ptr<chratos::block>>, unsigned = broadcast_interval_ms);
	void republish (chratos::block_hash const &, std::shared_ptr<std::vector<uint8_t>>, chratos::endpoint);
	void publish_broadcast (std::vector<chratos::peer_information> &, std::unique_ptr<chratos::block>);
	void confirm_send (chratos::confirm_ack const &, std::shared_ptr<std::vector<uint8_t>>, chratos::endpoint const &);
	void merge_peers (std::array<chratos::endpoint, 8> const &);
	void send_keepalive (chratos::endpoint const &);
	void send_node_id_handshake (chratos::endpoint const &, boost::optional<chratos::uint256_union> const & query, boost::optional<chratos::uint256_union> const & respond_to);
	void broadcast_confirm_req (std::shared_ptr<chratos::block>);
	void broadcast_confirm_req_base (std::shared_ptr<chratos::block>, std::shared_ptr<std::vector<chratos::peer_information>>, unsigned, bool = false);
	void send_confirm_req (chratos::endpoint const &, std::shared_ptr<chratos::block>);
	void send_buffer (uint8_t const *, size_t, chratos::endpoint const &, std::function<void(boost::system::error_code const &, size_t)>);
	chratos::endpoint endpoint ();
	chratos::udp_buffer buffer_container;
	boost::asio::ip::udp::socket socket;
	std::mutex socket_mutex;
	boost::asio::ip::udp::resolver resolver;
	std::vector<boost::thread> packet_processing_threads;
	chratos::node & node;
	bool on;
	static uint16_t const node_port = chratos::chratos_network == chratos::chratos_networks::chratos_live_network ? 9125 : 44000;
	static size_t const buffer_size = 512;
};

class node_init
{
public:
	node_init ();
	bool error ();
	bool block_store_init;
	bool wallet_init;
};
class node_observers
{
public:
	chratos::observer_set<std::shared_ptr<chratos::block>, chratos::account const &, chratos::uint128_t const &, bool> blocks;
	chratos::observer_set<bool> wallet;
	chratos::observer_set<chratos::transaction const &, std::shared_ptr<chratos::vote>, chratos::endpoint const &> vote;
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
	chratos::vote_code vote_blocking (chratos::transaction const &, std::shared_ptr<chratos::vote>, chratos::endpoint);
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
	boost::thread thread;
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
	chratos::process_return process_receive_one (chratos::transaction const &, std::shared_ptr<chratos::block>, std::chrono::steady_clock::time_point = std::chrono::steady_clock::now (), bool = false);

private:
	void queue_unchecked (chratos::transaction const &, chratos::block_hash const &);
	void process_receive_many (std::unique_lock<std::mutex> &);
	void verify_state_blocks (std::unique_lock<std::mutex> &);
	bool stopped;
	bool active;
	std::chrono::steady_clock::time_point next_log;
	std::deque<std::pair<std::shared_ptr<chratos::block>, std::chrono::steady_clock::time_point>> blocks;
	std::deque<std::pair<std::shared_ptr<chratos::block>, std::chrono::steady_clock::time_point>> state_blocks;
	std::unordered_set<chratos::block_hash> blocks_hashes;
	std::deque<std::shared_ptr<chratos::block>> forced;
	std::condition_variable condition;
	chratos::node & node;
	chratos::vote_generator generator;
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
	void search_pending ();
	int price (chratos::uint128_t const &, int);
	void work_generate_blocking (chratos::block &);
	uint64_t work_generate_blocking (chratos::uint256_union const &);
	void work_generate (chratos::uint256_union const &, std::function<void(uint64_t)>);
	void add_initial_peers ();
	void block_confirm (std::shared_ptr<chratos::block>);
	void process_fork (chratos::transaction const &, std::shared_ptr<chratos::block>);
	bool validate_block_by_previous (chratos::transaction const &, std::shared_ptr<chratos::block>);
	void process_dividend_fork (chratos::transaction const &, std::shared_ptr<chratos::block>);
	chratos::uint128_t delta ();
	boost::asio::io_service & service;
	chratos::node_config config;
	chratos::alarm & alarm;
	chratos::work_pool & work;
	boost::log::sources::logger_mt log;
	std::unique_ptr<chratos::block_store> store_impl;
	chratos::block_store & store;
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
	boost::thread block_processor_thread;
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
	static std::chrono::seconds constexpr search_pending_interval = (chratos::chratos_network == chratos::chratos_networks::chratos_test_network) ? std::chrono::seconds (1) : std::chrono::seconds (5 * 60);
};
class thread_runner
{
public:
	thread_runner (boost::asio::io_service &, unsigned);
	~thread_runner ();
	void join ();
	std::vector<boost::thread> threads;
};
class inactive_node
{
public:
	inactive_node (boost::filesystem::path const & path = chratos::working_path ());
	~inactive_node ();
	boost::filesystem::path path;
	std::shared_ptr<boost::asio::io_service> service;
	chratos::alarm alarm;
	chratos::logging logging;
	chratos::node_init init;
	chratos::work_pool work;
	std::shared_ptr<chratos::node> node;
};
}
