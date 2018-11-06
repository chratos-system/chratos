#include <chratos/node/lmdb.hpp>

#include <chratos/lib/utility.hpp>
#include <chratos/node/common.hpp>
#include <chratos/secure/versioning.hpp>

#include <boost/polymorphic_cast.hpp>

#include <queue>

chratos::mdb_env::mdb_env (bool & error_a, boost::filesystem::path const & path_a, int max_dbs)
{
	boost::system::error_code error_mkdir, error_chmod;
	if (path_a.has_parent_path ())
	{
		boost::filesystem::create_directories (path_a.parent_path (), error_mkdir);
		chratos::set_secure_perm_directory (path_a.parent_path (), error_chmod);
		if (!error_mkdir)
		{
			auto status1 (mdb_env_create (&environment));
			release_assert (status1 == 0);
			auto status2 (mdb_env_set_maxdbs (environment, max_dbs));
			release_assert (status2 == 0);
			auto status3 (mdb_env_set_mapsize (environment, 1ULL * 1024 * 1024 * 1024 * 128)); // 128 Gigabyte
			release_assert (status3 == 0);
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

chratos::transaction chratos::mdb_env::tx_begin (bool write_a) const
{
	return { std::make_unique<chratos::mdb_txn> (*this, write_a) };
}

MDB_txn * chratos::mdb_env::tx (chratos::transaction const & transaction_a) const
{
	auto result (boost::polymorphic_downcast<chratos::mdb_txn *> (transaction_a.impl.get ()));
	release_assert (mdb_txn_env (result->handle) == environment);
	return *result;
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

chratos::mdb_val::mdb_val (chratos::dividend_info const & val_a) :
mdb_val (val_a.db_size (), const_cast<chratos::dividend_info *> (&val_a))
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

chratos::mdb_val::operator chratos::dividend_info () const
{
	chratos::dividend_info result;
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

chratos::mdb_val::operator std::shared_ptr<chratos::state_block> () const
{
	chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<chratos::state_block> result (std::make_shared<chratos::state_block> (error, stream));
	assert (!error);
	return result;
}

chratos::mdb_val::operator std::shared_ptr<chratos::dividend_block> () const
{
	chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<chratos::dividend_block> result (std::make_shared<chratos::dividend_block> (error, stream));
	assert (!error);
	return result;
}

chratos::mdb_val::operator std::shared_ptr<chratos::claim_block> () const
{
	chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<chratos::claim_block> result (std::make_shared<chratos::claim_block> (error, stream));
	assert (!error);
	return result;
}

chratos::mdb_val::operator std::shared_ptr<chratos::vote> () const
{
	chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<chratos::vote> result (std::make_shared<chratos::vote> (error, stream));
	assert (!error);
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

chratos::mdb_txn::mdb_txn (chratos::mdb_env const & environment_a, bool write_a)
{
	auto status (mdb_txn_begin (environment_a, nullptr, write_a ? 0 : MDB_RDONLY, &handle));
	release_assert (status == 0);
}

chratos::mdb_txn::~mdb_txn ()
{
	auto status (mdb_txn_commit (handle));
	release_assert (status == 0);
}

chratos::mdb_txn::operator MDB_txn * () const
{
	return handle;
}

namespace chratos
{
/**
	 * Fill in our predecessors
	 */
class block_predecessor_set : public chratos::block_visitor
{
public:
	block_predecessor_set (chratos::transaction const & transaction_a, chratos::mdb_store & store_a) :
	transaction (transaction_a),
	store (store_a)
	{
	}
	virtual ~block_predecessor_set () = default;
	void fill_value (chratos::block const & block_a)
	{
		auto hash (block_a.hash ());
		chratos::block_type type;
		auto value (store.block_raw_get (transaction, block_a.previous (), type));
		auto version (store.block_version (transaction, block_a.previous ()));
		assert (value.mv_size != 0);
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.mv_data), static_cast<uint8_t *> (value.mv_data) + value.mv_size);
		std::copy (hash.bytes.begin (), hash.bytes.end (), data.end () - hash.bytes.size ());
		store.block_raw_put (transaction, store.block_database (type, version), block_a.previous (), chratos::mdb_val (data.size (), data.data ()));
	}
	void state_block (chratos::state_block const & block_a) override
	{
		if (!block_a.previous ().is_zero ())
		{
			fill_value (block_a);
		}
	}
	void dividend_block (chratos::dividend_block const & block_a) override
	{
		if (!block_a.previous ().is_zero ())
		{
			fill_value (block_a);
		}
	}
	void claim_block (chratos::claim_block const & block_a) override
	{
		if (!block_a.previous ().is_zero ())
		{
			fill_value (block_a);
		}
	}
	chratos::transaction const & transaction;
	chratos::mdb_store & store;
};
}

template <typename T, typename U>
chratos::mdb_iterator<T, U>::mdb_iterator (chratos::transaction const & transaction_a, MDB_dbi db_a, chratos::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
	auto status (mdb_cursor_open (tx (transaction_a), db_a, &cursor));
	release_assert (status == 0);
	auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_FIRST));
	release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
	if (status2 != MDB_NOTFOUND)
	{
		auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
		release_assert (status3 == 0 || status3 == MDB_NOTFOUND);
		if (current.first.size () != sizeof (T))
		{
			clear ();
		}
	}
	else
	{
		clear ();
	}
}

template <typename T, typename U>
chratos::mdb_iterator<T, U>::mdb_iterator (std::nullptr_t, chratos::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
}

template <typename T, typename U>
chratos::mdb_iterator<T, U>::mdb_iterator (chratos::transaction const & transaction_a, MDB_dbi db_a, MDB_val const & val_a, chratos::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
	auto status (mdb_cursor_open (tx (transaction_a), db_a, &cursor));
	release_assert (status == 0);
	current.first = val_a;
	auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_SET_RANGE));
	release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
	if (status2 != MDB_NOTFOUND)
	{
		auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
		release_assert (status3 == 0 || status3 == MDB_NOTFOUND);
		if (current.first.size () != sizeof (T))
		{
			clear ();
		}
	}
	else
	{
		clear ();
	}
}

template <typename T, typename U>
chratos::mdb_iterator<T, U>::mdb_iterator (chratos::mdb_iterator<T, U> && other_a)
{
	cursor = other_a.cursor;
	other_a.cursor = nullptr;
	current = other_a.current;
}

template <typename T, typename U>
chratos::mdb_iterator<T, U>::~mdb_iterator ()
{
	if (cursor != nullptr)
	{
		mdb_cursor_close (cursor);
	}
}

template <typename T, typename U>
chratos::store_iterator_impl<T, U> & chratos::mdb_iterator<T, U>::operator++ ()
{
	assert (cursor != nullptr);
	auto status (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_NEXT));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	if (status == MDB_NOTFOUND)
	{
		clear ();
	}
	if (current.first.size () != sizeof (T))
	{
		clear ();
	}
	return *this;
}

template <typename T, typename U>
chratos::mdb_iterator<T, U> & chratos::mdb_iterator<T, U>::operator= (chratos::mdb_iterator<T, U> && other_a)
{
	if (cursor != nullptr)
	{
		mdb_cursor_close (cursor);
	}
	cursor = other_a.cursor;
	other_a.cursor = nullptr;
	current = other_a.current;
	other_a.clear ();
	return *this;
}

template <typename T, typename U>
std::pair<chratos::mdb_val, chratos::mdb_val> * chratos::mdb_iterator<T, U>::operator-> ()
{
	return &current;
}

template <typename T, typename U>
bool chratos::mdb_iterator<T, U>::operator== (chratos::store_iterator_impl<T, U> const & base_a) const
{
	auto const other_a (boost::polymorphic_downcast<chratos::mdb_iterator<T, U> const *> (&base_a));
	auto result (current.first.data () == other_a->current.first.data ());
	assert (!result || (current.first.size () == other_a->current.first.size ()));
	assert (!result || (current.second.data () == other_a->current.second.data ()));
	assert (!result || (current.second.size () == other_a->current.second.size ()));
	return result;
}

