#pragma once

#include <chratos/node/lmdb.hpp>
#include <chratos/secure/common.hpp>

namespace chratos
{
class block_store;
template <typename T, typename U>
class store_iterator_impl
{
public:
	virtual ~store_iterator_impl () = default;
	virtual chratos::store_iterator_impl<T, U> & operator++ () = 0;
	virtual bool operator== (chratos::store_iterator_impl<T, U> const & other_a) const = 0;
	virtual void next_dup () = 0;
	virtual bool is_end_sentinal () const = 0;
	virtual void fill (std::pair<T, U> &) const = 0;
	chratos::store_iterator_impl<T, U> & operator= (chratos::store_iterator_impl<T, U> const &) = delete;
	bool operator== (chratos::store_iterator_impl<T, U> const * other_a) const
	{
		return (other_a != nullptr && *this == *other_a) || (other_a == nullptr && is_end_sentinal ());
	}
	bool operator!= (chratos::store_iterator_impl<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}
};
template <typename T, typename U>
class mdb_iterator : public store_iterator_impl<T, U>
{
public:
	mdb_iterator (MDB_txn * transaction_a, MDB_dbi db_a, chratos::epoch = chratos::epoch::unspecified);
	mdb_iterator (std::nullptr_t, chratos::epoch = chratos::epoch::unspecified);
	mdb_iterator (MDB_txn * transaction_a, MDB_dbi db_a, MDB_val const & val_a, chratos::epoch = chratos::epoch::unspecified);
	mdb_iterator (chratos::mdb_iterator<T, U> && other_a);
	mdb_iterator (chratos::mdb_iterator<T, U> const &) = delete;
	~mdb_iterator ();
	chratos::store_iterator_impl<T, U> & operator++ () override;
	std::pair<chratos::mdb_val, chratos::mdb_val> * operator-> ();
	bool operator== (chratos::store_iterator_impl<T, U> const & other_a) const override;
	void next_dup () override;
	bool is_end_sentinal () const override;
	void fill (std::pair<T, U> &) const override;
	void clear ();
	chratos::mdb_iterator<T, U> & operator= (chratos::mdb_iterator<T, U> && other_a);
	chratos::store_iterator_impl<T, U> & operator= (chratos::store_iterator_impl<T, U> const &) = delete;
	MDB_cursor * cursor;
	std::pair<chratos::mdb_val, chratos::mdb_val> current;
};
template <typename T, typename U>
class mdb_merge_iterator;
/**
 * Iterates the key/value pairs of a transaction
 */
template <typename T, typename U>
class store_iterator
{
	friend class chratos::block_store;
	friend class chratos::mdb_merge_iterator<T, U>;

public:
	store_iterator (std::nullptr_t)
	{
	}
	store_iterator (std::unique_ptr<chratos::store_iterator_impl<T, U>> impl_a) :
	impl (std::move (impl_a))
	{
		impl->fill (current);
	}
	store_iterator (chratos::store_iterator<T, U> && other_a) :
	current (std::move (other_a.current)),
	impl (std::move (other_a.impl))
	{
	}
	chratos::store_iterator<T, U> & operator++ ()
	{
		++*impl;
		impl->fill (current);
		return *this;
	}
	chratos::store_iterator<T, U> & operator= (chratos::store_iterator<T, U> && other_a)
	{
		impl = std::move (other_a.impl);
		current = std::move (other_a.current);
		return *this;
	}
	chratos::store_iterator<T, U> & operator= (chratos::store_iterator<T, U> const &) = delete;
	std::pair<T, U> * operator-> ()
	{
		return &current;
	}
	bool operator== (chratos::store_iterator<T, U> const & other_a) const
	{
		return (impl == nullptr && other_a.impl == nullptr) || (impl != nullptr && *impl == other_a.impl.get ()) || (other_a.impl != nullptr && *other_a.impl == impl.get ());
	}
	bool operator!= (chratos::store_iterator<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}

private:
	std::pair<T, U> current;
	std::unique_ptr<chratos::store_iterator_impl<T, U>> impl;
};

class block_predecessor_set;

/**
 * Iterates the key/value pairs of two stores merged together
 */
template <typename T, typename U>
class mdb_merge_iterator : public store_iterator_impl<T, U>
{
public:
	mdb_merge_iterator (MDB_txn *, MDB_dbi, MDB_dbi);
	mdb_merge_iterator (std::nullptr_t);
	mdb_merge_iterator (MDB_txn *, MDB_dbi, MDB_dbi, MDB_val const &);
	mdb_merge_iterator (chratos::mdb_merge_iterator<T, U> &&);
	mdb_merge_iterator (chratos::mdb_merge_iterator<T, U> const &) = delete;
	~mdb_merge_iterator ();
	chratos::store_iterator_impl<T, U> & operator++ () override;
	std::pair<chratos::mdb_val, chratos::mdb_val> * operator-> ();
	bool operator== (chratos::store_iterator_impl<T, U> const &) const override;
	void next_dup () override;
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
 * Manages block storage and iteration
 */
class block_store
{
	friend class chratos::block_predecessor_set;

public:
	block_store (bool &, boost::filesystem::path const &, int lmdb_max_dbs = 128);

