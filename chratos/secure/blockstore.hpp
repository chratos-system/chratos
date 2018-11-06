#pragma once

#include <chratos/secure/common.hpp>

namespace chratos
{
class transaction;
class block_store;
/**
 * Determine the balance as of this block
 */
class balance_visitor : public chratos::block_visitor
{
public:
	balance_visitor (chratos::transaction const &, chratos::block_store &);
	virtual ~balance_visitor () = default;
	void compute (chratos::block_hash const &);
	void state_block (chratos::state_block const &) override;
	void dividend_block (chratos::dividend_block const &) override;
	void claim_block (chratos::claim_block const &) override;
	chratos::transaction const & transaction;
	chratos::block_store & store;
	chratos::block_hash current_balance;
	chratos::block_hash current_amount;
	chratos::uint128_t balance;
};

/**
 * Determine the amount delta resultant from this block
 */
class amount_visitor : public chratos::block_visitor
{
public:
	amount_visitor (chratos::transaction const &, chratos::block_store &);
	virtual ~amount_visitor () = default;
	void compute (chratos::block_hash const &);
	void state_block (chratos::state_block const &) override;
	void dividend_block (chratos::dividend_block const &) override;
	void claim_block (chratos::claim_block const &) override;
	void from_send (chratos::block_hash const &);
	chratos::transaction const & transaction;
	chratos::block_store & store;
	chratos::block_hash current_amount;
	chratos::block_hash current_balance;
	chratos::uint128_t amount;
};

/**
 * Determine the representative for this block
 */
class representative_visitor : public chratos::block_visitor
{
public:
	representative_visitor (chratos::transaction const & transaction_a, chratos::block_store & store_a);
	virtual ~representative_visitor () = default;
	void compute (chratos::block_hash const & hash_a);
	void state_block (chratos::state_block const & block_a) override;
	void dividend_block (chratos::dividend_block const & block_a) override;
	void claim_block (chratos::claim_block const &) override;
	chratos::transaction const & transaction;
	chratos::block_store & store;
	chratos::block_hash current;
	chratos::block_hash result;
};
template <typename T, typename U>
class store_iterator_impl
{
public:
	virtual ~store_iterator_impl () = default;
	virtual chratos::store_iterator_impl<T, U> & operator++ () = 0;
	virtual bool operator== (chratos::store_iterator_impl<T, U> const & other_a) const = 0;
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
/**
 * Iterates the key/value pairs of a transaction
 */
template <typename T, typename U>
class store_iterator
{
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

class transaction_impl
{
public:
	virtual ~transaction_impl () = default;
};
/**
 * RAII wrapper of MDB_txn where the constructor starts the transaction
 * and the destructor commits it.
 */
class transaction
{
public:
	std::unique_ptr<chratos::transaction_impl> impl;
};

/**
 * Manages block storage and iteration
 */
class block_store
{
public:
	virtual ~block_store () = default;
	virtual void initialize (chratos::transaction const &, chratos::genesis const &) = 0;
	virtual void block_put (chratos::transaction const &, chratos::block_hash const &, chratos::block const &, chratos::block_hash const & = chratos::block_hash (0), chratos::epoch version = chratos::epoch::epoch_0) = 0;
	virtual chratos::block_hash block_successor (chratos::transaction const &, chratos::block_hash const &) = 0;
	virtual void block_successor_clear (chratos::transaction const &, chratos::block_hash const &) = 0;
	virtual std::unique_ptr<chratos::block> block_get (chratos::transaction const &, chratos::block_hash const &) = 0;
	virtual std::unique_ptr<chratos::block> block_random (chratos::transaction const &) = 0;
	virtual void block_del (chratos::transaction const &, chratos::block_hash const &) = 0;
	virtual bool block_exists (chratos::transaction const &, chratos::block_hash const &) = 0;
	virtual chratos::block_counts block_count (chratos::transaction const &) = 0;
	virtual bool root_exists (chratos::transaction const &, chratos::uint256_union const &) = 0;