template <typename T, typename U>
void chratos::mdb_iterator<T, U>::clear ()
{
	current.first = chratos::mdb_val (current.first.epoch);
	current.second = chratos::mdb_val (current.second.epoch);
	assert (is_end_sentinal ());
}

template <typename T, typename U>
MDB_txn * chratos::mdb_iterator<T, U>::tx (chratos::transaction const & transaction_a) const
{
	auto result (boost::polymorphic_downcast<chratos::mdb_txn *> (transaction_a.impl.get ()));
	return *result;
}

template <typename T, typename U>
bool chratos::mdb_iterator<T, U>::is_end_sentinal () const
{
	return current.first.size () == 0;
}

template <typename T, typename U>
void chratos::mdb_iterator<T, U>::fill (std::pair<T, U> & value_a) const
{
	if (current.first.size () != 0)
	{
		value_a.first = static_cast<T> (current.first);
	}
	else
	{
		value_a.first = T ();
	}
	if (current.second.size () != 0)
	{
		value_a.second = static_cast<U> (current.second);
	}
	else
	{
		value_a.second = U ();
	}
}

template <typename T, typename U>
std::pair<chratos::mdb_val, chratos::mdb_val> * chratos::mdb_merge_iterator<T, U>::operator-> ()
{
	return least_iterator ().operator-> ();
}

template <typename T, typename U>
chratos::mdb_merge_iterator<T, U>::mdb_merge_iterator (chratos::transaction const & transaction_a, MDB_dbi db1_a, MDB_dbi db2_a) :
impl1 (std::make_unique<chratos::mdb_iterator<T, U>> (transaction_a, db1_a, chratos::epoch::epoch_0)),
impl2 (std::make_unique<chratos::mdb_iterator<T, U>> (transaction_a, db2_a, chratos::epoch::epoch_1))
{
}

template <typename T, typename U>
chratos::mdb_merge_iterator<T, U>::mdb_merge_iterator (std::nullptr_t) :
impl1 (std::make_unique<chratos::mdb_iterator<T, U>> (nullptr, chratos::epoch::epoch_0)),
impl2 (std::make_unique<chratos::mdb_iterator<T, U>> (nullptr, chratos::epoch::epoch_1))
{
}

template <typename T, typename U>
chratos::mdb_merge_iterator<T, U>::mdb_merge_iterator (chratos::transaction const & transaction_a, MDB_dbi db1_a, MDB_dbi db2_a, MDB_val const & val_a) :
impl1 (std::make_unique<chratos::mdb_iterator<T, U>> (transaction_a, db1_a, val_a, chratos::epoch::epoch_0)),
impl2 (std::make_unique<chratos::mdb_iterator<T, U>> (transaction_a, db2_a, val_a, chratos::epoch::epoch_1))
{
}

template <typename T, typename U>
chratos::mdb_merge_iterator<T, U>::mdb_merge_iterator (chratos::mdb_merge_iterator<T, U> && other_a)
{
	impl1 = std::move (other_a.impl1);
	impl2 = std::move (other_a.impl2);
}

template <typename T, typename U>
chratos::mdb_merge_iterator<T, U>::~mdb_merge_iterator ()
{
}

template <typename T, typename U>
chratos::store_iterator_impl<T, U> & chratos::mdb_merge_iterator<T, U>::operator++ ()
{
	++least_iterator ();
	return *this;
}

template <typename T, typename U>
bool chratos::mdb_merge_iterator<T, U>::is_end_sentinal () const
{
	return least_iterator ().is_end_sentinal ();
}

template <typename T, typename U>
void chratos::mdb_merge_iterator<T, U>::fill (std::pair<T, U> & value_a) const
{
	auto & current (least_iterator ());
	if (current->first.size () != 0)
	{
		value_a.first = static_cast<T> (current->first);
	}
	else
	{
		value_a.first = T ();
	}
	if (current->second.size () != 0)
	{
		value_a.second = static_cast<U> (current->second);
	}
	else
	{
		value_a.second = U ();
	}
}

template <typename T, typename U>
bool chratos::mdb_merge_iterator<T, U>::operator== (chratos::store_iterator_impl<T, U> const & base_a) const
{
	assert ((dynamic_cast<chratos::mdb_merge_iterator<T, U> const *> (&base_a) != nullptr) && "Incompatible iterator comparison");
	auto & other (static_cast<chratos::mdb_merge_iterator<T, U> const &> (base_a));
	return *impl1 == *other.impl1 && *impl2 == *other.impl2;
}

template <typename T, typename U>
chratos::mdb_iterator<T, U> & chratos::mdb_merge_iterator<T, U>::least_iterator () const
{
	chratos::mdb_iterator<T, U> * result;
	if (impl1->is_end_sentinal ())
	{
		result = impl2.get ();
	}
	else if (impl2->is_end_sentinal ())
	{
		result = impl1.get ();
	}
	else
	{
		auto key_cmp (mdb_cmp (mdb_cursor_txn (impl1->cursor), mdb_cursor_dbi (impl1->cursor), impl1->current.first, impl2->current.first));

		if (key_cmp < 0)
		{
			result = impl1.get ();
		}
		else if (key_cmp > 0)
		{
			result = impl2.get ();
		}
		else
		{
			auto val_cmp (mdb_cmp (mdb_cursor_txn (impl1->cursor), mdb_cursor_dbi (impl1->cursor), impl1->current.second, impl2->current.second));
			result = val_cmp < 0 ? impl1.get () : impl2.get ();
		}
	}
	return *result;
}

chratos::wallet_value::wallet_value (chratos::mdb_val const & val_a)
{
	assert (val_a.size () == sizeof (*this));
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), key.chars.begin ());
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key) + sizeof (work), reinterpret_cast<char *> (&work));
}

chratos::wallet_value::wallet_value (chratos::uint256_union const & key_a, uint64_t work_a) :
key (key_a),
work (work_a)
{
}

chratos::mdb_val chratos::wallet_value::val () const
{
	static_assert (sizeof (*this) == sizeof (key) + sizeof (work), "Class not packed");
	return chratos::mdb_val (sizeof (*this), const_cast<chratos::wallet_value *> (this));
}

template class chratos::mdb_iterator<chratos::pending_key, chratos::pending_info>;
template class chratos::mdb_iterator<chratos::uint256_union, chratos::block_info>;
template class chratos::mdb_iterator<chratos::uint256_union, chratos::uint128_union>;
template class chratos::mdb_iterator<chratos::uint256_union, chratos::uint256_union>;
template class chratos::mdb_iterator<chratos::uint256_union, std::shared_ptr<chratos::block>>;
template class chratos::mdb_iterator<chratos::uint256_union, std::shared_ptr<chratos::vote>>;
template class chratos::mdb_iterator<chratos::uint256_union, chratos::wallet_value>;
template class chratos::mdb_iterator<std::array<char, 64>, chratos::mdb_val::no_value>;

chratos::store_iterator<chratos::block_hash, chratos::block_info> chratos::mdb_store::block_info_begin (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a)
{
	chratos::store_iterator<chratos::block_hash, chratos::block_info> result (std::make_unique<chratos::mdb_iterator<chratos::block_hash, chratos::block_info>> (transaction_a, blocks_info, chratos::mdb_val (hash_a)));
	return result;
}

chratos::store_iterator<chratos::block_hash, chratos::block_info> chratos::mdb_store::block_info_begin (chratos::transaction const & transaction_a)
{
	chratos::store_iterator<chratos::block_hash, chratos::block_info> result (std::make_unique<chratos::mdb_iterator<chratos::block_hash, chratos::block_info>> (transaction_a, blocks_info));
	return result;
}

chratos::store_iterator<chratos::block_hash, chratos::block_info> chratos::mdb_store::block_info_end ()
{
	chratos::store_iterator<chratos::block_hash, chratos::block_info> result (nullptr);
	return result;
}

chratos::store_iterator<chratos::account, chratos::uint128_union> chratos::mdb_store::representation_begin (chratos::transaction const & transaction_a)
{
	chratos::store_iterator<chratos::account, chratos::uint128_union> result (std::make_unique<chratos::mdb_iterator<chratos::account, chratos::uint128_union>> (transaction_a, representation));
	return result;
}

