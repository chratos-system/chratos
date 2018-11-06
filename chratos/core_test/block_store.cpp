#include <chratos/lib/utility.hpp>
#include <chratos/node/common.hpp>
#include <chratos/node/node.hpp>
#include <chratos/secure/versioning.hpp>
#include <gtest/gtest.h>

#include <fstream>

TEST (block_store, construction)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	auto now (chratos::seconds_since_epoch ());
	ASSERT_GT (now, 1408074640);
}

TEST (block_store, add_item)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::open_block block (0, 1, 0, chratos::keypair ().prv, 0, 0);
	chratos::uint256_union hash1 (block.hash ());
	auto transaction (store.tx_begin (true));
	auto latest1 (store.block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	ASSERT_FALSE (store.block_exists (transaction, hash1));
	store.block_put (transaction, hash1, block);
	auto latest2 (store.block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
	ASSERT_TRUE (store.block_exists (transaction, hash1));
	ASSERT_FALSE (store.block_exists (transaction, hash1.number () - 1));
	store.block_del (transaction, hash1);
	auto latest3 (store.block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest3);
}

TEST (block_store, add_nonempty_block)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::keypair key1;
	chratos::open_block block (0, 1, 0, chratos::keypair ().prv, 0, 0);
	chratos::uint256_union hash1 (block.hash ());
	block.signature = chratos::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store.tx_begin (true));
	auto latest1 (store.block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	store.block_put (transaction, hash1, block);
	auto latest2 (store.block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_two_items)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::keypair key1;
	chratos::open_block block (0, 1, 1, chratos::keypair ().prv, 0, 0);
	chratos::uint256_union hash1 (block.hash ());
	block.signature = chratos::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store.tx_begin (true));
	auto latest1 (store.block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	chratos::open_block block2 (0, 1, 3, chratos::keypair ().prv, 0, 0);
	block2.hashables.account = 3;
	chratos::uint256_union hash2 (block2.hash ());
	block2.signature = chratos::sign_message (key1.prv, key1.pub, hash2);
	auto latest2 (store.block_get (transaction, hash2));
	ASSERT_EQ (nullptr, latest2);
	store.block_put (transaction, hash1, block);
	store.block_put (transaction, hash2, block2);
	auto latest3 (store.block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest3);
	ASSERT_EQ (block, *latest3);
	auto latest4 (store.block_get (transaction, hash2));
	ASSERT_NE (nullptr, latest4);
	ASSERT_EQ (block2, *latest4);
	ASSERT_FALSE (*latest3 == *latest4);
}

TEST (block_store, add_receive)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::keypair key1;
	chratos::keypair key2;
	chratos::open_block block1 (0, 1, 0, chratos::keypair ().prv, 0, 0);
	auto transaction (store.tx_begin (true));
	store.block_put (transaction, block1.hash (), block1);
	chratos::receive_block block (block1.hash (), 1, chratos::keypair ().prv, 2, 3);
	chratos::block_hash hash1 (block.hash ());
	auto latest1 (store.block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	store.block_put (transaction, hash1, block);
	auto latest2 (store.block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_pending)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::keypair key1;
	chratos::pending_key key2 (0, 0);
	chratos::pending_info pending1;
	auto transaction (store.tx_begin (true));
	ASSERT_TRUE (store.pending_get (transaction, key2, pending1));
	store.pending_put (transaction, key2, pending1);
	chratos::pending_info pending2;
	ASSERT_FALSE (store.pending_get (transaction, key2, pending2));
	ASSERT_EQ (pending1, pending2);
	store.pending_del (transaction, key2);
	ASSERT_TRUE (store.pending_get (transaction, key2, pending2));
}

TEST (block_store, pending_iterator)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	auto transaction (store.tx_begin (true));
	ASSERT_EQ (store.pending_end (), store.pending_begin (transaction));
	store.pending_put (transaction, chratos::pending_key (1, 2), { 2, 3, chratos::epoch::epoch_1 });
	auto current (store.pending_begin (transaction));
	ASSERT_NE (store.pending_end (), current);
	chratos::pending_key key1 (current->first);
	ASSERT_EQ (chratos::account (1), key1.account);
	ASSERT_EQ (chratos::block_hash (2), key1.hash);
	chratos::pending_info pending (current->second);
	ASSERT_EQ (chratos::account (2), pending.source);
	ASSERT_EQ (chratos::amount (3), pending.amount);
	ASSERT_EQ (chratos::epoch::epoch_1, pending.epoch);
}

/**
 * Regression test for Issue 1164
 * This reconstructs the situation where a key is larger in pending than the account being iterated in pending_v1, leaving
 * iteration order up to the value, causing undefined behavior.
 * After the bugfix, the value is compared only if the keys are equal.
 */