	virtual void frontier_put (chratos::transaction const &, chratos::block_hash const &, chratos::account const &) = 0;
	virtual chratos::account frontier_get (chratos::transaction const &, chratos::block_hash const &) = 0;
	virtual void frontier_del (chratos::transaction const &, chratos::block_hash const &) = 0;

	virtual void account_put (chratos::transaction const &, chratos::account const &, chratos::account_info const &) = 0;
	virtual bool account_get (chratos::transaction const &, chratos::account const &, chratos::account_info &) = 0;
	virtual void account_del (chratos::transaction const &, chratos::account const &) = 0;
	virtual bool account_exists (chratos::transaction const &, chratos::account const &) = 0;
	virtual size_t account_count (chratos::transaction const &) = 0;
	virtual chratos::store_iterator<chratos::account, chratos::account_info> latest_v0_begin (chratos::transaction const &, chratos::account const &) = 0;
	virtual chratos::store_iterator<chratos::account, chratos::account_info> latest_v0_begin (chratos::transaction const &) = 0;
	virtual chratos::store_iterator<chratos::account, chratos::account_info> latest_v0_end () = 0;
	virtual chratos::store_iterator<chratos::account, chratos::account_info> latest_v1_begin (chratos::transaction const &, chratos::account const &) = 0;
	virtual chratos::store_iterator<chratos::account, chratos::account_info> latest_v1_begin (chratos::transaction const &) = 0;
	virtual chratos::store_iterator<chratos::account, chratos::account_info> latest_v1_end () = 0;
	virtual chratos::store_iterator<chratos::account, chratos::account_info> latest_begin (chratos::transaction const &, chratos::account const &) = 0;
	virtual chratos::store_iterator<chratos::account, chratos::account_info> latest_begin (chratos::transaction const &) = 0;
	virtual chratos::store_iterator<chratos::account, chratos::account_info> latest_end () = 0;
	virtual void dividend_put (chratos::transaction const &, chratos::dividend_info const &) = 0;
	virtual chratos::dividend_info dividend_get (chratos::transaction const &) = 0;
	virtual void pending_put (chratos::transaction const &, chratos::pending_key const &, chratos::pending_info const &) = 0;
	virtual void pending_del (chratos::transaction const &, chratos::pending_key const &) = 0;
	virtual bool pending_get (chratos::transaction const &, chratos::pending_key const &, chratos::pending_info &) = 0;
	virtual bool pending_exists (chratos::transaction const &, chratos::pending_key const &) = 0;
	virtual chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v0_begin (chratos::transaction const &, chratos::pending_key const &) = 0;
	virtual chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v0_begin (chratos::transaction const &) = 0;
	virtual chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v0_end () = 0;
	virtual chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v1_begin (chratos::transaction const &, chratos::pending_key const &) = 0;
	virtual chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v1_begin (chratos::transaction const &) = 0;
	virtual chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_v1_end () = 0;
	virtual chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_begin (chratos::transaction const &, chratos::pending_key const &) = 0;
	virtual chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_begin (chratos::transaction const &) = 0;
	virtual chratos::store_iterator<chratos::pending_key, chratos::pending_info> pending_end () = 0;

	virtual void block_info_put (chratos::transaction const &, chratos::block_hash const &, chratos::block_info const &) = 0;
	virtual void block_info_del (chratos::transaction const &, chratos::block_hash const &) = 0;
	virtual bool block_info_get (chratos::transaction const &, chratos::block_hash const &, chratos::block_info &) = 0;
	virtual bool block_info_exists (chratos::transaction const &, chratos::block_hash const &) = 0;
	virtual chratos::store_iterator<chratos::block_hash, chratos::block_info> block_info_begin (chratos::transaction const &, chratos::block_hash const &) = 0;
	virtual chratos::store_iterator<chratos::block_hash, chratos::block_info> block_info_begin (chratos::transaction const &) = 0;
	virtual chratos::store_iterator<chratos::block_hash, chratos::block_info> block_info_end () = 0;
	virtual chratos::uint128_t block_balance (chratos::transaction const &, chratos::block_hash const &) = 0;
	virtual chratos::epoch block_version (chratos::transaction const &, chratos::block_hash const &) = 0;
	static size_t const block_info_max = 32;