chratos::store_iterator<chratos::account, chratos::uint128_union> chratos::mdb_store::representation_end ()
{
	chratos::store_iterator<chratos::account, chratos::uint128_union> result (nullptr);
	return result;
}

chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> chratos::mdb_store::unchecked_begin (chratos::transaction const & transaction_a)
{
	chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> result (std::make_unique<chratos::mdb_iterator<chratos::account, std::shared_ptr<chratos::block>>> (transaction_a, unchecked));
	return result;
}

chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> chratos::mdb_store::unchecked_begin (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a)
{
	chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> result (std::make_unique<chratos::mdb_iterator<chratos::block_hash, std::shared_ptr<chratos::block>>> (transaction_a, unchecked, chratos::mdb_val (hash_a)));
	return result;
}

chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> chratos::mdb_store::unchecked_end ()
{
	chratos::store_iterator<chratos::block_hash, std::shared_ptr<chratos::block>> result (nullptr);
	return result;
}

chratos::store_iterator<chratos::account, std::shared_ptr<chratos::vote>> chratos::mdb_store::vote_begin (chratos::transaction const & transaction_a)
{
	return chratos::store_iterator<chratos::account, std::shared_ptr<chratos::vote>> (std::make_unique<chratos::mdb_iterator<chratos::account, std::shared_ptr<chratos::vote>>> (transaction_a, vote));
}

chratos::store_iterator<chratos::account, std::shared_ptr<chratos::vote>> chratos::mdb_store::vote_end ()
{
	return chratos::store_iterator<chratos::account, std::shared_ptr<chratos::vote>> (nullptr);
}

chratos::mdb_store::mdb_store (bool & error_a, boost::filesystem::path const & path_a, int lmdb_max_dbs) :
env (error_a, path_a, lmdb_max_dbs),
frontiers (0),
accounts_v0 (0),
accounts_v1 (0),
dividends_ledger (0),
state_blocks_v0 (0),
state_blocks_v1 (0),
dividend_blocks (0),
claim_blocks (0),
pending_v0 (0),
pending_v1 (0),
blocks_info (0),
representation (0),
unchecked (0),
checksum (0),
vote (0),
meta (0)
{
	if (!error_a)
	{
		auto transaction (tx_begin_write ());
		error_a |= mdb_dbi_open (env.tx (transaction), "frontiers", MDB_CREATE, &frontiers) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "accounts", MDB_CREATE, &accounts_v0) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "accounts_v1", MDB_CREATE, &accounts_v1) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "dividends_ledger", MDB_CREATE, &dividends_ledger) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "state", MDB_CREATE, &state_blocks_v0) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "state_v1", MDB_CREATE, &state_blocks_v1) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "dividend", MDB_CREATE, &dividend_blocks) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "claim", MDB_CREATE, &claim_blocks) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "pending", MDB_CREATE, &pending_v0) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "pending_v1", MDB_CREATE, &pending_v1) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "blocks_info", MDB_CREATE, &blocks_info) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "representation", MDB_CREATE, &representation) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "unchecked", MDB_CREATE | MDB_DUPSORT, &unchecked) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "checksum", MDB_CREATE, &checksum) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "vote", MDB_CREATE, &vote) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "meta", MDB_CREATE, &meta) != 0;
		if (!error_a)
		{
			do_upgrades (transaction);
			checksum_put (transaction, 0, 0, 0);
		}
	}
}

chratos::transaction chratos::mdb_store::tx_begin_write ()
{
	return tx_begin (true);
}

chratos::transaction chratos::mdb_store::tx_begin_read ()
{
	return tx_begin (false);
}

chratos::transaction chratos::mdb_store::tx_begin (bool write_a)
{
	return env.tx_begin (write_a);
}

void chratos::mdb_store::initialize (chratos::transaction const & transaction_a, chratos::genesis const & genesis_a)
{
	auto hash_l (genesis_a.hash ());
	assert (latest_v0_begin (transaction_a) == latest_v0_end ());
	assert (latest_v1_begin (transaction_a) == latest_v1_end ());
	block_put (transaction_a, hash_l, *genesis_a.open);
	account_put (transaction_a, genesis_account, { hash_l, genesis_a.open->hash (), genesis_a.open->hash (), 0, std::numeric_limits<chratos::uint128_t>::max (), chratos::seconds_since_epoch (), 1, chratos::epoch::epoch_0 });
	representation_put (transaction_a, genesis_account, std::numeric_limits<chratos::uint128_t>::max ());
	checksum_put (transaction_a, 0, 0, hash_l);
	frontier_put (transaction_a, hash_l, genesis_account);
	dividend_put (transaction_a, dividend_info ());
}

void chratos::mdb_store::version_put (chratos::transaction const & transaction_a, int version_a)
{
	chratos::uint256_union version_key (1);
	chratos::uint256_union version_value (version_a);
	auto status (mdb_put (env.tx (transaction_a), meta, chratos::mdb_val (version_key), chratos::mdb_val (version_value), 0));
	release_assert (status == 0);
}

int chratos::mdb_store::version_get (chratos::transaction const & transaction_a)
{
	chratos::uint256_union version_key (1);
	chratos::mdb_val data;
	auto error (mdb_get (env.tx (transaction_a), meta, chratos::mdb_val (version_key), data));
	int result (1);
	if (error != MDB_NOTFOUND)
	{
		chratos::uint256_union version_value (data);
		assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
		result = version_value.number ().convert_to<int> ();
	}
	return result;
}

chratos::raw_key chratos::mdb_store::get_node_id (chratos::transaction const & transaction_a)
{
	chratos::uint256_union node_id_mdb_key (3);
	chratos::raw_key node_id;
	chratos::mdb_val value;
	auto error (mdb_get (env.tx (transaction_a), meta, chratos::mdb_val (node_id_mdb_key), value));
	if (!error)
	{
		chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		error = chratos::read (stream, node_id.data);
		assert (!error);
	}
	if (error)
	{
		chratos::random_pool.GenerateBlock (node_id.data.bytes.data (), node_id.data.bytes.size ());
		error = mdb_put (env.tx (transaction_a), meta, chratos::mdb_val (node_id_mdb_key), chratos::mdb_val (node_id.data), 0);
	}
	assert (!error);
	return node_id;
}

void chratos::mdb_store::delete_node_id (chratos::transaction const & transaction_a)
{
	chratos::uint256_union node_id_mdb_key (3);
	auto error (mdb_del (env.tx (transaction_a), meta, chratos::mdb_val (node_id_mdb_key), nullptr));
	assert (!error || error == MDB_NOTFOUND);
}

void chratos::mdb_store::do_upgrades (chratos::transaction const & transaction_a)
{
	switch (version_get (transaction_a))
	{
		case 1:
			upgrade_v1_to_v2 (transaction_a);
		case 2:
			upgrade_v2_to_v3 (transaction_a);
		case 3:
			upgrade_v3_to_v4 (transaction_a);
		case 4:
			upgrade_v4_to_v5 (transaction_a);
		case 5:
			upgrade_v5_to_v6 (transaction_a);
		case 6:
			upgrade_v6_to_v7 (transaction_a);
		case 7:
			upgrade_v7_to_v8 (transaction_a);
		case 8:
			upgrade_v8_to_v9 (transaction_a);
		case 9:
			upgrade_v9_to_v10 (transaction_a);
		case 10:
			upgrade_v10_to_v11 (transaction_a);
		case 11:
			break;
		default:
			assert (false);
	}
}

