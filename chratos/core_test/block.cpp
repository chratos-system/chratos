#include <boost/property_tree/json_parser.hpp>

#include <fstream>

#include <gtest/gtest.h>

#include <chratos/lib/interface.h>
#include <chratos/node/common.hpp>
#include <chratos/node/node.hpp>

#include <ed25519-donna/ed25519.h>

TEST (ed25519, signing)
{
	chratos::uint256_union prv (0);
	chratos::uint256_union pub (chratos::pub_key (prv));
	chratos::uint256_union message (0);
	chratos::uint512_union signature;
	ed25519_sign (message.bytes.data (), sizeof (message.bytes), prv.bytes.data (), pub.bytes.data (), signature.bytes.data ());
	auto valid1 (ed25519_sign_open (message.bytes.data (), sizeof (message.bytes), pub.bytes.data (), signature.bytes.data ()));
	ASSERT_EQ (0, valid1);
	signature.bytes[32] ^= 0x1;
	auto valid2 (ed25519_sign_open (message.bytes.data (), sizeof (message.bytes), pub.bytes.data (), signature.bytes.data ()));
	ASSERT_NE (0, valid2);
}

TEST (transaction_block, empty)
{
	chratos::keypair key1;
	chratos::send_block block (0, 1, 13, 0, key1.prv, key1.pub, 2);
	chratos::uint256_union hash (block.hash ());
	ASSERT_FALSE (chratos::validate_message (key1.pub, hash, block.signature));
	block.signature.bytes[32] ^= 0x1;
	ASSERT_TRUE (chratos::validate_message (key1.pub, hash, block.signature));
}

