#include <chratos/core_test/testutil.hpp>
#include <chratos/node/stats.hpp>
#include <chratos/node/testing.hpp>
#include <cryptopp/filters.h>
#include <cryptopp/randpool.h>
#include <gtest/gtest.h>

using namespace std::chrono_literals;

// Init returns an error if it can't open files at the path
TEST (ledger, store_error)
{
	bool init (false);
	chratos::mdb_store store (init, boost::filesystem::path ("///"));
	ASSERT_FALSE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
}

// Ledger can be initialized and returns a basic query for an empty account
TEST (ledger, empty)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::account account;
	auto transaction (store.tx_begin ());
	auto balance (ledger.account_balance (transaction, account));
	ASSERT_TRUE (balance.is_zero ());
}

// Genesis account should have the max balance on empty initialization
TEST (ledger, genesis_balance)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	auto balance (ledger.account_balance (transaction, chratos::genesis_account));
	ASSERT_EQ (chratos::genesis_amount, balance);
	auto amount (ledger.amount (transaction, chratos::genesis_account));
	ASSERT_EQ (chratos::genesis_amount, amount);
	chratos::account_info info;
	ASSERT_FALSE (store.account_get (transaction, chratos::genesis_account, info));
	// Frontier time should have been updated when genesis balance was added
	ASSERT_GE (chratos::seconds_since_epoch (), info.modified);
	ASSERT_LT (chratos::seconds_since_epoch () - info.modified, 10);
}

// Make sure the checksum is the same when ledger reloaded
TEST (ledger, checksum_persistence)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::uint256_union checksum1;
	chratos::uint256_union max;
	max.qwords[0] = 0;
	max.qwords[0] = ~max.qwords[0];
	max.qwords[1] = 0;
	max.qwords[1] = ~max.qwords[1];
	max.qwords[2] = 0;
	max.qwords[2] = ~max.qwords[2];
	max.qwords[3] = 0;
	max.qwords[3] = ~max.qwords[3];
	chratos::stat stats;
	auto transaction (store.tx_begin (true));
	{
		chratos::ledger ledger (store, stats);
		chratos::genesis genesis;
		store.initialize (transaction, genesis);
		checksum1 = ledger.checksum (transaction, 0, max);
	}
	chratos::ledger ledger (store, stats);
	ASSERT_EQ (checksum1, ledger.checksum (transaction, 0, max));
}

// All nodes in the system should agree on the genesis balance
TEST (system, system_genesis)
{
	chratos::system system (24000, 2);
	for (auto & i : system.nodes)
	{
		auto transaction (i->store.tx_begin ());
		ASSERT_EQ (chratos::genesis_amount, i->ledger.account_balance (transaction, chratos::genesis_account));
	}
}

// Create a send block and publish it.
TEST (ledger, process_send)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	auto transaction (store.tx_begin (true));
	chratos::genesis genesis;
	store.initialize (transaction, genesis);
	chratos::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info1));
	chratos::keypair key2;
	chratos::send_block send (info1.head, key2.pub, 50, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	chratos::block_hash hash1 (send.hash ());
	ASSERT_EQ (chratos::test_genesis_key.pub, store.frontier_get (transaction, info1.head));
	ASSERT_EQ (1, info1.block_count);
	// This was a valid block, it should progress.
	auto return1 (ledger.process (transaction, send));
	ASSERT_EQ (chratos::genesis_amount - 50, ledger.amount (transaction, hash1));
	ASSERT_TRUE (store.frontier_get (transaction, info1.head).is_zero ());
	ASSERT_EQ (chratos::test_genesis_key.pub, store.frontier_get (transaction, hash1));
	ASSERT_EQ (chratos::process_result::progress, return1.code);
	ASSERT_EQ (chratos::test_genesis_key.pub, return1.account);
	ASSERT_EQ (chratos::genesis_amount - 50, return1.amount.number ());
	ASSERT_EQ (50, ledger.account_balance (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (chratos::genesis_amount - 50, ledger.account_pending (transaction, key2.pub));
	chratos::account_info info2;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info2));
	ASSERT_EQ (2, info2.block_count);
	auto latest6 (store.block_get (transaction, info2.head));
	ASSERT_NE (nullptr, latest6);
	auto latest7 (dynamic_cast<chratos::send_block *> (latest6.get ()));
	ASSERT_NE (nullptr, latest7);
	ASSERT_EQ (send, *latest7);
	// Create an open block opening an account accepting the send we just created
	chratos::open_block open (hash1, key2.pub, key2.pub, key2.prv, key2.pub, 0);
	chratos::block_hash hash2 (open.hash ());
	// This was a valid block, it should progress.
	auto return2 (ledger.process (transaction, open));
	ASSERT_EQ (chratos::genesis_amount - 50, ledger.amount (transaction, hash2));
	ASSERT_EQ (chratos::process_result::progress, return2.code);
	ASSERT_EQ (key2.pub, return2.account);
	ASSERT_EQ (chratos::genesis_amount - 50, return2.amount.number ());
	ASSERT_EQ (key2.pub, store.frontier_get (transaction, hash2));
	ASSERT_EQ (chratos::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (50, ledger.weight (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (chratos::genesis_amount - 50, ledger.weight (transaction, key2.pub));
	chratos::account_info info3;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info3));
	auto latest2 (store.block_get (transaction, info3.head));
	ASSERT_NE (nullptr, latest2);
	auto latest3 (dynamic_cast<chratos::send_block *> (latest2.get ()));
	ASSERT_NE (nullptr, latest3);
	ASSERT_EQ (send, *latest3);
	chratos::account_info info4;
	ASSERT_FALSE (store.account_get (transaction, key2.pub, info4));
	auto latest4 (store.block_get (transaction, info4.head));
	ASSERT_NE (nullptr, latest4);
	auto latest5 (dynamic_cast<chratos::open_block *> (latest4.get ()));
	ASSERT_NE (nullptr, latest5);
	ASSERT_EQ (open, *latest5);
	ledger.rollback (transaction, hash2);
	ASSERT_TRUE (store.frontier_get (transaction, hash2).is_zero ());
	chratos::account_info info5;
	ASSERT_TRUE (ledger.store.account_get (transaction, key2.pub, info5));
	chratos::pending_info pending1;
	ASSERT_FALSE (ledger.store.pending_get (transaction, chratos::pending_key (key2.pub, hash1), pending1));
	ASSERT_EQ (chratos::test_genesis_key.pub, pending1.source);
	ASSERT_EQ (chratos::genesis_amount - 50, pending1.amount.number ());
	ASSERT_EQ (0, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (chratos::genesis_amount - 50, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (50, ledger.account_balance (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (50, ledger.weight (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	chratos::account_info info6;
	ASSERT_FALSE (ledger.store.account_get (transaction, chratos::test_genesis_key.pub, info6));
	ASSERT_EQ (hash1, info6.head);
	ledger.rollback (transaction, info6.head);
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (chratos::test_genesis_key.pub, store.frontier_get (transaction, info1.head));
	ASSERT_TRUE (store.frontier_get (transaction, hash1).is_zero ());
	chratos::account_info info7;
	ASSERT_FALSE (ledger.store.account_get (transaction, chratos::test_genesis_key.pub, info7));
	ASSERT_EQ (1, info7.block_count);
	ASSERT_EQ (info1.head, info7.head);
	chratos::pending_info pending2;
	ASSERT_TRUE (ledger.store.pending_get (transaction, chratos::pending_key (key2.pub, hash1), pending2));
	ASSERT_EQ (chratos::genesis_amount, ledger.account_balance (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
}

TEST (ledger, process_receive)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info1));
	chratos::keypair key2;
	chratos::send_block send (info1.head, key2.pub, 50, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	chratos::block_hash hash1 (send.hash ());
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send).code);
	chratos::keypair key3;
	chratos::open_block open (hash1, key3.pub, key2.pub, key2.prv, key2.pub, 0);
	chratos::block_hash hash2 (open.hash ());
	auto return1 (ledger.process (transaction, open));
	ASSERT_EQ (chratos::process_result::progress, return1.code);
	ASSERT_EQ (key2.pub, return1.account);
	ASSERT_EQ (chratos::genesis_amount - 50, return1.amount.number ());
	ASSERT_EQ (chratos::genesis_amount - 50, ledger.weight (transaction, key3.pub));
	chratos::send_block send2 (hash1, key2.pub, 25, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	chratos::block_hash hash3 (send2.hash ());
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send2).code);
	chratos::receive_block receive (hash2, hash3, key2.prv, key2.pub, 0);
	auto hash4 (receive.hash ());
	ASSERT_EQ (key2.pub, store.frontier_get (transaction, hash2));
	auto return2 (ledger.process (transaction, receive));
	ASSERT_EQ (25, ledger.amount (transaction, hash4));
	ASSERT_TRUE (store.frontier_get (transaction, hash2).is_zero ());
	ASSERT_EQ (key2.pub, store.frontier_get (transaction, hash4));
	ASSERT_EQ (chratos::process_result::progress, return2.code);
	ASSERT_EQ (key2.pub, return2.account);
	ASSERT_EQ (25, return2.amount.number ());
	ASSERT_EQ (hash4, ledger.latest (transaction, key2.pub));
	ASSERT_EQ (25, ledger.account_balance (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (chratos::genesis_amount - 25, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (chratos::genesis_amount - 25, ledger.weight (transaction, key3.pub));
	ledger.rollback (transaction, hash4);
	ASSERT_TRUE (store.block_successor (transaction, hash2).is_zero ());
	ASSERT_EQ (key2.pub, store.frontier_get (transaction, hash2));
	ASSERT_TRUE (store.frontier_get (transaction, hash4).is_zero ());
	ASSERT_EQ (25, ledger.account_balance (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (25, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (chratos::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (chratos::genesis_amount - 50, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (hash2, ledger.latest (transaction, key2.pub));
	chratos::pending_info pending1;
	ASSERT_FALSE (ledger.store.pending_get (transaction, chratos::pending_key (key2.pub, hash3), pending1));
	ASSERT_EQ (chratos::test_genesis_key.pub, pending1.source);
	ASSERT_EQ (25, pending1.amount.number ());
}

TEST (ledger, rollback_receiver)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info1));
	chratos::keypair key2;
	chratos::send_block send (info1.head, key2.pub, 50, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	chratos::block_hash hash1 (send.hash ());
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send).code);
	chratos::keypair key3;
	chratos::open_block open (hash1, key3.pub, key2.pub, key2.prv, key2.pub, 0);
	chratos::block_hash hash2 (open.hash ());
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (hash2, ledger.latest (transaction, key2.pub));
	ASSERT_EQ (50, ledger.account_balance (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (chratos::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (50, ledger.weight (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (chratos::genesis_amount - 50, ledger.weight (transaction, key3.pub));
	ledger.rollback (transaction, hash1);
	ASSERT_EQ (chratos::genesis_amount, ledger.account_balance (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	chratos::account_info info2;
	ASSERT_TRUE (ledger.store.account_get (transaction, key2.pub, info2));
	chratos::pending_info pending1;
	ASSERT_TRUE (ledger.store.pending_get (transaction, chratos::pending_key (key2.pub, info2.head), pending1));
}

TEST (ledger, rollback_representation)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key5;
	chratos::change_block change1 (genesis.hash (), key5.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, change1).code);
	chratos::keypair key3;
	chratos::change_block change2 (change1.hash (), key3.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, change2).code);
	chratos::keypair key2;
	chratos::send_block send1 (change2.hash (), key2.pub, 50, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::keypair key4;
	chratos::open_block open (send1.hash (), key4.pub, key2.pub, key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, open).code);
	chratos::send_block send2 (send1.hash (), key2.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send2).code);
	chratos::receive_block receive1 (open.hash (), send2.hash (), key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_EQ (1, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (chratos::genesis_amount - 1, ledger.weight (transaction, key4.pub));
	chratos::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, key2.pub, info1));
	ASSERT_EQ (open.hash (), info1.rep_block);
	ledger.rollback (transaction, receive1.hash ());
	chratos::account_info info2;
	ASSERT_FALSE (store.account_get (transaction, key2.pub, info2));
	ASSERT_EQ (open.hash (), info2.rep_block);
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (chratos::genesis_amount - 50, ledger.weight (transaction, key4.pub));
	ledger.rollback (transaction, open.hash ());
	ASSERT_EQ (1, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key4.pub));
	ledger.rollback (transaction, send1.hash ());
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, key3.pub));
	chratos::account_info info3;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info3));
	ASSERT_EQ (change2.hash (), info3.rep_block);
	ledger.rollback (transaction, change2.hash ());
	chratos::account_info info4;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info4));
	ASSERT_EQ (change1.hash (), info4.rep_block);
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, key5.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
}

TEST (ledger, receive_rollback)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::send_block send (genesis.hash (), chratos::test_genesis_key.pub, chratos::genesis_amount - chratos::Gchr_ratio, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send).code);
	chratos::receive_block receive (send.hash (), send.hash (), chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, receive).code);
	ledger.rollback (transaction, receive.hash ());
}

TEST (ledger, process_duplicate)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info1));
	chratos::keypair key2;
	chratos::send_block send (info1.head, key2.pub, 50, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	chratos::block_hash hash1 (send.hash ());
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (chratos::process_result::old, ledger.process (transaction, send).code);
	chratos::open_block open (hash1, 1, key2.pub, key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (chratos::process_result::old, ledger.process (transaction, open).code);
}

TEST (ledger, representative_genesis)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	auto latest (ledger.latest (transaction, chratos::test_genesis_key.pub));
	ASSERT_FALSE (latest.is_zero ());
	ASSERT_EQ (genesis.open->hash (), ledger.representative (transaction, latest));
}