void chratos::mdb_store::upgrade_v1_to_v2 (chratos::transaction const & transaction_a)
{
	version_put (transaction_a, 2);
	chratos::account account (1);
	while (!account.is_zero ())
	{
		chratos::mdb_iterator<chratos::uint256_union, chratos::account_info_v1> i (transaction_a, accounts_v0, chratos::mdb_val (account));
		std::cerr << std::hex;
		if (i != chratos::mdb_iterator<chratos::uint256_union, chratos::account_info_v1> (nullptr))
		{
			account = chratos::uint256_union (i->first);
			chratos::account_info_v1 v1 (i->second);
			chratos::account_info_v5 v2;
			v2.balance = v1.balance;
			v2.head = v1.head;
			v2.modified = v1.modified;
			v2.rep_block = v1.rep_block;
			auto block (block_get (transaction_a, v1.head));
			while (!block->previous ().is_zero ())
			{
				block = block_get (transaction_a, block->previous ());
			}
			v2.open_block = block->hash ();
			auto status (mdb_put (env.tx (transaction_a), accounts_v0, chratos::mdb_val (account), v2.val (), 0));
			release_assert (status == 0);
			account = account.number () + 1;
		}
		else
		{
			account.clear ();
		}
	}
}

void chratos::mdb_store::upgrade_v2_to_v3 (chratos::transaction const & transaction_a)
{
	version_put (transaction_a, 3);
	mdb_drop (env.tx (transaction_a), representation, 0);
	for (auto i (std::make_unique<chratos::mdb_iterator<chratos::account, chratos::account_info_v5>> (transaction_a, accounts_v0)), n (std::make_unique<chratos::mdb_iterator<chratos::account, chratos::account_info_v5>> (nullptr)); *i != *n; ++(*i))
	{
		chratos::account account_l ((*i)->first);
		chratos::account_info_v5 info ((*i)->second);
		representative_visitor visitor (transaction_a, *this);
		visitor.compute (info.head);
		assert (!visitor.result.is_zero ());
		info.rep_block = visitor.result;
		auto impl (boost::polymorphic_downcast<chratos::mdb_iterator<chratos::account, chratos::account_info_v5> *> (i.get ()));
		mdb_cursor_put (impl->cursor, chratos::mdb_val (account_l), info.val (), MDB_CURRENT);
		representation_add (transaction_a, visitor.result, info.balance.number ());
	}
}

void chratos::mdb_store::upgrade_v3_to_v4 (chratos::transaction const & transaction_a)
{
	version_put (transaction_a, 4);
	std::queue<std::pair<chratos::pending_key, chratos::pending_info>> items;
	for (auto i (chratos::store_iterator<chratos::block_hash, chratos::pending_info_v3> (std::make_unique<chratos::mdb_iterator<chratos::block_hash, chratos::pending_info_v3>> (transaction_a, pending_v0))), n (chratos::store_iterator<chratos::block_hash, chratos::pending_info_v3> (nullptr)); i != n; ++i)
	{
		chratos::block_hash hash (i->first);
		chratos::pending_info_v3 info (i->second);
		items.push (std::make_pair (chratos::pending_key (info.destination, hash), chratos::pending_info (info.source, info.amount, 0, chratos::epoch::epoch_0)));
	}
	mdb_drop (env.tx (transaction_a), pending_v0, 0);
	while (!items.empty ())
	{
		pending_put (transaction_a, items.front ().first, items.front ().second);
		items.pop ();
	}
}

void chratos::mdb_store::upgrade_v4_to_v5 (chratos::transaction const & transaction_a)
{
	version_put (transaction_a, 5);
	for (auto i (chratos::store_iterator<chratos::account, chratos::account_info_v5> (std::make_unique<chratos::mdb_iterator<chratos::account, chratos::account_info_v5>> (transaction_a, accounts_v0))), n (chratos::store_iterator<chratos::account, chratos::account_info_v5> (nullptr)); i != n; ++i)
	{
		chratos::account_info_v5 info (i->second);
		chratos::block_hash successor (0);
		auto block (block_get (transaction_a, info.head));
		while (block != nullptr)
		{
			auto hash (block->hash ());
			if (block_successor (transaction_a, hash).is_zero () && !successor.is_zero ())
			{
				block_put (transaction_a, hash, *block, successor);
			}
			successor = hash;
			block = block_get (transaction_a, block->previous ());
		}
	}
}

void chratos::mdb_store::upgrade_v5_to_v6 (chratos::transaction const & transaction_a)
{
	version_put (transaction_a, 6);
	std::deque<std::pair<chratos::account, chratos::account_info>> headers;
	for (auto i (chratos::store_iterator<chratos::account, chratos::account_info_v5> (std::make_unique<chratos::mdb_iterator<chratos::account, chratos::account_info_v5>> (transaction_a, accounts_v0))), n (chratos::store_iterator<chratos::account, chratos::account_info_v5> (nullptr)); i != n; ++i)
	{
		chratos::account account (i->first);
		chratos::account_info_v5 info_old (i->second);
		uint64_t block_count (0);
		auto hash (info_old.head);
		while (!hash.is_zero ())
		{
			++block_count;
			auto block (block_get (transaction_a, hash));
			assert (block != nullptr);
			hash = block->previous ();
		}
		chratos::account_info info (info_old.head, info_old.rep_block, info_old.open_block, 0, info_old.balance, info_old.modified, block_count, chratos::epoch::epoch_0);
		headers.push_back (std::make_pair (account, info));
	}
	for (auto i (headers.begin ()), n (headers.end ()); i != n; ++i)
	{
		account_put (transaction_a, i->first, i->second);
	}
}

void chratos::mdb_store::upgrade_v6_to_v7 (chratos::transaction const & transaction_a)
{
	version_put (transaction_a, 7);
	mdb_drop (env.tx (transaction_a), unchecked, 0);
}

void chratos::mdb_store::upgrade_v7_to_v8 (chratos::transaction const & transaction_a)
{
	version_put (transaction_a, 8);
	mdb_drop (env.tx (transaction_a), unchecked, 1);
	mdb_dbi_open (env.tx (transaction_a), "unchecked", MDB_CREATE | MDB_DUPSORT, &unchecked);
}

void chratos::mdb_store::upgrade_v8_to_v9 (chratos::transaction const & transaction_a)
{
	version_put (transaction_a, 9);
	MDB_dbi sequence;
	mdb_dbi_open (env.tx (transaction_a), "sequence", MDB_CREATE | MDB_DUPSORT, &sequence);
	chratos::genesis genesis;
	std::shared_ptr<chratos::block> block (std::move (genesis.open));
	chratos::keypair junk;
	for (chratos::mdb_iterator<chratos::account, uint64_t> i (transaction_a, sequence), n (chratos::mdb_iterator<chratos::account, uint64_t> (nullptr)); i != n; ++i)
	{
		chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (i->second.data ()), i->second.size ());
		uint64_t sequence;
		auto error (chratos::read (stream, sequence));
		// Create a dummy vote with the same sequence number for easy upgrading.  This won't have a valid signature.
		chratos::vote dummy (chratos::account (i->first), junk.prv, sequence, block);
		std::vector<uint8_t> vector;
		{
			chratos::vectorstream stream (vector);
			dummy.serialize (stream);
		}
		auto status1 (mdb_put (env.tx (transaction_a), vote, chratos::mdb_val (i->first), chratos::mdb_val (vector.size (), vector.data ()), 0));
		release_assert (status1 == 0);
		assert (!error);
	}
	mdb_drop (env.tx (transaction_a), sequence, 1);
}

void chratos::mdb_store::upgrade_v9_to_v10 (chratos::transaction const & transaction_a)
{
	//std::cerr << boost::str (boost::format ("Performing database upgrade to version 10...\n"));
	version_put (transaction_a, 10);
	for (auto i (latest_v0_begin (transaction_a)), n (latest_v0_end ()); i != n; ++i)
	{
		chratos::account_info info (i->second);
		if (info.block_count >= block_info_max)
		{
			chratos::account account (i->first);
			//std::cerr << boost::str (boost::format ("Upgrading account %1%...\n") % account.to_account ());
			size_t block_count (1);
			auto hash (info.open_block);
			while (!hash.is_zero ())
			{
				if ((block_count % block_info_max) == 0)
				{
					chratos::block_info block_info;
					block_info.account = account;
					chratos::amount balance (block_balance (transaction_a, hash));
					block_info.balance = balance;
					block_info_put (transaction_a, hash, block_info);
				}
				hash = block_successor (transaction_a, hash);
				++block_count;
			}
		}
	}
}