	virtual chratos::uint128_t representation_get (chratos::transaction const &, chratos::account const &) = 0;
	virtual void representation_put (chratos::transaction const &, chratos::account const &, chratos::uint128_t const &) = 0;
	virtual void representation_add (chratos::transaction const &, chratos::account const &, chratos::uint128_t const &) = 0;
	virtual chratos::store_iterator<chratos::account, chratos::uint128_union> representation_begin (chratos::transaction const &) = 0;
	virtual chratos::store_iterator<chratos::account, chratos::uint128_union> representation_end () = 0;

	virtual void unchecked_clear (chratos::transaction const &) = 0;
	virtual void unchecked_put (chratos::transaction const &, chratos::block_hash const &, std::shared_ptr<chratos::block> const &) = 0;
	virtual std::vector<std::shared_ptr<chratos::block>> unchecked_get (chratos::transaction const &, chratos::block_hash const &) = 0;
	virtual void unchecked_del (chratos::transaction const &, chratos::block_hash const &, std::shared_ptr<chratos::block>) = 0;
	virtual chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> unchecked_begin (chratos::transaction const &) = 0;
	virtual chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> unchecked_begin (chratos::transaction const &, chratos::block_hash const &) = 0;
	virtual chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> unchecked_end () = 0;
	virtual size_t unchecked_count (chratos::transaction const &) = 0;

	virtual void checksum_put (chratos::transaction const &, uint64_t, uint8_t, chratos::checksum const &) = 0;
	virtual bool checksum_get (chratos::transaction const &, uint64_t, uint8_t, chratos::checksum &) = 0;
	virtual void checksum_del (chratos::transaction const &, uint64_t, uint8_t) = 0;

	// Return latest vote for an account from store
	virtual std::shared_ptr<chratos::vote> vote_get (chratos::transaction const &, chratos::account const &) = 0;
	// Populate vote with the next sequence number
	virtual std::shared_ptr<chratos::vote> vote_generate (chratos::transaction const &, chratos::account const &, chratos::raw_key const &, std::shared_ptr<chratos::block>) = 0;
	virtual std::shared_ptr<chratos::vote> vote_generate (chratos::transaction const &, chratos::account const &, chratos::raw_key const &, std::vector<chratos::block_hash>) = 0;
	// Return either vote or the stored vote with a higher sequence number
	virtual std::shared_ptr<chratos::vote> vote_max (chratos::transaction const &, std::shared_ptr<chratos::vote>) = 0;
	// Return latest vote for an account considering the vote cache
	virtual std::shared_ptr<chratos::vote> vote_current (chratos::transaction const &, chratos::account const &) = 0;
	virtual void flush (chratos::transaction const &) = 0;
	virtual chratos::store_iterator<chratos::account, std::shared_ptr<chratos::vote>> vote_begin (chratos::transaction const &) = 0;
	virtual chratos::store_iterator<chratos::account, std::shared_ptr<chratos::vote>> vote_end () = 0;

	virtual void version_put (chratos::transaction const &, int) = 0;
	virtual int version_get (chratos::transaction const &) = 0;

	// Requires a write transaction
	virtual chratos::raw_key get_node_id (chratos::transaction const &) = 0;

	/** Deletes the node ID from the store */
	virtual void delete_node_id (chratos::transaction const &) = 0;

	/** Start read-write transaction */
	virtual chratos::transaction tx_begin_write () = 0;

	/** Start read-only transaction */
	virtual chratos::transaction tx_begin_read () = 0;

	/**
	 * Start a read-only or read-write transaction
	 * @param write If true, start a read-write transaction
	 */
	virtual chratos::transaction tx_begin (bool write = false) = 0;
};
}
