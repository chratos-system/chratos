#pragma once

#include <boost/filesystem.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

#include <chratos/lib/numbers.hpp>
#include <chratos/secure/blockstore.hpp>
#include <chratos/secure/common.hpp>

namespace chratos
{
class mdb_env;
class mdb_txn : public transaction_impl
{
public:
	mdb_txn (chratos::mdb_env const &, bool = false);
	mdb_txn (chratos::mdb_txn const &) = delete;
	mdb_txn (chratos::mdb_txn &&) = default;
	~mdb_txn ();
	chratos::mdb_txn & operator= (chratos::mdb_txn const &) = delete;
	chratos::mdb_txn & operator= (chratos::mdb_txn &&) = default;
	operator MDB_txn * () const;
	MDB_txn * handle;
};
/**
 * RAII wrapper for MDB_env
 */
class mdb_env
{
public:
	mdb_env (bool &, boost::filesystem::path const &, int max_dbs = 128);
	~mdb_env ();
	operator MDB_env * () const;
	chratos::transaction tx_begin (bool = false) const;
	MDB_txn * tx (chratos::transaction const &) const;
	MDB_env * environment;
};

/**
 * Encapsulates MDB_val and provides uint256_union conversion of the data.
 */
class mdb_val
{
public:
	enum class no_value
	{
		dummy
	};
	mdb_val (chratos::epoch = chratos::epoch::unspecified);
	mdb_val (chratos::account_info const &);
	mdb_val (chratos::dividend_info const &);
	mdb_val (chratos::block_info const &);
	mdb_val (MDB_val const &, chratos::epoch = chratos::epoch::unspecified);
	mdb_val (chratos::pending_info const &);
	mdb_val (chratos::pending_key const &);
	mdb_val (size_t, void *);
	mdb_val (chratos::uint128_union const &);
	mdb_val (chratos::uint256_union const &);
	mdb_val (std::shared_ptr<chratos::block> const &);
	mdb_val (std::shared_ptr<chratos::vote> const &);
	void * data () const;
	size_t size () const;
	explicit operator chratos::account_info () const;
	explicit operator chratos::dividend_info () const;
	explicit operator chratos::block_info () const;
	explicit operator chratos::pending_info () const;
	explicit operator chratos::pending_key () const;
	explicit operator chratos::uint128_union () const;
	explicit operator chratos::uint256_union () const;
	explicit operator std::array<char, 64> () const;
	explicit operator no_value () const;
	explicit operator std::shared_ptr<chratos::block> () const;
	explicit operator std::shared_ptr<chratos::state_block> () const;
	explicit operator std::shared_ptr<chratos::dividend_block> () const;
	explicit operator std::shared_ptr<chratos::claim_block> () const;
	explicit operator std::shared_ptr<chratos::vote> () const;
	explicit operator uint64_t () const;
	operator MDB_val * () const;
	operator MDB_val const & () const;
	MDB_val value;
	std::shared_ptr<std::vector<uint8_t>> buffer;
	chratos::epoch epoch;
};
class block_store;

template <typename T, typename U>
class mdb_iterator : public store_iterator_impl<T, U>
{
public:
	mdb_iterator (chratos::transaction const & transaction_a, MDB_dbi db_a, chratos::epoch = chratos::epoch::unspecified);
	mdb_iterator (std::nullptr_t, chratos::epoch = chratos::epoch::unspecified);
	mdb_iterator (chratos::transaction const & transaction_a, MDB_dbi db_a, MDB_val const & val_a, chratos::epoch = chratos::epoch::unspecified);
	mdb_iterator (chratos::mdb_iterator<T, U> && other_a);
	mdb_iterator (chratos::mdb_iterator<T, U> const &) = delete;
	~mdb_iterator ();
	chratos::store_iterator_impl<T, U> & operator++ () override;
	std::pair<chratos::mdb_val, chratos::mdb_val> * operator-> ();
	bool operator== (chratos::store_iterator_impl<T, U> const & other_a) const override;
	bool is_end_sentinal () const override;
	void fill (std::pair<T, U> &) const override;
	void clear ();
	chratos::mdb_iterator<T, U> & operator= (chratos::mdb_iterator<T, U> && other_a);
	chratos::store_iterator_impl<T, U> & operator= (chratos::store_iterator_impl<T, U> const &) = delete;
	MDB_cursor * cursor;
	std::pair<chratos::mdb_val, chratos::mdb_val> current;

private:
	MDB_txn * tx (chratos::transaction const &) const;
};

/**
 * Iterates the key/value pairs of two stores merged together
 */
template <typename T, typename U>
class mdb_merge_iterator : public store_iterator_impl<T, U>
{
public:
	mdb_merge_iterator (chratos::transaction const &, MDB_dbi, MDB_dbi);
	mdb_merge_iterator (std::nullptr_t);
	mdb_merge_iterator (chratos::transaction const &, MDB_dbi, MDB_dbi, MDB_val const &);
	mdb_merge_iterator (chratos::mdb_merge_iterator<T, U> &&);
	mdb_merge_iterator (chratos::mdb_merge_iterator<T, U> const &) = delete;
	~mdb_merge_iterator ();
	chratos::store_iterator_impl<T, U> & operator++ () override;
	std::pair<chratos::mdb_val, chratos::mdb_val> * operator-> ();
	bool operator== (chratos::store_iterator_impl<T, U> const &) const override;
	bool is_end_sentinal () const override;
	void fill (std::pair<T, U> &) const override;
	void clear ();
	chratos::mdb_merge_iterator<T, U> & operator= (chratos::mdb_merge_iterator<T, U> &&) = default;
	chratos::mdb_merge_iterator<T, U> & operator= (chratos::mdb_merge_iterator<T, U> const &) = delete;

private:
	chratos::mdb_iterator<T, U> & least_iterator () const;
	std::unique_ptr<chratos::mdb_iterator<T, U>> impl1;
	std::unique_ptr<chratos::mdb_iterator<T, U>> impl2;
};

/**
 * mdb implementation of the block store
 */
class mdb_store : public block_store
{
	friend class chratos::block_predecessor_set;

public:
	mdb_store (bool &, boost::filesystem::path const &, int lmdb_max_dbs = 128);