void chratos::mdb_store::upgrade_v10_to_v11 (chratos::transaction const & transaction_a)
{
	version_put (transaction_a, 11);
	MDB_dbi unsynced;
	mdb_dbi_open (env.tx (transaction_a), "unsynced", MDB_CREATE | MDB_DUPSORT, &unsynced);
	mdb_drop (env.tx (transaction_a), unsynced, 1);
}

void chratos::mdb_store::clear (MDB_dbi db_a)
{
	auto transaction (tx_begin_write ());
	auto status (mdb_drop (env.tx (transaction), db_a, 0));
	release_assert (status == 0);
}

chratos::uint128_t chratos::mdb_store::block_balance (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a)
{
	balance_visitor visitor (transaction_a, *this);
	visitor.compute (hash_a);
	return visitor.balance;
}

chratos::epoch chratos::mdb_store::block_version (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a)
{
	chratos::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), state_blocks_v1, chratos::mdb_val (hash_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	return status == 0 ? chratos::epoch::epoch_1 : chratos::epoch::epoch_0;
}

void chratos::mdb_store::representation_add (chratos::transaction const & transaction_a, chratos::block_hash const & source_a, chratos::uint128_t const & amount_a)
{
	auto source_block (block_get (transaction_a, source_a));
	assert (source_block != nullptr);
	auto source_rep (source_block->representative ());
	auto source_previous (representation_get (transaction_a, source_rep));
	representation_put (transaction_a, source_rep, source_previous + amount_a);
}

MDB_dbi chratos::mdb_store::block_database (chratos::block_type type_a, chratos::epoch epoch_a)
{
	if (type_a == chratos::block_type::state)
	{
		assert (epoch_a == chratos::epoch::epoch_0 || epoch_a == chratos::epoch::epoch_1);
	}
	else
	{
		assert (epoch_a == chratos::epoch::epoch_0);
	}
	MDB_dbi result;
	switch (type_a)
	{
		case chratos::block_type::state:
			switch (epoch_a)
			{
				case chratos::epoch::epoch_0:
					result = state_blocks_v0;
					break;
				case chratos::epoch::epoch_1:
					result = state_blocks_v1;
					break;
				default:
					assert (false);
			}
			break;
		case chratos::block_type::dividend:
			result = dividend_blocks;
			break;
		case chratos::block_type::claim:
			result = claim_blocks;
			break;
		default:
			assert (false);
			break;
	}
	return result;
}

void chratos::mdb_store::block_raw_put (chratos::transaction const & transaction_a, MDB_dbi database_a, chratos::block_hash const & hash_a, MDB_val value_a)
{
	auto status2 (mdb_put (env.tx (transaction_a), database_a, chratos::mdb_val (hash_a), &value_a, 0));
	release_assert (status2 == 0);
}

void chratos::mdb_store::block_put (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a, chratos::block const & block_a, chratos::block_hash const & successor_a, chratos::epoch epoch_a)
{
	assert (successor_a.is_zero () || block_exists (transaction_a, successor_a));
	std::vector<uint8_t> vector;
	{
		chratos::vectorstream stream (vector);
		block_a.serialize (stream);
		chratos::write (stream, successor_a.bytes);
	}
	block_raw_put (transaction_a, block_database (block_a.type (), epoch_a), hash_a, { vector.size (), vector.data () });
	chratos::block_predecessor_set predecessor (transaction_a, *this);
	block_a.visit (predecessor);
	assert (block_a.previous ().is_zero () || block_successor (transaction_a, block_a.previous ()) == hash_a);
}

MDB_val chratos::mdb_store::block_raw_get (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a, chratos::block_type & type_a)
{
	chratos::mdb_val result;
	auto status (mdb_get (env.tx (transaction_a), state_blocks_v0, chratos::mdb_val (hash_a), result));
	assert (status == 0 || status == MDB_NOTFOUND);
	if (status != 0)
	{
		auto status (mdb_get (env.tx (transaction_a), state_blocks_v1, chratos::mdb_val (hash_a), result));
		assert (status == 0 || status == MDB_NOTFOUND);
		if (status != 0)
		{
			auto status (mdb_get (env.tx (transaction_a), dividend_blocks, chratos::mdb_val (hash_a), result));
			assert (status == 0 || status == MDB_NOTFOUND);
			if (status != 0)
			{
				auto status (mdb_get (env.tx (transaction_a), claim_blocks, chratos::mdb_val (hash_a), result));
				assert (status == 0 || status == MDB_NOTFOUND);
				if (status != 0)
				{
					// Block not found
				}
				else
				{
					type_a = chratos::block_type::claim;
				}
			}
			else
			{
				type_a = chratos::block_type::dividend;
			}
		}
		else
		{
			type_a = chratos::block_type::state;
		}
	}
	else
	{
		type_a = chratos::block_type::state;
	}
	return result;
}

template <typename T>
std::unique_ptr<chratos::block> chratos::mdb_store::block_random (chratos::transaction const & transaction_a, MDB_dbi database)
{
	chratos::block_hash hash;
	chratos::random_pool.GenerateBlock (hash.bytes.data (), hash.bytes.size ());
	chratos::store_iterator<chratos::block_hash, std::shared_ptr<T>> existing (std::make_unique<chratos::mdb_iterator<chratos::block_hash, std::shared_ptr<T>>> (transaction_a, database, chratos::mdb_val (hash)));
	if (existing == chratos::store_iterator<chratos::block_hash, std::shared_ptr<T>> (nullptr))
	{
		existing = chratos::store_iterator<chratos::block_hash, std::shared_ptr<T>> (std::make_unique<chratos::mdb_iterator<chratos::block_hash, std::shared_ptr<T>>> (transaction_a, database));
	}
	auto end (chratos::store_iterator<chratos::block_hash, std::shared_ptr<T>> (nullptr));
	assert (existing != end);
	return block_get (transaction_a, chratos::block_hash (existing->first));
}

std::unique_ptr<chratos::block> chratos::mdb_store::block_random (chratos::transaction const & transaction_a)
{
	auto count (block_count (transaction_a));
	auto region (chratos::random_pool.GenerateWord32 (0, count.sum () - 1));
	std::unique_ptr<chratos::block> result;
	if (region < count.state_v0)
	{
		result = block_random<chratos::state_block> (transaction_a, state_blocks_v0);
	}
	else
	{
		region -= count.state_v0;
		if (region < count.state_v1)
		{
			result = block_random<chratos::state_block> (transaction_a, state_blocks_v1);
		}
		else
		{
			region -= count.state_v1;
			if (region < count.dividend)
			{
				result = block_random<chratos::dividend_block> (transaction_a, dividend_blocks);
			}
			else
			{
				result = block_random<chratos::claim_block> (transaction_a, claim_blocks);
			}
		}
	}
	assert (result != nullptr);
	return result;
}

chratos::block_hash chratos::mdb_store::block_successor (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a)
{
	chratos::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	chratos::block_hash result;
	if (value.mv_size != 0)
	{
		assert (value.mv_size >= result.bytes.size ());
		chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data) + value.mv_size - result.bytes.size (), result.bytes.size ());
		auto error (chratos::read (stream, result.bytes));
		assert (!error);
	}
	else
	{
		result.clear ();
	}
	return result;
}

void chratos::mdb_store::block_successor_clear (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a)
{
	auto block (block_get (transaction_a, hash_a));
	auto version (block_version (transaction_a, hash_a));
	block_put (transaction_a, hash_a, *block, 0, version);
}

std::unique_ptr<chratos::block> chratos::mdb_store::block_get (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a)
{
	chratos::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	std::unique_ptr<chratos::block> result;
	if (value.mv_size != 0)
	{
		chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
		result = chratos::deserialize_block (stream, type);
		assert (result != nullptr);
	}
	return result;
}