TEST (ledger, weight)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::genesis_account));
}

TEST (ledger, representative_change)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::keypair key2;
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	chratos::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info1));
	chratos::change_block block (info1.head, key2.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::test_genesis_key.pub, store.frontier_get (transaction, info1.head));
	auto return1 (ledger.process (transaction, block));
	ASSERT_EQ (0, ledger.amount (transaction, block.hash ()));
	ASSERT_TRUE (store.frontier_get (transaction, info1.head).is_zero ());
	ASSERT_EQ (chratos::test_genesis_key.pub, store.frontier_get (transaction, block.hash ()));
	ASSERT_EQ (chratos::process_result::progress, return1.code);
	ASSERT_EQ (chratos::test_genesis_key.pub, return1.account);
	ASSERT_EQ (0, ledger.weight (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, key2.pub));
	chratos::account_info info2;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info2));
	ASSERT_EQ (block.hash (), info2.head);
	ledger.rollback (transaction, info2.head);
	ASSERT_EQ (chratos::test_genesis_key.pub, store.frontier_get (transaction, info1.head));
	ASSERT_TRUE (store.frontier_get (transaction, block.hash ()).is_zero ());
	chratos::account_info info3;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info3));
	ASSERT_EQ (info1.head, info3.head);
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
}

TEST (ledger, send_fork)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::keypair key2;
	chratos::keypair key3;
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info1));
	chratos::send_block block (info1.head, key2.pub, 100, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block).code);
	chratos::send_block block2 (info1.head, key3.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::fork, ledger.process (transaction, block2).code);
}

TEST (ledger, receive_fork)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::keypair key2;
	chratos::keypair key3;
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info1));
	chratos::send_block block (info1.head, key2.pub, 100, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block).code);
	chratos::open_block block2 (block.hash (), key2.pub, key2.pub, key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block2).code);
	chratos::change_block block3 (block2.hash (), key3.pub, key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block3).code);
	chratos::send_block block4 (block.hash (), key2.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block4).code);
	chratos::receive_block block5 (block2.hash (), block4.hash (), key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::fork, ledger.process (transaction, block5).code);
}

TEST (ledger, open_fork)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::keypair key2;
	chratos::keypair key3;
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info1));
	chratos::send_block block (info1.head, key2.pub, 100, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block).code);
	chratos::open_block block2 (block.hash (), key2.pub, key2.pub, key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block2).code);
	chratos::open_block block3 (block.hash (), key3.pub, key2.pub, key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::fork, ledger.process (transaction, block3).code);
}

TEST (ledger, checksum_single)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	store.initialize (transaction, genesis);
	store.checksum_put (transaction, 0, 0, genesis.hash ());
	ASSERT_EQ (genesis.hash (), ledger.checksum (transaction, 0, std::numeric_limits<chratos::uint256_t>::max ()));
	chratos::change_block block1 (ledger.latest (transaction, chratos::test_genesis_key.pub), chratos::account (1), chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	chratos::checksum check1 (ledger.checksum (transaction, 0, std::numeric_limits<chratos::uint256_t>::max ()));
	ASSERT_EQ (genesis.hash (), check1);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block1).code);
	chratos::checksum check2 (ledger.checksum (transaction, 0, std::numeric_limits<chratos::uint256_t>::max ()));
	ASSERT_EQ (block1.hash (), check2);
}

TEST (ledger, checksum_two)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	store.initialize (transaction, genesis);
	store.checksum_put (transaction, 0, 0, genesis.hash ());
	chratos::keypair key2;
	chratos::send_block block1 (ledger.latest (transaction, chratos::test_genesis_key.pub), key2.pub, 100, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block1).code);
	chratos::checksum check1 (ledger.checksum (transaction, 0, std::numeric_limits<chratos::uint256_t>::max ()));
	chratos::open_block block2 (block1.hash (), 1, key2.pub, key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block2).code);
	chratos::checksum check2 (ledger.checksum (transaction, 0, std::numeric_limits<chratos::uint256_t>::max ()));
	ASSERT_EQ (check1, check2 ^ block2.hash ());
}