	chratos::transaction tx_begin_write () override;
	chratos::transaction tx_begin_read () override;
	chratos::transaction tx_begin (bool write = false) override;

	void initialize (chratos::transaction const &, chratos::genesis const &) override;
	void block_put (chratos::transaction const &, chratos::block_hash const &, chratos::block const &, chratos::block_hash const & = chratos::block_hash (0), chratos::epoch version = chratos::epoch::epoch_0) override;
	chratos::block_hash block_successor (chratos::transaction const &, chratos::block_hash const &) override;
	void block_successor_clear (chratos::transaction const &, chratos::block_hash const &) override;
	std::unique_ptr<chratos::block> block_get (chratos::transaction const &, chratos::block_hash const &) override;
	std::unique_ptr<chratos::block> block_random (chratos::transaction const &) override;
	void block_del (chratos::transaction const &, chratos::block_hash const &) override;
	bool block_exists (chratos::transaction const &, chratos::block_hash const &) override;
	chratos::block_counts block_count (chratos::transaction const &) override;
	bool root_exists (chratos::transaction const &, chratos::uint256_union const &) override;

	void frontier_put (chratos::transaction const &, chratos::block_hash const &, chratos::account const &) override;
	chratos::account frontier_get (chratos::transaction const &, chratos::block_hash const &) override;
	void frontier_del (chratos::transaction const &, chratos::block_hash const &) override;

	void dividend_put (chratos::transaction const &, chratos::dividend_info const &) override;
	chratos::dividend_info dividend_get (chratos::transaction const &) override;

	void account_put (chratos::transaction const &, chratos::account const &, chratos::account_info const &) override;
	bool account_get (chratos::transaction const &, chratos::account const &, chratos::account_info &) override;
	void account_del (chratos::transaction const &, chratos::account const &) override;
	bool account_exists (chratos::transaction const &, chratos::account const &) override;
	size_t account_count (chratos::transaction const &) override;
	chratos::store_iterator<chratos::account, chratos::account_info> latest_v0_begin (chratos::transaction const &, chratos::account const &) override;
	chratos::store_iterator<chratos::account, chratos::account_info> latest_v0_begin (chratos::transaction const &) override;
	chratos::store_iterator<chratos::account, chratos::account_info> latest_v0_end () override;
	chratos::store_iterator<chratos::account, chratos::account_info> latest_v1_begin (chratos::transaction const &, chratos::account const &) override;
	chratos::store_iterator<chratos::account, chratos::account_info> latest_v1_begin (chratos::transaction const &) override;
	chratos::store_iterator<chratos::account, chratos::account_info> latest_v1_end () override;
	chratos::store_iterator<chratos::account, chratos::account_info> latest_begin (chratos::transaction const &, chratos::account const &) override;
	chratos::store_iterator<chratos::account, chratos::account_info> latest_begin (chratos::transaction const &) override;
	chratos::store_iterator<chratos::account, chratos::account_info> latest_end () override;

