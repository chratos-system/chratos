#include <chratos/node/testing.hpp>
#include <gtest/gtest.h>

namespace
{
class test_visitor : public chratos::message_visitor
{
public:
	test_visitor () :
	keepalive_count (0),
	publish_count (0),
	confirm_req_count (0),
	confirm_ack_count (0),
	bulk_pull_count (0),
	bulk_pull_account_count (0),
	bulk_pull_blocks_count (0),
	bulk_push_count (0),
	frontier_req_count (0)
	{
	}
	void keepalive (chratos::keepalive const &) override
	{
		++keepalive_count;
	}
	void publish (chratos::publish const &) override
	{
		++publish_count;
	}
	void confirm_req (chratos::confirm_req const &) override
	{
		++confirm_req_count;
	}
	void confirm_ack (chratos::confirm_ack const &) override
	{
		++confirm_ack_count;
	}
	void bulk_pull (chratos::bulk_pull const &) override
	{
		++bulk_pull_count;
	}
	void bulk_pull_account (chratos::bulk_pull_account const &) override
	{
		++bulk_pull_account_count;
	}
	void bulk_pull_blocks (chratos::bulk_pull_blocks const &) override
	{
		++bulk_pull_blocks_count;
	}
	void bulk_push (chratos::bulk_push const &) override
	{
		++bulk_push_count;
	}
	void frontier_req (chratos::frontier_req const &) override
	{
		++frontier_req_count;
	}
	void node_id_handshake (chratos::node_id_handshake const &) override
	{
		++node_id_handshake_count;
	}
	uint64_t keepalive_count;
	uint64_t publish_count;
	uint64_t confirm_req_count;
	uint64_t confirm_ack_count;
	uint64_t bulk_pull_count;
	uint64_t bulk_pull_account_count;
	uint64_t bulk_pull_blocks_count;
	uint64_t bulk_push_count;
	uint64_t frontier_req_count;
	uint64_t node_id_handshake_count;
};
}

TEST (message_parser, exact_confirm_ack_size)
{
	chratos::system system (24000, 1);
	test_visitor visitor;
	chratos::message_parser parser (visitor, system.work);
	auto block (std::unique_ptr<chratos::send_block> (new chratos::send_block (1, 1, 2, chratos::keypair ().prv, 4, system.work.generate (1))));
	auto vote (std::make_shared<chratos::vote> (0, chratos::keypair ().prv, 0, std::move (block)));
	chratos::confirm_ack message (vote);
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_ack_count);
	ASSERT_EQ (parser.status, chratos::message_parser::parse_status::success);
	auto error (false);
	chratos::bufferstream stream1 (bytes.data (), bytes.size ());
	chratos::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_ack (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_ack_count);
	ASSERT_EQ (parser.status, chratos::message_parser::parse_status::success);
	bytes.push_back (0);
	chratos::bufferstream stream2 (bytes.data (), bytes.size ());
	chratos::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_ack (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_ack_count);
	ASSERT_NE (parser.status, chratos::message_parser::parse_status::success);
}

TEST (message_parser, exact_confirm_req_size)
{
	chratos::system system (24000, 1);
	test_visitor visitor;
	chratos::message_parser parser (visitor, system.work);
	auto block (std::unique_ptr<chratos::send_block> (new chratos::send_block (1, 1, 2, chratos::keypair ().prv, 4, system.work.generate (1))));
	chratos::confirm_req message (std::move (block));
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, chratos::message_parser::parse_status::success);
	auto error (false);
	chratos::bufferstream stream1 (bytes.data (), bytes.size ());
	chratos::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, chratos::message_parser::parse_status::success);
	bytes.push_back (0);
	chratos::bufferstream stream2 (bytes.data (), bytes.size ());
	chratos::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_NE (parser.status, chratos::message_parser::parse_status::success);
}

TEST (message_parser, exact_publish_size)
{
	chratos::system system (24000, 1);
	test_visitor visitor;
	chratos::message_parser parser (visitor, system.work);
	auto block (std::unique_ptr<chratos::send_block> (new chratos::send_block (1, 1, 2, chratos::keypair ().prv, 4, system.work.generate (1))));
	chratos::publish message (std::move (block));
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.publish_count);
	ASSERT_EQ (parser.status, chratos::message_parser::parse_status::success);
	auto error (false);
	chratos::bufferstream stream1 (bytes.data (), bytes.size ());
	chratos::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_publish (stream1, header1);
	ASSERT_EQ (1, visitor.publish_count);
	ASSERT_EQ (parser.status, chratos::message_parser::parse_status::success);
	bytes.push_back (0);
	chratos::bufferstream stream2 (bytes.data (), bytes.size ());
	chratos::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_publish (stream2, header2);
	ASSERT_EQ (1, visitor.publish_count);
	ASSERT_NE (parser.status, chratos::message_parser::parse_status::success);
}

TEST (message_parser, exact_keepalive_size)
{
	chratos::system system (24000, 1);
	test_visitor visitor;
	chratos::message_parser parser (visitor, system.work);
	chratos::keepalive message;
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.keepalive_count);
	ASSERT_EQ (parser.status, chratos::message_parser::parse_status::success);
	auto error (false);
	chratos::bufferstream stream1 (bytes.data (), bytes.size ());
	chratos::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_keepalive (stream1, header1);
	ASSERT_EQ (1, visitor.keepalive_count);
	ASSERT_EQ (parser.status, chratos::message_parser::parse_status::success);
	bytes.push_back (0);
	chratos::bufferstream stream2 (bytes.data (), bytes.size ());
	chratos::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_keepalive (stream2, header2);
	ASSERT_EQ (1, visitor.keepalive_count);
	ASSERT_NE (parser.status, chratos::message_parser::parse_status::success);
}