void chratos::mdb_store::block_del (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a)
{
	auto status (mdb_del (env.tx (transaction_a), state_blocks_v1, chratos::mdb_val (hash_a), nullptr));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	if (status != 0)
	{
		auto status (mdb_del (env.tx (transaction_a), state_blocks_v0, chratos::mdb_val (hash_a), nullptr));
		release_assert (status == 0 || status == MDB_NOTFOUND);
		if (status != 0)
		{
			auto status (mdb_del (env.tx (transaction_a), dividend_blocks, chratos::mdb_val (hash_a), nullptr));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			if (status != 0)
			{
				auto status (mdb_del (env.tx (transaction_a), claim_blocks, chratos::mdb_val (hash_a), nullptr));
				release_assert (status == 0);
			}
		}
	}
}

bool chratos::mdb_store::block_exists (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a)
{
	auto exists (true);
	chratos::mdb_val junk;
	auto status (mdb_get (env.tx (transaction_a), state_blocks_v0, chratos::mdb_val (hash_a), junk));
	assert (status == 0 || status == MDB_NOTFOUND);
	exists = status == 0;
	if (!exists)
	{
		auto status (mdb_get (env.tx (transaction_a), state_blocks_v1, chratos::mdb_val (hash_a), junk));
		assert (status == 0 || status == MDB_NOTFOUND);
		exists = status == 0;
		if (!exists)
		{
			auto status (mdb_get (env.tx (transaction_a), dividend_blocks, chratos::mdb_val (hash_a), junk));
			assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			if (!exists)
			{
				auto status (mdb_get (env.tx (transaction_a), claim_blocks, chratos::mdb_val (hash_a), junk));
				assert (status == 0 || status == MDB_NOTFOUND);
				exists = status == 0;
			}
		}
	}
	return exists;
}

chratos::block_counts chratos::mdb_store::block_count (chratos::transaction const & transaction_a)
{
	chratos::block_counts result;
	MDB_stat state_v0_stats;
	auto status1 (mdb_stat (env.tx (transaction_a), state_blocks_v0, &state_v0_stats));
	assert (status1 == 0);
	MDB_stat state_v1_stats;
	auto status2 (mdb_stat (env.tx (transaction_a), state_blocks_v1, &state_v1_stats));
	assert (status2 == 0);
	MDB_stat dividend_stats;
	auto status3 (mdb_stat (env.tx (transaction_a), dividend_blocks, &dividend_stats));
	assert (status3 == 0);
	MDB_stat claim_stats;
	auto status4 (mdb_stat (env.tx (transaction_a), claim_blocks, &claim_stats));
	assert (status4 == 0);
	result.state_v0 = state_v0_stats.ms_entries;
	result.state_v1 = state_v1_stats.ms_entries;
	result.dividend = dividend_stats.ms_entries;
	result.claim = claim_stats.ms_entries;
	return result;
}

bool chratos::mdb_store::root_exists (chratos::transaction const & transaction_a, chratos::uint256_union const & root_a)
{
	return block_exists (transaction_a, root_a) || account_exists (transaction_a, root_a);
}

void chratos::mdb_store::account_del (chratos::transaction const & transaction_a, chratos::account const & account_a)
{
	auto status1 (mdb_del (env.tx (transaction_a), accounts_v1, chratos::mdb_val (account_a), nullptr));
	if (status1 != 0)
	{
		release_assert (status1 == MDB_NOTFOUND);
		auto status2 (mdb_del (env.tx (transaction_a), accounts_v0, chratos::mdb_val (account_a), nullptr));
		release_assert (status2 == 0);
	}
}

bool chratos::mdb_store::account_exists (chratos::transaction const & transaction_a, chratos::account const & account_a)
{
	auto iterator (latest_begin (transaction_a, account_a));
	return iterator != latest_end () && chratos::account (iterator->first) == account_a;
}

bool chratos::mdb_store::account_get (chratos::transaction const & transaction_a, chratos::account const & account_a, chratos::account_info & info_a)
{
	chratos::mdb_val value;
	auto status1 (mdb_get (env.tx (transaction_a), accounts_v1, chratos::mdb_val (account_a), value));
	release_assert (status1 == 0 || status1 == MDB_NOTFOUND);
	bool result (false);
	chratos::epoch epoch;
	if (status1 == 0)
	{
		epoch = chratos::epoch::epoch_1;
	}
	else
	{
		auto status2 (mdb_get (env.tx (transaction_a), accounts_v0, chratos::mdb_val (account_a), value));
		release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
		if (status2 == 0)
		{
			epoch = chratos::epoch::epoch_0;
		}
		else
		{
			result = true;
		}
	}
	if (!result)
	{
		chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		info_a.epoch = epoch;
		info_a.deserialize (stream);
	}
	return result;
}

chratos::dividend_info chratos::mdb_store::dividend_get (chratos::transaction const & transaction_a)
{
	chratos::mdb_val value;
	const chratos::uint256_union zero (0);
	auto status1 (mdb_get (env.tx (transaction_a), dividends_ledger, chratos::mdb_val (zero), value));
	assert (status1 == 0 || status1 == MDB_NOTFOUND);

	chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
	chratos::dividend_info info_l;
	info_l.epoch = chratos::epoch::epoch_1;
	info_l.deserialize (stream);

	return info_l;
}

void chratos::mdb_store::frontier_put (chratos::transaction const & transaction_a, chratos::block_hash const & block_a, chratos::account const & account_a)
{
	auto status (mdb_put (env.tx (transaction_a), frontiers, chratos::mdb_val (block_a), chratos::mdb_val (account_a), 0));
	release_assert (status == 0);
}

chratos::account chratos::mdb_store::frontier_get (chratos::transaction const & transaction_a, chratos::block_hash const & block_a)
{
	chratos::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), frontiers, chratos::mdb_val (block_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	chratos::account result (0);
	if (status == 0)
	{
		result = chratos::uint256_union (value);
	}
	return result;
}

void chratos::mdb_store::frontier_del (chratos::transaction const & transaction_a, chratos::block_hash const & block_a)
{
	auto status (mdb_del (env.tx (transaction_a), frontiers, chratos::mdb_val (block_a), nullptr));
	release_assert (status == 0);
}

size_t chratos::mdb_store::account_count (chratos::transaction const & transaction_a)
{
	MDB_stat stats1;
	auto status1 (mdb_stat (env.tx (transaction_a), accounts_v0, &stats1));
	release_assert (status1 == 0);
	MDB_stat stats2;
	auto status2 (mdb_stat (env.tx (transaction_a), accounts_v1, &stats2));
	release_assert (status2 == 0);
	auto result (stats1.ms_entries + stats2.ms_entries);
	return result;
}

void chratos::mdb_store::account_put (chratos::transaction const & transaction_a, chratos::account const & account_a, chratos::account_info const & info_a)
{
	MDB_dbi db;
	switch (info_a.epoch)
	{
		case chratos::epoch::invalid:
		case chratos::epoch::unspecified:
			assert (false);
		case chratos::epoch::epoch_0:
			db = accounts_v0;
			break;
		case chratos::epoch::epoch_1:
			db = accounts_v1;
			break;
	}
	auto status (mdb_put (env.tx (transaction_a), db, chratos::mdb_val (account_a), chratos::mdb_val (info_a), 0));
	release_assert (status == 0);
}

void chratos::mdb_store::dividend_put (chratos::transaction const & transaction_a, chratos::dividend_info const & info_a)
{
	MDB_dbi db = dividends_ledger;
	const chratos::uint256_union zero (0);
	auto status (mdb_put (env.tx (transaction_a), db, chratos::mdb_val (zero), chratos::mdb_val (info_a), 0));
	assert (status == 0);
}

void chratos::mdb_store::pending_put (chratos::transaction const & transaction_a, chratos::pending_key const & key_a, chratos::pending_info const & pending_a)
{
	MDB_dbi db;
	switch (pending_a.epoch)
	{
		case chratos::epoch::invalid:
		case chratos::epoch::unspecified:
			assert (false);
		case chratos::epoch::epoch_0:
			db = pending_v0;
			break;
		case chratos::epoch::epoch_1:
			db = pending_v1;
			break;
	}
	auto status (mdb_put (env.tx (transaction_a), db, chratos::mdb_val (key_a), chratos::mdb_val (pending_a), 0));
	release_assert (status == 0);
}