	void pending_put (chratos::transaction const &, chratos::pending_key const &, chratos::pending_info const &) override;
	void pending_del (chratos::transaction const &, chratos::pending_key const &) override;
	bool pending_get (chratos::transaction const &, chratos::pending_key const &, chratos::pending_info &) override;
	bool pending_exists (chratos::transaction const &, chratos::pending_key const &) override;
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v0_begin (chratos::transaction const &, chratos::pending_key const &) override;
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v0_begin (chratos::transaction const &) override;
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v0_end () override;
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v1_begin (chratos::transaction const &, chratos::pending_key const &) override;
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v1_begin (chratos::transaction const &) override;
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v1_end () override;
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_begin (chratos::transaction const &, chratos::pending_key const &) override;
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_begin (chratos::transaction const &) override;
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_end () override;

	void block_info_put (chratos::transaction const &, chratos::block_hash const &, chratos::block_info const &) override;
	void block_info_del (chratos::transaction const &, chratos::block_hash const &) override;
	bool block_info_get (chratos::transaction const &, chratos::block_hash const &, chratos::block_info &) override;
	bool block_info_exists (chratos::transaction const &, chratos::block_hash const &) override;
	chratos::store_iterator<chratos::block_hash, chratos::block_info> block_info_begin (chratos::transaction const &, chratos::block_hash const &) override;
	chratos::store_iterator<chratos::block_hash, chratos::block_info> block_info_begin (chratos::transaction const &) override;
	chratos::store_iterator<chratos::block_hash, chratos::block_info> block_info_end () override;
	chratos::uint128_t block_balance (chratos::transaction const &, chratos::block_hash const &) override;
	chratos::epoch block_version (chratos::transaction const &, chratos::block_hash const &) override;

	chratos::uint128_t representation_get (chratos::transaction const &, chratos::account const &) override;
	void representation_put (chratos::transaction const &, chratos::account const &, chratos::uint128_t const &) override;
	void representation_add (chratos::transaction const &, chratos::account const &, chratos::uint128_t const &) override;
	chratos::store_iterator<chratos::account, chratos::uint128_union> representation_begin (chratos::transaction const &) override;
	chratos::store_iterator<chratos::account, chratos::uint128_union> representation_end () override;

	void unchecked_clear (chratos::transaction const &) override;
	void unchecked_put (chratos::transaction const &, chratos::block_hash const &, std::shared_ptr<chratos::block> const &) override;
	std::vector<std::shared_ptr<chratos::block>> unchecked_get (chratos::transaction const &, chratos::block_hash const &) override;
	void unchecked_del (chratos::transaction const &, chratos::block_hash const &, std::shared_ptr<chratos::block>) override;
	chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> unchecked_begin (chratos::transaction const &) override;
	chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> unchecked_begin (chratos::transaction const &, chratos::block_hash const &) override;
	chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> unchecked_end () override;
	size_t unchecked_count (chratos::transaction const &) override;

	void checksum_put (chratos::transaction const &, uint64_t, uint8_t, chratos::checksum const &) override;
	bool checksum_get (chratos::transaction const &, uint64_t, uint8_t, chratos::checksum &) override;
	void checksum_del (chratos::transaction const &, uint64_t, uint8_t) override;

	// Return latest vote for an account from store
	std::shared_ptr<chratos::vote> vote_get (chratos::transaction const &, chratos::account const &) override;
	// Populate vote with the next sequence number
	std::shared_ptr<chratos::vote> vote_generate (chratos::transaction const &, chratos::account const &, chratos::raw_key const &, std::shared_ptr<chratos::block>) override;
	std::shared_ptr<chratos::vote> vote_generate (chratos::transaction const &, chratos::account const &, chratos::raw_key const &, std::vector<chratos::block_hash>) override;
	// Return either vote or the stored vote with a higher sequence number
	std::shared_ptr<chratos::vote> vote_max (chratos::transaction const &, std::shared_ptr<chratos::vote>) override;
	// Return latest vote for an account considering the vote cache
	std::shared_ptr<chratos::vote> vote_current (chratos::transaction const &, chratos::account const &) override;
	void flush (chratos::transaction const &) override;
	chratos::store_iterator<chratos::account, std::shared_ptr<chratos::vote>> vote_begin (chratos::transaction const &) override;
	chratos::store_iterator<chratos::account, std::shared_ptr<chratos::vote>> vote_end () override;
	std::mutex cache_mutex;
	std::unordered_map<chratos::account, std::shared_ptr<chratos::vote>> vote_cache;