TEST (block, send_serialize)
{
	chratos::send_block block1 (0, 1, 2, 0, chratos::keypair ().prv, 4, 5);
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream1 (bytes);
		block1.serialize (stream1);
	}
	auto data (bytes.data ());
	auto size (bytes.size ());
	ASSERT_NE (nullptr, data);
	ASSERT_NE (0, size);
	chratos::bufferstream stream2 (data, size);
	bool error (false);
	chratos::send_block block2 (error, stream2);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (block, send_serialize_json)
{
	chratos::send_block block1 (0, 1, 2, 0, chratos::keypair ().prv, 4, 5);
	std::string string1;
	block1.serialize_json (string1);
	ASSERT_NE (0, string1.size ());
	boost::property_tree::ptree tree1;
	std::stringstream istream (string1);
	boost::property_tree::read_json (istream, tree1);
	bool error (false);
	chratos::send_block block2 (error, tree1);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (block, receive_serialize)
{
	chratos::receive_block block1 (0, 1, 0, chratos::keypair ().prv, 3, 4);
	chratos::keypair key1;
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream1 (bytes);
		block1.serialize (stream1);
	}
	chratos::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	chratos::receive_block block2 (error, stream2);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (block, receive_serialize_json)
{
	chratos::receive_block block1 (0, 1, 0, chratos::keypair ().prv, 3, 4);
	std::string string1;
	block1.serialize_json (string1);
	ASSERT_NE (0, string1.size ());
	boost::property_tree::ptree tree1;
	std::stringstream istream (string1);
	boost::property_tree::read_json (istream, tree1);
	bool error (false);
	chratos::receive_block block2 (error, tree1);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (block, open_serialize_json)
{
	chratos::open_block block1 (0, 1, 0, 0, chratos::keypair ().prv, 0, 0);
	std::string string1;
	block1.serialize_json (string1);
	ASSERT_NE (0, string1.size ());
	boost::property_tree::ptree tree1;
	std::stringstream istream (string1);
	boost::property_tree::read_json (istream, tree1);
	bool error (false);
	chratos::open_block block2 (error, tree1);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (block, change_serialize_json)
{
	chratos::change_block block1 (0, 1, 0, chratos::keypair ().prv, 3, 4);
	std::string string1;
	block1.serialize_json (string1);
	ASSERT_NE (0, string1.size ());
	boost::property_tree::ptree tree1;
	std::stringstream istream (string1);
	boost::property_tree::read_json (istream, tree1);
	bool error (false);
	chratos::change_block block2 (error, tree1);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (uint512_union, parse_zero)
{
	chratos::uint512_union input (chratos::uint512_t (0));
	std::string text;
	input.encode_hex (text);
	chratos::uint512_union output;
	auto error (output.decode_hex (text));
	ASSERT_FALSE (error);
	ASSERT_EQ (input, output);
	ASSERT_TRUE (output.number ().is_zero ());
}

TEST (uint512_union, parse_zero_short)
{
	std::string text ("0");
	chratos::uint512_union output;
	auto error (output.decode_hex (text));
	ASSERT_FALSE (error);
	ASSERT_TRUE (output.number ().is_zero ());
}

TEST (uint512_union, parse_one)
{
	chratos::uint512_union input (chratos::uint512_t (1));
	std::string text;
	input.encode_hex (text);
	chratos::uint512_union output;
	auto error (output.decode_hex (text));
	ASSERT_FALSE (error);
	ASSERT_EQ (input, output);
	ASSERT_EQ (1, output.number ());
}

TEST (uint512_union, parse_error_symbol)
{
	chratos::uint512_union input (chratos::uint512_t (1000));
	std::string text;
	input.encode_hex (text);
	text[5] = '!';
	chratos::uint512_union output;
	auto error (output.decode_hex (text));
	ASSERT_TRUE (error);
}

TEST (uint512_union, max)
{
	chratos::uint512_union input (std::numeric_limits<chratos::uint512_t>::max ());
	std::string text;
	input.encode_hex (text);
	chratos::uint512_union output;
	auto error (output.decode_hex (text));
	ASSERT_FALSE (error);
	ASSERT_EQ (input, output);
	ASSERT_EQ (chratos::uint512_t ("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"), output.number ());
}

TEST (uint512_union, parse_error_overflow)
{
	chratos::uint512_union input (std::numeric_limits<chratos::uint512_t>::max ());
	std::string text;
	input.encode_hex (text);
	text.push_back (0);
	chratos::uint512_union output;
	auto error (output.decode_hex (text));
	ASSERT_TRUE (error);
}

TEST (send_block, deserialize)
{
	chratos::send_block block1 (0, 1, 2, 0, chratos::keypair ().prv, 4, 5);
	ASSERT_EQ (block1.hash (), block1.hash ());
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream1 (bytes);
		block1.serialize (stream1);
	}
	ASSERT_EQ (chratos::send_block::size, bytes.size ());
	chratos::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	chratos::send_block block2 (error, stream2);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (receive_block, deserialize)
{
	chratos::receive_block block1 (0, 1, 0, chratos::keypair ().prv, 3, 4);
	ASSERT_EQ (block1.hash (), block1.hash ());
	block1.hashables.previous = 2;
	block1.hashables.source = 4;
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream1 (bytes);
		block1.serialize (stream1);
	}
	ASSERT_EQ (chratos::receive_block::size, bytes.size ());
	chratos::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	chratos::receive_block block2 (error, stream2);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (open_block, deserialize)
{
	chratos::open_block block1 (0, 1, 0, 0, chratos::keypair ().prv, 0, 0);
	ASSERT_EQ (block1.hash (), block1.hash ());
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream (bytes);
		block1.serialize (stream);
	}
	ASSERT_EQ (chratos::open_block::size, bytes.size ());
	chratos::bufferstream stream (bytes.data (), bytes.size ());
	bool error (false);
	chratos::open_block block2 (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (change_block, deserialize)
{
	chratos::change_block block1 (1, 2, 0, chratos::keypair ().prv, 4, 5);
	ASSERT_EQ (block1.hash (), block1.hash ());
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream1 (bytes);
		block1.serialize (stream1);
	}
	ASSERT_EQ (chratos::change_block::size, bytes.size ());
	auto data (bytes.data ());
	auto size (bytes.size ());
	ASSERT_NE (nullptr, data);
	ASSERT_NE (0, size);
	chratos::bufferstream stream2 (data, size);
	bool error (false);
	chratos::change_block block2 (error, stream2);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (frontier_req, serialization)
{
	chratos::frontier_req request1;
	request1.start = 1;
	request1.age = 2;
	request1.count = 3;
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream (bytes);
		request1.serialize (stream);
	}
	auto error (false);
	chratos::bufferstream stream (bytes.data (), bytes.size ());
	chratos::message_header header (error, stream);
	ASSERT_FALSE (error);
	chratos::frontier_req request2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (request1, request2);
}

TEST (block, publish_req_serialization)
{
	chratos::keypair key1;
	chratos::keypair key2;
	auto block (std::unique_ptr<chratos::send_block> (new chratos::send_block (0, key2.pub, 200, 0, chratos::keypair ().prv, 2, 3)));
	chratos::publish req (std::move (block));
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	chratos::bufferstream stream2 (bytes.data (), bytes.size ());
	chratos::message_header header (error, stream2);
	ASSERT_FALSE (error);
	chratos::publish req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (*req.block, *req2.block);
}

TEST (block, confirm_req_serialization)
{
	chratos::keypair key1;
	chratos::keypair key2;
	auto block (std::unique_ptr<chratos::send_block> (new chratos::send_block (0, key2.pub, 200, 0, chratos::keypair ().prv, 2, 3)));
	chratos::confirm_req req (std::move (block));
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	chratos::bufferstream stream2 (bytes.data (), bytes.size ());
	chratos::message_header header (error, stream2);
	chratos::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (*req.block, *req2.block);
}

TEST (state_block, serialization)
{
	chratos::keypair key1;
	chratos::keypair key2;
	chratos::state_block block1 (key1.pub, 1, key2.pub, 2, 4, 0, key1.prv, key1.pub, 5);
	ASSERT_EQ (key1.pub, block1.hashables.account);
	ASSERT_EQ (chratos::block_hash (1), block1.previous ());
	ASSERT_EQ (key2.pub, block1.hashables.representative);
	ASSERT_EQ (chratos::amount (2), block1.hashables.balance);
	ASSERT_EQ (chratos::uint256_union (4), block1.hashables.link);
	std::vector<uint8_t> bytes;
	{
		chratos::vectorstream stream (bytes);
		block1.serialize (stream);
	}
	ASSERT_EQ (0x5, bytes[215]); // Ensure work is serialized big-endian
	ASSERT_EQ (chratos::state_block::size, bytes.size ());
	bool error1 (false);
	chratos::bufferstream stream (bytes.data (), bytes.size ());
	chratos::state_block block2 (error1, stream);
	ASSERT_FALSE (error1);
	ASSERT_EQ (block1, block2);
	block2.hashables.account.clear ();
	block2.hashables.previous.clear ();
	block2.hashables.representative.clear ();
	block2.hashables.balance.clear ();
	block2.hashables.link.clear ();
	block2.signature.clear ();
	block2.work = 0;
	chratos::bufferstream stream2 (bytes.data (), bytes.size ());
	ASSERT_FALSE (block2.deserialize (stream2));
	ASSERT_EQ (block1, block2);
	std::string json;
	block1.serialize_json (json);
	std::stringstream body (json);
	boost::property_tree::ptree tree;
	boost::property_tree::read_json (body, tree);
	bool error2 (false);
	chratos::state_block block3 (error2, tree);
	ASSERT_FALSE (error2);
	ASSERT_EQ (block1, block3);
	block3.hashables.account.clear ();
	block3.hashables.previous.clear ();
	block3.hashables.representative.clear ();
	block3.hashables.balance.clear ();
	block3.hashables.link.clear ();
	block3.signature.clear ();
	block3.work = 0;
	ASSERT_FALSE (block3.deserialize_json (tree));
	ASSERT_EQ (block1, block3);
}

TEST (state_block, hashing)
{
	chratos::keypair key;
	chratos::state_block block (key.pub, 0, key.pub, 0, 0, 0, key.prv, key.pub, 0);
	auto hash (block.hash ());
	block.hashables.account.bytes[0] ^= 0x1;
	ASSERT_NE (hash, block.hash ());
	block.hashables.account.bytes[0] ^= 0x1;
	ASSERT_EQ (hash, block.hash ());
	block.hashables.previous.bytes[0] ^= 0x1;
	ASSERT_NE (hash, block.hash ());
	block.hashables.previous.bytes[0] ^= 0x1;
	ASSERT_EQ (hash, block.hash ());
	block.hashables.representative.bytes[0] ^= 0x1;
	ASSERT_NE (hash, block.hash ());
	block.hashables.representative.bytes[0] ^= 0x1;
	ASSERT_EQ (hash, block.hash ());
	block.hashables.balance.bytes[0] ^= 0x1;
	ASSERT_NE (hash, block.hash ());
	block.hashables.balance.bytes[0] ^= 0x1;
	ASSERT_EQ (hash, block.hash ());
	block.hashables.link.bytes[0] ^= 0x1;
	ASSERT_NE (hash, block.hash ());
	block.hashables.link.bytes[0] ^= 0x1;
	ASSERT_EQ (hash, block.hash ());
}