void chratos::mdb_store::pending_del (chratos::transaction const & transaction_a, chratos::pending_key const & key_a)
{
	auto status1 (mdb_del (env.tx (transaction_a), pending_v1, mdb_val (key_a), nullptr));
	if (status1 != 0)
	{
		release_assert (status1 == MDB_NOTFOUND);
		auto status2 (mdb_del (env.tx (transaction_a), pending_v0, mdb_val (key_a), nullptr));
		release_assert (status2 == 0);
	}
}

bool chratos::mdb_store::pending_exists (chratos::transaction const & transaction_a, chratos::pending_key const & key_a)
{
	auto iterator (pending_begin (transaction_a, key_a));
	return iterator != pending_end () && chratos::pending_key (iterator->first) == key_a;
}

bool chratos::mdb_store::pending_get (chratos::transaction const & transaction_a, chratos::pending_key const & key_a, chratos::pending_info & pending_a)
{
	chratos::mdb_val value;
	auto status1 (mdb_get (env.tx (transaction_a), pending_v1, mdb_val (key_a), value));
	release_assert (status1 == 0 || status1 == MDB_NOTFOUND);
	bool result (false);
	chratos::epoch epoch;
	if (status1 == 0)
	{
		epoch = chratos::epoch::epoch_1;
	}
	else
	{
		auto status2 (mdb_get (env.tx (transaction_a), pending_v0, mdb_val (key_a), value));
		release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
		if (status2 == 0)
		{
			epoch = chratos::epoch::epoch_0;
		}
		else
		{
			result = true;
		}
	}
	if (!result)
	{
		chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		pending_a.epoch = epoch;
		pending_a.deserialize (stream);
	}
	return result;
}

chratos::store_iterator<chratos::pending_key, chratos::pending_info> chratos::mdb_store::pending_begin (chratos::transaction const & transaction_a, chratos::pending_key const & key_a)
{
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> result (std::make_unique<chratos::mdb_merge_iterator<chratos::pending_key, chratos::pending_info>> (transaction_a, pending_v0, pending_v1, mdb_val (key_a)));
	return result;
}

chratos::store_iterator<chratos::pending_key, chratos::pending_info> chratos::mdb_store::pending_begin (chratos::transaction const & transaction_a)
{
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> result (std::make_unique<chratos::mdb_merge_iterator<chratos::pending_key, chratos::pending_info>> (transaction_a, pending_v0, pending_v1));
	return result;
}

chratos::store_iterator<chratos::pending_key, chratos::pending_info> chratos::mdb_store::pending_end ()
{
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> result (nullptr);
	return result;
}

chratos::store_iterator<chratos::pending_key, chratos::pending_info> chratos::mdb_store::pending_v0_begin (chratos::transaction const & transaction_a, chratos::pending_key const & key_a)
{
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> result (std::make_unique<chratos::mdb_iterator<chratos::pending_key, chratos::pending_info>> (transaction_a, pending_v0, mdb_val (key_a)));
	return result;
}

chratos::store_iterator<chratos::pending_key, chratos::pending_info> chratos::mdb_store::pending_v0_begin (chratos::transaction const & transaction_a)
{
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> result (std::make_unique<chratos::mdb_iterator<chratos::pending_key, chratos::pending_info>> (transaction_a, pending_v0));
	return result;
}

chratos::store_iterator<chratos::pending_key, chratos::pending_info> chratos::mdb_store::pending_v0_end ()
{
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> result (nullptr);
	return result;
}

chratos::store_iterator<chratos::pending_key, chratos::pending_info> chratos::mdb_store::pending_v1_begin (chratos::transaction const & transaction_a, chratos::pending_key const & key_a)
{
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> result (std::make_unique<chratos::mdb_iterator<chratos::pending_key, chratos::pending_info>> (transaction_a, pending_v1, mdb_val (key_a)));
	return result;
}

chratos::store_iterator<chratos::pending_key, chratos::pending_info> chratos::mdb_store::pending_v1_begin (chratos::transaction const & transaction_a)
{
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> result (std::make_unique<chratos::mdb_iterator<chratos::pending_key, chratos::pending_info>> (transaction_a, pending_v1));
	return result;
}

chratos::store_iterator<chratos::pending_key, chratos::pending_info> chratos::mdb_store::pending_v1_end ()
{
	chratos::store_iterator<chratos::pending_key, chratos::pending_info> result (nullptr);
	return result;
}

void chratos::mdb_store::block_info_put (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a, chratos::block_info const & block_info_a)
{
	auto status (mdb_put (env.tx (transaction_a), blocks_info, chratos::mdb_val (hash_a), chratos::mdb_val (block_info_a), 0));
	release_assert (status == 0);
}

void chratos::mdb_store::block_info_del (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a)
{
	auto status (mdb_del (env.tx (transaction_a), blocks_info, chratos::mdb_val (hash_a), nullptr));
	release_assert (status == 0);
}

bool chratos::mdb_store::block_info_exists (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a)
{
	auto iterator (block_info_begin (transaction_a, hash_a));
	return iterator != block_info_end () && chratos::block_hash (iterator->first) == hash_a;
}

bool chratos::mdb_store::block_info_get (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a, chratos::block_info & block_info_a)
{
	chratos::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), blocks_info, chratos::mdb_val (hash_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	bool result (true);
	if (status != MDB_NOTFOUND)
	{
		result = false;
		assert (value.size () == sizeof (block_info_a.account.bytes) + sizeof (block_info_a.balance.bytes));
		chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error1 (chratos::read (stream, block_info_a.account));
		assert (!error1);
		auto error2 (chratos::read (stream, block_info_a.balance));
		assert (!error2);
	}
	return result;
}

chratos::uint128_t chratos::mdb_store::representation_get (chratos::transaction const & transaction_a, chratos::account const & account_a)
{
	chratos::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), representation, chratos::mdb_val (account_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	chratos::uint128_t result = 0;
	if (status == 0)
	{
		chratos::uint128_union rep;
		chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error (chratos::read (stream, rep));
		assert (!error);
		result = rep.number ();
	}
	return result;
}

void chratos::mdb_store::representation_put (chratos::transaction const & transaction_a, chratos::account const & account_a, chratos::uint128_t const & representation_a)
{
	chratos::uint128_union rep (representation_a);
	auto status (mdb_put (env.tx (transaction_a), representation, chratos::mdb_val (account_a), chratos::mdb_val (rep), 0));
	release_assert (status == 0);
}

void chratos::mdb_store::unchecked_clear (chratos::transaction const & transaction_a)
{
	auto status (mdb_drop (env.tx (transaction_a), unchecked, 0));
	release_assert (status == 0);
}

void chratos::mdb_store::unchecked_put (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a, std::shared_ptr<chratos::block> const & block_a)
{
	// Checking if same unchecked block is already in database
	bool exists (false);
	auto block_hash (block_a->hash ());
	auto cached (unchecked_get (transaction_a, hash_a));
	for (auto i (cached.begin ()), n (cached.end ()); i != n && !exists; ++i)
	{
		if ((*i)->hash () == block_hash)
		{
			exists = true;
		}
	}
	// Inserting block if it wasn't found in database
	if (!exists)
	{
		mdb_val block (block_a);
		auto status (mdb_put (env.tx (transaction_a), unchecked, chratos::mdb_val (hash_a), block, 0));
		release_assert (status == 0);
	}
}

std::shared_ptr<chratos::vote> chratos::mdb_store::vote_get (chratos::transaction const & transaction_a, chratos::account const & account_a)
{
	std::shared_ptr<chratos::vote> result;
	chratos::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), vote, chratos::mdb_val (account_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	if (status == 0)
	{
		std::shared_ptr<chratos::vote> result (value);
		assert (result != nullptr);
		return result;
	}
	return nullptr;
}