	void version_put (chratos::transaction const &, int) override;
	int version_get (chratos::transaction const &) override;
	void do_upgrades (chratos::transaction const &);
	void upgrade_v1_to_v2 (chratos::transaction const &);
	void upgrade_v2_to_v3 (chratos::transaction const &);
	void upgrade_v3_to_v4 (chratos::transaction const &);
	void upgrade_v4_to_v5 (chratos::transaction const &);
	void upgrade_v5_to_v6 (chratos::transaction const &);
	void upgrade_v6_to_v7 (chratos::transaction const &);
	void upgrade_v7_to_v8 (chratos::transaction const &);
	void upgrade_v8_to_v9 (chratos::transaction const &);
	void upgrade_v9_to_v10 (chratos::transaction const &);
	void upgrade_v10_to_v11 (chratos::transaction const &);
	void upgrade_v11_to_v12 (chratos::transaction const &);

	// Requires a write transaction
	chratos::raw_key get_node_id (chratos::transaction const &) override;

	/** Deletes the node ID from the store */
	void delete_node_id (chratos::transaction const &) override;

	chratos::mdb_env env;

	/**
	 * Maps head block to owning account
	 * chratos::block_hash -> chratos::account
	 */
	MDB_dbi frontiers;

	/**
	 * Maps account v1 to account information, head, rep, open, balance, timestamp and block count.
	 * chratos::account -> chratos::block_hash, chratos::block_hash, chratos::block_hash, chratos::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v0;

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp and block count.
	 * chratos::account -> chratos::block_hash, chratos::block_hash, chratos::block_hash, chratos::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v1;

	/**
   * Maps the dividend ledger
   */
	MDB_dbi dividends_ledger;

	/**
	 * Maps block hash to dividend block.
	 * chratos::block_hash -> chratos::dividend_block
   */
	MDB_dbi dividend_blocks;

	/**
	 * Maps block hash to dividend block.
	 * chratos::block_hash -> chratos::dividend_block
   */
	MDB_dbi claim_blocks;

	/**
	 * Maps block hash to v0 state block.
	 * chratos::block_hash -> chratos::state_block
	 */
	MDB_dbi state_blocks_v0;

	/**
	 * Maps block hash to v1 state block.
	 * chratos::block_hash -> chratos::state_block
	 */
	MDB_dbi state_blocks_v1;

	/**
	 * Maps min_version 0 (destination account, pending block) to (source account, amount).
	 * chratos::account, chratos::block_hash -> chratos::account, chratos::amount
	 */
	MDB_dbi pending_v0;

	/**
	 * Maps min_version 1 (destination account, pending block) to (source account, amount).
	 * chratos::account, chratos::block_hash -> chratos::account, chratos::amount
	 */
	MDB_dbi pending_v1;

	/**
	 * Maps block hash to account and balance.
	 * block_hash -> chratos::account, chratos::amount
	 */
	MDB_dbi blocks_info;

	/**
	 * Representative weights.
	 * chratos::account -> chratos::uint128_t
	 */
	MDB_dbi representation;

	/**
	 * Unchecked bootstrap blocks.
	 * chratos::block_hash -> chratos::block
	 */
	MDB_dbi unchecked;

	/**
	 * Mapping of region to checksum.
	 * (uint56_t, uint8_t) -> chratos::block_hash
	 */
	MDB_dbi checksum;

	/**
	 * Highest vote observed for account.
	 * chratos::account -> uint64_t
	 */
	MDB_dbi vote;

	/**
	 * Meta information about block store, such as versions.
	 * chratos::uint256_union (arbitrary key) -> blob
	 */
	MDB_dbi meta;

private:
	MDB_dbi block_database (chratos::block_type, chratos::epoch);
	template <typename T>
	std::unique_ptr<chratos::block> block_random (chratos::transaction const &, MDB_dbi);
	MDB_val block_raw_get (chratos::transaction const &, chratos::block_hash const &, chratos::block_type &);
	void block_raw_put (chratos::transaction const &, MDB_dbi, chratos::block_hash const &, MDB_val);
	void clear (MDB_dbi);
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
}