TEST (ledger, DISABLED_checksum_range)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	auto transaction (store.tx_begin ());
	chratos::checksum check1 (ledger.checksum (transaction, 0, std::numeric_limits<chratos::uint256_t>::max ()));
	ASSERT_TRUE (check1.is_zero ());
	chratos::block_hash hash1 (42);
	chratos::checksum check2 (ledger.checksum (transaction, 0, 42));
	ASSERT_TRUE (check2.is_zero ());
	chratos::checksum check3 (ledger.checksum (transaction, 42, std::numeric_limits<chratos::uint256_t>::max ()));
	ASSERT_EQ (hash1, check3);
}

TEST (system, generate_send_existing)
{
	chratos::system system (24000, 1);
	chratos::thread_runner runner (system.service, system.nodes[0]->config.io_threads);
	system.wallet (0)->insert_adhoc (chratos::test_genesis_key.prv);
	chratos::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (chratos::genesis_account, stake_preserver.pub, chratos::genesis_amount / 3 * 2, true));
	chratos::account_info info1;
	{
		auto transaction (system.wallet (0)->wallets.tx_begin ());
		ASSERT_FALSE (system.nodes[0]->store.account_get (transaction, chratos::test_genesis_key.pub, info1));
	}
	std::vector<chratos::account> accounts;
	accounts.push_back (chratos::test_genesis_key.pub);
	system.generate_send_existing (*system.nodes[0], accounts);
	// Have stake_preserver receive funds after generate_send_existing so it isn't chosen as the destination
	{
		auto transaction (system.nodes[0]->store.tx_begin (true));
		auto open_block (std::make_shared<chratos::open_block> (send_block->hash (), chratos::genesis_account, stake_preserver.pub, stake_preserver.prv, stake_preserver.pub, 0));
		system.nodes[0]->work_generate_blocking (*open_block);
		ASSERT_EQ (chratos::process_result::progress, system.nodes[0]->ledger.process (transaction, *open_block).code);
	}
	ASSERT_GT (system.nodes[0]->balance (stake_preserver.pub), system.nodes[0]->balance (chratos::genesis_account));
	chratos::account_info info2;
	{
		auto transaction (system.wallet (0)->wallets.tx_begin ());
		ASSERT_FALSE (system.nodes[0]->store.account_get (transaction, chratos::test_genesis_key.pub, info2));
	}
	ASSERT_NE (info1.head, info2.head);
	system.deadline_set (15s);
	while (info2.block_count < info1.block_count + 2)
	{
		ASSERT_NO_ERROR (system.poll ());
		auto transaction (system.wallet (0)->wallets.tx_begin ());
		ASSERT_FALSE (system.nodes[0]->store.account_get (transaction, chratos::test_genesis_key.pub, info2));
	}
	ASSERT_EQ (info1.block_count + 2, info2.block_count);
	ASSERT_EQ (info2.balance, chratos::genesis_amount / 3);
	{
		auto transaction (system.wallet (0)->wallets.tx_begin ());
		ASSERT_NE (system.nodes[0]->ledger.amount (transaction, info2.head), 0);
	}
	system.stop ();
	runner.join ();
}

TEST (system, generate_send_new)
{
	chratos::system system (24000, 1);
	chratos::thread_runner runner (system.service, system.nodes[0]->config.io_threads);
	system.wallet (0)->insert_adhoc (chratos::test_genesis_key.prv);
	{
		auto transaction (system.nodes[0]->store.tx_begin ());
		auto iterator1 (system.nodes[0]->store.latest_begin (transaction));
		ASSERT_NE (system.nodes[0]->store.latest_end (), iterator1);
		++iterator1;
		ASSERT_EQ (system.nodes[0]->store.latest_end (), iterator1);
	}
	chratos::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (chratos::genesis_account, stake_preserver.pub, chratos::genesis_amount / 3 * 2, true));
	{
		auto transaction (system.nodes[0]->store.tx_begin (true));
		auto open_block (std::make_shared<chratos::open_block> (send_block->hash (), chratos::genesis_account, stake_preserver.pub, stake_preserver.prv, stake_preserver.pub, 0));
		system.nodes[0]->work_generate_blocking (*open_block);
		ASSERT_EQ (chratos::process_result::progress, system.nodes[0]->ledger.process (transaction, *open_block).code);
	}
	ASSERT_GT (system.nodes[0]->balance (stake_preserver.pub), system.nodes[0]->balance (chratos::genesis_account));
	std::vector<chratos::account> accounts;
	accounts.push_back (chratos::test_genesis_key.pub);
	system.generate_send_new (*system.nodes[0], accounts);
	chratos::account new_account (0);
	{
		auto transaction (system.nodes[0]->store.tx_begin ());
		auto iterator2 (system.wallet (0)->store.begin (transaction));
		if (chratos::uint256_union (iterator2->first) != chratos::test_genesis_key.pub)
		{
			new_account = chratos::uint256_union (iterator2->first);
		}
		++iterator2;
		ASSERT_NE (system.wallet (0)->store.end (), iterator2);
		if (chratos::uint256_union (iterator2->first) != chratos::test_genesis_key.pub)
		{
			new_account = chratos::uint256_union (iterator2->first);
		}
		++iterator2;
		ASSERT_EQ (system.wallet (0)->store.end (), iterator2);
		ASSERT_FALSE (new_account.is_zero ());
	}
	system.deadline_set (10s);
	while (system.nodes[0]->balance (new_account) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.stop ();
	runner.join ();
}

TEST (ledger, representation)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	ASSERT_EQ (chratos::genesis_amount, store.representation_get (transaction, chratos::test_genesis_key.pub));
	chratos::keypair key2;
	chratos::send_block block1 (genesis.hash (), key2.pub, chratos::genesis_amount - 100, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block1).code);
	ASSERT_EQ (chratos::genesis_amount - 100, store.representation_get (transaction, chratos::test_genesis_key.pub));
	chratos::keypair key3;
	chratos::open_block block2 (block1.hash (), key3.pub, key2.pub, key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (chratos::genesis_amount - 100, store.representation_get (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (100, store.representation_get (transaction, key3.pub));
	chratos::send_block block3 (block1.hash (), key2.pub, chratos::genesis_amount - 200, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block3).code);
	ASSERT_EQ (chratos::genesis_amount - 200, store.representation_get (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (100, store.representation_get (transaction, key3.pub));
	chratos::receive_block block4 (block2.hash (), block3.hash (), key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (chratos::genesis_amount - 200, store.representation_get (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (200, store.representation_get (transaction, key3.pub));
	chratos::keypair key4;
	chratos::change_block block5 (block4.hash (), key4.pub, key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block5).code);
	ASSERT_EQ (chratos::genesis_amount - 200, store.representation_get (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key3.pub));
	ASSERT_EQ (200, store.representation_get (transaction, key4.pub));
	chratos::keypair key5;
	chratos::send_block block6 (block5.hash (), key5.pub, 100, key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block6).code);
	ASSERT_EQ (chratos::genesis_amount - 200, store.representation_get (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key3.pub));
	ASSERT_EQ (100, store.representation_get (transaction, key4.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key5.pub));
	chratos::keypair key6;
	chratos::open_block block7 (block6.hash (), key6.pub, key5.pub, key5.prv, key5.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block7).code);
	ASSERT_EQ (chratos::genesis_amount - 200, store.representation_get (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key3.pub));
	ASSERT_EQ (100, store.representation_get (transaction, key4.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key5.pub));
	ASSERT_EQ (100, store.representation_get (transaction, key6.pub));
	chratos::send_block block8 (block6.hash (), key5.pub, 0, key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block8).code);
	ASSERT_EQ (chratos::genesis_amount - 200, store.representation_get (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key3.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key4.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key5.pub));
	ASSERT_EQ (100, store.representation_get (transaction, key6.pub));
	chratos::receive_block block9 (block7.hash (), block8.hash (), key5.prv, key5.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block9).code);
	ASSERT_EQ (chratos::genesis_amount - 200, store.representation_get (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key3.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key4.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key5.pub));
	ASSERT_EQ (200, store.representation_get (transaction, key6.pub));
}

TEST (ledger, double_open)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key2;
	chratos::send_block send1 (genesis.hash (), key2.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::open_block open1 (send1.hash (), key2.pub, key2.pub, key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, open1).code);
	chratos::open_block open2 (send1.hash (), chratos::test_genesis_key.pub, key2.pub, key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::fork, ledger.process (transaction, open2).code);
}

TEST (ledegr, double_receive)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key2;
	chratos::send_block send1 (genesis.hash (), key2.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::open_block open1 (send1.hash (), key2.pub, key2.pub, key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, open1).code);
	chratos::receive_block receive1 (open1.hash (), send1.hash (), key2.prv, key2.pub, 0);
	ASSERT_EQ (chratos::process_result::unreceivable, ledger.process (transaction, receive1).code);
}