TEST (block_store, pending_iterator_comparison)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::stat stats;
	auto transaction (store.tx_begin (true));
	// Populate pending
	store.pending_put (transaction, chratos::pending_key (chratos::account (3), chratos::block_hash (1)), chratos::pending_info (chratos::account (10), chratos::amount (1), chratos::epoch::epoch_0));
	store.pending_put (transaction, chratos::pending_key (chratos::account (3), chratos::block_hash (4)), chratos::pending_info (chratos::account (10), chratos::amount (0), chratos::epoch::epoch_0));
	// Populate pending_v1
	store.pending_put (transaction, chratos::pending_key (chratos::account (2), chratos::block_hash (2)), chratos::pending_info (chratos::account (10), chratos::amount (2), chratos::epoch::epoch_1));
	store.pending_put (transaction, chratos::pending_key (chratos::account (2), chratos::block_hash (3)), chratos::pending_info (chratos::account (10), chratos::amount (3), chratos::epoch::epoch_1));

	// Iterate account 3 (pending)
	{
		size_t count = 0;
		chratos::account begin (3);
		chratos::account end (begin.number () + 1);
		for (auto i (store.pending_begin (transaction, chratos::pending_key (begin, 0))), n (store.pending_begin (transaction, chratos::pending_key (end, 0))); i != n; ++i, ++count)
		{
			chratos::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}

	// Iterate account 2 (pending_v1)
	{
		size_t count = 0;
		chratos::account begin (2);
		chratos::account end (begin.number () + 1);
		for (auto i (store.pending_begin (transaction, chratos::pending_key (begin, 0))), n (store.pending_begin (transaction, chratos::pending_key (end, 0))); i != n; ++i, ++count)
		{
			chratos::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}
}

TEST (block_store, genesis)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::genesis genesis;
	auto hash (genesis.hash ());
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::account_info info;
	ASSERT_FALSE (store.account_get (transaction, chratos::genesis_account, info));
	ASSERT_EQ (hash, info.head);
	auto block1 (store.block_get (transaction, info.head));
	ASSERT_NE (nullptr, block1);
	auto receive1 (dynamic_cast<chratos::open_block *> (block1.get ()));
	ASSERT_NE (nullptr, receive1);
	ASSERT_LE (info.modified, chratos::seconds_since_epoch ());
	auto test_pub_text (chratos::test_genesis_key.pub.to_string ());
	auto test_pub_account (chratos::test_genesis_key.pub.to_account ());
	auto test_prv_text (chratos::test_genesis_key.prv.data.to_string ());
	ASSERT_EQ (chratos::genesis_account, chratos::test_genesis_key.pub);
}

TEST (representation, changes)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::keypair key1;
	auto transaction (store.tx_begin (true));
	ASSERT_EQ (0, store.representation_get (transaction, key1.pub));
	store.representation_put (transaction, key1.pub, 1);
	ASSERT_EQ (1, store.representation_get (transaction, key1.pub));
	store.representation_put (transaction, key1.pub, 2);
	ASSERT_EQ (2, store.representation_get (transaction, key1.pub));
}

TEST (bootstrap, simple)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	auto block1 (std::make_shared<chratos::send_block> (0, 1, 2, chratos::keypair ().prv, 4, 5));
	auto transaction (store.tx_begin (true));
	auto block2 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store.unchecked_put (transaction, block1->previous (), block1);
	auto block3 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_FALSE (block3.empty ());
	ASSERT_EQ (*block1, *block3[0]);
	store.unchecked_del (transaction, block1->previous (), block1);
	auto block4 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block4.empty ());
}

TEST (unchecked, multiple)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	auto block1 (std::make_shared<chratos::send_block> (4, 1, 2, chratos::keypair ().prv, 4, 5));
	auto transaction (store.tx_begin (true));
	auto block2 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store.unchecked_put (transaction, block1->previous (), block1);
	store.unchecked_put (transaction, block1->source (), block1);
	auto block3 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_FALSE (block3.empty ());
	auto block4 (store.unchecked_get (transaction, block1->source ()));
	ASSERT_FALSE (block4.empty ());
}

TEST (unchecked, double_put)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	auto block1 (std::make_shared<chratos::send_block> (4, 1, 2, chratos::keypair ().prv, 4, 5));
	auto transaction (store.tx_begin (true));
	auto block2 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store.unchecked_put (transaction, block1->previous (), block1);
	store.unchecked_put (transaction, block1->previous (), block1);
	auto block3 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_EQ (block3.size (), 1);
}

