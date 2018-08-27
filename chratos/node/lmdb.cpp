
#include <chratos/node/lmdb.hpp>

chratos::mdb_env::mdb_env (bool & error_a, boost::filesystem::path const & path_a, int max_dbs)
{
	boost::system::error_code error;
	if (path_a.has_parent_path ())
	{
		boost::filesystem::create_directories (path_a.parent_path (), error);
		if (!error)
		{
			auto status1 (mdb_env_create (&environment));
			assert (status1 == 0);
			auto status2 (mdb_env_set_maxdbs (environment, max_dbs));
			assert (status2 == 0);
			auto status3 (mdb_env_set_mapsize (environment, 1ULL * 1024 * 1024 * 1024 * 128)); // 128 Gigabyte
			assert (status3 == 0);
			// It seems if there's ever more threads than mdb_env_set_maxreaders has read slots available, we get failures on transaction creation unless MDB_NOTLS is specified
			// This can happen if something like 256 io_threads are specified in the node config
			auto status4 (mdb_env_open (environment, path_a.string ().c_str (), MDB_NOSUBDIR | MDB_NOTLS, 00600));
			error_a = status4 != 0;
		}
		else
		{
			error_a = true;
			environment = nullptr;
		}
	}
	else
	{
		error_a = true;
		environment = nullptr;
	}
}

chratos::mdb_env::~mdb_env ()
{
	if (environment != nullptr)
	{
		mdb_env_close (environment);
	}
}

chratos::mdb_env::operator MDB_env * () const
{
	return environment;
}

chratos::mdb_val::mdb_val (chratos::epoch epoch_a) :
value ({ 0, nullptr }),
epoch (epoch_a)
{
}

chratos::mdb_val::mdb_val (MDB_val const & value_a, chratos::epoch epoch_a) :
value (value_a),
epoch (epoch_a)
{
}

chratos::mdb_val::mdb_val (size_t size_a, void * data_a) :
value ({ size_a, data_a })
{
}

chratos::mdb_val::mdb_val (chratos::uint128_union const & val_a) :
mdb_val (sizeof (val_a), const_cast<chratos::uint128_union *> (&val_a))
{
}

chratos::mdb_val::mdb_val (chratos::uint256_union const & val_a) :
mdb_val (sizeof (val_a), const_cast<chratos::uint256_union *> (&val_a))
{
}

chratos::mdb_val::mdb_val (chratos::account_info const & val_a) :
mdb_val (val_a.db_size (), const_cast<chratos::account_info *> (&val_a))
{
}

chratos::mdb_val::mdb_val (chratos::pending_info const & val_a) :
mdb_val (sizeof (val_a.source) + sizeof (val_a.amount), const_cast<chratos::pending_info *> (&val_a))
{
}

chratos::mdb_val::mdb_val (chratos::pending_key const & val_a) :
mdb_val (sizeof (val_a), const_cast<chratos::pending_key *> (&val_a))
{
}

chratos::mdb_val::mdb_val (chratos::block_info const & val_a) :
mdb_val (sizeof (val_a), const_cast<chratos::block_info *> (&val_a))
{
}

chratos::mdb_val::mdb_val (std::shared_ptr<chratos::block> const & val_a) :
buffer (std::make_shared<std::vector<uint8_t>> ())
{
	{
		chratos::vectorstream stream (*buffer);
		chratos::serialize_block (stream, *val_a);
	}
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}

void * chratos::mdb_val::data () const
{
	return value.mv_data;
}

size_t chratos::mdb_val::size () const
{
	return value.mv_size;
}