TEST (votes, check_signature)
{
	chratos::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	chratos::genesis genesis;
	chratos::keypair key1;
	auto send1 (std::make_shared<chratos::send_block> (genesis.hash (), key1.pub, chratos::genesis_amount - 100, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	auto transaction (node1.store.tx_begin (true));
	ASSERT_EQ (chratos::process_result::progress, node1.ledger.process (transaction, *send1).code);
	auto node_l (system.nodes[0]);
	node1.active.start (send1);
	auto votes1 (node1.active.roots.find (send1->root ())->election);
	ASSERT_EQ (1, votes1->last_votes.size ());
	auto vote1 (std::make_shared<chratos::vote> (chratos::test_genesis_key.pub, chratos::test_genesis_key.prv, 1, send1));
	vote1->signature.bytes[0] ^= 1;
	ASSERT_EQ (chratos::vote_code::invalid, node1.vote_processor.vote_blocking (transaction, vote1, chratos::endpoint (boost::asio::ip::address_v6 (), 0)));
	vote1->signature.bytes[0] ^= 1;
	ASSERT_EQ (chratos::vote_code::vote, node1.vote_processor.vote_blocking (transaction, vote1, chratos::endpoint (boost::asio::ip::address_v6 (), 0)));
	ASSERT_EQ (chratos::vote_code::replay, node1.vote_processor.vote_blocking (transaction, vote1, chratos::endpoint (boost::asio::ip::address_v6 (), 0)));
}

TEST (votes, add_one)
{
	chratos::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	chratos::genesis genesis;
	chratos::keypair key1;
	auto send1 (std::make_shared<chratos::send_block> (genesis.hash (), key1.pub, chratos::genesis_amount - 100, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	{
		auto transaction (node1.store.tx_begin (true));
		ASSERT_EQ (chratos::process_result::progress, node1.ledger.process (transaction, *send1).code);
	}
	auto node_l (system.nodes[0]);
	node1.active.start (send1);
	auto votes1 (node1.active.roots.find (send1->root ())->election);
	ASSERT_EQ (1, votes1->last_votes.size ());
	auto vote1 (std::make_shared<chratos::vote> (chratos::test_genesis_key.pub, chratos::test_genesis_key.prv, 1, send1));
	ASSERT_FALSE (node1.active.vote (vote1));
	auto vote2 (std::make_shared<chratos::vote> (chratos::test_genesis_key.pub, chratos::test_genesis_key.prv, 2, send1));
	ASSERT_FALSE (node1.active.vote (vote2));
	ASSERT_EQ (2, votes1->last_votes.size ());
	auto existing1 (votes1->last_votes.find (chratos::test_genesis_key.pub));
	ASSERT_NE (votes1->last_votes.end (), existing1);
	ASSERT_EQ (send1->hash (), existing1->second.hash);
	auto transaction (system.nodes[0]->store.tx_begin ());
	auto winner (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (chratos::genesis_amount - 100, winner.first);
}

TEST (votes, add_two)
{
	chratos::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	chratos::genesis genesis;
	chratos::keypair key1;
	auto send1 (std::make_shared<chratos::send_block> (genesis.hash (), key1.pub, chratos::genesis_amount - 100, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	{
		auto transaction (node1.store.tx_begin (true));
		ASSERT_EQ (chratos::process_result::progress, node1.ledger.process (transaction, *send1).code);
	}
	auto node_l (system.nodes[0]);
	node1.active.start (send1);
	auto votes1 (node1.active.roots.find (send1->root ())->election);
	auto vote1 (std::make_shared<chratos::vote> (chratos::test_genesis_key.pub, chratos::test_genesis_key.prv, 1, send1));
	ASSERT_FALSE (node1.active.vote (vote1));
	chratos::keypair key2;
	auto send2 (std::make_shared<chratos::send_block> (genesis.hash (), key2.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	auto vote2 (std::make_shared<chratos::vote> (key2.pub, key2.prv, 1, send2));
	ASSERT_FALSE (node1.active.vote (vote2));
	ASSERT_EQ (3, votes1->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (chratos::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes1->last_votes[chratos::test_genesis_key.pub].hash);
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (key2.pub));
	ASSERT_EQ (send2->hash (), votes1->last_votes[key2.pub].hash);
	auto transaction (system.nodes[0]->store.tx_begin ());
	auto winner (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner.second);
}

// Higher sequence numbers change the vote
TEST (votes, add_existing)
{
	chratos::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	chratos::genesis genesis;
	chratos::keypair key1;
	auto send1 (std::make_shared<chratos::send_block> (genesis.hash (), key1.pub, chratos::genesis_amount - chratos::Gchr_ratio, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	{
		auto transaction (node1.store.tx_begin (true));
		ASSERT_EQ (chratos::process_result::progress, node1.ledger.process (transaction, *send1).code);
	}
	auto node_l (system.nodes[0]);
	node1.active.start (send1);
	auto votes1 (node1.active.roots.find (send1->root ())->election);
	auto vote1 (std::make_shared<chratos::vote> (chratos::test_genesis_key.pub, chratos::test_genesis_key.prv, 1, send1));
	ASSERT_FALSE (node1.active.vote (vote1));
	ASSERT_FALSE (node1.active.publish (send1));
	ASSERT_EQ (1, votes1->last_votes[chratos::test_genesis_key.pub].sequence);
	chratos::keypair key2;
	auto send2 (std::make_shared<chratos::send_block> (genesis.hash (), key2.pub, chratos::genesis_amount - chratos::Gchr_ratio, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	auto vote2 (std::make_shared<chratos::vote> (chratos::test_genesis_key.pub, chratos::test_genesis_key.prv, 2, send2));
	// Pretend we've waited the timeout
	votes1->last_votes[chratos::test_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	ASSERT_FALSE (node1.active.vote (vote2));
	ASSERT_FALSE (node1.active.publish (send2));
	ASSERT_EQ (2, votes1->last_votes[chratos::test_genesis_key.pub].sequence);
	// Also resend the old vote, and see if we respect the sequence number
	votes1->last_votes[chratos::test_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	ASSERT_TRUE (node1.active.vote (vote1));
	ASSERT_EQ (2, votes1->last_votes[chratos::test_genesis_key.pub].sequence);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (chratos::test_genesis_key.pub));
	ASSERT_EQ (send2->hash (), votes1->last_votes[chratos::test_genesis_key.pub].hash);
	auto transaction (system.nodes[0]->store.tx_begin ());
	auto winner (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send2, *winner.second);
}

// Lower sequence numbers are ignored
TEST (votes, add_old)
{
	chratos::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	chratos::genesis genesis;
	chratos::keypair key1;
	auto send1 (std::make_shared<chratos::send_block> (genesis.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	auto transaction (node1.store.tx_begin (true));
	ASSERT_EQ (chratos::process_result::progress, node1.ledger.process (transaction, *send1).code);
	auto node_l (system.nodes[0]);
	node1.active.start (send1);
	auto votes1 (node1.active.roots.find (send1->root ())->election);
	auto vote1 (std::make_shared<chratos::vote> (chratos::test_genesis_key.pub, chratos::test_genesis_key.prv, 2, send1));
	node1.vote_processor.vote_blocking (transaction, vote1, node1.network.endpoint ());
	chratos::keypair key2;
	auto send2 (std::make_shared<chratos::send_block> (genesis.hash (), key2.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	auto vote2 (std::make_shared<chratos::vote> (chratos::test_genesis_key.pub, chratos::test_genesis_key.prv, 1, send2));
	votes1->last_votes[chratos::test_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	node1.vote_processor.vote_blocking (transaction, vote2, node1.network.endpoint ());
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (chratos::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes1->last_votes[chratos::test_genesis_key.pub].hash);
	auto winner (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner.second);
}

// Lower sequence numbers are accepted for different accounts
TEST (votes, add_old_different_account)
{
	chratos::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	chratos::genesis genesis;
	chratos::keypair key1;
	auto send1 (std::make_shared<chratos::send_block> (genesis.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	auto send2 (std::make_shared<chratos::send_block> (send1->hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	{
		auto transaction (node1.store.tx_begin (true));
		ASSERT_EQ (chratos::process_result::progress, node1.ledger.process (transaction, *send1).code);
		ASSERT_EQ (chratos::process_result::progress, node1.ledger.process (transaction, *send2).code);
	}
	auto node_l (system.nodes[0]);
	node1.active.start (send1);
	node1.active.start (send2);
	auto votes1 (node1.active.roots.find (send1->root ())->election);
	auto votes2 (node1.active.roots.find (send2->root ())->election);
	ASSERT_EQ (1, votes1->last_votes.size ());
	ASSERT_EQ (1, votes2->last_votes.size ());
	auto vote1 (std::make_shared<chratos::vote> (chratos::test_genesis_key.pub, chratos::test_genesis_key.prv, 2, send1));
	auto transaction (system.nodes[0]->store.tx_begin ());
	auto vote_result1 (node1.vote_processor.vote_blocking (transaction, vote1, node1.network.endpoint ()));
	ASSERT_EQ (chratos::vote_code::vote, vote_result1);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_EQ (1, votes2->last_votes.size ());
	auto vote2 (std::make_shared<chratos::vote> (chratos::test_genesis_key.pub, chratos::test_genesis_key.prv, 1, send2));
	auto vote_result2 (node1.vote_processor.vote_blocking (transaction, vote2, node1.network.endpoint ()));
	ASSERT_EQ (chratos::vote_code::vote, vote_result2);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_EQ (2, votes2->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (chratos::test_genesis_key.pub));
	ASSERT_NE (votes2->last_votes.end (), votes2->last_votes.find (chratos::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes1->last_votes[chratos::test_genesis_key.pub].hash);
	ASSERT_EQ (send2->hash (), votes2->last_votes[chratos::test_genesis_key.pub].hash);
	auto winner1 (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner1.second);
	auto winner2 (*votes2->tally (transaction).begin ());
	ASSERT_EQ (*send2, *winner2.second);
}

// The voting cooldown is respected
TEST (votes, add_cooldown)
{
	chratos::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	chratos::genesis genesis;
	chratos::keypair key1;
	auto send1 (std::make_shared<chratos::send_block> (genesis.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	auto transaction (node1.store.tx_begin (true));
	ASSERT_EQ (chratos::process_result::progress, node1.ledger.process (transaction, *send1).code);
	auto node_l (system.nodes[0]);
	node1.active.start (send1);
	auto votes1 (node1.active.roots.find (send1->root ())->election);
	auto vote1 (std::make_shared<chratos::vote> (chratos::test_genesis_key.pub, chratos::test_genesis_key.prv, 1, send1));
	node1.vote_processor.vote_blocking (transaction, vote1, node1.network.endpoint ());
	chratos::keypair key2;
	auto send2 (std::make_shared<chratos::send_block> (genesis.hash (), key2.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	auto vote2 (std::make_shared<chratos::vote> (chratos::test_genesis_key.pub, chratos::test_genesis_key.prv, 2, send2));
	node1.vote_processor.vote_blocking (transaction, vote2, node1.network.endpoint ());
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (chratos::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes1->last_votes[chratos::test_genesis_key.pub].hash);
	auto winner (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner.second);
}

// Query for block successor
TEST (ledger, successor)
{
	chratos::system system (24000, 1);
	chratos::keypair key1;
	chratos::genesis genesis;
	chratos::send_block send1 (genesis.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto transaction (system.nodes[0]->store.tx_begin (true));
	ASSERT_EQ (chratos::process_result::progress, system.nodes[0]->ledger.process (transaction, send1).code);
	ASSERT_EQ (send1, *system.nodes[0]->ledger.successor (transaction, genesis.hash ()));
	ASSERT_EQ (*genesis.open, *system.nodes[0]->ledger.successor (transaction, genesis.open->root ()));
	ASSERT_EQ (nullptr, system.nodes[0]->ledger.successor (transaction, 0));
}

TEST (ledger, fail_change_old)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::change_block block (genesis.hash (), key1.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (chratos::process_result::progress, result1.code);
	auto result2 (ledger.process (transaction, block));
	ASSERT_EQ (chratos::process_result::old, result2.code);
}

TEST (ledger, fail_change_gap_previous)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::change_block block (1, key1.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (chratos::process_result::gap_previous, result1.code);
}

TEST (ledger, fail_change_bad_signature)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::change_block block (genesis.hash (), key1.pub, chratos::keypair ().prv, 0, 0);
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (chratos::process_result::bad_signature, result1.code);
}

TEST (ledger, fail_change_fork)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::change_block block1 (genesis.hash (), key1.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (chratos::process_result::progress, result1.code);
	chratos::keypair key2;
	chratos::change_block block2 (genesis.hash (), key2.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (chratos::process_result::fork, result2.code);
}

TEST (ledger, fail_send_old)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block (genesis.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (chratos::process_result::progress, result1.code);
	auto result2 (ledger.process (transaction, block));
	ASSERT_EQ (chratos::process_result::old, result2.code);
}

TEST (ledger, fail_send_gap_previous)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block (1, key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (chratos::process_result::gap_previous, result1.code);
}

TEST (ledger, fail_send_bad_signature)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block (genesis.hash (), key1.pub, 1, chratos::keypair ().prv, 0, 0);
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (chratos::process_result::bad_signature, result1.code);
}

TEST (ledger, fail_send_negative_spend)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block1 (genesis.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block1).code);
	chratos::keypair key2;
	chratos::send_block block2 (block1.hash (), key2.pub, 2, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::negative_spend, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_send_fork)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block1 (genesis.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block1).code);
	chratos::keypair key2;
	chratos::send_block block2 (genesis.hash (), key2.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::fork, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_old)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block1 (genesis.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block1).code);
	chratos::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (chratos::process_result::old, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_gap_source)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::open_block block2 (1, 1, key1.pub, key1.prv, key1.pub, 0);
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (chratos::process_result::gap_source, result2.code);
}

TEST (ledger, fail_open_bad_signature)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block1 (genesis.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block1).code);
	chratos::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, 0);
	block2.signature.clear ();
	ASSERT_EQ (chratos::process_result::bad_signature, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_fork_previous)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block1 (genesis.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block1).code);
	chratos::send_block block2 (block1.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block2).code);
	chratos::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block3).code);
	chratos::open_block block4 (block2.hash (), 1, key1.pub, key1.prv, key1.pub, 0);
	ASSERT_EQ (chratos::process_result::fork, ledger.process (transaction, block4).code);
}

TEST (ledger, fail_open_account_mismatch)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block1 (genesis.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block1).code);
	chratos::keypair badkey;
	chratos::open_block block2 (block1.hash (), 1, badkey.pub, badkey.prv, badkey.pub, 0);
	ASSERT_NE (chratos::process_result::progress, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_receive_old)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block1 (genesis.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block1).code);
	chratos::send_block block2 (block1.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block2).code);
	chratos::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block3).code);
	chratos::receive_block block4 (block3.hash (), block2.hash (), key1.prv, key1.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (chratos::process_result::old, ledger.process (transaction, block4).code);
}

TEST (ledger, fail_receive_gap_source)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block1 (genesis.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (chratos::process_result::progress, result1.code);
	chratos::send_block block2 (block1.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (chratos::process_result::progress, result2.code);
	chratos::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, 0);
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (chratos::process_result::progress, result3.code);
	chratos::receive_block block4 (block3.hash (), 1, key1.prv, key1.pub, 0);
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (chratos::process_result::gap_source, result4.code);
}

TEST (ledger, fail_receive_overreceive)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block1 (genesis.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (chratos::process_result::progress, result1.code);
	chratos::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, 0);
	auto result3 (ledger.process (transaction, block2));
	ASSERT_EQ (chratos::process_result::progress, result3.code);
	chratos::receive_block block3 (block2.hash (), block1.hash (), key1.prv, key1.pub, 0);
	auto result4 (ledger.process (transaction, block3));
	ASSERT_EQ (chratos::process_result::unreceivable, result4.code);
}

TEST (ledger, fail_receive_bad_signature)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block1 (genesis.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (chratos::process_result::progress, result1.code);
	chratos::send_block block2 (block1.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (chratos::process_result::progress, result2.code);
	chratos::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, 0);
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (chratos::process_result::progress, result3.code);
	chratos::receive_block block4 (block3.hash (), block2.hash (), chratos::keypair ().prv, 0, 0);
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (chratos::process_result::bad_signature, result4.code);
}

TEST (ledger, fail_receive_gap_previous_opened)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block1 (genesis.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (chratos::process_result::progress, result1.code);
	chratos::send_block block2 (block1.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (chratos::process_result::progress, result2.code);
	chratos::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, 0);
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (chratos::process_result::progress, result3.code);
	chratos::receive_block block4 (1, block2.hash (), key1.prv, key1.pub, 0);
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (chratos::process_result::gap_previous, result4.code);
}

TEST (ledger, fail_receive_gap_previous_unopened)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block1 (genesis.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (chratos::process_result::progress, result1.code);
	chratos::send_block block2 (block1.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (chratos::process_result::progress, result2.code);
	chratos::receive_block block3 (1, block2.hash (), key1.prv, key1.pub, 0);
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (chratos::process_result::gap_previous, result3.code);
}

TEST (ledger, fail_receive_fork_previous)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block1 (genesis.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (chratos::process_result::progress, result1.code);
	chratos::send_block block2 (block1.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (chratos::process_result::progress, result2.code);
	chratos::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, 0);
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (chratos::process_result::progress, result3.code);
	chratos::keypair key2;
	chratos::send_block block4 (block3.hash (), key1.pub, 1, key1.prv, key1.pub, 0);
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (chratos::process_result::progress, result4.code);
	chratos::receive_block block5 (block3.hash (), block2.hash (), key1.prv, key1.pub, 0);
	auto result5 (ledger.process (transaction, block5));
	ASSERT_EQ (chratos::process_result::fork, result5.code);
}

TEST (ledger, fail_receive_received_source)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::send_block block1 (genesis.hash (), key1.pub, 2, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (chratos::process_result::progress, result1.code);
	chratos::send_block block2 (block1.hash (), key1.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (chratos::process_result::progress, result2.code);
	chratos::send_block block6 (block2.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto result6 (ledger.process (transaction, block6));
	ASSERT_EQ (chratos::process_result::progress, result6.code);
	chratos::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, 0);
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (chratos::process_result::progress, result3.code);
	chratos::keypair key2;
	chratos::send_block block4 (block3.hash (), key1.pub, 1, key1.prv, key1.pub, 0);
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (chratos::process_result::progress, result4.code);
	chratos::receive_block block5 (block4.hash (), block2.hash (), key1.prv, key1.pub, 0);
	auto result5 (ledger.process (transaction, block5));
	ASSERT_EQ (chratos::process_result::progress, result5.code);
	chratos::receive_block block7 (block3.hash (), block2.hash (), key1.prv, key1.pub, 0);
	auto result7 (ledger.process (transaction, block7));
	ASSERT_EQ (chratos::process_result::fork, result7.code);
}

TEST (ledger, latest_empty)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::keypair key;
	auto transaction (store.tx_begin ());
	auto latest (ledger.latest (transaction, key.pub));
	ASSERT_TRUE (latest.is_zero ());
}

TEST (ledger, latest_root)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_FALSE (init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key;
	ASSERT_EQ (key.pub, ledger.latest_root (transaction, key.pub));
	auto hash1 (ledger.latest (transaction, chratos::test_genesis_key.pub));
	chratos::send_block send (hash1, 0, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (send.hash (), ledger.latest_root (transaction, chratos::test_genesis_key.pub));
}

TEST (ledger, change_representative_move_representation)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::keypair key1;
	auto transaction (store.tx_begin (true));
	chratos::genesis genesis;
	store.initialize (transaction, genesis);
	auto hash1 (genesis.hash ());
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::test_genesis_key.pub));
	chratos::send_block send (hash1, key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (0, ledger.weight (transaction, chratos::test_genesis_key.pub));
	chratos::keypair key2;
	chratos::change_block change (send.hash (), key2.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, change).code);
	chratos::keypair key3;
	chratos::open_block open (send.hash (), key3.pub, key1.pub, key1.prv, key1.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, key3.pub));
}

TEST (ledger, send_open_receive_rollback)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	auto transaction (store.tx_begin (true));
	chratos::genesis genesis;
	store.initialize (transaction, genesis);
	chratos::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info1));
	chratos::keypair key1;
	chratos::send_block send1 (info1.head, key1.pub, chratos::genesis_amount - 50, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto return1 (ledger.process (transaction, send1));
	ASSERT_EQ (chratos::process_result::progress, return1.code);
	chratos::send_block send2 (send1.hash (), key1.pub, chratos::genesis_amount - 100, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto return2 (ledger.process (transaction, send2));
	ASSERT_EQ (chratos::process_result::progress, return2.code);
	chratos::keypair key2;
	chratos::open_block open (send2.hash (), key2.pub, key1.pub, key1.prv, key1.pub, 0);
	auto return4 (ledger.process (transaction, open));
	ASSERT_EQ (chratos::process_result::progress, return4.code);
	chratos::receive_block receive (open.hash (), send1.hash (), key1.prv, key1.pub, 0);
	auto return5 (ledger.process (transaction, receive));
	ASSERT_EQ (chratos::process_result::progress, return5.code);
	chratos::keypair key3;
	ASSERT_EQ (100, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (chratos::genesis_amount - 100, ledger.weight (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	chratos::change_block change1 (send2.hash (), key3.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	auto return6 (ledger.process (transaction, change1));
	ASSERT_EQ (chratos::process_result::progress, return6.code);
	ASSERT_EQ (100, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (chratos::genesis_amount - 100, ledger.weight (transaction, key3.pub));
	ledger.rollback (transaction, receive.hash ());
	ASSERT_EQ (50, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (chratos::genesis_amount - 100, ledger.weight (transaction, key3.pub));
	ledger.rollback (transaction, open.hash ());
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, chratos::test_genesis_key.pub));
	ASSERT_EQ (chratos::genesis_amount - 100, ledger.weight (transaction, key3.pub));
	ledger.rollback (transaction, change1.hash ());
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (chratos::genesis_amount - 100, ledger.weight (transaction, chratos::test_genesis_key.pub));
	ledger.rollback (transaction, send2.hash ());
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (chratos::genesis_amount - 50, ledger.weight (transaction, chratos::test_genesis_key.pub));
	ledger.rollback (transaction, send1.hash ());
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (chratos::genesis_amount - 0, ledger.weight (transaction, chratos::test_genesis_key.pub));
}

TEST (ledger, bootstrap_rep_weight)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::account_info info1;
	chratos::keypair key2;
	chratos::genesis genesis;
	{
		auto transaction (store.tx_begin (true));
		store.initialize (transaction, genesis);
		ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info1));
		chratos::send_block send (info1.head, key2.pub, std::numeric_limits<chratos::uint128_t>::max () - 50, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
		ledger.process (transaction, send);
	}
	{
		auto transaction (store.tx_begin ());
		ledger.bootstrap_weight_max_blocks = 3;
		ledger.bootstrap_weights[key2.pub] = 1000;
		ASSERT_EQ (1000, ledger.weight (transaction, key2.pub));
	}
	{
		auto transaction (store.tx_begin (true));
		ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info1));
		chratos::send_block send (info1.head, key2.pub, std::numeric_limits<chratos::uint128_t>::max () - 100, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
		ledger.process (transaction, send);
	}
	{
		auto transaction (store.tx_begin ());
		ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	}
}

TEST (ledger, block_destination_source)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair dest;
	chratos::uint128_t balance (chratos::genesis_amount);
	balance -= chratos::Gchr_ratio;
	chratos::send_block block1 (genesis.hash (), dest.pub, balance, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	balance -= chratos::Gchr_ratio;
	chratos::send_block block2 (block1.hash (), chratos::genesis_account, balance, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	balance += chratos::Gchr_ratio;
	chratos::receive_block block3 (block2.hash (), block2.hash (), chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	balance -= chratos::Gchr_ratio;
	chratos::state_block block4 (chratos::genesis_account, block3.hash (), chratos::genesis_account, balance, dest.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	balance -= chratos::Gchr_ratio;
	chratos::state_block block5 (chratos::genesis_account, block4.hash (), chratos::genesis_account, balance, chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	balance += chratos::Gchr_ratio;
	chratos::state_block block6 (chratos::genesis_account, block5.hash (), chratos::genesis_account, balance, block5.hash (), chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block1).code);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block3).code);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block5).code);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block6).code);
	ASSERT_EQ (balance, ledger.balance (transaction, block6.hash ()));
	ASSERT_EQ (dest.pub, ledger.block_destination (transaction, block1));
	ASSERT_TRUE (ledger.block_source (transaction, block1).is_zero ());
	ASSERT_EQ (chratos::genesis_account, ledger.block_destination (transaction, block2));
	ASSERT_TRUE (ledger.block_source (transaction, block2).is_zero ());
	ASSERT_TRUE (ledger.block_destination (transaction, block3).is_zero ());
	ASSERT_EQ (block2.hash (), ledger.block_source (transaction, block3));
	ASSERT_EQ (dest.pub, ledger.block_destination (transaction, block4));
	ASSERT_TRUE (ledger.block_source (transaction, block4).is_zero ());
	ASSERT_EQ (chratos::genesis_account, ledger.block_destination (transaction, block5));
	ASSERT_TRUE (ledger.block_source (transaction, block5).is_zero ());
	ASSERT_TRUE (ledger.block_destination (transaction, block6).is_zero ());
	ASSERT_EQ (block5.hash (), ledger.block_source (transaction, block6));
}

TEST (ledger, state_account)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_EQ (chratos::genesis_account, ledger.account (transaction, send1.hash ()));
}

TEST (ledger, state_send_receive)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.weight (transaction, chratos::genesis_account));
	ASSERT_TRUE (store.pending_exists (transaction, chratos::pending_key (chratos::genesis_account, send1.hash ())));
	chratos::state_block receive1 (chratos::genesis_account, send1.hash (), chratos::genesis_account, chratos::genesis_amount, send1.hash (), chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store.block_exists (transaction, receive1.hash ()));
	auto receive2 (store.block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (chratos::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::genesis_account));
	ASSERT_FALSE (store.pending_exists (transaction, chratos::pending_key (chratos::genesis_account, send1.hash ())));
}

TEST (ledger, state_receive)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::send_block send1 (genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.weight (transaction, chratos::genesis_account));
	chratos::state_block receive1 (chratos::genesis_account, send1.hash (), chratos::genesis_account, chratos::genesis_amount, send1.hash (), chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store.block_exists (transaction, receive1.hash ()));
	auto receive2 (store.block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (chratos::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::genesis_account));
}

TEST (ledger, state_rep_change)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair rep;
	chratos::state_block change1 (chratos::genesis_account, genesis.hash (), rep.pub, chratos::genesis_amount, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, change1).code);
	ASSERT_TRUE (store.block_exists (transaction, change1.hash ()));
	auto change2 (store.block_get (transaction, change1.hash ()));
	ASSERT_NE (nullptr, change2);
	ASSERT_EQ (change1, *change2);
	ASSERT_EQ (chratos::genesis_amount, ledger.balance (transaction, change1.hash ()));
	ASSERT_EQ (0, ledger.amount (transaction, change1.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, chratos::genesis_account));
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_open)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair destination;
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, destination.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.weight (transaction, chratos::genesis_account));
	ASSERT_TRUE (store.pending_exists (transaction, chratos::pending_key (destination.pub, send1.hash ())));
	chratos::state_block open1 (destination.pub, 0, chratos::genesis_account, chratos::Gchr_ratio, send1.hash (), destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_FALSE (store.pending_exists (transaction, chratos::pending_key (destination.pub, send1.hash ())));
	ASSERT_TRUE (store.block_exists (transaction, open1.hash ()));
	auto open2 (store.block_get (transaction, open1.hash ()));
	ASSERT_NE (nullptr, open2);
	ASSERT_EQ (open1, *open2);
	ASSERT_EQ (chratos::Gchr_ratio, ledger.balance (transaction, open1.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, open1.hash ()));
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::genesis_account));
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, send_after_state_fail)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::send_block send2 (send1.hash (), chratos::genesis_account, chratos::genesis_amount - (2 * chratos::Gchr_ratio), chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::block_position, ledger.process (transaction, send2).code);
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, receive_after_state_fail)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::receive_block receive1 (send1.hash (), send1.hash (), chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::block_position, ledger.process (transaction, receive1).code);
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, change_after_state_fail)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::keypair rep;
	chratos::change_block change1 (send1.hash (), rep.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::block_position, ledger.process (transaction, change1).code);
}

