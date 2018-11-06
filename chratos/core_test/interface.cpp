#include <gtest/gtest.h>

#include <memory>

#include <chratos/lib/blocks.hpp>
#include <chratos/lib/interface.h>
#include <chratos/lib/numbers.hpp>
#include <chratos/lib/work.hpp>

TEST (interface, chr_uint128_to_dec)
{
	chratos::uint128_union zero (0);
	char text[40] = { 0 };
	chr_uint128_to_dec (zero.bytes.data (), text);
	ASSERT_STREQ ("0", text);
}

TEST (interface, chr_uint256_to_string)
{
	chratos::uint256_union zero (0);
	char text[65] = { 0 };
	chr_uint256_to_string (zero.bytes.data (), text);
	ASSERT_STREQ ("0000000000000000000000000000000000000000000000000000000000000000", text);
}

TEST (interface, chr_uint256_to_address)
{
	chratos::uint256_union zero (0);
	char text[65] = { 0 };
	chr_uint256_to_address (zero.bytes.data (), text);
	ASSERT_STREQ ("chr_1111111111111111111111111111111111111111111111111111hifc8npp", text);
}

TEST (interface, chr_uint512_to_string)
{
	chratos::uint512_union zero (0);
	char text[129] = { 0 };
	chr_uint512_to_string (zero.bytes.data (), text);
	ASSERT_STREQ ("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", text);
}

TEST (interface, chr_uint128_from_dec)
{
	chratos::uint128_union zero (0);
	ASSERT_EQ (0, chr_uint128_from_dec ("340282366920938463463374607431768211455", zero.bytes.data ()));
	ASSERT_EQ (1, chr_uint128_from_dec ("340282366920938463463374607431768211456", zero.bytes.data ()));
	ASSERT_EQ (1, chr_uint128_from_dec ("3402823669209384634633%4607431768211455", zero.bytes.data ()));
}

TEST (interface, chr_uint256_from_string)
{
	chratos::uint256_union zero (0);
	ASSERT_EQ (0, chr_uint256_from_string ("0000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
	ASSERT_EQ (1, chr_uint256_from_string ("00000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
	ASSERT_EQ (1, chr_uint256_from_string ("000000000000000000000000000%000000000000000000000000000000000000", zero.bytes.data ()));
}

TEST (interface, chr_uint512_from_string)
{
	chratos::uint512_union zero (0);
	ASSERT_EQ (0, chr_uint512_from_string ("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
	ASSERT_EQ (1, chr_uint512_from_string ("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
	ASSERT_EQ (1, chr_uint512_from_string ("0000000000000000000000000000000000000000000000000000000000%000000000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
}

TEST (interface, chr_valid_address)
{
	ASSERT_EQ (0, chr_valid_address ("chr_1111111111111111111111111111111111111111111111111111hifc8npp"));
	ASSERT_EQ (1, chr_valid_address ("chr_1111111111111111111111111111111111111111111111111111hifc8nppp"));
	ASSERT_EQ (1, chr_valid_address ("chr_1111111211111111111111111111111111111111111111111111hifc8npp"));
}

TEST (interface, chr_seed_create)
{
	chratos::uint256_union seed;
	chr_generate_random (seed.bytes.data ());
	ASSERT_FALSE (seed.is_zero ());
}

TEST (interface, chr_seed_key)
{
	chratos::uint256_union seed (0);
	chratos::uint256_union prv;
	chr_seed_key (seed.bytes.data (), 0, prv.bytes.data ());
	ASSERT_FALSE (prv.is_zero ());
}

TEST (interface, chr_key_account)
{
	chratos::uint256_union prv (0);
	chratos::uint256_union pub;
	chr_key_account (prv.bytes.data (), pub.bytes.data ());
	ASSERT_FALSE (pub.is_zero ());
}

TEST (interface, sign_transaction)
{
	chratos::raw_key key;
	chr_generate_random (key.data.bytes.data ());
	chratos::uint256_union pub;
	chr_key_account (key.data.bytes.data (), pub.bytes.data ());
	chratos::send_block send (0, 0, 0, key, pub, 0);
	ASSERT_FALSE (chratos::validate_message (pub, send.hash (), send.signature));
	send.signature.bytes[0] ^= 1;
	ASSERT_TRUE (chratos::validate_message (pub, send.hash (), send.signature));
	auto transaction (chr_sign_transaction (send.to_json ().c_str (), key.data.bytes.data ()));
	boost::property_tree::ptree block_l;
	std::string transaction_l (transaction);
	std::stringstream block_stream (transaction_l);
	boost::property_tree::read_json (block_stream, block_l);
	auto block (chratos::deserialize_block_json (block_l));
	ASSERT_NE (nullptr, block);
	auto send1 (dynamic_cast<chratos::send_block *> (block.get ()));
	ASSERT_NE (nullptr, send1);
	ASSERT_FALSE (chratos::validate_message (pub, send.hash (), send1->signature));
	free (transaction);
}

TEST (interface, fail_sign_transaction)
{
	chratos::uint256_union data (0);
	chr_sign_transaction ("", data.bytes.data ());
}

TEST (interface, work_transaction)
{
	chratos::raw_key key;
	chr_generate_random (key.data.bytes.data ());
	chratos::uint256_union pub;
	chr_key_account (key.data.bytes.data (), pub.bytes.data ());
	chratos::send_block send (1, 0, 0, key, pub, 0);
	auto transaction (chr_work_transaction (send.to_json ().c_str ()));
	boost::property_tree::ptree block_l;
	std::string transaction_l (transaction);
	std::stringstream block_stream (transaction_l);
	boost::property_tree::read_json (block_stream, block_l);
	auto block (chratos::deserialize_block_json (block_l));
	ASSERT_NE (nullptr, block);
	ASSERT_FALSE (chratos::work_validate (*block));
	free (transaction);
}

TEST (interface, fail_work_transaction)
{
	chratos::uint256_union data (0);
	chr_work_transaction ("");
}
