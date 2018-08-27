#pragma once

#include <chratos/lib/interface.h>
#include <chratos/secure/common.hpp>

#include <boost/asio.hpp>

#include <bitset>

#include <xxhash/xxhash.h>

namespace chratos
{
using endpoint = boost::asio::ip::udp::endpoint;
bool parse_port (std::string const &, uint16_t &);
bool parse_address_port (std::string const &, boost::asio::ip::address &, uint16_t &);
using tcp_endpoint = boost::asio::ip::tcp::endpoint;
bool parse_endpoint (std::string const &, chratos::endpoint &);
bool parse_tcp_endpoint (std::string const &, chratos::tcp_endpoint &);
bool reserved_address (chratos::endpoint const &, bool);
}
static uint64_t endpoint_hash_raw (chratos::endpoint const & endpoint_a)
{
	assert (endpoint_a.address ().is_v6 ());
	chratos::uint128_union address;
	address.bytes = endpoint_a.address ().to_v6 ().to_bytes ();
	XXH64_state_t hash;
	XXH64_reset (&hash, 0);
	XXH64_update (&hash, address.bytes.data (), address.bytes.size ());
	auto port (endpoint_a.port ());
	XXH64_update (&hash, &port, sizeof (port));
	auto result (XXH64_digest (&hash));
	return result;
}
static uint64_t ip_address_hash_raw (boost::asio::ip::address const & ip_a)
{
	assert (ip_a.is_v6 ());
	chratos::uint128_union bytes;
	bytes.bytes = ip_a.to_v6 ().to_bytes ();
	XXH64_state_t hash;
	XXH64_reset (&hash, 0);
	XXH64_update (&hash, bytes.bytes.data (), bytes.bytes.size ());
	auto result (XXH64_digest (&hash));
	return result;
}

namespace std
{
template <size_t size>
struct endpoint_hash
{
};
template <>
struct endpoint_hash<8>
{
	size_t operator() (chratos::endpoint const & endpoint_a) const
	{
		return endpoint_hash_raw (endpoint_a);
	}
};
template <>
struct endpoint_hash<4>
{
	size_t operator() (chratos::endpoint const & endpoint_a) const
	{
		uint64_t big (endpoint_hash_raw (endpoint_a));
		uint32_t result (static_cast<uint32_t> (big) ^ static_cast<uint32_t> (big >> 32));
		return result;
	}
};
template <>
struct hash<chratos::endpoint>
{
	size_t operator() (chratos::endpoint const & endpoint_a) const
	{
		endpoint_hash<sizeof (size_t)> ehash;
		return ehash (endpoint_a);
	}
};
template <size_t size>
struct ip_address_hash
{
};
template <>
struct ip_address_hash<8>
{
	size_t operator() (boost::asio::ip::address const & ip_address_a) const
	{
		return ip_address_hash_raw (ip_address_a);
	}
};
template <>
struct ip_address_hash<4>
{
	size_t operator() (boost::asio::ip::address const & ip_address_a) const
	{
		uint64_t big (ip_address_hash_raw (ip_address_a));
		uint32_t result (static_cast<uint32_t> (big) ^ static_cast<uint32_t> (big >> 32));
		return result;
	}
};
template <>
struct hash<boost::asio::ip::address>
{
	size_t operator() (boost::asio::ip::address const & ip_a) const
	{
		ip_address_hash<sizeof (size_t)> ihash;
		return ihash (ip_a);
	}
};
}
namespace boost
{
template <>
struct hash<chratos::endpoint>
{
	size_t operator() (chratos::endpoint const & endpoint_a) const
	{
		std::hash<chratos::endpoint> hash;
		return hash (endpoint_a);
	}
};
}