TEST (ledger, state_unreceivable_fail)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::send_block send1 (genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.weight (transaction, chratos::genesis_account));
	chratos::state_block receive1 (chratos::genesis_account, send1.hash (), chratos::genesis_account, chratos::genesis_amount, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::gap_source, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_receive_bad_amount_fail)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::send_block send1 (genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.weight (transaction, chratos::genesis_account));
	chratos::state_block receive1 (chratos::genesis_account, send1.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, send1.hash (), chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::balance_mismatch, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_no_link_amount_fail)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::keypair rep;
	chratos::state_block change1 (chratos::genesis_account, send1.hash (), rep.pub, chratos::genesis_amount, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::balance_mismatch, ledger.process (transaction, change1).code);
}

TEST (ledger, state_receive_wrong_account_fail)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.weight (transaction, chratos::genesis_account));
	chratos::keypair key;
	chratos::state_block receive1 (key.pub, 0, chratos::genesis_account, chratos::Gchr_ratio, send1.hash (), key.prv, key.pub, 0);
	ASSERT_EQ (chratos::process_result::unreceivable, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_open_state_fork)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair destination;
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, destination.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::state_block open1 (destination.pub, 0, chratos::genesis_account, chratos::Gchr_ratio, send1.hash (), destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, open1).code);
	chratos::open_block open2 (send1.hash (), chratos::genesis_account, destination.pub, destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::fork, ledger.process (transaction, open2).code);
	ASSERT_EQ (open1.root (), open2.root ());
}

