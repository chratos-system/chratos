#pragma once

#include <boost/filesystem.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

#include <chratos/lib/numbers.hpp>
#include <chratos/secure/common.hpp>

namespace chratos
{
/**
 * RAII wrapper for MDB_env
 */
class mdb_env
{
public:
  mdb_env (bool &, boost::filesystem::path const &, int max_dbs = 128);
  ~mdb_env ();
  operator MDB_env * () const;
  MDB_env * environment;
};

/**
 * Encapsulates MDB_val and provides uint256_union conversion of the data.
 */
class mdb_val
{
public:
  enum class no_value
  {
    dummy
  };
  mdb_val (chratos::epoch = chratos::epoch::unspecified);
  mdb_val (chratos::account_info const &);
  mdb_val (chratos::dividend_info const &);
  mdb_val (chratos::block_info const &);
  mdb_val (MDB_val const &, chratos::epoch = chratos::epoch::unspecified);
  mdb_val (chratos::pending_info const &);
  mdb_val (chratos::pending_key const &);
  mdb_val (size_t, void *);
  mdb_val (chratos::uint128_union const &);
  mdb_val (chratos::uint256_union const &);
  mdb_val (std::shared_ptr<chratos::block> const &);
  mdb_val (std::shared_ptr<chratos::vote> const &);
  void * data () const;
  size_t size () const;
  explicit operator chratos::account_info () const;
  explicit operator chratos::dividend_info () const;
  explicit operator chratos::block_info () const;
  explicit operator chratos::pending_info () const;
  explicit operator chratos::pending_key () const;
  explicit operator chratos::uint128_union () const;
  explicit operator chratos::uint256_union () const;
  explicit operator std::array<char, 64> () const;
  explicit operator no_value () const;
  explicit operator std::shared_ptr<chratos::block> () const;
  explicit operator std::shared_ptr<chratos::send_block> () const;
  explicit operator std::shared_ptr<chratos::receive_block> () const;
  explicit operator std::shared_ptr<chratos::open_block> () const;
  explicit operator std::shared_ptr<chratos::change_block> () const;
  explicit operator std::shared_ptr<chratos::state_block> () const;
  explicit operator std::shared_ptr<chratos::vote> () const;
  explicit operator uint64_t () const;
  operator MDB_val * () const;
  operator MDB_val const & () const;
  MDB_val value;
  std::shared_ptr<std::vector<uint8_t>> buffer;
  chratos::epoch epoch;
};

/**
 * RAII wrapper of MDB_txn where the constructor starts the transaction
 * and the destructor commits it.
 */
class transaction
{
public:
  transaction (chratos::mdb_env &, MDB_txn *, bool);
  ~transaction ();
  operator MDB_txn * () const;
  MDB_txn * handle;
  chratos::mdb_env & environment;
};
class block_store;
/**
 * Determine the balance as of this block
 */
class balance_visitor : public chratos::block_visitor
{
public:
  balance_visitor (MDB_txn *, chratos::block_store &);
  virtual ~balance_visitor () = default;
  void compute (chratos::block_hash const &);
  void send_block (chratos::send_block const &) override;
  void receive_block (chratos::receive_block const &) override;
  void open_block (chratos::open_block const &) override;
  void change_block (chratos::change_block const &) override;
  void state_block (chratos::state_block const &) override;
  MDB_txn * transaction;
  chratos::block_store & store;
  chratos::block_hash current_balance;
  chratos::block_hash current_amount;
  chratos::uint128_t balance;
};

/**
 * Determine the amount delta resultant from this block
 */
class amount_visitor : public chratos::block_visitor
{
public:
  amount_visitor (MDB_txn *, chratos::block_store &);
  virtual ~amount_visitor () = default;
  void compute (chratos::block_hash const &);
  void send_block (chratos::send_block const &) override;
  void receive_block (chratos::receive_block const &) override;
  void open_block (chratos::open_block const &) override;
  void change_block (chratos::change_block const &) override;
  void state_block (chratos::state_block const &) override;
  void from_send (chratos::block_hash const &);
  MDB_txn * transaction;
  chratos::block_store & store;
  chratos::block_hash current_amount;
  chratos::block_hash current_balance;
  chratos::uint128_t amount;
};

/**
 * Determine the representative for this block
 */
class representative_visitor : public chratos::block_visitor
{
public:
  representative_visitor (MDB_txn * transaction_a, chratos::block_store & store_a);
  virtual ~representative_visitor () = default;
  void compute (chratos::block_hash const & hash_a);
  void send_block (chratos::send_block const & block_a) override;
  void receive_block (chratos::receive_block const & block_a) override;
  void open_block (chratos::open_block const & block_a) override;
  void change_block (chratos::change_block const & block_a) override;
  void state_block (chratos::state_block const & block_a) override;
  MDB_txn * transaction;
  chratos::block_store & store;
  chratos::block_hash current;
  chratos::block_hash result;
};
}
