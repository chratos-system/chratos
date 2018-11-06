#include <chratos/core_test/testutil.hpp>
#include <chratos/node/testing.hpp>
#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (gap_cache, add_new)
{
	chratos::system system (24000, 1);
	chratos::gap_cache cache (*system.nodes[0]);
	auto block1 (std::make_shared<chratos::send_block> (0, 1, 2, chratos::keypair ().prv, 4, 5));
	auto transaction (system.nodes[0]->store.tx_begin (true));
	cache.add (transaction, block1);
}

TEST (gap_cache, add_existing)
{
	chratos::system system (24000, 1);
	chratos::gap_cache cache (*system.nodes[0]);
	auto block1 (std::make_shared<chratos::send_block> (0, 1, 2, chratos::keypair ().prv, 4, 5));
	auto transaction (system.nodes[0]->store.tx_begin (true));
	cache.add (transaction, block1);
	auto existing1 (cache.blocks.get<1> ().find (block1->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing1);
	auto arrival (existing1->arrival);
	while (arrival == std::chrono::steady_clock::now ())
		;
	cache.add (transaction, block1);
	ASSERT_EQ (1, cache.blocks.size ());
	auto existing2 (cache.blocks.get<1> ().find (block1->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing2);
	ASSERT_GT (existing2->arrival, arrival);
}

TEST (gap_cache, comparison)
{
	chratos::system system (24000, 1);
	chratos::gap_cache cache (*system.nodes[0]);
	auto block1 (std::make_shared<chratos::send_block> (1, 0, 2, chratos::keypair ().prv, 4, 5));
	auto transaction (system.nodes[0]->store.tx_begin (true));
	cache.add (transaction, block1);
	auto existing1 (cache.blocks.get<1> ().find (block1->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing1);
	auto arrival (existing1->arrival);
	while (std::chrono::steady_clock::now () == arrival)
		;
	auto block3 (std::make_shared<chratos::send_block> (0, 42, 1, chratos::keypair ().prv, 3, 4));
	cache.add (transaction, block3);
	ASSERT_EQ (2, cache.blocks.size ());
	auto existing2 (cache.blocks.get<1> ().find (block3->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing2);
	ASSERT_GT (existing2->arrival, arrival);
	ASSERT_EQ (arrival, cache.blocks.get<1> ().begin ()->arrival);
}

TEST (gap_cache, gap_bootstrap)
{
	chratos::system system (24000, 2);
	chratos::block_hash latest (system.nodes[0]->latest (chratos::test_genesis_key.pub));
	chratos::keypair key;
	auto send (std::make_shared<chratos::send_block> (latest, key.pub, chratos::genesis_amount - 100, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, system.work.generate (latest)));
	{
		auto transaction (system.nodes[0]->store.tx_begin (true));
		ASSERT_EQ (chratos::process_result::progress, system.nodes[0]->block_processor.process_receive_one (transaction, send).code);
	}
	ASSERT_EQ (chratos::genesis_amount - 100, system.nodes[0]->balance (chratos::genesis_account));
	ASSERT_EQ (chratos::genesis_amount, system.nodes[1]->balance (chratos::genesis_account));
	system.wallet (0)->insert_adhoc (chratos::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	auto latest_block (system.wallet (0)->send_action (chratos::test_genesis_key.pub, key.pub, 100));
	ASSERT_NE (nullptr, latest_block);
	ASSERT_EQ (chratos::genesis_amount - 200, system.nodes[0]->balance (chratos::genesis_account));
	ASSERT_EQ (chratos::genesis_amount, system.nodes[1]->balance (chratos::genesis_account));
	system.deadline_set (10s);
	{
		// The separate publish and vote system doesn't work very well here because it's instantly confirmed.
		// We help it get the block and vote out here.
		auto transaction (system.nodes[0]->store.tx_begin ());
		system.nodes[0]->network.republish_block (latest_block);
	}
	while (system.nodes[1]->balance (chratos::genesis_account) != chratos::genesis_amount - 200)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (gap_cache, two_dependencies)
{
	chratos::system system (24000, 1);
	chratos::keypair key;
	chratos::genesis genesis;
	auto send1 (std::make_shared<chratos::send_block> (genesis.hash (), key.pub, 1, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<chratos::send_block> (send1->hash (), key.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, system.work.generate (send1->hash ())));
	auto open (std::make_shared<chratos::open_block> (send1->hash (), key.pub, key.pub, key.prv, key.pub, system.work.generate (key.pub)));
	ASSERT_EQ (0, system.nodes[0]->gap_cache.blocks.size ());
	system.nodes[0]->block_processor.add (send2, std::chrono::steady_clock::now ());
	system.nodes[0]->block_processor.flush ();
	ASSERT_EQ (1, system.nodes[0]->gap_cache.blocks.size ());
	system.nodes[0]->block_processor.add (open, std::chrono::steady_clock::now ());
	system.nodes[0]->block_processor.flush ();
	ASSERT_EQ (2, system.nodes[0]->gap_cache.blocks.size ());
	system.nodes[0]->block_processor.add (send1, std::chrono::steady_clock::now ());
	system.nodes[0]->block_processor.flush ();
	ASSERT_EQ (0, system.nodes[0]->gap_cache.blocks.size ());
	auto transaction (system.nodes[0]->store.tx_begin ());
	ASSERT_TRUE (system.nodes[0]->store.block_exists (transaction, send1->hash ()));
	ASSERT_TRUE (system.nodes[0]->store.block_exists (transaction, send2->hash ()));
	ASSERT_TRUE (system.nodes[0]->store.block_exists (transaction, open->hash ()));
}