TEST (unchecked, multiple_get)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	auto block1 (std::make_shared<chratos::send_block> (4, 1, 2, chratos::keypair ().prv, 4, 5));
	auto block2 (std::make_shared<chratos::send_block> (3, 1, 2, chratos::keypair ().prv, 4, 5));
	auto block3 (std::make_shared<chratos::send_block> (5, 1, 2, chratos::keypair ().prv, 4, 5));
	{
		auto transaction (store.tx_begin (true));
		store.unchecked_put (transaction, block1->previous (), block1); // unchecked1
		store.unchecked_put (transaction, block1->hash (), block1); // unchecked2
		store.unchecked_put (transaction, block2->previous (), block2); // unchecked3
		store.unchecked_put (transaction, block1->previous (), block2); // unchecked1
		store.unchecked_put (transaction, block1->hash (), block2); // unchecked2
		store.unchecked_put (transaction, block3->previous (), block3);
		store.unchecked_put (transaction, block3->hash (), block3); // unchecked4
		store.unchecked_put (transaction, block1->previous (), block3); // unchecked1
	}
	auto transaction (store.tx_begin ());
	auto unchecked_count (store.unchecked_count (transaction));
	ASSERT_EQ (unchecked_count, 8);
	std::vector<chratos::block_hash> unchecked1;
	auto unchecked1_blocks (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_EQ (unchecked1_blocks.size (), 3);
	for (auto & i : unchecked1_blocks)
	{
		unchecked1.push_back (i->hash ());
	}
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block1->hash ()) != unchecked1.end ());
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block2->hash ()) != unchecked1.end ());
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block3->hash ()) != unchecked1.end ());
	std::vector<chratos::block_hash> unchecked2;
	auto unchecked2_blocks (store.unchecked_get (transaction, block1->hash ()));
	ASSERT_EQ (unchecked2_blocks.size (), 2);
	for (auto & i : unchecked2_blocks)
	{
		unchecked2.push_back (i->hash ());
	}
	ASSERT_TRUE (std::find (unchecked2.begin (), unchecked2.end (), block1->hash ()) != unchecked2.end ());
	ASSERT_TRUE (std::find (unchecked2.begin (), unchecked2.end (), block2->hash ()) != unchecked2.end ());
	auto unchecked3 (store.unchecked_get (transaction, block2->previous ()));
	ASSERT_EQ (unchecked3.size (), 1);
	ASSERT_EQ (unchecked3[0]->hash (), block2->hash ());
	auto unchecked4 (store.unchecked_get (transaction, block3->hash ()));
	ASSERT_EQ (unchecked4.size (), 1);
	ASSERT_EQ (unchecked4[0]->hash (), block3->hash ());
	auto unchecked5 (store.unchecked_get (transaction, block2->hash ()));
	ASSERT_EQ (unchecked5.size (), 0);
}

TEST (checksum, simple)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::block_hash hash0 (0);
	auto transaction (store.tx_begin (true));
	ASSERT_TRUE (store.checksum_get (transaction, 0x100, 0x10, hash0));
	chratos::block_hash hash1 (0);
	store.checksum_put (transaction, 0x100, 0x10, hash1);
	chratos::block_hash hash2;
	ASSERT_FALSE (store.checksum_get (transaction, 0x100, 0x10, hash2));
	ASSERT_EQ (hash1, hash2);
	store.checksum_del (transaction, 0x100, 0x10);
	chratos::block_hash hash3;
	ASSERT_TRUE (store.checksum_get (transaction, 0x100, 0x10, hash3));
}

TEST (block_store, empty_accounts)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	auto transaction (store.tx_begin ());
	auto begin (store.latest_begin (transaction));
	auto end (store.latest_end ());
	ASSERT_EQ (end, begin);
}

TEST (block_store, one_block)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::open_block block1 (0, 1, 0, chratos::keypair ().prv, 0, 0);
	auto transaction (store.tx_begin (true));
	store.block_put (transaction, block1.hash (), block1);
	ASSERT_TRUE (store.block_exists (transaction, block1.hash ()));
}

TEST (block_store, empty_bootstrap)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	auto transaction (store.tx_begin ());
	auto begin (store.unchecked_begin (transaction));
	auto end (store.unchecked_end ());
	ASSERT_EQ (end, begin);
}

TEST (block_store, one_bootstrap)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	auto block1 (std::make_shared<chratos::send_block> (0, 1, 2, chratos::keypair ().prv, 4, 5));
	auto transaction (store.tx_begin (true));
	store.unchecked_put (transaction, block1->hash (), block1);
	store.flush (transaction);
	auto begin (store.unchecked_begin (transaction));
	auto end (store.unchecked_end ());
	ASSERT_NE (end, begin);
	chratos::uint256_union hash1 (begin->first);
	ASSERT_EQ (block1->hash (), hash1);
	auto blocks (store.unchecked_get (transaction, chratos::block_hash (begin->first)));
	ASSERT_EQ (1, blocks.size ());
	auto block2 (blocks[0]);
	ASSERT_EQ (*block1, *block2);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, unchecked_begin_search)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::keypair key0;
	chratos::send_block block1 (0, 1, 2, key0.prv, key0.pub, 3);
	chratos::send_block block2 (5, 6, 7, key0.prv, key0.pub, 8);
}