TEST (ledger, state_state_open_fork)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair destination;
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, destination.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::open_block open1 (send1.hash (), chratos::genesis_account, destination.pub, destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, open1).code);
	chratos::state_block open2 (destination.pub, 0, chratos::genesis_account, chratos::Gchr_ratio, send1.hash (), destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::fork, ledger.process (transaction, open2).code);
	ASSERT_EQ (open1.root (), open2.root ());
}

TEST (ledger, state_open_previous_fail)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair destination;
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, destination.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::state_block open1 (destination.pub, destination.pub, chratos::genesis_account, chratos::Gchr_ratio, send1.hash (), destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::gap_previous, ledger.process (transaction, open1).code);
}

TEST (ledger, state_open_source_fail)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair destination;
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, destination.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::state_block open1 (destination.pub, 0, chratos::genesis_account, 0, 0, destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::gap_source, ledger.process (transaction, open1).code);
}

TEST (ledger, state_send_change)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair rep;
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), rep.pub, chratos::genesis_amount - chratos::Gchr_ratio, chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, chratos::genesis_account));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_receive_change)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.weight (transaction, chratos::genesis_account));
	chratos::keypair rep;
	chratos::state_block receive1 (chratos::genesis_account, send1.hash (), rep.pub, chratos::genesis_amount, send1.hash (), chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store.block_exists (transaction, receive1.hash ()));
	auto receive2 (store.block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (chratos::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, chratos::genesis_account));
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_open_old)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair destination;
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, destination.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::open_block open1 (send1.hash (), chratos::genesis_account, destination.pub, destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_EQ (chratos::Gchr_ratio, ledger.balance (transaction, open1.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, open1.hash ()));
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::genesis_account));
}

