
#include <chratos/node/common.hpp>

#include <chratos/lib/work.hpp>
#include <chratos/node/wallet.hpp>

std::array<uint8_t, 2> constexpr chratos::message_header::magic_number;
size_t constexpr chratos::message_header::ipv4_only_position;
size_t constexpr chratos::message_header::bootstrap_server_position;
std::bitset<16> constexpr chratos::message_header::block_type_mask;

chratos::message_header::message_header (chratos::message_type type_a) :
version_max (chratos::protocol_version),
version_using (chratos::protocol_version),
version_min (chratos::protocol_version_min),
type (type_a)
{
}

chratos::message_header::message_header (bool & error_a, chratos::stream & stream_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void chratos::message_header::serialize (chratos::stream & stream_a)
{
	chratos::write (stream_a, chratos::message_header::magic_number);
	chratos::write (stream_a, version_max);
	chratos::write (stream_a, version_using);
	chratos::write (stream_a, version_min);
	chratos::write (stream_a, type);
	chratos::write (stream_a, static_cast<uint16_t> (extensions.to_ullong ()));
}

bool chratos::message_header::deserialize (chratos::stream & stream_a)
{
	uint16_t extensions_l;
	std::array<uint8_t, 2> magic_number_l;
	auto result (chratos::read (stream_a, magic_number_l));
	result = result || magic_number_l != magic_number;
	result = result || chratos::read (stream_a, version_max);
	result = result || chratos::read (stream_a, version_using);
	result = result || chratos::read (stream_a, version_min);
	result = result || chratos::read (stream_a, type);
	result = result || chratos::read (stream_a, extensions_l);
	if (!result)
	{
		extensions = extensions_l;
	}
	return result;
}

chratos::message::message (chratos::message_type type_a) :
header (type_a)
{
}

chratos::message::message (chratos::message_header const & header_a) :
header (header_a)
{
}

chratos::block_type chratos::message_header::block_type () const
{
	return static_cast<chratos::block_type> (((extensions & block_type_mask) >> 8).to_ullong ());
}

void chratos::message_header::block_type_set (chratos::block_type type_a)
{
	extensions &= ~block_type_mask;
	extensions |= std::bitset<16> (static_cast<unsigned long long> (type_a) << 8);
}

bool chratos::message_header::ipv4_only ()
{
	return extensions.test (ipv4_only_position);
}

void chratos::message_header::ipv4_only_set (bool value_a)
{
	extensions.set (ipv4_only_position, value_a);
}

// MTU - IP header - UDP header
const size_t chratos::message_parser::max_safe_udp_message_size = 508;

chratos::message_parser::message_parser (chratos::message_visitor & visitor_a, chratos::work_pool & pool_a) :
visitor (visitor_a),
pool (pool_a),
status (parse_status::success)
{
}

void chratos::message_parser::deserialize_buffer (uint8_t const * buffer_a, size_t size_a)
{
	status = parse_status::success;
	auto error (false);
	if (size_a <= max_safe_udp_message_size)
	{
		// Guaranteed to be deliverable
		chratos::bufferstream stream (buffer_a, size_a);
		chratos::message_header header (error, stream);
		if (!error)
		{
			if (chratos::chratos_network == chratos::chratos_networks::chratos_beta_network && header.version_using < chratos::protocol_version)
			{
				status = parse_status::outdated_version;
			}
			else
			{
				switch (header.type)
				{
					case chratos::message_type::keepalive:
					{
						deserialize_keepalive (stream, header);
						break;
					}
					case chratos::message_type::publish:
					{
						deserialize_publish (stream, header);
						break;
					}
					case chratos::message_type::confirm_req:
					{
						deserialize_confirm_req (stream, header);
						break;
					}
					case chratos::message_type::confirm_ack:
					{
						deserialize_confirm_ack (stream, header);
						break;
					}
					case chratos::message_type::node_id_handshake:
					{
						deserialize_node_id_handshake (stream, header);
						break;
					}
					default:
					{
						status = parse_status::invalid_message_type;
						break;
					}
				}
			}
		}
		else
		{
			status = parse_status::invalid_header;
		}
	}
}

void chratos::message_parser::deserialize_keepalive (chratos::stream & stream_a, chratos::message_header const & header_a)
{
	auto error (false);
	chratos::keepalive incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		visitor.keepalive (incoming);
	}
	else
	{
		status = parse_status::invalid_keepalive_message;
	}
}

