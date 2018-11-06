#include <gtest/gtest.h>

#include <chratos/node/common.hpp>

TEST (message, keepalive_serialization)
{
	chratos::keepalive request1;
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream (bytes);
		request1.serialize (stream);
	}
	auto error (false);
	chratos::bufferstream stream (bytes.data (), bytes.size ());
	chratos::message_header header (error, stream);
	ASSERT_FALSE (error);
	chratos::keepalive request2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (request1, request2);
}

TEST (message, keepalive_deserialize)
{
	chratos::keepalive message1;
	message1.peers[0] = chratos::endpoint (boost::asio::ip::address_v6::loopback (), 10000);
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream (bytes);
		message1.serialize (stream);
	}
	chratos::bufferstream stream (bytes.data (), bytes.size ());
	auto error (false);
	chratos::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (chratos::message_type::keepalive, header.type);
	chratos::keepalive message2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (message1.peers, message2.peers);
}

TEST (message, publish_serialization)
{
	chratos::publish publish (std::unique_ptr<chratos::block> (new chratos::send_block (0, 1, 2, chratos::keypair ().prv, 4, 5)));
	ASSERT_EQ (chratos::block_type::send, publish.header.block_type ());
	ASSERT_FALSE (publish.header.ipv4_only ());
	publish.header.ipv4_only_set (true);
	ASSERT_TRUE (publish.header.ipv4_only ());
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream (bytes);
		publish.header.serialize (stream);
	}
	ASSERT_EQ (8, bytes.size ());
	ASSERT_EQ (0x52, bytes[0]);
	ASSERT_EQ (0x41, bytes[1]);
	ASSERT_EQ (chratos::protocol_version, bytes[2]);
	ASSERT_EQ (chratos::protocol_version, bytes[3]);
	ASSERT_EQ (chratos::protocol_version_min, bytes[4]);
	ASSERT_EQ (static_cast<uint8_t> (chratos::message_type::publish), bytes[5]);
	ASSERT_EQ (0x02, bytes[6]);
	ASSERT_EQ (static_cast<uint8_t> (chratos::block_type::send), bytes[7]);
	chratos::bufferstream stream (bytes.data (), bytes.size ());
	auto error (false);
	chratos::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (chratos::protocol_version_min, header.version_min);
	ASSERT_EQ (chratos::protocol_version, header.version_using);
	ASSERT_EQ (chratos::protocol_version, header.version_max);
	ASSERT_EQ (chratos::message_type::publish, header.type);
}

TEST (message, confirm_ack_serialization)
{
	chratos::keypair key1;
	auto vote (std::make_shared<chratos::vote> (key1.pub, key1.prv, 0, std::unique_ptr<chratos::block> (new chratos::send_block (0, 1, 2, key1.prv, 4, 5))));
	chratos::confirm_ack con1 (vote);
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream1 (bytes);
		con1.serialize (stream1);
	}
	chratos::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	chratos::message_header header (error, stream2);
	chratos::confirm_ack con2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (con1, con2);
}
