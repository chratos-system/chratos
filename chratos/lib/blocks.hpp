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
	send = 2,
	receive = 3,
	open = 4,
	change = 5,
	state = 6,
  dividend = 7,
  dividend_claim = 8
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
  // Previous dividend for blocks or zero if there isn't one
  virtual chratos::block_hash dividend () const = 0;
	virtual chratos::account representative () const = 0;
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
class send_hashables
{
public:
	send_hashables (chratos::account const &, chratos::block_hash const &, chratos::amount const &, chratos::block_hash const &);
	send_hashables (bool &, chratos::stream &);
	send_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	chratos::block_hash previous;
  chratos::block_hash dividend;
	chratos::account destination;
	chratos::amount balance;
};
class send_block : public chratos::block
{
public:
	send_block (chratos::block_hash const &, chratos::account const &, chratos::amount const &, chratos::block_hash const &, chratos::raw_key const &, chratos::public_key const &, uint64_t);
	send_block (bool &, chratos::stream &);
	send_block (bool &, boost::property_tree::ptree const &);
	virtual ~send_block () = default;
	using chratos::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	chratos::block_hash previous () const override;
	chratos::block_hash source () const override;
	chratos::block_hash root () const override;
  chratos::block_hash dividend () const override;
	chratos::account representative () const override;
	void serialize (chratos::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (chratos::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (chratos::block_visitor &) const override;
	chratos::block_type type () const override;
	chratos::signature block_signature () const override;
	void signature_set (chratos::uint512_union const &) override;
	bool operator== (chratos::block const &) const override;
	bool operator== (chratos::send_block const &) const;
	bool valid_predecessor (chratos::block const &) const override;
	static size_t constexpr size = sizeof (chratos::account) + sizeof (chratos::block_hash) + sizeof (chratos::amount) + sizeof (chratos::signature) + sizeof (uint64_t);
	send_hashables hashables;
	chratos::signature signature;
	uint64_t work;
};
class receive_hashables
{
public:
	receive_hashables (chratos::block_hash const &, chratos::block_hash const &,
 chratos::block_hash const &);
	receive_hashables (bool &, chratos::stream &);
	receive_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	chratos::block_hash previous;
	chratos::block_hash source;
  chratos::block_hash dividend;
};
class receive_block : public chratos::block
{
public:
	receive_block (chratos::block_hash const &, chratos::block_hash const &, chratos::block_hash const &, chratos::raw_key const &, chratos::public_key const &, uint64_t);
	receive_block (bool &, chratos::stream &);
	receive_block (bool &, boost::property_tree::ptree const &);
	virtual ~receive_block () = default;
	using chratos::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	chratos::block_hash previous () const override;
	chratos::block_hash source () const override;
	chratos::block_hash root () const override;
  chratos::block_hash dividend () const override;
	chratos::account representative () const override;
	void serialize (chratos::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (chratos::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (chratos::block_visitor &) const override;
	chratos::block_type type () const override;
	chratos::signature block_signature () const override;
	void signature_set (chratos::uint512_union const &) override;
	bool operator== (chratos::block const &) const override;
	bool operator== (chratos::receive_block const &) const;
	bool valid_predecessor (chratos::block const &) const override;
	static size_t constexpr size = sizeof (chratos::block_hash) + sizeof (chratos::block_hash) + sizeof (chratos::signature) + sizeof (uint64_t);
	receive_hashables hashables;
	chratos::signature signature;
	uint64_t work;
};
class open_hashables
{
public:
	open_hashables (chratos::block_hash const &, chratos::account const &, chratos::account const &, chratos::block_hash const &);
	open_hashables (bool &, chratos::stream &);
	open_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	chratos::block_hash source;
	chratos::account representative;
	chratos::account account;
  chratos::block_hash dividend;
};
class open_block : public chratos::block
{
public:
	open_block (chratos::block_hash const &, chratos::account const &, chratos::account const &, chratos::block_hash const &, chratos::raw_key const &, chratos::public_key const &, uint64_t);
	open_block (chratos::block_hash const &, chratos::account const &, chratos::account const &, chratos::block_hash const &, std::nullptr_t);
	open_block (bool &, chratos::stream &);
	open_block (bool &, boost::property_tree::ptree const &);
	virtual ~open_block () = default;
	using chratos::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	chratos::block_hash previous () const override;
	chratos::block_hash source () const override;
	chratos::block_hash root () const override;
  chratos::block_hash dividend () const override;
	chratos::account representative () const override;
	void serialize (chratos::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (chratos::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (chratos::block_visitor &) const override;
	chratos::block_type type () const override;
	chratos::signature block_signature () const override;
	void signature_set (chratos::uint512_union const &) override;
	bool operator== (chratos::block const &) const override;
	bool operator== (chratos::open_block const &) const;
	bool valid_predecessor (chratos::block const &) const override;
	static size_t constexpr size = sizeof (chratos::block_hash) + sizeof (chratos::account) + sizeof (chratos::account) + sizeof (chratos::signature) + sizeof (uint64_t);
	chratos::open_hashables hashables;
	chratos::signature signature;
	uint64_t work;
};
class change_hashables
{
public:
	change_hashables (chratos::block_hash const &, chratos::account const &, chratos::block_hash const &);
	change_hashables (bool &, chratos::stream &);
	change_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	chratos::block_hash previous;
	chratos::account representative;
  chratos::block_hash dividend;
};
class change_block : public chratos::block
{
public:
	change_block (chratos::block_hash const &, chratos::account const &, chratos::block_hash const &, chratos::raw_key const &, chratos::public_key const &, uint64_t);
	change_block (bool &, chratos::stream &);
	change_block (bool &, boost::property_tree::ptree const &);
	virtual ~change_block () = default;
	using chratos::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	chratos::block_hash previous () const override;
	chratos::block_hash source () const override;
	chratos::block_hash root () const override;
  chratos::block_hash dividend () const override;
	chratos::account representative () const override;
	void serialize (chratos::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (chratos::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (chratos::block_visitor &) const override;
	chratos::block_type type () const override;
	chratos::signature block_signature () const override;
	void signature_set (chratos::uint512_union const &) override;
	bool operator== (chratos::block const &) const override;
	bool operator== (chratos::change_block const &) const;
	bool valid_predecessor (chratos::block const &) const override;
	static size_t constexpr size = sizeof (chratos::block_hash) + sizeof (chratos::account) + sizeof (chratos::signature) + sizeof (uint64_t);
	chratos::change_hashables hashables;
	chratos::signature signature;
	uint64_t work;
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
  chratos::block_hash dividend () const override;
	chratos::account representative () const override;
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
	static size_t constexpr size = sizeof (chratos::account) + sizeof (chratos::block_hash) + sizeof (chratos::account) + sizeof (chratos::amount) + sizeof (chratos::uint256_union) + sizeof (chratos::signature) + sizeof (uint64_t);
	chratos::state_hashables hashables;
	chratos::signature signature;
	uint64_t work;
};
class dividend_hashables
{
public:
	dividend_hashables (chratos::account const &, chratos::amount const &, chratos::block_hash const &);
	dividend_hashables (bool &, chratos::stream &);
	dividend_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	chratos::block_hash previous;
  chratos::block_hash dividend;
	chratos::amount balance;
};
class dividend_block : public chratos::block
{
public:
	dividend_block (chratos::block_hash const &, chratos::amount const &, chratos::block_hash const &, chratos::raw_key const &, chratos::public_key const &, uint64_t);
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
	chratos::account representative () const override;
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
	static size_t constexpr size = sizeof (chratos::account) + sizeof (chratos::block_hash) + sizeof (chratos::amount) + sizeof (chratos::signature) + sizeof (uint64_t);
	dividend_hashables hashables;
	chratos::signature signature;
	uint64_t work;

};
class block_visitor
{
public:
	virtual void send_block (chratos::send_block const &) = 0;
	virtual void receive_block (chratos::receive_block const &) = 0;
	virtual void open_block (chratos::open_block const &) = 0;
	virtual void change_block (chratos::change_block const &) = 0;
	virtual void state_block (chratos::state_block const &) = 0;
  virtual void dividend_block (chratos::dividend_block const &) = 0;
	virtual ~block_visitor () = default;
};
std::unique_ptr<chratos::block> deserialize_block (chratos::stream &);
std::unique_ptr<chratos::block> deserialize_block (chratos::stream &, chratos::block_type);
std::unique_ptr<chratos::block> deserialize_block_json (boost::property_tree::ptree const &);
void serialize_block (chratos::stream &, chratos::block const &);
}
