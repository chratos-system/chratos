#pragma once

#include <chratos/lib/numbers.hpp>

#include <assert.h>
#include <blake2/blake2.h>
#include <boost/property_tree/json_parser.hpp>
#include <streambuf>

namespace chratos
{
std::string to_string_hex (uint64_t);
bool from_string_hex (std::string const &, uint64_t &);
// We operate on streams of uint8_t by convention
using stream = std::basic_streambuf<uint8_t>;
// Read a raw byte stream the size of `T' and fill value.
template <typename T>
bool read (chratos::stream & stream_a, T & value)
{
	static_assert (std::is_pod<T>::value, "Can't stream read non-standard layout types");
	auto amount_read (stream_a.sgetn (reinterpret_cast<uint8_t *> (&value), sizeof (value)));
	return amount_read != sizeof (value);
}
template <typename T>
void write (chratos::stream & stream_a, T const & value)
{
	static_assert (std::is_pod<T>::value, "Can't stream write non-standard layout types");
	auto amount_written (stream_a.sputn (reinterpret_cast<uint8_t const *> (&value), sizeof (value)));
	assert (amount_written == sizeof (value));
}
class block_visitor;
enum class block_type : uint8_t
{
	invalid = 0,
	not_a_block = 1,
	state = 2,
	dividend = 3,
	claim = 4
};
class block
{
public:
	// Return a digest of the hashables in this block.
	chratos::block_hash hash () const;
	std::string to_json ();
	virtual void hash (blake2b_state &) const = 0;
	virtual uint64_t block_work () const = 0;
	virtual void block_work_set (uint64_t) = 0;
	// Previous block in account's chain, zero for open block
	virtual chratos::block_hash previous () const = 0;
	// Source block for open/receive blocks, zero otherwise.
	virtual chratos::block_hash source () const = 0;
	// Previous block or account number for open blocks
	virtual chratos::block_hash root () const = 0;
	// Link field for state blocks, zero otherwise.
	virtual chratos::block_hash link () const = 0;
	// Previous dividend for blocks or zero if there isn't one
	virtual chratos::block_hash dividend () const = 0;
	virtual chratos::account representative () const = 0;
  virtual chratos::account account () const = 0;
	virtual void serialize (chratos::stream &) const = 0;
	virtual void serialize_json (std::string &) const = 0;
	virtual void visit (chratos::block_visitor &) const = 0;
	virtual bool operator== (chratos::block const &) const = 0;
	virtual chratos::block_type type () const = 0;
	virtual chratos::signature block_signature () const = 0;
	virtual void signature_set (chratos::uint512_union const &) = 0;
	virtual ~block () = default;
	virtual bool valid_predecessor (chratos::block const &) const = 0;
};
class state_hashables
{
public:
	state_hashables (chratos::account const &, chratos::block_hash const &, chratos::account const &, chratos::amount const &, chratos::uint256_union const &, chratos::block_hash const &);
	state_hashables (bool &, chratos::stream &);
	state_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	// Account# / public key that operates this account
	// Uses:
	// Bulk signature validation in advance of further ledger processing
	// Arranging uncomitted transactions by account
	chratos::account account;
	// Previous transaction in this chain
	chratos::block_hash previous;
	// Representative of this account
	chratos::account representative;
	// Current balance of this account
	// Allows lookup of account balance simply by looking at the head block
	chratos::amount balance;
	// Link field contains source block_hash if receiving, destination account if sending or the dividend block_hash if a dividend.
	chratos::uint256_union link;
	chratos::block_hash dividend;
};
class state_block : public chratos::block
{
public:
	state_block (chratos::account const &, chratos::block_hash const &, chratos::account const &, chratos::amount const &, chratos::uint256_union const &, chratos::block_hash const &, chratos::raw_key const &, chratos::public_key const &, uint64_t);
	state_block (bool &, chratos::stream &);
	state_block (bool &, boost::property_tree::ptree const &);
	virtual ~state_block () = default;
	using chratos::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	chratos::block_hash previous () const override;
	chratos::block_hash source () const override;
	chratos::block_hash root () const override;
	chratos::block_hash link () const override;
	chratos::block_hash dividend () const override;
	chratos::account representative () const override;
  chratos::account account () const override;
	void serialize (chratos::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (chratos::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (chratos::block_visitor &) const override;
	chratos::block_type type () const override;
	chratos::signature block_signature () const override;
	void signature_set (chratos::uint512_union const &) override;
	bool operator== (chratos::block const &) const override;
	bool operator== (chratos::state_block const &) const;
	bool valid_predecessor (chratos::block const &) const override;
	static size_t constexpr size = sizeof (chratos::account) + sizeof (chratos::block_hash) + sizeof (chratos::account) + sizeof (chratos::amount) + sizeof (chratos::block_hash) + sizeof (chratos::uint256_union) + sizeof (chratos::signature) + sizeof (uint64_t);
	chratos::state_hashables hashables;
	chratos::signature signature;
	uint64_t work;
};
class dividend_hashables
{
public:
	dividend_hashables (chratos::account const &, chratos::block_hash const &, chratos::account const &, chratos::amount const &, chratos::block_hash const &);
	dividend_hashables (bool &, chratos::stream &);
	dividend_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	chratos::account account;
	chratos::block_hash previous;
	chratos::account representative;
	chratos::amount balance;
	chratos::block_hash dividend;
};
class dividend_block : public chratos::block
{
public:
	dividend_block (chratos::account const &, chratos::block_hash const &, chratos::account const &, chratos::amount const &, chratos::block_hash const &, chratos::raw_key const &, chratos::public_key const &, uint64_t);
	dividend_block (bool &, chratos::stream &);
	dividend_block (bool &, boost::property_tree::ptree const &);
	virtual ~dividend_block () = default;
	using chratos::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	chratos::block_hash previous () const override;
	chratos::block_hash source () const override;
	chratos::block_hash root () const override;
	chratos::block_hash dividend () const override;
	chratos::block_hash link () const override;
	chratos::account representative () const override;
  chratos::account account () const override;
	void serialize (chratos::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (chratos::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (chratos::block_visitor &) const override;
	chratos::block_type type () const override;
	chratos::signature block_signature () const override;
	void signature_set (chratos::uint512_union const &) override;
	bool operator== (chratos::block const &) const override;
	bool operator== (chratos::dividend_block const &) const;
	bool valid_predecessor (chratos::block const &) const override;
	static size_t constexpr size = sizeof (chratos::account) + sizeof (chratos::block_hash) + sizeof (chratos::account) + sizeof (chratos::amount) + sizeof (chratos::block_hash) + sizeof (chratos::signature) + sizeof (uint64_t);
	dividend_hashables hashables;
	chratos::signature signature;
	uint64_t work;
};
class claim_hashables
{
public:
	claim_hashables (chratos::account const &, chratos::block_hash const &, chratos::account const &, chratos::amount const &, chratos::block_hash const &);
	claim_hashables (bool &, chratos::stream &);
	claim_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	chratos::account account;
	chratos::block_hash previous;
	chratos::account representative;
	chratos::amount balance;
	chratos::block_hash dividend;
};
class claim_block : public chratos::block
{
public:
	claim_block (chratos::account const &, chratos::block_hash const &, chratos::account const &, chratos::amount const &, chratos::block_hash const &, chratos::raw_key const &, chratos::public_key const &, uint64_t);
	claim_block (bool &, chratos::stream &);
	claim_block (bool &, boost::property_tree::ptree const &);
	virtual ~claim_block () = default;
	using chratos::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	chratos::block_hash previous () const override;
	chratos::block_hash source () const override;
	chratos::block_hash root () const override;
	chratos::block_hash dividend () const override;
	chratos::block_hash link () const override;
	chratos::account representative () const override;
  chratos::account account () const override;
	void serialize (chratos::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (chratos::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (chratos::block_visitor &) const override;
	chratos::block_type type () const override;
	chratos::signature block_signature () const override;
	void signature_set (chratos::uint512_union const &) override;
	bool operator== (chratos::block const &) const override;
	bool operator== (chratos::claim_block const &) const;
	bool valid_predecessor (chratos::block const &) const override;
	static size_t constexpr size = sizeof (chratos::account) + sizeof (chratos::block_hash) + sizeof (chratos::account) + sizeof (chratos::amount) + sizeof (chratos::block_hash) + sizeof (chratos::signature) + sizeof (uint64_t);
	claim_hashables hashables;
	chratos::signature signature;
	uint64_t work;
};

class block_visitor
{
public:
	virtual void state_block (chratos::state_block const &) = 0;
	virtual void dividend_block (chratos::dividend_block const &) = 0;
	virtual void claim_block (chratos::claim_block const &) = 0;
	virtual ~block_visitor () = default;
};
std::unique_ptr<chratos::block> deserialize_block (chratos::stream &);
std::unique_ptr<chratos::block> deserialize_block (chratos::stream &, chratos::block_type);
std::unique_ptr<chratos::block> deserialize_block_json (boost::property_tree::ptree const &);
void serialize_block (chratos::stream &, chratos::block const &);
}