	void initialize (MDB_txn *, chratos::genesis const &);
	void block_put (MDB_txn *, chratos::block_hash const &, chratos::block const &, chratos::block_hash const & = chratos::block_hash (0), chratos::epoch version = chratos::epoch::epoch_0);
	chratos::block_hash block_successor (MDB_txn *, chratos::block_hash const &);
	void block_successor_clear (MDB_txn *, chratos::block_hash const &);
	std::unique_ptr<chratos::block> block_get (MDB_txn *, chratos::block_hash const &);
	std::unique_ptr<chratos::block> block_random (MDB_txn *);
	void block_del (MDB_txn *, chratos::block_hash const &);
	bool block_exists (MDB_txn *, chratos::block_hash const &);
	chratos::block_counts block_count (MDB_txn *);
	bool root_exists (MDB_txn *, chratos::uint256_union const &);

	void frontier_put (MDB_txn *, chratos::block_hash const &, chratos::account const &);
	chratos::account frontier_get (MDB_txn *, chratos::block_hash const &);
	void frontier_del (MDB_txn *, chratos::block_hash const &);

	void account_put (MDB_txn *, chratos::account const &, chratos::account_info const &);
	bool account_get (MDB_txn *, chratos::account const &, chratos::account_info &);
	void account_del (MDB_txn *, chratos::account const &);
	bool account_exists (MDB_txn *, chratos::account const &);
	size_t account_count (MDB_txn *);
	chratos::store_iterator<chratos::account, chratos::account_info> latest_v0_begin (MDB_txn *, chratos::account const &);
	chratos::store_iterator<chratos::account, chratos::account_info> latest_v0_begin (MDB_txn *);
	chratos::store_iterator<chratos::account, chratos::account_info> latest_v0_end ();
	chratos::store_iterator<chratos::account, chratos::account_info> latest_v1_begin (MDB_txn *, chratos::account const &);
	chratos::store_iterator<chratos::account, chratos::account_info> latest_v1_begin (MDB_txn *);
	chratos::store_iterator<chratos::account, chratos::account_info> latest_v1_end ();
	chratos::store_iterator<chratos::account, chratos::account_info> latest_begin (MDB_txn *, chratos::account const &);
	chratos::store_iterator<chratos::account, chratos::account_info> latest_begin (MDB_txn *);
	chratos::store_iterator<chratos::account, chratos::account_info> latest_end ();

	void pending_put (MDB_txn *, chratos::pending_key const &, chratos::pending_info const &);
	void pending_del (MDB_txn *, chratos::pending_key const &);
	bool pending_get (MDB_txn *, chratos::pending_key const &, chratos::pending_info &);
	bool pending_exists (MDB_txn *, chratos::pending_key const &);
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v0_begin (MDB_txn *, chratos::pending_key const &);
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v0_begin (MDB_txn *);
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v0_end ();
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v1_begin (MDB_txn *, chratos::pending_key const &);
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v1_begin (MDB_txn *);
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v1_end ();
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_begin (MDB_txn *, chratos::pending_key const &);
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_begin (MDB_txn *);
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_end ();

	void block_info_put (MDB_txn *, chratos::block_hash const &, chratos::block_info const &);
	void block_info_del (MDB_txn *, chratos::block_hash const &);
	bool block_info_get (MDB_txn *, chratos::block_hash const &, chratos::block_info &);
	bool block_info_exists (MDB_txn *, chratos::block_hash const &);
	chratos::store_iterator<chratos::block_hash, chratos::block_info> block_info_begin (MDB_txn *, chratos::block_hash const &);
	chratos::store_iterator<chratos::block_hash, chratos::block_info> block_info_begin (MDB_txn *);
	chratos::store_iterator<chratos::block_hash, chratos::block_info> block_info_end ();
	chratos::uint128_t block_balance (MDB_txn *, chratos::block_hash const &);
	chratos::epoch block_version (MDB_txn *, chratos::block_hash const &);
	static size_t const block_info_max = 32;