void chratos::message_parser::deserialize_publish (chratos::stream & stream_a, chratos::message_header const & header_a)
{
	auto error (false);
	chratos::publish incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		if (!chratos::work_validate (*incoming.block))
		{
			visitor.publish (incoming);
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_publish_message;
	}
}

void chratos::message_parser::deserialize_confirm_req (chratos::stream & stream_a, chratos::message_header const & header_a)
{
	auto error (false);
	chratos::confirm_req incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		if (!chratos::work_validate (*incoming.block))
		{
			visitor.confirm_req (incoming);
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_confirm_req_message;
	}
}

void chratos::message_parser::deserialize_confirm_ack (chratos::stream & stream_a, chratos::message_header const & header_a)
{
	auto error (false);
	chratos::confirm_ack incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		for (auto & vote_block : incoming.vote->blocks)
		{
			if (!vote_block.which ())
			{
				auto block (boost::get<std::shared_ptr<chratos::block>> (vote_block));
				if (chratos::work_validate (*block))
				{
					status = parse_status::insufficient_work;
					break;
				}
			}
		}
		if (status == parse_status::success)
		{
			visitor.confirm_ack (incoming);
		}
	}
	else
	{
		status = parse_status::invalid_confirm_ack_message;
	}
}

void chratos::message_parser::deserialize_node_id_handshake (chratos::stream & stream_a, chratos::message_header const & header_a)
{
	bool error_l (false);
	chratos::node_id_handshake incoming (error_l, stream_a, header_a);
	if (!error_l && at_end (stream_a))
	{
		visitor.node_id_handshake (incoming);
	}
	else
	{
		status = parse_status::invalid_node_id_handshake_message;
	}
}

bool chratos::message_parser::at_end (chratos::stream & stream_a)
{
	uint8_t junk;
	auto end (chratos::read (stream_a, junk));
	return end;
}

chratos::keepalive::keepalive () :
message (chratos::message_type::keepalive)
{
	chratos::endpoint endpoint (boost::asio::ip::address_v6{}, 0);
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
	{
		*i = endpoint;
	}
}