std::vector<std::shared_ptr<chratos::block>> chratos::mdb_store::unchecked_get (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a)
{
	std::vector<std::shared_ptr<chratos::block>> result;
	for (auto i (unchecked_begin (transaction_a, hash_a)), n (unchecked_end ()); i != n && chratos::block_hash (i->first) == hash_a; ++i)
	{
		std::shared_ptr<chratos::block> block (i->second);
		result.push_back (block);
	}
	return result;
}

void chratos::mdb_store::unchecked_del (chratos::transaction const & transaction_a, chratos::block_hash const & hash_a, std::shared_ptr<chratos::block> block_a)
{
	chratos::mdb_val block (block_a);
	auto status (mdb_del (env.tx (transaction_a), unchecked, chratos::mdb_val (hash_a), block));
	release_assert (status == 0 || status == MDB_NOTFOUND);
}

size_t chratos::mdb_store::unchecked_count (chratos::transaction const & transaction_a)
{
	MDB_stat unchecked_stats;
	auto status (mdb_stat (env.tx (transaction_a), unchecked, &unchecked_stats));
	release_assert (status == 0);
	auto result (unchecked_stats.ms_entries);
	return result;
}

void chratos::mdb_store::checksum_put (chratos::transaction const & transaction_a, uint64_t prefix, uint8_t mask, chratos::uint256_union const & hash_a)
{
	assert ((prefix & 0xff) == 0);
	uint64_t key (prefix | mask);
	auto status (mdb_put (env.tx (transaction_a), checksum, chratos::mdb_val (sizeof (key), &key), chratos::mdb_val (hash_a), 0));
	release_assert (status == 0);
}

bool chratos::mdb_store::checksum_get (chratos::transaction const & transaction_a, uint64_t prefix, uint8_t mask, chratos::uint256_union & hash_a)
{
	assert ((prefix & 0xff) == 0);
	uint64_t key (prefix | mask);
	chratos::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), checksum, chratos::mdb_val (sizeof (key), &key), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	bool result (true);
	if (status == 0)
	{
		result = false;
		chratos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error (chratos::read (stream, hash_a));
		assert (!error);
	}
	return result;
}

void chratos::mdb_store::checksum_del (chratos::transaction const & transaction_a, uint64_t prefix, uint8_t mask)
{
	assert ((prefix & 0xff) == 0);
	uint64_t key (prefix | mask);
	auto status (mdb_del (env.tx (transaction_a), checksum, chratos::mdb_val (sizeof (key), &key), nullptr));
	release_assert (status == 0);
}

void chratos::mdb_store::flush (chratos::transaction const & transaction_a)
{
	std::unordered_map<chratos::account, std::shared_ptr<chratos::vote>> sequence_cache_l;
	{
		std::lock_guard<std::mutex> lock (cache_mutex);
		sequence_cache_l.swap (vote_cache);
	}
	for (auto i (sequence_cache_l.begin ()), n (sequence_cache_l.end ()); i != n; ++i)
	{
		std::vector<uint8_t> vector;
		{
			chratos::vectorstream stream (vector);
			i->second->serialize (stream);
		}
		auto status1 (mdb_put (env.tx (transaction_a), vote, chratos::mdb_val (i->first), chratos::mdb_val (vector.size (), vector.data ()), 0));
		release_assert (status1 == 0);
	}
}
std::shared_ptr<chratos::vote> chratos::mdb_store::vote_current (chratos::transaction const & transaction_a, chratos::account const & account_a)
{
	assert (!cache_mutex.try_lock ());
	std::shared_ptr<chratos::vote> result;
	auto existing (vote_cache.find (account_a));
	if (existing != vote_cache.end ())
	{
		result = existing->second;
	}
	else
	{
		result = vote_get (transaction_a, account_a);
	}
	return result;
}

std::shared_ptr<chratos::vote> chratos::mdb_store::vote_generate (chratos::transaction const & transaction_a, chratos::account const & account_a, chratos::raw_key const & key_a, std::shared_ptr<chratos::block> block_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto result (vote_current (transaction_a, account_a));
	uint64_t sequence ((result ? result->sequence : 0) + 1);
	result = std::make_shared<chratos::vote> (account_a, key_a, sequence, block_a);
	vote_cache[account_a] = result;
	return result;
}

std::shared_ptr<chratos::vote> chratos::mdb_store::vote_generate (chratos::transaction const & transaction_a, chratos::account const & account_a, chratos::raw_key const & key_a, std::vector<chratos::block_hash> blocks_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto result (vote_current (transaction_a, account_a));
	uint64_t sequence ((result ? result->sequence : 0) + 1);
	result = std::make_shared<chratos::vote> (account_a, key_a, sequence, blocks_a);
	vote_cache[account_a] = result;
	return result;
}

std::shared_ptr<chratos::vote> chratos::mdb_store::vote_max (chratos::transaction const & transaction_a, std::shared_ptr<chratos::vote> vote_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto current (vote_current (transaction_a, vote_a->account));
	auto result (vote_a);
	if (current != nullptr && current->sequence > result->sequence)
	{
		result = current;
	}
	vote_cache[vote_a->account] = result;
	return result;
}

chratos::store_iterator<chratos::account, chratos::account_info> chratos::mdb_store::latest_begin (chratos::transaction const & transaction_a, chratos::account const & account_a)
{
	chratos::store_iterator<chratos::account, chratos::account_info> result (std::make_unique<chratos::mdb_merge_iterator<chratos::account, chratos::account_info>> (transaction_a, accounts_v0, accounts_v1, chratos::mdb_val (account_a)));
	return result;
}

chratos::store_iterator<chratos::account, chratos::account_info> chratos::mdb_store::latest_begin (chratos::transaction const & transaction_a)
{
	chratos::store_iterator<chratos::account, chratos::account_info> result (std::make_unique<chratos::mdb_merge_iterator<chratos::account, chratos::account_info>> (transaction_a, accounts_v0, accounts_v1));
	return result;
}

chratos::store_iterator<chratos::account, chratos::account_info> chratos::mdb_store::latest_end ()
{
	chratos::store_iterator<chratos::account, chratos::account_info> result (nullptr);
	return result;
}

chratos::store_iterator<chratos::account, chratos::account_info> chratos::mdb_store::latest_v0_begin (chratos::transaction const & transaction_a, chratos::account const & account_a)
{
	chratos::store_iterator<chratos::account, chratos::account_info> result (std::make_unique<chratos::mdb_iterator<chratos::account, chratos::account_info>> (transaction_a, accounts_v0, chratos::mdb_val (account_a)));
	return result;
}

chratos::store_iterator<chratos::account, chratos::account_info> chratos::mdb_store::latest_v0_begin (chratos::transaction const & transaction_a)
{
	chratos::store_iterator<chratos::account, chratos::account_info> result (std::make_unique<chratos::mdb_iterator<chratos::account, chratos::account_info>> (transaction_a, accounts_v0));
	return result;
}

chratos::store_iterator<chratos::account, chratos::account_info> chratos::mdb_store::latest_v0_end ()
{
	chratos::store_iterator<chratos::account, chratos::account_info> result (nullptr);
	return result;
}

chratos::store_iterator<chratos::account, chratos::account_info> chratos::mdb_store::latest_v1_begin (chratos::transaction const & transaction_a, chratos::account const & account_a)
{
	chratos::store_iterator<chratos::account, chratos::account_info> result (std::make_unique<chratos::mdb_iterator<chratos::account, chratos::account_info>> (transaction_a, accounts_v1, chratos::mdb_val (account_a)));
	return result;
}

chratos::store_iterator<chratos::account, chratos::account_info> chratos::mdb_store::latest_v1_begin (chratos::transaction const & transaction_a)
{
	chratos::store_iterator<chratos::account, chratos::account_info> result (std::make_unique<chratos::mdb_iterator<chratos::account, chratos::account_info>> (transaction_a, accounts_v1));
	return result;
}

chratos::store_iterator<chratos::account, chratos::account_info> chratos::mdb_store::latest_v1_end ()
{
	chratos::store_iterator<chratos::account, chratos::account_info> result (nullptr);
	return result;
}
