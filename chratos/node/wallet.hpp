#pragma once

#include <chratos/node/common.hpp>
#include <chratos/node/openclwork.hpp>
#include <chratos/secure/blockstore.hpp>
#include <chratos/secure/common.hpp>

#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>

namespace chratos
{
// The fan spreads a key out over the heap to decrease the likelihood of it being recovered by memory inspection
class fan
{
public:
	fan (chratos::uint256_union const &, size_t);
	void value (chratos::raw_key &);
	void value_set (chratos::raw_key const &);
	std::vector<std::unique_ptr<chratos::uint256_union>> values;

private:
	std::mutex mutex;
	void value_get (chratos::raw_key &);
};
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (chratos::mdb_val const &);
	wallet_value (chratos::uint256_union const &, uint64_t);
	chratos::mdb_val val () const;
	chratos::private_key key;
	uint64_t work;
};
class node_config;
class kdf
{
public:
	void phs (chratos::raw_key &, std::string const &, chratos::uint256_union const &);
	std::mutex mutex;
};
enum class key_type
{
	not_a_type,
	unknown,
	adhoc,
	deterministic
};
class wallet_store
{
public:
	wallet_store (bool &, chratos::kdf &, chratos::transaction &, chratos::account, unsigned, std::string const &);
	wallet_store (bool &, chratos::kdf &, chratos::transaction &, chratos::account, unsigned, std::string const &, std::string const &);
	std::vector<chratos::account> accounts (MDB_txn *);
	void initialize (MDB_txn *, bool &, std::string const &);
	chratos::uint256_union check (MDB_txn *);
	bool rekey (MDB_txn *, std::string const &);
	bool valid_password (MDB_txn *);
	bool attempt_password (MDB_txn *, std::string const &);
	void wallet_key (chratos::raw_key &, MDB_txn *);
	void seed (chratos::raw_key &, MDB_txn *);
	void seed_set (MDB_txn *, chratos::raw_key const &);
	chratos::key_type key_type (chratos::wallet_value const &);
	chratos::public_key deterministic_insert (MDB_txn *);
	void deterministic_key (chratos::raw_key &, MDB_txn *, uint32_t);
	uint32_t deterministic_index_get (MDB_txn *);
	void deterministic_index_set (MDB_txn *, uint32_t);
	void deterministic_clear (MDB_txn *);
	chratos::uint256_union salt (MDB_txn *);
	bool is_representative (MDB_txn *);
	chratos::account representative (MDB_txn *);
	void representative_set (MDB_txn *, chratos::account const &);
	chratos::public_key insert_adhoc (MDB_txn *, chratos::raw_key const &);
	void insert_watch (MDB_txn *, chratos::public_key const &);
	void erase (MDB_txn *, chratos::public_key const &);
	chratos::wallet_value entry_get_raw (MDB_txn *, chratos::public_key const &);
	void entry_put_raw (MDB_txn *, chratos::public_key const &, chratos::wallet_value const &);
	bool fetch (MDB_txn *, chratos::public_key const &, chratos::raw_key &);
	bool exists (MDB_txn *, chratos::public_key const &);
	void destroy (MDB_txn *);
	chratos::store_iterator<chratos::uint256_union, chratos::wallet_value> find (MDB_txn *, chratos::uint256_union const &);
	chratos::store_iterator<chratos::uint256_union, chratos::wallet_value> begin (MDB_txn *, chratos::uint256_union const &);
	chratos::store_iterator<chratos::uint256_union, chratos::wallet_value> begin (MDB_txn *);
	chratos::store_iterator<chratos::uint256_union, chratos::wallet_value> end ();
	void derive_key (chratos::raw_key &, MDB_txn *, std::string const &);
	void serialize_json (MDB_txn *, std::string &);
	void write_backup (MDB_txn *, boost::filesystem::path const &);
	bool move (MDB_txn *, chratos::wallet_store &, std::vector<chratos::public_key> const &);
	bool import (MDB_txn *, chratos::wallet_store &);
	bool work_get (MDB_txn *, chratos::public_key const &, uint64_t &);
	void work_put (MDB_txn *, chratos::public_key const &, uint64_t);
	unsigned version (MDB_txn *);
	void version_put (MDB_txn *, unsigned);
	void upgrade_v1_v2 ();
	void upgrade_v2_v3 ();
	void upgrade_v3_v4 ();
	chratos::fan password;
	chratos::fan wallet_key_mem;
	static unsigned const version_1 = 1;
	static unsigned const version_2 = 2;
	static unsigned const version_3 = 3;
	static unsigned const version_4 = 4;
	unsigned const version_current = version_4;
	static chratos::uint256_union const version_special;
	static chratos::uint256_union const wallet_key_special;
	static chratos::uint256_union const salt_special;
	static chratos::uint256_union const check_special;
	static chratos::uint256_union const representative_special;
	static chratos::uint256_union const seed_special;
	static chratos::uint256_union const deterministic_index_special;
	static size_t const check_iv_index;
	static size_t const seed_iv_index;
	static int const special_count;
	static unsigned const kdf_full_work = 64 * 1024;
	static unsigned const kdf_test_work = 8;
	static unsigned const kdf_work = chratos::chratos_network == chratos::chratos_networks::chratos_test_network ? kdf_test_work : kdf_full_work;
	chratos::kdf & kdf;
	chratos::mdb_env & environment;
	MDB_dbi handle;
	std::recursive_mutex mutex;
};
class node;
// A wallet is a set of account keys encrypted by a common encryption key
class wallet : public std::enable_shared_from_this<chratos::wallet>
{
public:
	std::shared_ptr<chratos::block> change_action (chratos::account const &, chratos::account const &, bool = true);
	std::shared_ptr<chratos::block> receive_action (chratos::block const &, chratos::account const &, chratos::uint128_union const &, bool = true);
	std::shared_ptr<chratos::block> send_action (chratos::account const &, chratos::account const &, chratos::uint128_t const &, bool = true, boost::optional<std::string> = {});
  std::shared_ptr<chratos::block> pay_dividend_action (chratos::account const &, chratos::uint128_t const &, bool = true, boost::optional<std::string> = {});
  std::shared_ptr<chratos::block> claim_dividend_action (chratos::block const &, chratos::account const &, chratos::account const &, bool = true);
	wallet (bool &, chratos::transaction &, chratos::node &, std::string const &);
	wallet (bool &, chratos::transaction &, chratos::node &, std::string const &, std::string const &);
	void enter_initial_password ();
	bool valid_password ();
	bool enter_password (std::string const &);
	chratos::public_key insert_adhoc (chratos::raw_key const &, bool = true);
	chratos::public_key insert_adhoc (MDB_txn *, chratos::raw_key const &, bool = true);
	void insert_watch (MDB_txn *, chratos::public_key const &);
	chratos::public_key deterministic_insert (MDB_txn *, bool = true);
	chratos::public_key deterministic_insert (bool = true);
	bool exists (chratos::public_key const &);
	bool import (std::string const &, std::string const &);
	void serialize (std::string &);
	bool change_sync (chratos::account const &, chratos::account const &);
	void change_async (chratos::account const &, chratos::account const &, std::function<void(std::shared_ptr<chratos::block>)> const &, bool = true);
	bool receive_sync (std::shared_ptr<chratos::block>, chratos::account const &, chratos::uint128_t const &);
	void receive_async (std::shared_ptr<chratos::block>, chratos::account const &, chratos::uint128_t const &, std::function<void(std::shared_ptr<chratos::block>)> const &, bool = true);
	chratos::block_hash send_sync (chratos::account const &, chratos::account const &, chratos::uint128_t const &);
	void send_async (chratos::account const &, chratos::account const &, chratos::uint128_t const &, std::function<void(std::shared_ptr<chratos::block>)> const &, bool = true, boost::optional<std::string> = {});
	chratos::block_hash send_dividend_sync (chratos::account const &, chratos::uint128_t const &);
  void send_dividend_async (chratos::account const &, chratos::uint128_t const &, std::function<void(std::shared_ptr<chratos::block>)> const &, bool = true, boost::optional<std::string> = {});
  bool claim_dividend_sync (std::shared_ptr<chratos::block>, chratos::account const &, chratos::account const &);
  void claim_dividend_async (std::shared_ptr<chratos::block>, chratos::account const &, chratos::account const &, std::function<void(std::shared_ptr<chratos::block>)> const &, bool = true);
	void work_apply (chratos::account const &, std::function<void(uint64_t)>);
	void work_cache_blocking (chratos::account const &, chratos::block_hash const &);
	void work_update (MDB_txn *, chratos::account const &, chratos::block_hash const &, uint64_t);
	void work_ensure (chratos::account const &, chratos::block_hash const &);
	bool search_pending ();
  std::vector<chratos::account> search_unclaimed (chratos::block_hash const &);
  chratos::amount amount_for_dividend (MDB_txn *, std::shared_ptr<chratos::block>, chratos::account const &);
  bool has_outstanding_pendings_for_dividend (MDB_txn *, std::shared_ptr<chratos::block>, chratos::account const &);
	void init_free_accounts (MDB_txn *);
	/** Changes the wallet seed and returns the first account */
	chratos::public_key change_seed (MDB_txn * transaction_a, chratos::raw_key const & prv_a);
	std::unordered_set<chratos::account> free_accounts;
	std::function<void(bool, bool)> lock_observer;
	chratos::wallet_store store;
	chratos::node & node;
};
// The wallets set is all the wallets a node controls.  A node may contain multiple wallets independently encrypted and operated.
class wallets
{
public:
	wallets (bool &, chratos::node &);
	~wallets ();
	std::shared_ptr<chratos::wallet> open (chratos::uint256_union const &);
	std::shared_ptr<chratos::wallet> create (chratos::uint256_union const &);
	bool search_pending (chratos::uint256_union const &);
	void search_pending_all ();
  std::vector<chratos::account> search_unclaimed (chratos::block_hash const &);
  std::unordered_map<chratos::block_hash, std::vector<chratos::account>> search_unclaimed_all ();
	void destroy (chratos::uint256_union const &);
	void do_wallet_actions ();
	void queue_wallet_action (chratos::uint128_t const &, std::function<void()> const &);
	void foreach_representative (MDB_txn *, std::function<void(chratos::public_key const &, chratos::raw_key const &)> const &);
	bool exists (MDB_txn *, chratos::public_key const &);
	void stop ();
	std::function<void(bool)> observer;
	std::unordered_map<chratos::uint256_union, std::shared_ptr<chratos::wallet>> items;
	std::multimap<chratos::uint128_t, std::function<void()>, std::greater<chratos::uint128_t>> actions;
	std::mutex mutex;
	std::condition_variable condition;
	chratos::kdf kdf;
	MDB_dbi handle;
	MDB_dbi send_action_ids;
  MDB_dbi pay_dividend_action_ids;
	chratos::node & node;
	bool stopped;
	std::thread thread;
	static chratos::uint128_t const generate_priority;
	static chratos::uint128_t const high_priority;
};
}