TEST (block_store, frontier_retrieval)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::account account1 (0);
	chratos::account_info info1 (0, 0, 0, 0, 0, 0, chratos::epoch::epoch_0);
	auto transaction (store.tx_begin (true));
	store.account_put (transaction, account1, info1);
	chratos::account_info info2;
	store.account_get (transaction, account1, info2);
	ASSERT_EQ (info1, info2);
}

TEST (block_store, one_account)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::account account (0);
	chratos::block_hash hash (0);
	auto transaction (store.tx_begin (true));
	store.account_put (transaction, account, { hash, account, hash, 42, 100, 200, chratos::epoch::epoch_0 });
	auto begin (store.latest_begin (transaction));
	auto end (store.latest_end ());
	ASSERT_NE (end, begin);
	ASSERT_EQ (account, chratos::account (begin->first));
	chratos::account_info info (begin->second);
	ASSERT_EQ (hash, info.head);
	ASSERT_EQ (42, info.balance.number ());
	ASSERT_EQ (100, info.modified);
	ASSERT_EQ (200, info.block_count);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, two_block)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::open_block block1 (0, 1, 1, chratos::keypair ().prv, 0, 0);
	block1.hashables.account = 1;
	std::vector<chratos::block_hash> hashes;
	std::vector<chratos::open_block> blocks;
	hashes.push_back (block1.hash ());
	blocks.push_back (block1);
	auto transaction (store.tx_begin (true));
	store.block_put (transaction, hashes[0], block1);
	chratos::open_block block2 (0, 1, 2, chratos::keypair ().prv, 0, 0);
	hashes.push_back (block2.hash ());
	blocks.push_back (block2);
	store.block_put (transaction, hashes[1], block2);
	ASSERT_TRUE (store.block_exists (transaction, block1.hash ()));
	ASSERT_TRUE (store.block_exists (transaction, block2.hash ()));
}

TEST (block_store, two_account)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::account account1 (1);
	chratos::block_hash hash1 (2);
	chratos::account account2 (3);
	chratos::block_hash hash2 (4);
	auto transaction (store.tx_begin (true));
	store.account_put (transaction, account1, { hash1, account1, hash1, 42, 100, 300, chratos::epoch::epoch_0 });
	store.account_put (transaction, account2, { hash2, account2, hash2, 84, 200, 400, chratos::epoch::epoch_0 });
	auto begin (store.latest_begin (transaction));
	auto end (store.latest_end ());
	ASSERT_NE (end, begin);
	ASSERT_EQ (account1, chratos::account (begin->first));
	chratos::account_info info1 (begin->second);
	ASSERT_EQ (hash1, info1.head);
	ASSERT_EQ (42, info1.balance.number ());
	ASSERT_EQ (100, info1.modified);
	ASSERT_EQ (300, info1.block_count);
	++begin;
	ASSERT_NE (end, begin);
	ASSERT_EQ (account2, chratos::account (begin->first));
	chratos::account_info info2 (begin->second);
	ASSERT_EQ (hash2, info2.head);
	ASSERT_EQ (84, info2.balance.number ());
	ASSERT_EQ (200, info2.modified);
	ASSERT_EQ (400, info2.block_count);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, latest_find)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::account account1 (1);
	chratos::block_hash hash1 (2);
	chratos::account account2 (3);
	chratos::block_hash hash2 (4);
	auto transaction (store.tx_begin (true));
	store.account_put (transaction, account1, { hash1, account1, hash1, 100, 0, 300, chratos::epoch::epoch_0 });
	store.account_put (transaction, account2, { hash2, account2, hash2, 200, 0, 400, chratos::epoch::epoch_0 });
	auto first (store.latest_begin (transaction));
	auto second (store.latest_begin (transaction));
	++second;
	auto find1 (store.latest_begin (transaction, 1));
	ASSERT_EQ (first, find1);
	auto find2 (store.latest_begin (transaction, 3));
	ASSERT_EQ (second, find2);
	auto find3 (store.latest_begin (transaction, 2));
	ASSERT_EQ (second, find3);
}

TEST (block_store, bad_path)
{
	bool init (false);
	chratos::mdb_store store (init, boost::filesystem::path ("///"));
	ASSERT_TRUE (init);
}

TEST (block_store, DISABLED_already_open) // File can be shared
{
	auto path (chratos::unique_path ());
	boost::filesystem::create_directories (path.parent_path ());
	chratos::set_secure_perm_directory (path.parent_path ());
	std::ofstream file;
	file.open (path.string ().c_str ());
	ASSERT_TRUE (file.is_open ());
	bool init (false);
	chratos::mdb_store store (init, path);
	ASSERT_TRUE (init);
}