	chratos::uint128_t representation_get (MDB_txn *, chratos::account const &);
	void representation_put (MDB_txn *, chratos::account const &, chratos::uint128_t const &);
	void representation_add (MDB_txn *, chratos::account const &, chratos::uint128_t const &);
	chratos::store_iterator<chratos::account, chratos::uint128_union> representation_begin (MDB_txn *);
	chratos::store_iterator<chratos::account, chratos::uint128_union> representation_end ();

	void unchecked_clear (MDB_txn *);
	void unchecked_put (MDB_txn *, chratos::block_hash const &, std::shared_ptr<chratos::block> const &);
	std::vector<std::shared_ptr<chratos::block>> unchecked_get (MDB_txn *, chratos::block_hash const &);
	void unchecked_del (MDB_txn *, chratos::block_hash const &, std::shared_ptr<chratos::block>);
	chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> unchecked_begin (MDB_txn *);
	chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> unchecked_begin (MDB_txn *, chratos::block_hash const &);
	chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> unchecked_end ();
	size_t unchecked_count (MDB_txn *);
	std::unordered_multimap<chratos::block_hash, std::shared_ptr<chratos::block>> unchecked_cache;

	void checksum_put (MDB_txn *, uint64_t, uint8_t, chratos::checksum const &);
	bool checksum_get (MDB_txn *, uint64_t, uint8_t, chratos::checksum &);
	void checksum_del (MDB_txn *, uint64_t, uint8_t);

	// Return latest vote for an account from store
	std::shared_ptr<chratos::vote> vote_get (MDB_txn *, chratos::account const &);
	// Populate vote with the next sequence number
	std::shared_ptr<chratos::vote> vote_generate (MDB_txn *, chratos::account const &, chratos::raw_key const &, std::shared_ptr<chratos::block>);
	std::shared_ptr<chratos::vote> vote_generate (MDB_txn *, chratos::account const &, chratos::raw_key const &, std::vector<chratos::block_hash>);
	// Return either vote or the stored vote with a higher sequence number
	std::shared_ptr<chratos::vote> vote_max (MDB_txn *, std::shared_ptr<chratos::vote>);
	// Return latest vote for an account considering the vote cache
	std::shared_ptr<chratos::vote> vote_current (MDB_txn *, chratos::account const &);
	void flush (MDB_txn *);
	chratos::store_iterator<chratos::account, std::shared_ptr<chratos::vote>> vote_begin (MDB_txn *);
	chratos::store_iterator<chratos::account, std::shared_ptr<chratos::vote>> vote_end ();
	std::mutex cache_mutex;
	std::unordered_map<chratos::account, std::shared_ptr<chratos::vote>> vote_cache;

	void version_put (MDB_txn *, int);
	int version_get (MDB_txn *);
	void do_upgrades (MDB_txn *);
	void upgrade_v1_to_v2 (MDB_txn *);
	void upgrade_v2_to_v3 (MDB_txn *);
	void upgrade_v3_to_v4 (MDB_txn *);
	void upgrade_v4_to_v5 (MDB_txn *);
	void upgrade_v5_to_v6 (MDB_txn *);
	void upgrade_v6_to_v7 (MDB_txn *);
	void upgrade_v7_to_v8 (MDB_txn *);
	void upgrade_v8_to_v9 (MDB_txn *);
	void upgrade_v9_to_v10 (MDB_txn *);
	void upgrade_v10_to_v11 (MDB_txn *);
	void upgrade_v11_to_v12 (MDB_txn *);

	// Requires a write transaction
	chratos::raw_key get_node_id (MDB_txn *);

	/** Deletes the node ID from the store */
	void delete_node_id (MDB_txn *);

	chratos::mdb_env environment;

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
	 * Maps block hash to send block.
	 * chratos::block_hash -> chratos::send_block
	 */
	MDB_dbi send_blocks;

	/**
	 * Maps block hash to receive block.
	 * chratos::block_hash -> chratos::receive_block
	 */
	MDB_dbi receive_blocks;

	/**
	 * Maps block hash to open block.
	 * chratos::block_hash -> chratos::open_block
	 */
	MDB_dbi open_blocks;

	/**
	 * Maps block hash to change block.
	 * chratos::block_hash -> chratos::change_block
	 */
	MDB_dbi change_blocks;

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
	std::unique_ptr<chratos::block> block_random (MDB_txn *, MDB_dbi);
	MDB_val block_raw_get (MDB_txn *, chratos::block_hash const &, chratos::block_type &);
	void block_raw_put (MDB_txn *, MDB_dbi, chratos::block_hash const &, MDB_val);
	void clear (MDB_dbi);
};
}
