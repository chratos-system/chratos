#pragma once

#include <chratos/lib/blocks.hpp>
#include <chratos/secure/utility.hpp>

#include <boost/iterator/transform_iterator.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/variant.hpp>

#include <unordered_map>

#include <blake2/blake2.h>

namespace boost
{
template <>
struct hash<chratos::uint256_union>
{
	size_t operator() (chratos::uint256_union const & value_a) const
	{
		std::hash<chratos::uint256_union> hash;
		return hash (value_a);
	}
};
}
namespace chratos
{
const uint8_t protocol_version = 0x0d;
const uint8_t protocol_version_min = 0x07;
const uint8_t node_id_version = 0x0c;

/**
 * A key pair. The private key is generated from the random pool, or passed in
 * as a hex string. The public key is derived using ed25519.
 */
class keypair
{
public:
	keypair ();
	keypair (std::string const &);
	keypair (chratos::raw_key &&);
	chratos::public_key pub;
	chratos::raw_key prv;
};

/**
 * Tag for which epoch an entry belongs to
 */
enum class epoch : uint8_t
{
	invalid = 0,
	unspecified = 1,
	epoch_0 = 2,
	epoch_1 = 3
};

/**
 * Latest information about an account
 */
class account_info
{
public:
	account_info ();
	account_info (chratos::account_info const &) = default;
	account_info (chratos::block_hash const &, chratos::block_hash const &, chratos::block_hash const &, chratos::block_hash const &, chratos::amount const &, uint64_t, uint64_t, epoch);
	void serialize (chratos::stream &) const;
	bool deserialize (chratos::stream &);
	bool operator== (chratos::account_info const &) const;
	bool operator!= (chratos::account_info const &) const;
	size_t db_size () const;
	chratos::block_hash head;
	chratos::block_hash rep_block;
	chratos::block_hash open_block;
  chratos::block_hash dividend_block;
	chratos::amount balance;
	/** Seconds since posix epoch */
	uint64_t modified;
	uint64_t block_count;
	chratos::epoch epoch;
};

/**
 * Information about the dividend chain
 */
class dividend_info
{
public:
  dividend_info ();
  dividend_info (chratos::dividend_info const &) = default;
	dividend_info (chratos::block_hash const &, chratos::amount const &, uint64_t, uint64_t, epoch);
  void serialize (chratos::stream &) const;
	bool deserialize (chratos::stream &);
	bool operator== (chratos::dividend_info const &) const;
	bool operator!= (chratos::dividend_info const &) const;
	size_t db_size () const;
	chratos::block_hash head;
	chratos::amount balance;
	/** Seconds since posix epoch */
	uint64_t modified;
	uint64_t block_count;
	chratos::epoch epoch;
};

/**
 * Information on an uncollected send
 */
class pending_info
{
public:
	pending_info ();
	pending_info (chratos::account const &, chratos::amount const &, chratos::block_hash const &, epoch);
	void serialize (chratos::stream &) const;
	bool deserialize (chratos::stream &);
	bool operator== (chratos::pending_info const &) const;
	chratos::account source;
	chratos::amount amount;
	chratos::epoch epoch;
  chratos::block_hash dividend;
};
class pending_key
{
public:
	pending_key ();
	pending_key (chratos::account const &, chratos::block_hash const &);
	void serialize (chratos::stream &) const;
	bool deserialize (chratos::stream &);
	bool operator== (chratos::pending_key const &) const;
	chratos::account account;
	chratos::block_hash hash;
};
class pending_dividend_info
{
public:
  pending_dividend_info ();
};
class pending_dividend_key
{
};
class block_info
{
public:
	block_info ();
	block_info (chratos::account const &, chratos::amount const &);
	void serialize (chratos::stream &) const;
	bool deserialize (chratos::stream &);
	bool operator== (chratos::block_info const &) const;
	chratos::account account;
	chratos::amount balance;
};
class block_counts
{
public:
	block_counts ();
	size_t sum ();
	size_t send;
	size_t receive;
	size_t open;
	size_t change;
	size_t state_v0;
	size_t state_v1;
};
typedef std::vector<boost::variant<std::shared_ptr<chratos::block>, chratos::block_hash>>::const_iterator vote_blocks_vec_iter;
class iterate_vote_blocks_as_hash
{
public:
	iterate_vote_blocks_as_hash () = default;
	chratos::block_hash operator() (boost::variant<std::shared_ptr<chratos::block>, chratos::block_hash> const & item) const;
};
class vote
{
public:
	vote () = default;
	vote (chratos::vote const &);
	vote (bool &, chratos::stream &);
	vote (bool &, chratos::stream &, chratos::block_type);
	vote (chratos::account const &, chratos::raw_key const &, uint64_t, std::shared_ptr<chratos::block>);
	vote (chratos::account const &, chratos::raw_key const &, uint64_t, std::vector<chratos::block_hash>);
	std::string hashes_string () const;
	chratos::uint256_union hash () const;
	bool operator== (chratos::vote const &) const;
	bool operator!= (chratos::vote const &) const;
	void serialize (chratos::stream &, chratos::block_type);
	void serialize (chratos::stream &);
	bool deserialize (chratos::stream &);
	bool validate ();
	boost::transform_iterator<chratos::iterate_vote_blocks_as_hash, chratos::vote_blocks_vec_iter> begin () const;
	boost::transform_iterator<chratos::iterate_vote_blocks_as_hash, chratos::vote_blocks_vec_iter> end () const;
	std::string to_json () const;
	// Vote round sequence number
	uint64_t sequence;
	// The blocks, or block hashes, that this vote is for
	std::vector<boost::variant<std::shared_ptr<chratos::block>, chratos::block_hash>> blocks;
	// Account that's voting
	chratos::account account;
	// Signature of sequence + block hashes
	chratos::signature signature;
	static const std::string hash_prefix;
};
enum class vote_code
{
	invalid, // Vote is not signed correctly
	replay, // Vote does not have the highest sequence number, it's a replay
	vote // Vote has the highest sequence number
};

enum class process_result
{
	progress, // Hasn't been seen before, signed correctly
	bad_signature, // Signature was bad, forged or transmission error
	old, // Already seen and was valid
	negative_spend, // Malicious attempt to spend a negative amount
	fork, // Malicious fork based on previous
	unreceivable, // Source block doesn't exist, has already been received, or requires an account upgrade (epoch blocks)
	gap_previous, // Block marked as previous is unknown
	gap_source, // Block marked as source is unknown
	opened_burn_account, // The impossible happened, someone found the private key associated with the public key '0'.
	balance_mismatch, // Balance and amount delta don't match
	representative_mismatch, // Representative is changed when it is not allowed
	block_position, // This block cannot follow the previous block
  outstanding_pendings, // Dividend claim block has outstanding pendings.
  dividend_too_small // Dividend amount is not large enough
};
class process_return
{
public:
	chratos::process_result code;
	chratos::account account;
	chratos::amount amount;
	chratos::account pending_account;
	boost::optional<bool> state_is_send;
};
enum class tally_result
{
	vote,
	changed,
	confirm
};
extern chratos::keypair const & zero_key;
extern chratos::keypair const & test_genesis_key;
extern chratos::account const & chratos_test_account;
extern chratos::account const & chratos_beta_account;
extern chratos::account const & chratos_live_account;
extern std::string const & chratos_test_genesis;
extern std::string const & chratos_beta_genesis;
extern std::string const & chratos_live_genesis;
extern std::string const & genesis_block;
extern chratos::account const & genesis_account;
extern chratos::account const & burn_account;
extern chratos::uint128_t const & genesis_amount;
// A block hash that compares inequal to any real block hash
extern chratos::block_hash const & not_a_block;
// An account number that compares inequal to any real account number
extern chratos::block_hash const & not_an_account;
class genesis
{
public:
	explicit genesis ();
	chratos::block_hash hash () const;
	std::unique_ptr<chratos::open_block> open;
};
}