chratos::mdb_val::operator chratos::account_info () const
{
	chratos::account_info result;
	result.epoch = epoch;
	assert (value.mv_size == result.db_size ());
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

chratos::mdb_val::operator chratos::block_info () const
{
	chratos::block_info result;
	assert (value.mv_size == sizeof (result));
	static_assert (sizeof (chratos::block_info::account) + sizeof (chratos::block_info::balance) == sizeof (result), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
	return result;
}

chratos::mdb_val::operator chratos::pending_info () const
{
	chratos::pending_info result;
	result.epoch = epoch;
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (chratos::pending_info::source) + sizeof (chratos::pending_info::amount), reinterpret_cast<uint8_t *> (&result));
	return result;
}

chratos::mdb_val::operator chratos::pending_key () const
{
	chratos::pending_key result;
	assert (value.mv_size == sizeof (result));
	static_assert (sizeof (chratos::pending_key::account) + sizeof (chratos::pending_key::hash) == sizeof (result), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
	return result;
}

chratos::mdb_val::operator chratos::uint128_union () const
{
	chratos::uint128_union result;
	assert (size () == sizeof (result));
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
	return result;
}

chratos::mdb_val::operator chratos::uint256_union () const
{
	chratos::uint256_union result;
	assert (size () == sizeof (result));
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
	return result;
}

chratos::mdb_val::operator std::array<char, 64> () const
{
	chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	std::array<char, 64> result;
	chratos::read (stream, result);
	return result;
}

chratos::mdb_val::operator no_value () const
{
	return no_value::dummy;
}

chratos::mdb_val::operator std::shared_ptr<chratos::block> () const
{
	chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	std::shared_ptr<chratos::block> result (chratos::deserialize_block (stream));
	return result;
}

chratos::mdb_val::operator std::shared_ptr<chratos::send_block> () const
{
	chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<chratos::send_block> result (std::make_shared<chratos::send_block> (error, stream));
	assert (!error);
	return result;
}

chratos::mdb_val::operator std::shared_ptr<chratos::receive_block> () const
{
	chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<chratos::receive_block> result (std::make_shared<chratos::receive_block> (error, stream));
	assert (!error);
	return result;
}

chratos::mdb_val::operator std::shared_ptr<chratos::open_block> () const
{
	chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<chratos::open_block> result (std::make_shared<chratos::open_block> (error, stream));
	assert (!error);
	return result;
}

chratos::mdb_val::operator std::shared_ptr<chratos::change_block> () const
{
	chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<chratos::change_block> result (std::make_shared<chratos::change_block> (error, stream));
	assert (!error);
	return result;
}

chratos::mdb_val::operator std::shared_ptr<chratos::state_block> () const
{
	chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<chratos::state_block> result (std::make_shared<chratos::state_block> (error, stream));
	assert (!error);
	return result;
}

chratos::mdb_val::operator std::shared_ptr<chratos::vote> () const
{
	auto result (std::make_shared<chratos::vote> ());
	chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (chratos::read (stream, result->account.bytes));
	assert (!error);
	error = chratos::read (stream, result->signature.bytes);
	assert (!error);
	error = chratos::read (stream, result->sequence);
	assert (!error);
	result->blocks.push_back (chratos::deserialize_block (stream));
	assert (boost::get<std::shared_ptr<chratos::block>> (result->blocks[0]) != nullptr);
	return result;
}

chratos::mdb_val::operator uint64_t () const
{
	uint64_t result;
	chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (chratos::read (stream, result));
	assert (!error);
	return result;
}

chratos::mdb_val::operator MDB_val * () const
{
	// Allow passing a temporary to a non-c++ function which doesn't have constness
	return const_cast<MDB_val *> (&value);
};

chratos::mdb_val::operator MDB_val const & () const
{
	return value;
}

chratos::transaction::transaction (chratos::mdb_env & environment_a, MDB_txn * parent_a, bool write) :
environment (environment_a)
{
	auto status (mdb_txn_begin (environment_a, parent_a, write ? 0 : MDB_RDONLY, &handle));
	assert (status == 0);
}

chratos::transaction::~transaction ()
{
	auto status (mdb_txn_commit (handle));
	assert (status == 0);
}

chratos::transaction::operator MDB_txn * () const
{
	return handle;
}