TEST (block_store, roots)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::send_block send_block (0, 1, 2, chratos::keypair ().prv, 4, 5);
	ASSERT_EQ (send_block.hashables.previous, send_block.root ());
	chratos::change_block change_block (0, 1, chratos::keypair ().prv, 3, 4);
	ASSERT_EQ (change_block.hashables.previous, change_block.root ());
	chratos::receive_block receive_block (0, 1, chratos::keypair ().prv, 3, 4);
	ASSERT_EQ (receive_block.hashables.previous, receive_block.root ());
	chratos::open_block open_block (0, 1, 2, chratos::keypair ().prv, 4, 5);
	ASSERT_EQ (open_block.hashables.account, open_block.root ());
}

TEST (block_store, pending_exists)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::pending_key two (2, 0);
	chratos::pending_info pending;
	auto transaction (store.tx_begin (true));
	store.pending_put (transaction, two, pending);
	chratos::pending_key one (1, 0);
	ASSERT_FALSE (store.pending_exists (transaction, one));
}

TEST (block_store, latest_exists)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::block_hash two (2);
	chratos::account_info info;
	auto transaction (store.tx_begin (true));
	store.account_put (transaction, two, info);
	chratos::block_hash one (1);
	ASSERT_FALSE (store.account_exists (transaction, one));
}

TEST (block_store, large_iteration)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	std::unordered_set<chratos::account> accounts1;
	for (auto i (0); i < 1000; ++i)
	{
		auto transaction (store.tx_begin (true));
		chratos::account account;
		chratos::random_pool.GenerateBlock (account.bytes.data (), account.bytes.size ());
		accounts1.insert (account);
		store.account_put (transaction, account, chratos::account_info ());
	}
	std::unordered_set<chratos::account> accounts2;
	chratos::account previous (0);
	auto transaction (store.tx_begin ());
	for (auto i (store.latest_begin (transaction, 0)), n (store.latest_end ()); i != n; ++i)
	{
		chratos::account current (i->first);
		assert (current.number () > previous.number ());
		accounts2.insert (current);
		previous = current;
	}
	ASSERT_EQ (accounts1, accounts2);
}

TEST (block_store, frontier)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	auto transaction (store.tx_begin (true));
	chratos::block_hash hash (100);
	chratos::account account (200);
	ASSERT_TRUE (store.frontier_get (transaction, hash).is_zero ());
	store.frontier_put (transaction, hash, account);
	ASSERT_EQ (account, store.frontier_get (transaction, hash));
	store.frontier_del (transaction, hash);
	ASSERT_TRUE (store.frontier_get (transaction, hash).is_zero ());
}

TEST (block_store, block_replace)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::send_block send1 (0, 0, 0, chratos::keypair ().prv, 0, 1);
	chratos::send_block send2 (0, 0, 0, chratos::keypair ().prv, 0, 2);
	auto transaction (store.tx_begin (true));
	store.block_put (transaction, 0, send1);
	store.block_put (transaction, 0, send2);
	auto block3 (store.block_get (transaction, 0));
	ASSERT_NE (nullptr, block3);
	ASSERT_EQ (2, block3->block_work ());
}

TEST (block_store, block_count)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	auto transaction (store.tx_begin (true));
	ASSERT_EQ (0, store.block_count (transaction).sum ());
	chratos::open_block block (0, 1, 0, chratos::keypair ().prv, 0, 0);
	chratos::uint256_union hash1 (block.hash ());
	store.block_put (transaction, hash1, block);
	ASSERT_EQ (1, store.block_count (transaction).sum ());
}

TEST (block_store, account_count)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	auto transaction (store.tx_begin (true));
	ASSERT_EQ (0, store.account_count (transaction));
	chratos::account account (200);
	store.account_put (transaction, account, chratos::account_info ());
	ASSERT_EQ (1, store.account_count (transaction));
}

TEST (block_store, sequence_increment)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::keypair key1;
	chratos::keypair key2;
	auto block1 (std::make_shared<chratos::open_block> (0, 1, 0, chratos::keypair ().prv, 0, 0));
	auto transaction (store.tx_begin (true));
	auto vote1 (store.vote_generate (transaction, key1.pub, key1.prv, block1));
	ASSERT_EQ (1, vote1->sequence);
	auto vote2 (store.vote_generate (transaction, key1.pub, key1.prv, block1));
	ASSERT_EQ (2, vote2->sequence);
	auto vote3 (store.vote_generate (transaction, key2.pub, key2.prv, block1));
	ASSERT_EQ (1, vote3->sequence);
	auto vote4 (store.vote_generate (transaction, key2.pub, key2.prv, block1));
	ASSERT_EQ (2, vote4->sequence);
	vote1->sequence = 20;
	auto seq5 (store.vote_max (transaction, vote1));
	ASSERT_EQ (20, seq5->sequence);
	vote3->sequence = 30;
	auto seq6 (store.vote_max (transaction, vote3));
	ASSERT_EQ (30, seq6->sequence);
	auto vote5 (store.vote_generate (transaction, key1.pub, key1.prv, block1));
	ASSERT_EQ (21, vote5->sequence);
	auto vote6 (store.vote_generate (transaction, key2.pub, key2.prv, block1));
	ASSERT_EQ (31, vote6->sequence);
}

