#include <gtest/gtest.h>

#include <chratos/node/node.hpp>
#include <chratos/node/wallet.hpp>

TEST (work, one)
{
	chratos::work_pool pool (std::numeric_limits<unsigned>::max (), nullptr);
	chratos::change_block block (1, 1, chratos::keypair ().prv, 3, 4);
	block.block_work_set (pool.generate (block.root ()));
	ASSERT_FALSE (chratos::work_validate (block));
}

TEST (work, validate)
{
	chratos::work_pool pool (std::numeric_limits<unsigned>::max (), nullptr);
	chratos::send_block send_block (1, 1, 2, chratos::keypair ().prv, 4, 6);
	ASSERT_TRUE (chratos::work_validate (send_block));
	send_block.block_work_set (pool.generate (send_block.root ()));
	ASSERT_FALSE (chratos::work_validate (send_block));
}

TEST (work, cancel)
{
	chratos::work_pool pool (std::numeric_limits<unsigned>::max (), nullptr);
	auto iterations (0);
	auto done (false);
	while (!done)
	{
		chratos::uint256_union key (1);
		pool.generate (key, [&done](boost::optional<uint64_t> work_a) {
			done = !work_a;
		});
		pool.cancel (key);
		++iterations;
		ASSERT_LT (iterations, 200);
	}
}

TEST (work, cancel_many)
{
	chratos::work_pool pool (std::numeric_limits<unsigned>::max (), nullptr);
	chratos::uint256_union key1 (1);
	chratos::uint256_union key2 (2);
	chratos::uint256_union key3 (1);
	chratos::uint256_union key4 (1);
	chratos::uint256_union key5 (3);
	chratos::uint256_union key6 (1);
	pool.generate (key1, [](boost::optional<uint64_t>) {});
	pool.generate (key2, [](boost::optional<uint64_t>) {});
	pool.generate (key3, [](boost::optional<uint64_t>) {});
	pool.generate (key4, [](boost::optional<uint64_t>) {});
	pool.generate (key5, [](boost::optional<uint64_t>) {});
	pool.generate (key6, [](boost::optional<uint64_t>) {});
	pool.cancel (key1);
}

TEST (work, DISABLED_opencl)
{
	chratos::logging logging;
	logging.init (chratos::unique_path ());
	auto opencl (chratos::opencl_work::create (true, { 0, 1, 1024 * 1024 }, logging));
	if (opencl != nullptr)
	{
		chratos::work_pool pool (std::numeric_limits<unsigned>::max (), opencl ? [&opencl](chratos::uint256_union const & root_a) {
			return opencl->generate_work (root_a);
		}
		                                                                       : std::function<boost::optional<uint64_t> (chratos::uint256_union const &)> (nullptr));
		ASSERT_NE (nullptr, pool.opencl);
		chratos::uint256_union root;
		for (auto i (0); i < 1; ++i)
		{
			chratos::random_pool.GenerateBlock (root.bytes.data (), root.bytes.size ());
			auto result (pool.generate (root));
			ASSERT_FALSE (chratos::work_validate (root, result));
		}
	}
}

TEST (work, opencl_config)
{
	chratos::opencl_config config1;
	config1.platform = 1;
	config1.device = 2;
	config1.threads = 3;
	boost::property_tree::ptree tree;
	config1.serialize_json (tree);
	chratos::opencl_config config2;
	ASSERT_FALSE (config2.deserialize_json (tree));
	ASSERT_EQ (1, config2.platform);
	ASSERT_EQ (2, config2.device);
	ASSERT_EQ (3, config2.threads);
}