chratos::keepalive::keepalive (bool & error_a, chratos::stream & stream_a, chratos::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void chratos::keepalive::visit (chratos::message_visitor & visitor_a) const
{
	visitor_a.keepalive (*this);
}

void chratos::keepalive::serialize (chratos::stream & stream_a)
{
	header.serialize (stream_a);
	for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
	{
		assert (i->address ().is_v6 ());
		auto bytes (i->address ().to_v6 ().to_bytes ());
		write (stream_a, bytes);
		write (stream_a, i->port ());
	}
}

bool chratos::keepalive::deserialize (chratos::stream & stream_a)
{
	assert (header.type == chratos::message_type::keepalive);
	auto error (false);
	for (auto i (peers.begin ()), j (peers.end ()); i != j && !error; ++i)
	{
		std::array<uint8_t, 16> address;
		uint16_t port;
		if (!read (stream_a, address) && !read (stream_a, port))
		{
			*i = chratos::endpoint (boost::asio::ip::address_v6 (address), port);
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool chratos::keepalive::operator== (chratos::keepalive const & other_a) const
{
	return peers == other_a.peers;
}

chratos::publish::publish (bool & error_a, chratos::stream & stream_a, chratos::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

chratos::publish::publish (std::shared_ptr<chratos::block> block_a) :
message (chratos::message_type::publish),
block (block_a)
{
	header.block_type_set (block->type ());
}

bool chratos::publish::deserialize (chratos::stream & stream_a)
{
	assert (header.type == chratos::message_type::publish);
	block = chratos::deserialize_block (stream_a, header.block_type ());
	auto result (block == nullptr);
	return result;
}

void chratos::publish::serialize (chratos::stream & stream_a)
{
	assert (block != nullptr);
	header.serialize (stream_a);
	block->serialize (stream_a);
}

void chratos::publish::visit (chratos::message_visitor & visitor_a) const
{
	visitor_a.publish (*this);
}

bool chratos::publish::operator== (chratos::publish const & other_a) const
{
	return *block == *other_a.block;
}

chratos::confirm_req::confirm_req (bool & error_a, chratos::stream & stream_a, chratos::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

chratos::confirm_req::confirm_req (std::shared_ptr<chratos::block> block_a) :
message (chratos::message_type::confirm_req),
block (block_a)
{
	header.block_type_set (block->type ());
}

bool chratos::confirm_req::deserialize (chratos::stream & stream_a)
{
	assert (header.type == chratos::message_type::confirm_req);
	block = chratos::deserialize_block (stream_a, header.block_type ());
	auto result (block == nullptr);
	return result;
}

void chratos::confirm_req::visit (chratos::message_visitor & visitor_a) const
{
	visitor_a.confirm_req (*this);
}

void chratos::confirm_req::serialize (chratos::stream & stream_a)
{
	assert (block != nullptr);
	header.serialize (stream_a);
	block->serialize (stream_a);
}

bool chratos::confirm_req::operator== (chratos::confirm_req const & other_a) const
{
	return *block == *other_a.block;
}

chratos::confirm_ack::confirm_ack (bool & error_a, chratos::stream & stream_a, chratos::message_header const & header_a) :
message (header_a),
vote (std::make_shared<chratos::vote> (error_a, stream_a, header.block_type ()))
{
}

chratos::confirm_ack::confirm_ack (std::shared_ptr<chratos::vote> vote_a) :
message (chratos::message_type::confirm_ack),
vote (vote_a)
{
	auto & first_vote_block (vote_a->blocks[0]);
	if (first_vote_block.which ())
	{
		header.block_type_set (chratos::block_type::not_a_block);
	}
	else
	{
		header.block_type_set (boost::get<std::shared_ptr<chratos::block>> (first_vote_block)->type ());
	}
}

bool chratos::confirm_ack::deserialize (chratos::stream & stream_a)
{
	assert (header.type == chratos::message_type::confirm_ack);
	auto result (vote->deserialize (stream_a));
	return result;
}

void chratos::confirm_ack::serialize (chratos::stream & stream_a)
{
	assert (header.block_type () == chratos::block_type::not_a_block || header.block_type () == chratos::block_type::state || header.block_type () == chratos::block_type::dividend || header.block_type () == chratos::block_type::claim);
	header.serialize (stream_a);
	vote->serialize (stream_a, header.block_type ());
}

bool chratos::confirm_ack::operator== (chratos::confirm_ack const & other_a) const
{
	auto result (*vote == *other_a.vote);
	return result;
}

void chratos::confirm_ack::visit (chratos::message_visitor & visitor_a) const
{
	visitor_a.confirm_ack (*this);
}

chratos::frontier_req::frontier_req () :
message (chratos::message_type::frontier_req)
{
}

chratos::frontier_req::frontier_req (bool & error_a, chratos::stream & stream_a, chratos::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

bool chratos::frontier_req::deserialize (chratos::stream & stream_a)
{
	assert (header.type == chratos::message_type::frontier_req);
	auto result (read (stream_a, start.bytes));
	if (!result)
	{
		result = read (stream_a, age);
		if (!result)
		{
			result = read (stream_a, count);
		}
	}
	return result;
}

void chratos::frontier_req::serialize (chratos::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, start.bytes);
	write (stream_a, age);
	write (stream_a, count);
}

void chratos::frontier_req::visit (chratos::message_visitor & visitor_a) const
{
	visitor_a.frontier_req (*this);
}

bool chratos::frontier_req::operator== (chratos::frontier_req const & other_a) const
{
	return start == other_a.start && age == other_a.age && count == other_a.count;
}

chratos::bulk_pull::bulk_pull () :
message (chratos::message_type::bulk_pull)
{
}

chratos::bulk_pull::bulk_pull (bool & error_a, chratos::stream & stream_a, chratos::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void chratos::bulk_pull::visit (chratos::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull (*this);
}

bool chratos::bulk_pull::deserialize (chratos::stream & stream_a)
{
	assert (header.type == chratos::message_type::bulk_pull);
	auto result (read (stream_a, start));
	if (!result)
	{
		result = read (stream_a, end);
	}
	return result;
}

void chratos::bulk_pull::serialize (chratos::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, start);
	write (stream_a, end);
}

chratos::bulk_pull_account::bulk_pull_account () :
message (chratos::message_type::bulk_pull_account)
{
}

chratos::bulk_pull_account::bulk_pull_account (bool & error_a, chratos::stream & stream_a, chratos::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void chratos::bulk_pull_account::visit (chratos::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull_account (*this);
}

bool chratos::bulk_pull_account::deserialize (chratos::stream & stream_a)
{
	assert (header.type == chratos::message_type::bulk_pull_account);
	auto result (read (stream_a, account));
	if (!result)
	{
		result = read (stream_a, minimum_amount);
		if (!result)
		{
			result = read (stream_a, flags);
		}
	}
	return result;
}

void chratos::bulk_pull_account::serialize (chratos::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, account);
	write (stream_a, minimum_amount);
	write (stream_a, flags);
}

chratos::bulk_pull_blocks::bulk_pull_blocks () :
message (chratos::message_type::bulk_pull_blocks)
{
}

chratos::bulk_pull_blocks::bulk_pull_blocks (bool & error_a, chratos::stream & stream_a, chratos::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void chratos::bulk_pull_blocks::visit (chratos::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull_blocks (*this);
}

bool chratos::bulk_pull_blocks::deserialize (chratos::stream & stream_a)
{
	assert (header.type == chratos::message_type::bulk_pull_blocks);
	auto result (read (stream_a, min_hash));
	if (!result)
	{
		result = read (stream_a, max_hash);
		if (!result)
		{
			result = read (stream_a, mode);
			if (!result)
			{
				result = read (stream_a, max_count);
			}
		}
	}
	return result;
}

void chratos::bulk_pull_blocks::serialize (chratos::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, min_hash);
	write (stream_a, max_hash);
	write (stream_a, mode);
	write (stream_a, max_count);
}

chratos::bulk_push::bulk_push () :
message (chratos::message_type::bulk_push)
{
}

chratos::bulk_push::bulk_push (chratos::message_header const & header_a) :
message (header_a)
{
}

bool chratos::bulk_push::deserialize (chratos::stream & stream_a)
{
	assert (header.type == chratos::message_type::bulk_push);
	return false;
}

void chratos::bulk_push::serialize (chratos::stream & stream_a)
{
	header.serialize (stream_a);
}

void chratos::bulk_push::visit (chratos::message_visitor & visitor_a) const
{
	visitor_a.bulk_push (*this);
}

size_t constexpr chratos::node_id_handshake::query_flag;
size_t constexpr chratos::node_id_handshake::response_flag;

chratos::node_id_handshake::node_id_handshake (bool & error_a, chratos::stream & stream_a, chratos::message_header const & header_a) :
message (header_a),
query (boost::none),
response (boost::none)
{
	error_a = deserialize (stream_a);
}

chratos::node_id_handshake::node_id_handshake (boost::optional<chratos::uint256_union> query, boost::optional<std::pair<chratos::account, chratos::signature>> response) :
message (chratos::message_type::node_id_handshake),
query (query),
response (response)
{
	if (query)
	{
		header.extensions.set (query_flag);
	}
	if (response)
	{
		header.extensions.set (response_flag);
	}
}

bool chratos::node_id_handshake::deserialize (chratos::stream & stream_a)
{
	auto result (false);
	assert (header.type == chratos::message_type::node_id_handshake);
	if (!result && header.extensions.test (query_flag))
	{
		chratos::uint256_union query_hash;
		result = read (stream_a, query_hash);
		if (!result)
		{
			query = query_hash;
		}
	}
	if (!result && header.extensions.test (response_flag))
	{
		chratos::account response_account;
		result = read (stream_a, response_account);
		if (!result)
		{
			chratos::signature response_signature;
			result = read (stream_a, response_signature);
			if (!result)
			{
				response = std::make_pair (response_account, response_signature);
			}
		}
	}
	return result;
}

void chratos::node_id_handshake::serialize (chratos::stream & stream_a)
{
	header.serialize (stream_a);
	if (query)
	{
		write (stream_a, *query);
	}
	if (response)
	{
		write (stream_a, response->first);
		write (stream_a, response->second);
	}
}

bool chratos::node_id_handshake::operator== (chratos::node_id_handshake const & other_a) const
{
	auto result (*query == *other_a.query && *response == *other_a.response);
	return result;
}

void chratos::node_id_handshake::visit (chratos::message_visitor & visitor_a) const
{
	visitor_a.node_id_handshake (*this);
}

chratos::message_visitor::~message_visitor ()
{
}