TEST (block_store, upgrade_v2_v3)
{
	chratos::keypair key1;
	chratos::keypair key2;
	chratos::block_hash change_hash;
	auto path (chratos::unique_path ());
	{
		bool init (false);
		chratos::mdb_store store (init, path);
		ASSERT_TRUE (!init);
		auto transaction (store.tx_begin (true));
		chratos::genesis genesis;
		auto hash (genesis.hash ());
		store.initialize (transaction, genesis);
		chratos::stat stats;
		chratos::ledger ledger (store, stats);
		chratos::change_block change (hash, key1.pub, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
		change_hash = change.hash ();
		ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, change).code);
		ASSERT_EQ (0, ledger.weight (transaction, chratos::test_genesis_key.pub));
		ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, key1.pub));
		store.version_put (transaction, 2);
		store.representation_put (transaction, key1.pub, 7);
		ASSERT_EQ (7, ledger.weight (transaction, key1.pub));
		ASSERT_EQ (2, store.version_get (transaction));
		store.representation_put (transaction, key2.pub, 6);
		ASSERT_EQ (6, ledger.weight (transaction, key2.pub));
		chratos::account_info info;
		ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info));
		info.rep_block = 42;
		chratos::account_info_v5 info_old (info.head, info.rep_block, info.open_block, info.balance, info.modified);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, chratos::mdb_val (chratos::test_genesis_key.pub), info_old.val (), 0));
		assert (status == 0);
	}
	bool init (false);
	chratos::mdb_store store (init, path);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	auto transaction (store.tx_begin (true));
	ASSERT_TRUE (!init);
	ASSERT_LT (2, store.version_get (transaction));
	ASSERT_EQ (chratos::genesis_amount, ledger.weight (transaction, key1.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	chratos::account_info info;
	ASSERT_FALSE (store.account_get (transaction, chratos::test_genesis_key.pub, info));
	ASSERT_EQ (change_hash, info.rep_block);
}

TEST (block_store, upgrade_v3_v4)
{
	chratos::keypair key1;
	chratos::keypair key2;
	chratos::keypair key3;
	auto path (chratos::unique_path ());
	{
		bool init (false);
		chratos::mdb_store store (init, path);
		ASSERT_FALSE (init);
		auto transaction (store.tx_begin (true));
		store.version_put (transaction, 3);
		chratos::pending_info_v3 info (key1.pub, 100, key2.pub);
		auto status (mdb_put (store.env.tx (transaction), store.pending_v0, chratos::mdb_val (key3.pub), info.val (), 0));
		ASSERT_EQ (0, status);
	}
	bool init (false);
	chratos::mdb_store store (init, path);
	chratos::stat stats;
	chratos::ledger ledger (store, stats);
	auto transaction (store.tx_begin (true));
	ASSERT_FALSE (init);
	ASSERT_LT (3, store.version_get (transaction));
	chratos::pending_key key (key2.pub, key3.pub);
	chratos::pending_info info;
	auto error (store.pending_get (transaction, key, info));
	ASSERT_FALSE (error);
	ASSERT_EQ (key1.pub, info.source);
	ASSERT_EQ (chratos::amount (100), info.amount);
	ASSERT_EQ (chratos::epoch::epoch_0, info.epoch);
}

TEST (block_store, upgrade_v4_v5)
{
	chratos::block_hash genesis_hash (0);
	chratos::block_hash hash (0);
	auto path (chratos::unique_path ());
	{
		bool init (false);
		chratos::mdb_store store (init, path);
		ASSERT_FALSE (init);
		auto transaction (store.tx_begin (true));
		chratos::genesis genesis;
		chratos::stat stats;
		chratos::ledger ledger (store, stats);
		store.initialize (transaction, genesis);
		store.version_put (transaction, 4);
		chratos::account_info info;
		store.account_get (transaction, chratos::test_genesis_key.pub, info);
		chratos::keypair key0;
		chratos::send_block block0 (info.head, key0.pub, chratos::genesis_amount - chratos::Gchr_ratio, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
		ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block0).code);
		hash = block0.hash ();
		auto original (store.block_get (transaction, info.head));
		genesis_hash = info.head;
		store.block_successor_clear (transaction, info.head);
		ASSERT_TRUE (store.block_successor (transaction, genesis_hash).is_zero ());
		chratos::account_info info2;
		store.account_get (transaction, chratos::test_genesis_key.pub, info2);
		chratos::account_info_v5 info_old (info2.head, info2.rep_block, info2.open_block, info2.balance, info2.modified);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, chratos::mdb_val (chratos::test_genesis_key.pub), info_old.val (), 0));
		assert (status == 0);
	}
	bool init (false);
	chratos::mdb_store store (init, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin ());
	ASSERT_EQ (hash, store.block_successor (transaction, genesis_hash));
}

