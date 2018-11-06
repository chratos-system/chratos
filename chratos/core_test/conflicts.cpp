#include <chratos/node/testing.hpp>
#include <gtest/gtest.h>

TEST (conflicts, start_stop)
{
	chratos::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	chratos::genesis genesis;
	chratos::keypair key1;
	auto send1 (std::make_shared<chratos::send_block> (genesis.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	ASSERT_EQ (chratos::process_result::progress, node1.process (*send1).code);
	ASSERT_EQ (0, node1.active.roots.size ());
	auto node_l (system.nodes[0]);
	node1.active.start (send1);
	ASSERT_EQ (1, node1.active.roots.size ());
	auto root1 (send1->root ());
	auto existing1 (node1.active.roots.find (root1));
	ASSERT_NE (node1.active.roots.end (), existing1);
	auto votes1 (existing1->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (1, votes1->last_votes.size ());
}

TEST (conflicts, add_existing)
{
	chratos::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	chratos::genesis genesis;
	chratos::keypair key1;
	auto send1 (std::make_shared<chratos::send_block> (genesis.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	ASSERT_EQ (chratos::process_result::progress, node1.process (*send1).code);
	auto node_l (system.nodes[0]);
	node1.active.start (send1);
	chratos::keypair key2;
	auto send2 (std::make_shared<chratos::send_block> (genesis.hash (), key2.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	node1.active.start (send2);
	ASSERT_EQ (1, node1.active.roots.size ());
	auto vote1 (std::make_shared<chratos::vote> (key2.pub, key2.prv, 0, send2));
	node1.active.vote (vote1);
	ASSERT_EQ (1, node1.active.roots.size ());
	auto votes1 (node1.active.roots.find (send2->root ())->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (key2.pub));
}

TEST (conflicts, add_two)
{
	chratos::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	chratos::genesis genesis;
	chratos::keypair key1;
	auto send1 (std::make_shared<chratos::send_block> (genesis.hash (), key1.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	ASSERT_EQ (chratos::process_result::progress, node1.process (*send1).code);
	auto node_l (system.nodes[0]);
	node1.active.start (send1);
	chratos::keypair key2;
	auto send2 (std::make_shared<chratos::send_block> (send1->hash (), key2.pub, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	ASSERT_EQ (chratos::process_result::progress, node1.process (*send2).code);
	node1.active.start (send2);
	ASSERT_EQ (2, node1.active.roots.size ());
}