TEST (ledger, state_receive_old)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair destination;
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, destination.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::state_block send2 (chratos::genesis_account, send1.hash (), chratos::genesis_account, chratos::genesis_amount - (2 * chratos::Gchr_ratio), destination.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send2).code);
	chratos::open_block open1 (send1.hash (), chratos::genesis_account, destination.pub, destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, open1).code);
	chratos::receive_block receive1 (open1.hash (), send2.hash (), destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_EQ (2 * chratos::Gchr_ratio, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::genesis_account));
}

TEST (ledger, state_rollback_send)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.account_balance (transaction, chratos::genesis_account));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.weight (transaction, chratos::genesis_account));
	chratos::pending_info info;
	ASSERT_FALSE (store.pending_get (transaction, chratos::pending_key (chratos::genesis_account, send1.hash ()), info));
	ASSERT_EQ (chratos::genesis_account, info.source);
	ASSERT_EQ (chratos::Gchr_ratio, info.amount.number ());
	ledger.rollback (transaction, send1.hash ());
	ASSERT_FALSE (store.block_exists (transaction, send1.hash ()));
	ASSERT_EQ (chratos::genesis_amount, ledger.account_balance (transaction, chratos::genesis_account));
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::genesis_account));
	ASSERT_FALSE (store.pending_exists (transaction, chratos::pending_key (chratos::genesis_account, send1.hash ())));
	ASSERT_TRUE (store.block_successor (transaction, genesis.hash ()).is_zero ());
}

TEST (ledger, state_rollback_receive)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::state_block receive1 (chratos::genesis_account, send1.hash (), chratos::genesis_account, chratos::genesis_amount, send1.hash (), chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (store.pending_exists (transaction, chratos::pending_key (chratos::genesis_account, receive1.hash ())));
	ledger.rollback (transaction, receive1.hash ());
	chratos::pending_info info;
	ASSERT_FALSE (store.pending_get (transaction, chratos::pending_key (chratos::genesis_account, send1.hash ()), info));
	ASSERT_EQ (chratos::genesis_account, info.source);
	ASSERT_EQ (chratos::Gchr_ratio, info.amount.number ());
	ASSERT_FALSE (store.block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.account_balance (transaction, chratos::genesis_account));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.weight (transaction, chratos::genesis_account));
}

TEST (ledger, state_rollback_received_send)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key;
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, key.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::state_block receive1 (key.pub, 0, key.pub, chratos::Gchr_ratio, send1.hash (), key.prv, key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (store.pending_exists (transaction, chratos::pending_key (chratos::genesis_account, receive1.hash ())));
	ledger.rollback (transaction, send1.hash ());
	ASSERT_FALSE (store.pending_exists (transaction, chratos::pending_key (chratos::genesis_account, send1.hash ())));
	ASSERT_FALSE (store.block_exists (transaction, send1.hash ()));
	ASSERT_FALSE (store.block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (chratos::genesis_amount, ledger.account_balance (transaction, chratos::genesis_account));
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::genesis_account));
	ASSERT_EQ (0, ledger.account_balance (transaction, key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key.pub));
}

TEST (ledger, state_rep_change_rollback)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair rep;
	chratos::state_block change1 (chratos::genesis_account, genesis.hash (), rep.pub, chratos::genesis_amount, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, change1).code);
	ledger.rollback (transaction, change1.hash ());
	ASSERT_FALSE (store.block_exists (transaction, change1.hash ()));
	ASSERT_EQ (chratos::genesis_amount, ledger.account_balance (transaction, chratos::genesis_account));
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::genesis_account));
	ASSERT_EQ (0, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_open_rollback)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair destination;
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, destination.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::state_block open1 (destination.pub, 0, chratos::genesis_account, chratos::Gchr_ratio, send1.hash (), destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, open1).code);
	ledger.rollback (transaction, open1.hash ());
	ASSERT_FALSE (store.block_exists (transaction, open1.hash ()));
	ASSERT_EQ (0, ledger.account_balance (transaction, destination.pub));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.weight (transaction, chratos::genesis_account));
	chratos::pending_info info;
	ASSERT_FALSE (store.pending_get (transaction, chratos::pending_key (destination.pub, send1.hash ()), info));
	ASSERT_EQ (chratos::genesis_account, info.source);
	ASSERT_EQ (chratos::Gchr_ratio, info.amount.number ());
}