namespace chratos
{
/**
 * Message types are serialized to the network and existing values must thus never change as
 * types are added, removed and reordered in the enum.
 */
enum class message_type : uint8_t
{
	invalid = 0x0,
	not_a_type = 0x1,
	keepalive = 0x2,
	publish = 0x3,
	confirm_req = 0x4,
	confirm_ack = 0x5,
	bulk_pull = 0x6,
	bulk_push = 0x7,
	frontier_req = 0x8,
	bulk_pull_blocks = 0x9,
	node_id_handshake = 0x0a,
	bulk_pull_account = 0x0b
};
enum class bulk_pull_blocks_mode : uint8_t
{
	list_blocks,
	checksum_blocks
};
enum class bulk_pull_account_flags : uint8_t
{
	pending_hash_and_amount = 0x0,
	pending_address_only = 0x1
};
class message_visitor;
class message_header
{
public:
	message_header (chratos::message_type);
	message_header (bool &, chratos::stream &);
	void serialize (chratos::stream &);
	bool deserialize (chratos::stream &);
	chratos::block_type block_type () const;
	void block_type_set (chratos::block_type);
	bool ipv4_only ();
	void ipv4_only_set (bool);
	static std::array<uint8_t, 2> constexpr magic_number = chratos::chratos_network == chratos::chratos_networks::chratos_test_network ? std::array<uint8_t, 2>{ { 'R', 'A' } } : chratos::chratos_network == chratos::chratos_networks::chratos_beta_network ? std::array<uint8_t, 2>{ { 'R', 'B' } } : std::array<uint8_t, 2>{ { 'R', 'C' } };
	uint8_t version_max;
	uint8_t version_using;
	uint8_t version_min;
	chratos::message_type type;
	std::bitset<16> extensions;
	static size_t constexpr ipv4_only_position = 1;
	static size_t constexpr bootstrap_server_position = 2;
	static std::bitset<16> constexpr block_type_mask = std::bitset<16> (0x0f00);
};
class message
{
public:
	message (chratos::message_type);
	message (chratos::message_header const &);
	virtual ~message () = default;
	virtual void serialize (chratos::stream &) = 0;
	virtual bool deserialize (chratos::stream &) = 0;
	virtual void visit (chratos::message_visitor &) const = 0;
	chratos::message_header header;
};
class work_pool;
class message_parser
{
public:
	enum class parse_status
	{
		success,
		insufficient_work,
		invalid_header,
		invalid_message_type,
		invalid_keepalive_message,
		invalid_publish_message,
		invalid_confirm_req_message,
		invalid_confirm_ack_message,
		invalid_node_id_handshake_message,
		outdated_version
	};
	message_parser (chratos::message_visitor &, chratos::work_pool &);
	void deserialize_buffer (uint8_t const *, size_t);
	void deserialize_keepalive (chratos::stream &, chratos::message_header const &);
	void deserialize_publish (chratos::stream &, chratos::message_header const &);
	void deserialize_confirm_req (chratos::stream &, chratos::message_header const &);
	void deserialize_confirm_ack (chratos::stream &, chratos::message_header const &);
	void deserialize_node_id_handshake (chratos::stream &, chratos::message_header const &);
	bool at_end (chratos::stream &);
	chratos::message_visitor & visitor;
	chratos::work_pool & pool;
	parse_status status;
	static const size_t max_safe_udp_message_size;
};
class keepalive : public message
{
public:
	keepalive (bool &, chratos::stream &, chratos::message_header const &);
	keepalive ();
	void visit (chratos::message_visitor &) const override;
	bool deserialize (chratos::stream &) override;
	void serialize (chratos::stream &) override;
	bool operator== (chratos::keepalive const &) const;
	std::array<chratos::endpoint, 8> peers;
};
class publish : public message
{
public:
	publish (bool &, chratos::stream &, chratos::message_header const &);
	publish (std::shared_ptr<chratos::block>);
	void visit (chratos::message_visitor &) const override;
	bool deserialize (chratos::stream &) override;
	void serialize (chratos::stream &) override;
	bool operator== (chratos::publish const &) const;
	std::shared_ptr<chratos::block> block;
};
class confirm_req : public message
{
public:
	confirm_req (bool &, chratos::stream &, chratos::message_header const &);
	confirm_req (std::shared_ptr<chratos::block>);
	bool deserialize (chratos::stream &) override;
	void serialize (chratos::stream &) override;
	void visit (chratos::message_visitor &) const override;
	bool operator== (chratos::confirm_req const &) const;
	std::shared_ptr<chratos::block> block;
};
class confirm_ack : public message
{
public:
	confirm_ack (bool &, chratos::stream &, chratos::message_header const &);
	confirm_ack (std::shared_ptr<chratos::vote>);
	bool deserialize (chratos::stream &) override;
	void serialize (chratos::stream &) override;
	void visit (chratos::message_visitor &) const override;
	bool operator== (chratos::confirm_ack const &) const;
	std::shared_ptr<chratos::vote> vote;
};
class frontier_req : public message
{
public:
	frontier_req ();
	frontier_req (bool &, chratos::stream &, chratos::message_header const &);
	bool deserialize (chratos::stream &) override;
	void serialize (chratos::stream &) override;
	void visit (chratos::message_visitor &) const override;
	bool operator== (chratos::frontier_req const &) const;
	chratos::account start;
	uint32_t age;
	uint32_t count;
};
class bulk_pull : public message
{
public:
	bulk_pull ();
	bulk_pull (bool &, chratos::stream &, chratos::message_header const &);
	bool deserialize (chratos::stream &) override;
	void serialize (chratos::stream &) override;
	void visit (chratos::message_visitor &) const override;
	chratos::uint256_union start;
	chratos::block_hash end;
};
class bulk_pull_account : public message
{
public:
	bulk_pull_account ();
	bulk_pull_account (bool &, chratos::stream &, chratos::message_header const &);
	bool deserialize (chratos::stream &) override;
	void serialize (chratos::stream &) override;
	void visit (chratos::message_visitor &) const override;
	chratos::uint256_union account;
	chratos::uint128_union minimum_amount;
	bulk_pull_account_flags flags;
};
class bulk_pull_blocks : public message
{
public:
	bulk_pull_blocks ();
	bulk_pull_blocks (bool &, chratos::stream &, chratos::message_header const &);
	bool deserialize (chratos::stream &) override;
	void serialize (chratos::stream &) override;
	void visit (chratos::message_visitor &) const override;
	chratos::block_hash min_hash;
	chratos::block_hash max_hash;
	bulk_pull_blocks_mode mode;
	uint32_t max_count;
};
class bulk_push : public message
{
public:
	bulk_push ();
	bulk_push (chratos::message_header const &);
	bool deserialize (chratos::stream &) override;
	void serialize (chratos::stream &) override;
	void visit (chratos::message_visitor &) const override;
};
class node_id_handshake : public message
{
public:
	node_id_handshake (bool &, chratos::stream &, chratos::message_header const &);
	node_id_handshake (boost::optional<chratos::block_hash>, boost::optional<std::pair<chratos::account, chratos::signature>>);
	bool deserialize (chratos::stream &) override;
	void serialize (chratos::stream &) override;
	void visit (chratos::message_visitor &) const override;
	bool operator== (chratos::node_id_handshake const &) const;
	boost::optional<chratos::uint256_union> query;
	boost::optional<std::pair<chratos::account, chratos::signature>> response;
	static size_t constexpr query_flag = 0;
	static size_t constexpr response_flag = 1;
};
class message_visitor
{
public:
	virtual void keepalive (chratos::keepalive const &) = 0;
	virtual void publish (chratos::publish const &) = 0;
	virtual void confirm_req (chratos::confirm_req const &) = 0;
	virtual void confirm_ack (chratos::confirm_ack const &) = 0;
	virtual void bulk_pull (chratos::bulk_pull const &) = 0;
	virtual void bulk_pull_account (chratos::bulk_pull_account const &) = 0;
	virtual void bulk_pull_blocks (chratos::bulk_pull_blocks const &) = 0;
	virtual void bulk_push (chratos::bulk_push const &) = 0;
	virtual void frontier_req (chratos::frontier_req const &) = 0;
	virtual void node_id_handshake (chratos::node_id_handshake const &) = 0;
	virtual ~message_visitor ();
};

/**
 * Returns seconds passed since unix epoch (posix time)
 */
inline uint64_t seconds_since_epoch ()
{
	return std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
}
}