TEST (block_store, block_random)
{
	bool init (false);
	chratos::mdb_store store (init, chratos::unique_path ());
	ASSERT_TRUE (!init);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	auto block (store.block_random (transaction));
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (*block, *genesis.open);
}

TEST (block_store, upgrade_v5_v6)
{
	auto path (chratos::unique_path ());
	{
		bool init (false);
		chratos::mdb_store store (init, path);
		ASSERT_FALSE (init);
		auto transaction (store.tx_begin (true));
		chratos::genesis genesis;
		store.initialize (transaction, genesis);
		store.version_put (transaction, 5);
		chratos::account_info info;
		store.account_get (transaction, chratos::test_genesis_key.pub, info);
		chratos::account_info_v5 info_old (info.head, info.rep_block, info.open_block, info.balance, info.modified);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, chratos::mdb_val (chratos::test_genesis_key.pub), info_old.val (), 0));
		assert (status == 0);
	}
	bool init (false);
	chratos::mdb_store store (init, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin ());
	chratos::account_info info;
	store.account_get (transaction, chratos::test_genesis_key.pub, info);
	ASSERT_EQ (1, info.block_count);
}

TEST (block_store, upgrade_v6_v7)
{
	auto path (chratos::unique_path ());
	{
		bool init (false);
		chratos::mdb_store store (init, path);
		ASSERT_FALSE (init);
		auto transaction (store.tx_begin (true));
		chratos::genesis genesis;
		store.initialize (transaction, genesis);
		store.version_put (transaction, 6);
		auto send1 (std::make_shared<chratos::send_block> (0, 0, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
		store.unchecked_put (transaction, send1->hash (), send1);
		store.flush (transaction);
		ASSERT_NE (store.unchecked_end (), store.unchecked_begin (transaction));
	}
	bool init (false);
	chratos::mdb_store store (init, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin ());
	ASSERT_EQ (store.unchecked_end (), store.unchecked_begin (transaction));
}

// Databases need to be dropped in order to convert to dupsort compatible
TEST (block_store, change_dupsort)
{
	auto path (chratos::unique_path ());
	bool init (false);
	chratos::mdb_store store (init, path);
	auto transaction (store.tx_begin (true));
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 1));
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE, &store.unchecked));
	auto send1 (std::make_shared<chratos::send_block> (0, 0, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	auto send2 (std::make_shared<chratos::send_block> (1, 0, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	ASSERT_NE (send1->hash (), send2->hash ());
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 0));
	mdb_dbi_close (store.env, store.unchecked);
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE | MDB_DUPSORT, &store.unchecked));
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 1));
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE | MDB_DUPSORT, &store.unchecked));
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_NE (store.unchecked_end (), iterator1);
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
}