TEST (ledger, state_send_change_rollback)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair rep;
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), rep.pub, chratos::genesis_amount - chratos::Gchr_ratio, chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	ledger.rollback (transaction, send1.hash ());
	ASSERT_FALSE (store.block_exists (transaction, send1.hash ()));
	ASSERT_EQ (chratos::genesis_amount, ledger.account_balance (transaction, chratos::genesis_account));
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, chratos::genesis_account));
	ASSERT_EQ (0, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_receive_change_rollback)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::keypair rep;
	chratos::state_block receive1 (chratos::genesis_account, send1.hash (), rep.pub, chratos::genesis_amount, send1.hash (), chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, receive1).code);
	ledger.rollback (transaction, receive1.hash ());
	ASSERT_FALSE (store.block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.account_balance (transaction, chratos::genesis_account));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.weight (transaction, chratos::genesis_account));
	ASSERT_EQ (0, ledger.weight (transaction, rep.pub));
}

TEST (ledger, epoch_blocks_general)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::keypair epoch_key;
	chratos::ledger ledger (store, stats, 123, epoch_key.pub);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair destination;
	chratos::state_block epoch1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount, 123, epoch_key.prv, epoch_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, epoch1).code);
	chratos::state_block epoch2 (chratos::genesis_account, epoch1.hash (), chratos::genesis_account, chratos::genesis_amount, 123, epoch_key.prv, epoch_key.pub, 0);
	ASSERT_EQ (chratos::process_result::block_position, ledger.process (transaction, epoch2).code);
	chratos::account_info genesis_info;
	ASSERT_FALSE (ledger.store.account_get (transaction, chratos::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch, chratos::epoch::epoch_1);
	ledger.rollback (transaction, epoch1.hash ());
	ASSERT_FALSE (ledger.store.account_get (transaction, chratos::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch, chratos::epoch::epoch_0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_FALSE (ledger.store.account_get (transaction, chratos::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch, chratos::epoch::epoch_1);
	chratos::change_block change1 (epoch1.hash (), chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::block_position, ledger.process (transaction, change1).code);
	chratos::state_block send1 (chratos::genesis_account, epoch1.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, destination.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::open_block open1 (send1.hash (), chratos::genesis_account, destination.pub, destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::unreceivable, ledger.process (transaction, open1).code);
	chratos::state_block epoch3 (destination.pub, 0, chratos::genesis_account, 0, 123, epoch_key.prv, epoch_key.pub, 0);
	ASSERT_EQ (chratos::process_result::representative_mismatch, ledger.process (transaction, epoch3).code);
	chratos::state_block epoch4 (destination.pub, 0, 0, 0, 123, epoch_key.prv, epoch_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, epoch4).code);
	chratos::receive_block receive1 (epoch4.hash (), send1.hash (), destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::block_position, ledger.process (transaction, receive1).code);
	chratos::state_block receive2 (destination.pub, epoch4.hash (), destination.pub, chratos::Gchr_ratio, send1.hash (), destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, receive2).code);
	ASSERT_EQ (0, ledger.balance (transaction, epoch4.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.balance (transaction, receive2.hash ()));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.amount (transaction, receive2.hash ()));
	ASSERT_EQ (chratos::genesis_amount - chratos::Gchr_ratio, ledger.weight (transaction, chratos::genesis_account));
	ASSERT_EQ (chratos::Gchr_ratio, ledger.weight (transaction, destination.pub));
}

TEST (ledger, epoch_blocks_receive_upgrade)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::keypair epoch_key;
	chratos::ledger ledger (store, stats, 123, epoch_key.pub);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair destination;
	chratos::state_block send1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, destination.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::state_block epoch1 (chratos::genesis_account, send1.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio, 123, epoch_key.prv, epoch_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, epoch1).code);
	chratos::state_block send2 (chratos::genesis_account, epoch1.hash (), chratos::genesis_account, chratos::genesis_amount - chratos::Gchr_ratio * 2, destination.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send2).code);
	chratos::open_block open1 (send1.hash (), destination.pub, destination.pub, destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, open1).code);
	chratos::receive_block receive1 (open1.hash (), send2.hash (), destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::unreceivable, ledger.process (transaction, receive1).code);
	chratos::state_block receive2 (destination.pub, open1.hash (), destination.pub, chratos::Gchr_ratio * 2, send2.hash (), destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, receive2).code);
	chratos::account_info destination_info;
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch, chratos::epoch::epoch_1);
	ledger.rollback (transaction, receive2.hash ());
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch, chratos::epoch::epoch_0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, receive2).code);
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch, chratos::epoch::epoch_1);
	chratos::keypair destination2;
	chratos::state_block send3 (destination.pub, receive2.hash (), destination.pub, chratos::Gchr_ratio, destination2.pub, destination.prv, destination.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send3).code);
	chratos::open_block open2 (send3.hash (), destination2.pub, destination2.pub, destination2.prv, destination2.pub, 0);
	ASSERT_EQ (chratos::process_result::unreceivable, ledger.process (transaction, open2).code);
}

TEST (ledger, epoch_blocks_fork)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::keypair epoch_key;
	chratos::ledger ledger (store, stats, 123, epoch_key.pub);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair destination;
	chratos::send_block send1 (genesis.hash (), chratos::account (0), chratos::genesis_amount, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send1).code);
	chratos::state_block epoch1 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount, 123, epoch_key.prv, epoch_key.pub, 0);
	ASSERT_EQ (chratos::process_result::fork, ledger.process (transaction, epoch1).code);
}

TEST (ledger, could_fit)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	chratos::keypair epoch_key;
	chratos::ledger ledger (store, stats, 123, epoch_key.pub);
	chratos::keypair epoch_signer;
	ledger.epoch_link = 123;
	ledger.epoch_signer = epoch_signer.pub;
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair destination;
	// Test legacy and state change blocks could_fit
	chratos::change_block change1 (genesis.hash (), chratos::genesis_account, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	chratos::state_block change2 (chratos::genesis_account, genesis.hash (), chratos::genesis_account, chratos::genesis_amount, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_TRUE (ledger.could_fit (transaction, change1));
	ASSERT_TRUE (ledger.could_fit (transaction, change2));
	// Test legacy and state send
	chratos::keypair key1;
	chratos::send_block send1 (change1.hash (), key1.pub, chratos::genesis_amount - 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	chratos::state_block send2 (chratos::genesis_account, change1.hash (), chratos::genesis_account, chratos::genesis_amount - 1, key1.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	ASSERT_FALSE (ledger.could_fit (transaction, send1));
	ASSERT_FALSE (ledger.could_fit (transaction, send2));
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, change1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, change1));
	ASSERT_TRUE (ledger.could_fit (transaction, change2));
	ASSERT_TRUE (ledger.could_fit (transaction, send1));
	ASSERT_TRUE (ledger.could_fit (transaction, send2));
	// Test legacy and state open
	chratos::open_block open1 (send2.hash (), chratos::genesis_account, key1.pub, key1.prv, key1.pub, 0);
	chratos::state_block open2 (key1.pub, 0, chratos::genesis_account, 1, send2.hash (), key1.prv, key1.pub, 0);
	ASSERT_FALSE (ledger.could_fit (transaction, open1));
	ASSERT_FALSE (ledger.could_fit (transaction, open2));
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send2).code);
	ASSERT_TRUE (ledger.could_fit (transaction, send1));
	ASSERT_TRUE (ledger.could_fit (transaction, send2));
	ASSERT_TRUE (ledger.could_fit (transaction, open1));
	ASSERT_TRUE (ledger.could_fit (transaction, open2));
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, open1));
	ASSERT_TRUE (ledger.could_fit (transaction, open2));
	// Create another send to receive
	chratos::state_block send3 (chratos::genesis_account, send2.hash (), chratos::genesis_account, chratos::genesis_amount - 2, key1.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
	// Test legacy and state receive
	chratos::receive_block receive1 (open1.hash (), send3.hash (), key1.prv, key1.pub, 0);
	chratos::state_block receive2 (key1.pub, open1.hash (), chratos::genesis_account, 2, send3.hash (), key1.prv, key1.pub, 0);
	ASSERT_FALSE (ledger.could_fit (transaction, receive1));
	ASSERT_FALSE (ledger.could_fit (transaction, receive2));
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, send3).code);
	ASSERT_TRUE (ledger.could_fit (transaction, receive1));
	ASSERT_TRUE (ledger.could_fit (transaction, receive2));
	// Test epoch (state)
	chratos::state_block epoch1 (key1.pub, receive1.hash (), chratos::genesis_account, 2, ledger.epoch_link, epoch_signer.prv, epoch_signer.pub, 0);
	ASSERT_FALSE (ledger.could_fit (transaction, epoch1));
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, receive1));
	ASSERT_TRUE (ledger.could_fit (transaction, receive2));
	ASSERT_TRUE (ledger.could_fit (transaction, epoch1));
	ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, epoch1));
}