TEST (block_store, upgrade_v7_v8)
{
	auto path (chratos::unique_path ());
	{
		bool init (false);
		chratos::mdb_store store (init, path);
		auto transaction (store.tx_begin (true));
		ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 1));
		ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE, &store.unchecked));
		store.version_put (transaction, 7);
	}
	bool init (false);
	chratos::mdb_store store (init, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin (true));
	auto send1 (std::make_shared<chratos::send_block> (0, 0, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	auto send2 (std::make_shared<chratos::send_block> (1, 0, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_NE (store.unchecked_end (), iterator1);
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
}

TEST (block_store, sequence_flush)
{
	auto path (chratos::unique_path ());
	bool init (false);
	chratos::mdb_store store (init, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin (true));
	chratos::keypair key1;
	auto send1 (std::make_shared<chratos::send_block> (0, 0, 0, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0));
	auto vote1 (store.vote_generate (transaction, key1.pub, key1.prv, send1));
	auto seq2 (store.vote_get (transaction, vote1->account));
	ASSERT_EQ (nullptr, seq2);
	store.flush (transaction);
	auto seq3 (store.vote_get (transaction, vote1->account));
	ASSERT_EQ (*seq3, *vote1);
}

TEST (block_store, sequence_flush_by_hash)
{
	auto path (chratos::unique_path ());
	bool init (false);
	chratos::mdb_store store (init, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin_write ());
	chratos::keypair key1;
	std::vector<chratos::block_hash> blocks1;
	blocks1.push_back (chratos::genesis ().hash ());
	blocks1.push_back (1234);
	blocks1.push_back (5678);
	auto vote1 (store.vote_generate (transaction, key1.pub, key1.prv, blocks1));
	auto seq2 (store.vote_get (transaction, vote1->account));
	ASSERT_EQ (nullptr, seq2);
	store.flush (transaction);
	auto seq3 (store.vote_get (transaction, vote1->account));
	ASSERT_EQ (*seq3, *vote1);
}

// Upgrading tracking block sequence numbers to whole vote.
TEST (block_store, upgrade_v8_v9)
{
	auto path (chratos::unique_path ());
	chratos::keypair key;
	{
		bool init (false);
		chratos::mdb_store store (init, path);
		auto transaction (store.tx_begin (true));
		ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.vote, 1));
		ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "sequence", MDB_CREATE, &store.vote));
		uint64_t sequence (10);
		ASSERT_EQ (0, mdb_put (store.env.tx (transaction), store.vote, chratos::mdb_val (key.pub), chratos::mdb_val (sizeof (sequence), &sequence), 0));
		store.version_put (transaction, 8);
	}
	bool init (false);
	chratos::mdb_store store (init, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin ());
	ASSERT_LT (8, store.version_get (transaction));
	auto vote (store.vote_get (transaction, key.pub));
	ASSERT_NE (nullptr, vote);
	ASSERT_EQ (10, vote->sequence);
}

TEST (block_store, upgrade_v9_v10)
{
	auto path (chratos::unique_path ());
	chratos::block_hash hash (0);
	{
		bool init (false);
		chratos::mdb_store store (init, path);
		ASSERT_FALSE (init);
		auto transaction (store.tx_begin (true));
		chratos::genesis genesis;
		chratos::stat stats;
		chratos::ledger ledger (store, stats);
		store.initialize (transaction, genesis);
		store.version_put (transaction, 9);
		chratos::account_info info;
		store.account_get (transaction, chratos::test_genesis_key.pub, info);
		chratos::keypair key0;
		chratos::uint128_t balance (chratos::genesis_amount);
		hash = info.head;
		for (auto i (1); i < 32; ++i) // Making 31 send blocks (+ 1 open = 32 total)
		{
			balance = balance - chratos::Gchr_ratio;
			chratos::send_block block0 (hash, key0.pub, balance, chratos::test_genesis_key.prv, chratos::test_genesis_key.pub, 0);
			ASSERT_EQ (chratos::process_result::progress, ledger.process (transaction, block0).code);
			hash = block0.hash ();
		}
		chratos::block_info block_info_auto; // Checking automatic block_info creation for block 32
		store.block_info_get (transaction, hash, block_info_auto);
		ASSERT_EQ (block_info_auto.account, chratos::test_genesis_key.pub);
		ASSERT_EQ (block_info_auto.balance.number (), balance);
		ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.blocks_info, 0)); // Cleaning blocks_info subdatabase
		bool block_info_exists (store.block_info_exists (transaction, hash));
		ASSERT_EQ (block_info_exists, 0); // Checking if automatic block_info is deleted
	}
	bool init (false);
	chratos::mdb_store store (init, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin ());
	ASSERT_LT (9, store.version_get (transaction));
	chratos::block_info block_info;
	store.block_info_get (transaction, hash, block_info);
	ASSERT_EQ (block_info.account, chratos::test_genesis_key.pub);
	ASSERT_EQ (block_info.balance.number (), chratos::genesis_amount - chratos::Gchr_ratio * 31);
}

TEST (block_store, state_block)
{
	bool error (false);
	chratos::mdb_store store (error, chratos::unique_path ());
	ASSERT_FALSE (error);
	chratos::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	chratos::keypair key1;
	chratos::state_block block1 (1, genesis.hash (), 3, 4, 6, key1.prv, key1.pub, 7);
	ASSERT_EQ (chratos::block_type::state, block1.type ());
	store.block_put (transaction, block1.hash (), block1);
	ASSERT_TRUE (store.block_exists (transaction, block1.hash ()));
	auto block2 (store.block_get (transaction, block1.hash ()));
	ASSERT_NE (nullptr, block2);
	ASSERT_EQ (block1, *block2);
	auto count (store.block_count (transaction));
	ASSERT_EQ (1, count.state_v0);
	ASSERT_EQ (0, count.state_v1);
	store.block_del (transaction, block1.hash ());
	ASSERT_FALSE (store.block_exists (transaction, block1.hash ()));
	auto count2 (store.block_count (transaction));
	ASSERT_EQ (0, count2.state_v0);
	ASSERT_EQ (0, count2.state_v1);
}
