#include <chratos/node/common.hpp>
#include <chratos/node/stats.hpp>
#include <chratos/secure/blockstore.hpp>
#include <chratos/secure/ledger.hpp>
#include <boost/multiprecision/cpp_bin_float.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>

namespace
{
/**
 * Roll back the visited block
 */
class rollback_visitor : public chratos::block_visitor
{
public:
  rollback_visitor (MDB_txn * transaction_a, chratos::ledger & ledger_a) :
  transaction (transaction_a),
  ledger (ledger_a)
  {
  }
  virtual ~rollback_visitor () = default;
  void send_block (chratos::send_block const & block_a) override
  {
    auto hash (block_a.hash ());
    chratos::pending_info pending;
    chratos::pending_key key (block_a.hashables.destination, hash);
    while (ledger.store.pending_get (transaction, key, pending))
    {
      ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.destination));
    }
    chratos::account_info info;
    auto error (ledger.store.account_get (transaction, pending.source, info));
    assert (!error);
    ledger.store.pending_del (transaction, key);
    ledger.store.representation_add (transaction, ledger.representative (transaction, hash), pending.amount.number ());
    ledger.change_latest (transaction, pending.source, block_a.hashables.previous, info.rep_block, block_a.hashables.dividend, ledger.balance (transaction, block_a.hashables.previous), info.block_count - 1);
    ledger.store.block_del (transaction, hash);
    ledger.store.frontier_del (transaction, hash);
    ledger.store.frontier_put (transaction, block_a.hashables.previous, pending.source);
    ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
    if (!(info.block_count % ledger.store.block_info_max))
    {
      ledger.store.block_info_del (transaction, hash);
    }
    ledger.stats.inc (chratos::stat::type::rollback, chratos::stat::detail::send);
  }
  void receive_block (chratos::receive_block const & block_a) override
  {
    auto hash (block_a.hash ());
    auto representative (ledger.representative (transaction, block_a.hashables.previous));
    auto amount (ledger.amount (transaction, block_a.hashables.source));
    auto destination_account (ledger.account (transaction, hash));
    auto source_account (ledger.account (transaction, block_a.hashables.source));
    chratos::account_info info;
    auto error (ledger.store.account_get (transaction, destination_account, info));
    assert (!error);
    ledger.store.representation_add (transaction, ledger.representative (transaction, hash), 0 - amount);
    ledger.change_latest (transaction, destination_account, block_a.hashables.previous, representative, block_a.hashables.dividend, ledger.balance (transaction, block_a.hashables.previous), info.block_count - 1);
    ledger.store.block_del (transaction, hash);
    ledger.store.pending_put (transaction, chratos::pending_key (destination_account, block_a.hashables.source), { source_account, amount, block_a.hashables.dividend, chratos::epoch::epoch_0 });
    ledger.store.frontier_del (transaction, hash);
    ledger.store.frontier_put (transaction, block_a.hashables.previous, destination_account);
    ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
    if (!(info.block_count % ledger.store.block_info_max))
    {
      ledger.store.block_info_del (transaction, hash);
    }
    ledger.stats.inc (chratos::stat::type::rollback, chratos::stat::detail::receive);
  }
  void open_block (chratos::open_block const & block_a) override
  {
    auto hash (block_a.hash ());
    auto amount (ledger.amount (transaction, block_a.hashables.source));
    auto destination_account (ledger.account (transaction, hash));
    auto source_account (ledger.account (transaction, block_a.hashables.source));
    ledger.store.representation_add (transaction, ledger.representative (transaction, hash), 0 - amount);
    ledger.change_latest (transaction, destination_account, 0, 0, block_a.hashables.dividend, 0, 0);
    ledger.store.block_del (transaction, hash);
    ledger.store.pending_put (transaction, chratos::pending_key (destination_account, block_a.hashables.source), { source_account, amount, block_a.hashables.dividend, chratos::epoch::epoch_0 });
    ledger.store.frontier_del (transaction, hash);
    ledger.stats.inc (chratos::stat::type::rollback, chratos::stat::detail::open);
  }
  void change_block (chratos::change_block const & block_a) override
  {
    auto hash (block_a.hash ());
    auto representative (ledger.representative (transaction, block_a.hashables.previous));
    auto account (ledger.account (transaction, block_a.hashables.previous));
    chratos::account_info info;
    auto error (ledger.store.account_get (transaction, account, info));
    assert (!error);
    auto balance (ledger.balance (transaction, block_a.hashables.previous));
    ledger.store.representation_add (transaction, representative, balance);
    ledger.store.representation_add (transaction, hash, 0 - balance);
    ledger.store.block_del (transaction, hash);
    ledger.change_latest (transaction, account, block_a.hashables.previous, representative, block_a.hashables.dividend, info.balance, info.block_count - 1);
    ledger.store.frontier_del (transaction, hash);
    ledger.store.frontier_put (transaction, block_a.hashables.previous, account);
    ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
    if (!(info.block_count % ledger.store.block_info_max))
    {
      ledger.store.block_info_del (transaction, hash);
    }
    ledger.stats.inc (chratos::stat::type::rollback, chratos::stat::detail::change);
  }
  void state_block (chratos::state_block const & block_a) override
  {
    auto hash (block_a.hash ());
    chratos::block_hash representative (0);
    if (!block_a.hashables.previous.is_zero ())
    {
      representative = ledger.representative (transaction, block_a.hashables.previous);
    }
    auto balance (ledger.balance (transaction, block_a.hashables.previous));
    auto is_send (block_a.hashables.balance < balance);
    // Add in amount delta
    ledger.store.representation_add (transaction, hash, 0 - block_a.hashables.balance.number ());
    if (!representative.is_zero ())
    {
      // Move existing representation
      ledger.store.representation_add (transaction, representative, balance);
    }

    chratos::account_info info;
    auto error (ledger.store.account_get (transaction, block_a.hashables.account, info));

    if (is_send)
    {
      chratos::pending_key key (block_a.hashables.link, hash);
      while (!ledger.store.pending_exists (transaction, key))
      {
        ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.link));
      }
      ledger.store.pending_del (transaction, key);
      ledger.stats.inc (chratos::stat::type::rollback, chratos::stat::detail::send);
    }
    else if (!block_a.hashables.link.is_zero () && block_a.hashables.link != ledger.epoch_link)
    {
      auto source_version (ledger.store.block_version (transaction, block_a.hashables.link));
      chratos::pending_info pending_info (ledger.account (transaction, block_a.hashables.link), block_a.hashables.balance.number () - balance, block_a.hashables.dividend, source_version);
      ledger.store.pending_put (transaction, chratos::pending_key (block_a.hashables.account, block_a.hashables.link), pending_info);
      ledger.stats.inc (chratos::stat::type::rollback, chratos::stat::detail::receive);
    }

    assert (!error);
    auto previous_version (ledger.store.block_version (transaction, block_a.hashables.previous));
    ledger.change_latest (transaction, block_a.hashables.account, block_a.hashables.previous, representative, block_a.hashables.dividend, balance, info.block_count - 1, false, previous_version);

    auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
    if (previous != nullptr)
    {
      ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
      if (previous->type () < chratos::block_type::state)
      {
        ledger.store.frontier_put (transaction, block_a.hashables.previous, block_a.hashables.account);
      }
    }
    else
    {
      ledger.stats.inc (chratos::stat::type::rollback, chratos::stat::detail::open);
    }
    ledger.store.block_del (transaction, hash);
  } 
  void dividend_block (chratos::dividend_block const & block_a) override
  {
    auto hash (block_a.hash ());
  }
  void claim_block (chratos::claim_block const & block_a) override
  {
    auto hash (block_a.hash ());
  }

  MDB_txn * transaction;
  chratos::ledger & ledger;
};

class ledger_processor : public chratos::block_visitor
{
public:
  ledger_processor (chratos::ledger &, MDB_txn *);
  virtual ~ledger_processor () = default;
  void send_block (chratos::send_block const &) override;
  void receive_block (chratos::receive_block const &) override;
  void open_block (chratos::open_block const &) override;
  void change_block (chratos::change_block const &) override;
  void state_block (chratos::state_block const &) override;
  void dividend_block (chratos::dividend_block const &) override;
  void claim_block (chratos::claim_block const &) override;
  void state_block_impl (chratos::state_block const &);
  void epoch_block_impl (chratos::state_block const &);
  chratos::ledger & ledger;
  MDB_txn * transaction;
  chratos::process_return result;
};

void ledger_processor::state_block (chratos::state_block const & block_a)
{
  result.code = chratos::process_result::progress;
  // Check if this is an epoch block
  chratos::amount prev_balance (0);
  if (!block_a.hashables.previous.is_zero ())
  {
    result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? chratos::process_result::progress : chratos::process_result::gap_previous;
    if (result.code == chratos::process_result::progress)
    {
      prev_balance = ledger.balance (transaction, block_a.hashables.previous);
    }
  }
  if (result.code == chratos::process_result::progress)
  {
    if (block_a.hashables.balance == prev_balance && !ledger.epoch_link.is_zero () && block_a.hashables.link == ledger.epoch_link)
    {
      epoch_block_impl (block_a);
    }
    else
    {
      state_block_impl (block_a);
    }
  }
}

void ledger_processor::state_block_impl (chratos::state_block const & block_a)
{
  auto hash (block_a.hash ());
  auto existing (ledger.store.block_exists (transaction, hash));
  result.code = existing ? chratos::process_result::old : chratos::process_result::progress; // Have we seen this block before? (Unambiguous)
  if (result.code == chratos::process_result::progress)
  {
    result.code = validate_message (block_a.hashables.account, hash, block_a.signature) ? chratos::process_result::bad_signature : chratos::process_result::progress; // Is this block signed correctly (Unambiguous)
    if (result.code == chratos::process_result::progress)
    {
      result.code = block_a.hashables.account.is_zero () ? chratos::process_result::opened_burn_account : chratos::process_result::progress; // Is this for the burn account? (Unambiguous)
      if (result.code == chratos::process_result::progress)
      {
        chratos::epoch epoch (chratos::epoch::epoch_0);
        chratos::account_info info;
        result.amount = block_a.hashables.balance;
        auto is_send (false);
        auto account_error (ledger.store.account_get (transaction, block_a.hashables.account, info));
        if (!account_error)
        {
          epoch = info.epoch;
          result.code = block_a.hashables.previous.is_zero () ? chratos::process_result::fork : chratos::process_result::progress; // Has this account already been opened? (Ambigious)
          if (result.code == chratos::process_result::progress)
          {
            // Account already exists
            result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? chratos::process_result::progress : chratos::process_result::gap_previous; // Does the previous block exist in the ledger? (Unambigious)
            if (result.code == chratos::process_result::progress)
            {
              is_send = block_a.hashables.balance < info.balance;

              result.amount = is_send ? (info.balance.number () - result.amount.number ()) : (result.amount.number () - info.balance.number ());

              result.code = block_a.hashables.previous == info.head ? chratos::process_result::progress : chratos::process_result::fork; // Is the previous block the account's head block? (Ambigious)

            }
          }
        }
        else
        {
          // Account does not yet exists
          result.code = block_a.previous ().is_zero () ? chratos::process_result::progress : chratos::process_result::gap_previous; // Does the first block in an account yield 0 for previous() ? (Unambigious)
          if (result.code == chratos::process_result::progress)
          {
            result.code = !block_a.hashables.link.is_zero () ? chratos::process_result::progress : chratos::process_result::gap_source; // Is the first block receiving from a send ? (Unambigious)
          }
        }
        if (result.code == chratos::process_result::progress)
        {
          if (!is_send)
          {
            if (!block_a.hashables.link.is_zero ())
            {
              result.code = ledger.store.block_exists (transaction, block_a.hashables.link) ? chratos::process_result::progress : chratos::process_result::gap_source; // Have we seen the source block already? (Harmless)

              if (result.code == chratos::process_result::progress)
              {
                // Make sure the dividend is ordered with the most recent
                if (info.head != chratos::dividend_base) 
                {
                  result.code = ledger.dividends_are_ordered (transaction, block_a.hashables.dividend, info.dividend_block) ? chratos::process_result::progress : chratos::process_result::unreceivable;
                }
                if (result.code == chratos::process_result::progress)
                {
                  chratos::pending_key key (block_a.hashables.account, block_a.hashables.link);
                  chratos::pending_info pending;
                  result.code = ledger.store.pending_get (transaction, key, pending) ? chratos::process_result::unreceivable : chratos::process_result::progress; // Has this source already been received (Malformed)
                  if (result.code == chratos::process_result::progress)
                  {
                    result.code = result.amount == pending.amount ? chratos::process_result::progress : chratos::process_result::balance_mismatch;
                    epoch = std::max (epoch, pending.epoch);
                  }
                }
              }
            }
            else
            {
              // If there's no link, the balance must remain the same, only the representative can change
              result.code = result.amount.is_zero () ? chratos::process_result::progress : chratos::process_result::balance_mismatch;
            }
          } 
          else if (is_send)
          {
            result.code = (info.dividend_block == block_a.hashables.dividend) ? chratos::process_result::progress : chratos::process_result::incorrect_dividend;
          }
        }
        if (result.code == chratos::process_result::progress)
        {
          ledger.stats.inc (chratos::stat::type::ledger, chratos::stat::detail::state_block);
          result.state_is_send = is_send;
          ledger.store.block_put (transaction, hash, block_a, 0, epoch);

          if (!info.rep_block.is_zero ())
          {
            // Move existing representation
            ledger.store.representation_add (transaction, info.rep_block, 0 - info.balance.number ());
          }
          // Add in amount delta
          ledger.store.representation_add (transaction, hash, block_a.hashables.balance.number ());

          if (is_send)
          {
            chratos::pending_key key (block_a.hashables.link, hash);
            chratos::pending_info info (block_a.hashables.account, result.amount.number (), block_a.hashables.dividend, epoch);
            ledger.store.pending_put (transaction, key, info);
          }
          else if (!block_a.hashables.link.is_zero ())
          {
            ledger.store.pending_del (transaction, chratos::pending_key (block_a.hashables.account, block_a.hashables.link));
          }

          ledger.change_latest (transaction, block_a.hashables.account, hash, hash, block_a.hashables.dividend, block_a.hashables.balance, info.block_count + 1, true, epoch);
          if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
          {
            ledger.store.frontier_del (transaction, info.head);
          }
          // Frontier table is unnecessary for state blocks and this also prevents old blocks from being inserted on top of state blocks
          result.account = block_a.hashables.account;
        }
      }
    }
  }
}

void ledger_processor::epoch_block_impl (chratos::state_block const & block_a)
{
  auto hash (block_a.hash ());
  auto existing (ledger.store.block_exists (transaction, hash));
  result.code = existing ? chratos::process_result::old : chratos::process_result::progress; // Have we seen this block before? (Unambiguous)
  if (result.code == chratos::process_result::progress)
  {
    result.code = validate_message (ledger.epoch_signer, hash, block_a.signature) ? chratos::process_result::bad_signature : chratos::process_result::progress; // Is this block signed correctly (Unambiguous)
    if (result.code == chratos::process_result::progress)
    {
      result.code = block_a.hashables.account.is_zero () ? chratos::process_result::opened_burn_account : chratos::process_result::progress; // Is this for the burn account? (Unambiguous)
      if (result.code == chratos::process_result::progress)
      {
        chratos::account_info info;
        auto account_error (ledger.store.account_get (transaction, block_a.hashables.account, info));
        if (!account_error)
        {
          // Account already exists
          result.code = block_a.hashables.previous.is_zero () ? chratos::process_result::fork : chratos::process_result::progress; // Has this account already been opened? (Ambigious)
          if (result.code == chratos::process_result::progress)
          {
            result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? chratos::process_result::progress : chratos::process_result::gap_previous; // Does the previous block exist in the ledger? (Unambigious)
            if (result.code == chratos::process_result::progress)
            {
              result.code = block_a.hashables.previous == info.head ? chratos::process_result::progress : chratos::process_result::fork; // Is the previous block the account's head block? (Ambigious)
              if (result.code == chratos::process_result::progress)
              {
                auto last_rep_block (ledger.store.block_get (transaction, info.rep_block));
                assert (last_rep_block != nullptr);
                result.code = block_a.hashables.representative == last_rep_block->representative () ? chratos::process_result::progress : chratos::process_result::representative_mismatch;
              }
            }
          }
        }
        else
        {
          result.code = block_a.hashables.representative.is_zero () ? chratos::process_result::progress : chratos::process_result::representative_mismatch;
        }
        if (result.code == chratos::process_result::progress)
        {
          result.code = info.epoch == chratos::epoch::epoch_0 ? chratos::process_result::progress : chratos::process_result::block_position;
          if (result.code == chratos::process_result::progress)
          {
            result.code = block_a.hashables.balance == info.balance ? chratos::process_result::progress : chratos::process_result::balance_mismatch;
            if (result.code == chratos::process_result::progress)
            {
              ledger.stats.inc (chratos::stat::type::ledger, chratos::stat::detail::epoch_block);
              result.account = block_a.hashables.account;
              result.amount = 0;
              ledger.store.block_put (transaction, hash, block_a, 0, chratos::epoch::epoch_1);
              ledger.change_latest (transaction, block_a.hashables.account, hash, hash, block_a.hashables.dividend, info.balance, info.block_count + 1, true, chratos::epoch::epoch_1);
              if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
              {
                ledger.store.frontier_del (transaction, info.head);
              }
            }
          }
        }
      }
    }
  }
}

void ledger_processor::change_block (chratos::change_block const & block_a)
{
  auto hash (block_a.hash ());
  auto existing (ledger.store.block_exists (transaction, hash));
  result.code = existing ? chratos::process_result::old : chratos::process_result::progress; // Have we seen this block before? (Harmless)
  if (result.code == chratos::process_result::progress)
  {
    auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
    result.code = previous != nullptr ? chratos::process_result::progress : chratos::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
    if (result.code == chratos::process_result::progress)
    {
      result.code = block_a.valid_predecessor (*previous) ? chratos::process_result::progress : chratos::process_result::block_position;
      if (result.code == chratos::process_result::progress)
      {
        auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
        result.code = account.is_zero () ? chratos::process_result::fork : chratos::process_result::progress;
        if (result.code == chratos::process_result::progress)
        {
          chratos::account_info info;
          auto latest_error (ledger.store.account_get (transaction, account, info));
          assert (!latest_error);
          assert (info.head == block_a.hashables.previous);
          result.code = validate_message (account, hash, block_a.signature) ? chratos::process_result::bad_signature : chratos::process_result::progress; // Is this block signed correctly (Malformed)
          if (result.code == chratos::process_result::progress)
          {
            ledger.store.block_put (transaction, hash, block_a);
            auto balance (ledger.balance (transaction, block_a.hashables.previous));
            ledger.store.representation_add (transaction, hash, balance);
            ledger.store.representation_add (transaction, info.rep_block, 0 - balance);
            ledger.change_latest (transaction, account, hash, hash, block_a.hashables.dividend, info.balance, info.block_count + 1);
            ledger.store.frontier_del (transaction, block_a.hashables.previous);
            ledger.store.frontier_put (transaction, hash, account);
            result.account = account;
            result.amount = 0;
            ledger.stats.inc (chratos::stat::type::ledger, chratos::stat::detail::change);
          }
        }
      }
    }
  }
}

void ledger_processor::send_block (chratos::send_block const & block_a)
{
  auto hash (block_a.hash ());
  auto existing (ledger.store.block_exists (transaction, hash));
  result.code = existing ? chratos::process_result::old : chratos::process_result::progress; // Have we seen this block before? (Harmless)
  if (result.code == chratos::process_result::progress)
  {
    auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
    result.code = previous != nullptr ? chratos::process_result::progress : chratos::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
    if (result.code == chratos::process_result::progress)
    {
      result.code = block_a.valid_predecessor (*previous) ? chratos::process_result::progress : chratos::process_result::block_position;
      if (result.code == chratos::process_result::progress)
      {
        auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
        result.code = account.is_zero () ? chratos::process_result::fork : chratos::process_result::progress;
        if (result.code == chratos::process_result::progress)
        {
          result.code = validate_message (account, hash, block_a.signature) ? chratos::process_result::bad_signature : chratos::process_result::progress; // Is this block signed correctly (Malformed)
          if (result.code == chratos::process_result::progress)
          {
            chratos::account_info info;
            auto latest_error (ledger.store.account_get (transaction, account, info));
            assert (!latest_error);
            assert (info.head == block_a.hashables.previous);
            result.code = info.balance.number () >= block_a.hashables.balance.number () ? chratos::process_result::progress : chratos::process_result::negative_spend; // Is this trying to spend a negative amount (Malicious)
            if (result.code == chratos::process_result::progress)
            {
              auto amount (info.balance.number () - block_a.hashables.balance.number ());
              ledger.store.representation_add (transaction, info.rep_block, 0 - amount);
              ledger.store.block_put (transaction, hash, block_a);
              ledger.change_latest (transaction, account, hash, info.rep_block, block_a.hashables.dividend, block_a.hashables.balance, info.block_count + 1);
              ledger.store.pending_put (transaction, chratos::pending_key (block_a.hashables.destination, hash), { account, amount, block_a.hashables.dividend, chratos::epoch::epoch_0 });
              ledger.store.frontier_del (transaction, block_a.hashables.previous);
              ledger.store.frontier_put (transaction, hash, account);
              result.account = account;
              result.amount = amount;
              result.pending_account = block_a.hashables.destination;
              ledger.stats.inc (chratos::stat::type::ledger, chratos::stat::detail::send);
            }
          }
        }
      }
    }
  }
}

void ledger_processor::receive_block (chratos::receive_block const & block_a)
{
  auto hash (block_a.hash ());
  auto existing (ledger.store.block_exists (transaction, hash));
  result.code = existing ? chratos::process_result::old : chratos::process_result::progress; // Have we seen this block already?  (Harmless)
  if (result.code == chratos::process_result::progress)
  {
    auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
    result.code = previous != nullptr ? chratos::process_result::progress : chratos::process_result::gap_previous;
    if (result.code == chratos::process_result::progress)
    {
      result.code = block_a.valid_predecessor (*previous) ? chratos::process_result::progress : chratos::process_result::block_position;
      if (result.code == chratos::process_result::progress)
      {
        result.code = ledger.store.block_exists (transaction, block_a.hashables.source) ? chratos::process_result::progress : chratos::process_result::gap_source; // Have we seen the source block already? (Harmless)
        if (result.code == chratos::process_result::progress)
        {
          auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
          result.code = account.is_zero () ? chratos::process_result::gap_previous : chratos::process_result::progress; //Have we seen the previous block? No entries for account at all (Harmless)
          if (result.code == chratos::process_result::progress)
          {
            result.code = chratos::validate_message (account, hash, block_a.signature) ? chratos::process_result::bad_signature : chratos::process_result::progress; // Is the signature valid (Malformed)
            if (result.code == chratos::process_result::progress)
            {
              chratos::account_info info;
              ledger.store.account_get (transaction, account, info);
              result.code = info.head == block_a.hashables.previous ? chratos::process_result::progress : chratos::process_result::gap_previous; // Block doesn't immediately follow latest block (Harmless)
              if (result.code == chratos::process_result::progress)
              {
                chratos::pending_key key (account, block_a.hashables.source);
                chratos::pending_info pending;
                result.code = ledger.store.pending_get (transaction, key, pending) ? chratos::process_result::unreceivable : chratos::process_result::progress; // Has this source already been received (Malformed)
                if (result.code == chratos::process_result::progress)
                {
                  result.code = pending.epoch == chratos::epoch::epoch_0 ? chratos::process_result::progress : chratos::process_result::unreceivable; // Are we receiving a state-only send? (Malformed)
                  if (result.code == chratos::process_result::progress)
                  {
                    auto new_balance (info.balance.number () + pending.amount.number ());
                    chratos::account_info source_info;
                    auto error (ledger.store.account_get (transaction, pending.source, source_info));
                    assert (!error);
                    ledger.store.pending_del (transaction, key);
                    ledger.store.block_put (transaction, hash, block_a);
                    ledger.change_latest (transaction, account, hash, info.rep_block, block_a.hashables.dividend, new_balance, info.block_count + 1);
                    ledger.store.representation_add (transaction, info.rep_block, pending.amount.number ());
                    ledger.store.frontier_del (transaction, block_a.hashables.previous);
                    ledger.store.frontier_put (transaction, hash, account);
                    result.account = account;
                    result.amount = pending.amount;
                    ledger.stats.inc (chratos::stat::type::ledger, chratos::stat::detail::receive);
                  }
                }
              }
            }
          }
          else
          {
            result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? chratos::process_result::fork : chratos::process_result::gap_previous; // If we have the block but it's not the latest we have a signed fork (Malicious)
          }
        }
      }
    }
  }
}

void ledger_processor::open_block (chratos::open_block const & block_a)
{
  auto hash (block_a.hash ());
  auto existing (ledger.store.block_exists (transaction, hash));
  result.code = existing ? chratos::process_result::old : chratos::process_result::progress; // Have we seen this block already? (Harmless)
  if (result.code == chratos::process_result::progress)
  {
    auto source_missing (!ledger.store.block_exists (transaction, block_a.hashables.source));
    result.code = source_missing ? chratos::process_result::gap_source : chratos::process_result::progress; // Have we seen the source block? (Harmless)
    if (result.code == chratos::process_result::progress)
    {
      result.code = chratos::validate_message (block_a.hashables.account, hash, block_a.signature) ? chratos::process_result::bad_signature : chratos::process_result::progress; // Is the signature valid (Malformed)
      if (result.code == chratos::process_result::progress)
      {
        chratos::account_info info;
        result.code = ledger.store.account_get (transaction, block_a.hashables.account, info) ? chratos::process_result::progress : chratos::process_result::fork; // Has this account already been opened? (Malicious)
        if (result.code == chratos::process_result::progress)
        {
          chratos::pending_key key (block_a.hashables.account, block_a.hashables.source);
          chratos::pending_info pending;
          result.code = ledger.store.pending_get (transaction, key, pending) ? chratos::process_result::unreceivable : chratos::process_result::progress; // Has this source already been received (Malformed)
          if (result.code == chratos::process_result::progress)
          {
            result.code = block_a.hashables.account == chratos::burn_account ? chratos::process_result::opened_burn_account : chratos::process_result::progress; // Is it burning 0 account? (Malicious)
            if (result.code == chratos::process_result::progress)
            {
              result.code = pending.epoch == chratos::epoch::epoch_0 ? chratos::process_result::progress : chratos::process_result::unreceivable; // Are we receiving a state-only send? (Malformed)
              if (result.code == chratos::process_result::progress)
              {
                chratos::account_info source_info;
                auto error (ledger.store.account_get (transaction, pending.source, source_info));
                assert (!error);
                ledger.store.pending_del (transaction, key);
                ledger.store.block_put (transaction, hash, block_a);
                ledger.change_latest (transaction, block_a.hashables.account, hash, hash, block_a.hashables.dividend, pending.amount.number (), info.block_count + 1);
                ledger.store.representation_add (transaction, hash, pending.amount.number ());
                ledger.store.frontier_put (transaction, hash, block_a.hashables.account);
                result.account = block_a.hashables.account;
                result.amount = pending.amount;
                ledger.stats.inc (chratos::stat::type::ledger, chratos::stat::detail::open);
              }
            }
          }
        }
      }
    }
  }
}

void ledger_processor::dividend_block (chratos::dividend_block const & block_a)
{
  auto hash (block_a.hash ());
  auto existing (ledger.store.block_exists (transaction, hash));
  result.code = existing ? chratos::process_result::old : chratos::process_result::progress; // Have we seen this block before? (Harmless)
  if (result.code == chratos::process_result::progress)
  {
    std::shared_ptr<chratos::block> previous (ledger.store.block_get (transaction, block_a.hashables.previous));
    result.code = previous != nullptr ? chratos::process_result::progress : chratos::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
    if (result.code == chratos::process_result::progress)
    {
      auto account = block_a.hashables.account;
      result.code = account.is_zero () ? chratos::process_result::fork : chratos::process_result::progress;
      if (result.code == chratos::process_result::progress)
      {
        // Here is the dividend account check.
        result.code = account == chratos::dividend_account ? chratos::process_result::progress : chratos::process_result::invalid_dividend_account;
        if (result.code == chratos::process_result::progress)
        {
          result.code = validate_message (account, hash, block_a.signature) ? chratos::process_result::bad_signature : chratos::process_result::progress; // Is this block signed correctly (Malformed)
          if (result.code == chratos::process_result::progress)
          {
            chratos::account_info info;
            auto latest_error (ledger.store.account_get (transaction, account, info));
            assert (!latest_error);
            assert (info.head == block_a.hashables.previous);
            result.code = info.balance.number () >= block_a.hashables.balance.number () ? chratos::process_result::progress : chratos::process_result::negative_spend; // Is this trying to spend a negative amount (Malicious)
            if (result.code == chratos::process_result::progress)
            {
              auto amount (info.balance.number () - block_a.hashables.balance.number ());
              result.code = amount > chratos::minimum_dividend_amount ? chratos::process_result::progress : chratos::process_result::dividend_too_small;

              if (result.code == chratos::process_result::progress)
              {
                // Do dividend checks. Make sure that the previous dividend hasn't been used before.
                if (block_a.hashables.dividend != chratos::dividend_base) 
                {
                  // check block exists
                  result.code = ledger.store.block_exists (transaction, block_a.hashables.dividend) ? chratos::process_result::progress : chratos::process_result::gap_source;
                }

                if (result.code == chratos::process_result::progress) 
                {
                  auto dividend_info (ledger.store.dividend_get (transaction));
                  result.code = block_a.hashables.dividend == dividend_info.head ? chratos::process_result::progress : chratos::process_result::dividend_fork;

                  if (result.code == chratos::process_result::progress)
                  {
                    ledger.store.block_put (transaction, hash, block_a);

                    if (!info.rep_block.is_zero ())
                    {
                      // Move existing representation
                      ledger.store.representation_add (transaction, info.rep_block, 0 - info.balance.number ());
                    }
                    // Add in amount delta
                    ledger.store.representation_add (transaction, hash, block_a.hashables.balance.number ());

                    ledger.change_latest (transaction, account, hash, info.rep_block, block_a.hashables.dividend, block_a.hashables.balance, info.block_count + 1);
                    if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
                    {
                      ledger.store.frontier_del (transaction, info.head);
                    }
                    // Frontier table is unnecessary for state blocks and this also prevents old blocks from being inserted on top of state blocks.
                    result.account = account;
                    result.amount = amount;
                    ledger.stats.inc (chratos::stat::type::ledger, chratos::stat::detail::dividend_block);
                    auto previous_info (ledger.store.dividend_get (transaction));
                    const auto balance = previous_info.balance.number () + result.amount.number ();
                    const auto count = previous_info.block_count + 1;
                    const auto time = chratos::seconds_since_epoch ();
                    chratos::dividend_info info (hash, balance, time, count, chratos::epoch::epoch_0);
                    ledger.store.dividend_put (transaction, info);
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

void ledger_processor::claim_block (chratos::claim_block const & block_a)
{
  auto hash (block_a.hash ());
  auto existing (ledger.store.block_exists (transaction, hash));
  result.code = existing ? chratos::process_result::old : chratos::process_result::progress; // Have we seen this block already?  (Harmless)
  if (result.code == chratos::process_result::progress)
  {
    auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
    result.code = previous != nullptr ? chratos::process_result::progress : chratos::process_result::gap_previous;
    if (result.code == chratos::process_result::progress)
    {
      std::shared_ptr<chratos::block> dividend = ledger.store.block_get (transaction, block_a.hashables.dividend);
      result.code = dividend != nullptr ? chratos::process_result::progress : chratos::process_result::gap_source; // Have we seen the source block already? (Harmless)
      if (result.code == chratos::process_result::progress)
      {
        chratos::dividend_block const * div_block = dynamic_cast<chratos::dividend_block const *> (dividend.get ());
        result.code = div_block != nullptr ? chratos::process_result::progress : chratos::process_result::incorrect_dividend;
        if (result.code == chratos::process_result::progress)
        {
          auto account = block_a.hashables.account;
          result.code = account.is_zero () ? chratos::process_result::gap_previous : chratos::process_result::progress; //Have we seen the previous block? No entries for account at all (Harmless)
          if (result.code == chratos::process_result::progress)
          {
            result.code = chratos::validate_message (account, hash, block_a.signature) ? chratos::process_result::bad_signature : chratos::process_result::progress; // Is the signature valid (Malformed)
            if (result.code == chratos::process_result::progress)
            {
              chratos::account_info info;
              ledger.store.account_get (transaction, account, info);
              result.code = info.head == block_a.hashables.previous ? chratos::process_result::progress : chratos::process_result::gap_previous; // Block doesn't immediately follow latest block (Harmless)
              if (result.code == chratos::process_result::progress)
              {
                result.code = ledger.has_outstanding_pendings_for_dividend (transaction, block_a.hashables.dividend, account) ? chratos::process_result::outstanding_pendings : chratos::process_result::progress;
                if (result.code == chratos::process_result::progress)
                {
                  result.code = info.dividend_block != block_a.hashables.dividend && ledger.dividends_are_ordered (transaction, info.dividend_block, block_a.hashables.dividend) ? chratos::process_result::progress : chratos::process_result::unreceivable; // Hash this dividend been claimed already.
                  if (result.code == chratos::process_result::progress)
                  {
                    result.amount = block_a.hashables.balance.number () - info.balance.number ();
                    const auto expected (ledger.amount_for_dividend (transaction, block_a.hashables.dividend, account));
                    result.code = result.amount == expected ? chratos::process_result::progress : chratos::process_result::balance_mismatch;
                    if (result.code == chratos::process_result::progress)
                    {
                      ledger.store.block_put (transaction, hash, block_a);

                      if (!info.rep_block.is_zero ())
                      {
                        // Move existing representation
                        ledger.store.representation_add (transaction, info.rep_block, 0 - info.balance.number ());
                      }
                      // Add in amount delta
                      ledger.store.representation_add (transaction, hash, block_a.hashables.balance.number ());

                      chratos::account account = block_a.hashables.account;

                      if (ledger.dividends_are_ordered (transaction, info.dividend_block, block_a.hashables.dividend))
                      {
                        info.dividend_block = block_a.hashables.dividend;
                        ledger.store.account_put (transaction, account, info);
                      }

                      ledger.change_latest (transaction, account, hash, info.rep_block, block_a.hashables.dividend, block_a.hashables.balance, info.block_count + 1);
                      if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
                      {
                        ledger.store.frontier_del (transaction, info.head);
                      }
                      // Frontier table is unnecessary for state blocks and this also prevents old blocks from being inserted on top of state blocks.
                      result.account = account;
                      ledger.stats.inc (chratos::stat::type::ledger, chratos::stat::detail::claim_block);
                    }
                  }
                }
              }
            }
          }
          else
          {
            result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? chratos::process_result::fork : chratos::process_result::gap_previous; // If we have the block but it's not the latest we have a signed fork (Malicious)
          }
        }
      }
    }
  }
}

ledger_processor::ledger_processor (chratos::ledger & ledger_a, MDB_txn * transaction_a) :
ledger (ledger_a),
transaction (transaction_a)
{
}
} // namespace

size_t chratos::shared_ptr_block_hash::operator() (std::shared_ptr<chratos::block> const & block_a) const
{
  auto hash (block_a->hash ());
  auto result (static_cast<size_t> (hash.qwords[0]));
  return result;
}

bool chratos::shared_ptr_block_hash::operator() (std::shared_ptr<chratos::block> const & lhs, std::shared_ptr<chratos::block> const & rhs) const
{
  return lhs->hash () == rhs->hash ();
}

chratos::ledger::ledger (chratos::block_store & store_a, chratos::stat & stat_a, chratos::uint256_union const & epoch_link_a, chratos::account const & epoch_signer_a) :
store (store_a),
stats (stat_a),
check_bootstrap_weights (true),
epoch_link (epoch_link_a),
epoch_signer (epoch_signer_a)
{
}

// Balance for account containing hash
chratos::uint128_t chratos::ledger::balance (MDB_txn * transaction_a, chratos::block_hash const & hash_a)
{
  balance_visitor visitor (transaction_a, store);
  visitor.compute (hash_a);
  return visitor.balance;
}

// Balance for an account by account number
chratos::uint128_t chratos::ledger::account_balance (MDB_txn * transaction_a, chratos::account const & account_a)
{
  chratos::uint128_t result (0);
  chratos::account_info info;
  auto none (store.account_get (transaction_a, account_a, info));
  if (!none)
  {
    result = info.balance.number ();
  }
  return result;
}

chratos::uint128_t chratos::ledger::account_pending (MDB_txn * transaction_a, chratos::account const & account_a)
{
  chratos::uint128_t result (0);
  chratos::account end (account_a.number () + 1);
  for (auto i (store.pending_v0_begin (transaction_a, chratos::pending_key (account_a, 0))), n (store.pending_v0_begin (transaction_a, chratos::pending_key (end, 0))); i != n; ++i)
  {
    chratos::pending_info info (i->second);
    result += info.amount.number ();
  }
  for (auto i (store.pending_v1_begin (transaction_a, chratos::pending_key (account_a, 0))), n (store.pending_v1_begin (transaction_a, chratos::pending_key (end, 0))); i != n; ++i)
  {
    chratos::pending_info info (i->second);
    result += info.amount.number ();
  }
  return result;
}

chratos::process_return chratos::ledger::process (MDB_txn * transaction_a, chratos::block const & block_a)
{
  ledger_processor processor (*this, transaction_a);
  block_a.visit (processor);
  return processor.result;
}

chratos::block_hash chratos::ledger::representative (MDB_txn * transaction_a, chratos::block_hash const & hash_a)
{
  auto result (representative_calculated (transaction_a, hash_a));
  assert (result.is_zero () || store.block_exists (transaction_a, result));
  return result;
}

chratos::block_hash chratos::ledger::representative_calculated (MDB_txn * transaction_a, chratos::block_hash const & hash_a)
{
  representative_visitor visitor (transaction_a, store);
  visitor.compute (hash_a);
  return visitor.result;
}

bool chratos::ledger::block_exists (chratos::block_hash const & hash_a)
{
  chratos::transaction transaction (store.environment, nullptr, false);
  auto result (store.block_exists (transaction, hash_a));
  return result;
}

std::string chratos::ledger::block_text (char const * hash_a)
{
  return block_text (chratos::block_hash (hash_a));
}

std::string chratos::ledger::block_text (chratos::block_hash const & hash_a)
{
  std::string result;
  chratos::transaction transaction (store.environment, nullptr, false);
  auto block (store.block_get (transaction, hash_a));
  if (block != nullptr)
  {
    block->serialize_json (result);
  }
  return result;
}

bool chratos::ledger::is_send (MDB_txn * transaction_a, chratos::state_block const & block_a)
{
  bool result (false);
  chratos::block_hash previous (block_a.hashables.previous);
  if (!previous.is_zero ())
  {
    if (block_a.hashables.balance < balance (transaction_a, previous))
    {
      result = true;
    }
  }
  return result;
}

bool chratos::ledger::is_dividend (MDB_txn * transaction_a, chratos::state_block const & block_a)
{
  bool result (false);

  chratos::block_hash dividend (block_a.hashables.dividend);
  chratos::block_hash link (block_a.hashables.link);

  if (link == dividend) 
  {
    result = !is_send (transaction_a, block_a);
  }

  return result;
}

bool chratos::ledger::is_dividend_claim (MDB_txn * transaction_a, chratos::state_block const & block_a)
{
  bool result (false);

  chratos::block_hash dividend (block_a.hashables.dividend);
  chratos::block_hash link (block_a.hashables.link);

  if (link == dividend)
  {
    result = !is_send (transaction_a, block_a);
  }

  return result;
}

bool chratos::ledger::has_outstanding_pendings_for_dividend (MDB_txn * transaction_a, chratos::block_hash const & dividend_a, chratos::account const & account_a) {
  bool result (false);

  chratos::account end (account_a.number () + 1);

  for (auto i (store.pending_begin (transaction_a, chratos::pending_key (account_a, 0))), n (store.pending_begin (transaction_a, chratos::pending_key (end, 0))); i != n && !result; ++i) 
  {
    chratos::pending_info info (i->second);
    if (info.dividend == dividend_a) {
      result = true;
    }
  }

  return result;
}

bool chratos::ledger::dividends_are_ordered (MDB_txn * transaction_a, chratos::block_hash const & first_a, chratos::block_hash const & last_a)
{
  bool result (false);

  if (first_a == last_a) 
  {
    return true;
  }

  std::shared_ptr<chratos::block> block = store.block_get (transaction_a, last_a);

  while (block != nullptr)
  {
    chratos::block_hash previous = block->dividend ();
    if (previous == first_a)
    {
      result = true;
    }
    block = store.block_get (transaction_a, previous);
  }

  return result;
}

chratos::amount chratos::ledger::amount_for_dividend (MDB_txn * transaction_a, chratos::block_hash const & dividend_a, chratos::account const & account_a)
{
  chratos::amount result (0);
  chratos::account_info account_info;
  std::shared_ptr<chratos::block> block_l = store.block_get(transaction_a, dividend_a);
  auto dividend_hash (block_l->dividend ());
  auto previous (store.block_get(transaction_a, block_l->previous ()));
  chratos::dividend_block const * dividend_block (dynamic_cast<chratos::dividend_block const *> (block_l.get ()));

  assert (dividend_block != nullptr);

  if (dividend_block != nullptr)
  {
    if (!store.account_get (transaction_a, account_a, account_info))
    {
      chratos::amount genesis_supply (std::numeric_limits<chratos::uint128_t>::max ());
      chratos::amount burned_amount (burn_account_balance (transaction_a));
      chratos::amount balance_at_dividend (account_info.balance);
      //chratos::amount previous_balance (balance (transaction_a, block_l->previous ()));
      //chratos::amount dividend_balance (balance (transaction_a, block_l->hash ()));
      chratos::amount dividend_amount (amount (transaction_a, block_l->hash ()));
      chratos::amount total_supply (genesis_supply.number () - burned_amount.number ());
      boost::multiprecision::cpp_bin_float_100 balance_f (balance_at_dividend.number ());
      boost::multiprecision::cpp_bin_float_100 daf (dividend_amount.number ());
      boost::multiprecision::cpp_bin_float_100 tsf (total_supply.number ());
      boost::multiprecision::cpp_bin_float_100 total_f (tsf - daf);
      boost::multiprecision::cpp_bin_float_100 proportion (balance_f / total_f);
      boost::multiprecision::cpp_bin_float_100 reward (proportion * daf);
      
      result = chratos::amount (static_cast<uint128_t> (reward));
    }
  }

  return result;
}

chratos::amount chratos::ledger::burn_account_balance (MDB_txn * transaction_a)
{
  chratos::account burn = burn_account;
  chratos::amount result (0);
  chratos::account_info info;

  if (!store.account_get (transaction_a, burn, info))
  {
    result = info.balance;
  }

  chratos::account end (burn.number () + 1);
  for (auto i (store.pending_v0_begin (transaction_a, chratos::pending_key (burn, 0))), n (store.pending_v0_begin (transaction_a, chratos::pending_key (end, 0))); i != n; ++i)
  {
    auto pending_info = i->second;
    result = result.number () + pending_info.amount.number ();
  }

  return result;
}

std::vector<std::shared_ptr<chratos::block>> chratos::ledger::dividend_claim_blocks (MDB_txn * transaction_a, chratos::account const & account_a)
{
  chratos::account_info account_info;
  std::vector<std::shared_ptr<chratos::block>> result;

  if (!store.account_get (transaction_a, account_a, account_info))
  {
    chratos::block_hash current = account_info.head;

    while (current != account_a)
    {
      std::shared_ptr<chratos::block> block = store.block_get (transaction_a, current);
      chratos::state_block const * state = dynamic_cast<chratos::state_block const *> (block.get ());

      if (!state) {
        break;
      }

      if (is_dividend_claim (transaction_a, *state))
      {
        result.push_back(block);
      }
      current = block->previous ();
    }
  }

  return result;
}

std::unordered_map<chratos::block_hash, int> chratos::ledger::get_dividend_indexes (MDB_txn * transaction_a)
{
  std::unordered_map<chratos::block_hash, int> results;

  auto dividend_info = store.dividend_get (transaction_a);
  auto current = dividend_info.head;
  int index (0);

  while (current != chratos::dividend_base)
  {
    std::shared_ptr<chratos::block> block = store.block_get (transaction_a, current);
    results[current] = index++;
    current = block->dividend ();
  }

  const auto count = results.size();

  for (auto & it : results)
  {
    it.second = (count - 1) - it.second;
  }

  return results;
}

chratos::block_hash chratos::ledger::block_destination (MDB_txn * transaction_a, chratos::block const & block_a)
{
  chratos::block_hash result (0);
  chratos::send_block const * send_block (dynamic_cast<chratos::send_block const *> (&block_a));
  chratos::state_block const * state_block (dynamic_cast<chratos::state_block const *> (&block_a));
  if (send_block != nullptr)
  {
    result = send_block->hashables.destination;
  }
  else if (state_block != nullptr && is_send (transaction_a, *state_block))
  {
    result = state_block->hashables.link;
  }
  return result;
}

chratos::block_hash chratos::ledger::block_source (MDB_txn * transaction_a, chratos::block const & block_a)
{
  // If block_a.source () is nonzero, then we have our source.
  // However, universal blocks will always return zero.
  chratos::block_hash result (block_a.source ());
  chratos::state_block const * state_block (dynamic_cast<chratos::state_block const *> (&block_a));
  if (state_block != nullptr && !is_send (transaction_a, *state_block))
  {
    result = state_block->hashables.link;
  }
  return result;
}

// Vote weight of an account
chratos::uint128_t chratos::ledger::weight (MDB_txn * transaction_a, chratos::account const & account_a)
{
  if (check_bootstrap_weights.load ())
  {
    auto blocks = store.block_count (transaction_a);
    if (blocks.sum () < bootstrap_weight_max_blocks)
    {
      auto weight = bootstrap_weights.find (account_a);
      if (weight != bootstrap_weights.end ())
      {
        return weight->second;
      }
    }
    else
    {
      check_bootstrap_weights = false;
    }
  }
  return store.representation_get (transaction_a, account_a);
}

// Rollback blocks until `block_a' doesn't exist
void chratos::ledger::rollback (MDB_txn * transaction_a, chratos::block_hash const & block_a)
{
  assert (store.block_exists (transaction_a, block_a));
  auto account_l (account (transaction_a, block_a));
  rollback_visitor rollback (transaction_a, *this);
  chratos::account_info info;
  while (store.block_exists (transaction_a, block_a))
  {
    auto latest_error (store.account_get (transaction_a, account_l, info));
    assert (!latest_error);
    auto block (store.block_get (transaction_a, info.head));
    block->visit (rollback);
  }
}

// Return account containing hash
chratos::account chratos::ledger::account (MDB_txn * transaction_a, chratos::block_hash const & hash_a)
{
  chratos::account result;
  auto hash (hash_a);
  chratos::block_hash successor (1);
  chratos::block_info block_info;
  std::unique_ptr<chratos::block> block (store.block_get (transaction_a, hash));
  while (!successor.is_zero () && (block->type () != chratos::block_type::state || block->type () != chratos::block_type::dividend || block->type () != chratos::block_type::claim) && store.block_info_get (transaction_a, successor, block_info))
  {
    successor = store.block_successor (transaction_a, hash);
    if (!successor.is_zero ())
    {
      hash = successor;
      block = store.block_get (transaction_a, hash);
    }
  }
  if (block->type () == chratos::block_type::state)
  {
    auto state_block (dynamic_cast<chratos::state_block *> (block.get ()));
    result = state_block->hashables.account;
  }
  else if (block->type () == chratos::block_type::dividend)
  {
    auto dividend_block (dynamic_cast<chratos::dividend_block *> (block.get ()));
    result = dividend_block->hashables.account;
  }
  else if (block->type () == chratos::block_type::claim)
  {
    auto claim_block (dynamic_cast<chratos::claim_block *> (block.get ()));
    result = claim_block->hashables.account;
  }
  else if (successor.is_zero ())
  {
    result = store.frontier_get (transaction_a, hash);
  }
  else
  {
    result = block_info.account;
  }
  assert (!result.is_zero ());
  return result;
}

// Return amount decrease or increase for block
chratos::uint128_t chratos::ledger::amount (MDB_txn * transaction_a, chratos::block_hash const & hash_a)
{
  amount_visitor amount (transaction_a, store);
  amount.compute (hash_a);
  return amount.amount;
}

// Return latest block for account
chratos::block_hash chratos::ledger::latest (MDB_txn * transaction_a, chratos::account const & account_a)
{
  chratos::account_info info;
  auto latest_error (store.account_get (transaction_a, account_a, info));
  return latest_error ? 0 : info.head;
}

// Return latest root for account, account number of there are no blocks for this account.
chratos::block_hash chratos::ledger::latest_root (MDB_txn * transaction_a, chratos::account const & account_a)
{
  chratos::account_info info;
  auto latest_error (store.account_get (transaction_a, account_a, info));
  chratos::block_hash result;
  if (latest_error)
  {
    result = account_a;
  }
  else
  {
    result = info.head;
  }
  return result;
}

chratos::block_hash chratos::ledger::latest_dividend (MDB_txn * transaction_a) {
  chratos::dividend_info info = store.dividend_get (transaction_a);
  return info.head;
}

chratos::checksum chratos::ledger::checksum (MDB_txn * transaction_a, chratos::account const & begin_a, chratos::account const & end_a)
{
  chratos::checksum result;
  auto error (store.checksum_get (transaction_a, 0, 0, result));
  assert (!error);
  return result;
}

void chratos::ledger::dump_account_chain (chratos::account const & account_a)
{
  chratos::transaction transaction (store.environment, nullptr, false);
  auto hash (latest (transaction, account_a));
  while (!hash.is_zero ())
  {
    auto block (store.block_get (transaction, hash));
    assert (block != nullptr);
    std::cerr << hash.to_string () << std::endl;
    hash = block->previous ();
  }
}

class block_fit_visitor : public chratos::block_visitor
{
public:
  block_fit_visitor (chratos::ledger & ledger_a, MDB_txn * transaction_a) :
  ledger (ledger_a),
  transaction (transaction_a),
  result (false)
  {
  }
  void send_block (chratos::send_block const & block_a) override
  {
    result = ledger.store.block_exists (transaction, block_a.previous ());
  }
  void receive_block (chratos::receive_block const & block_a) override
  {
    result = ledger.store.block_exists (transaction, block_a.previous ());
    result &= ledger.store.block_exists (transaction, block_a.source ());
  }
  void open_block (chratos::open_block const & block_a) override
  {
    result = ledger.store.block_exists (transaction, block_a.source ());
  }
  void change_block (chratos::change_block const & block_a) override
  {
    result = ledger.store.block_exists (transaction, block_a.previous ());
  }
  void state_block (chratos::state_block const & block_a) override
  {
    result = block_a.previous ().is_zero () || ledger.store.block_exists (transaction, block_a.previous ());
    if (result && !ledger.is_send (transaction, block_a))
    {
      result &= ledger.store.block_exists (transaction, block_a.hashables.link);
    }
  }
  void dividend_block (chratos::dividend_block const & block_a) override
  {
    result = ledger.store.block_exists (transaction, block_a.previous ());
  }
  void claim_block (chratos::claim_block const & block_a) override
  {
    result = ledger.store.block_exists (transaction, block_a.previous ());
    result &= ledger.store.block_exists (transaction, block_a.dividend ());
  }
  chratos::ledger & ledger;
  MDB_txn * transaction;
  bool result;
};

bool chratos::ledger::could_fit (MDB_txn * transaction_a, chratos::block const & block_a)
{
  block_fit_visitor visitor (*this, transaction_a);
  block_a.visit (visitor);
  return visitor.result;
}

void chratos::ledger::checksum_update (MDB_txn * transaction_a, chratos::block_hash const & hash_a)
{
  chratos::checksum value;
  auto error (store.checksum_get (transaction_a, 0, 0, value));
  assert (!error);
  value ^= hash_a;
  store.checksum_put (transaction_a, 0, 0, value);
}

void chratos::ledger::change_latest (MDB_txn * transaction_a, chratos::account const & account_a, chratos::block_hash const & hash_a, chratos::block_hash const & rep_block_a, chratos::block_hash const & dividend_a, chratos::amount const & balance_a, uint64_t block_count_a, bool is_state, chratos::epoch epoch_a)
{
  chratos::account_info info;
  auto exists (!store.account_get (transaction_a, account_a, info));
  if (exists)
  {
    checksum_update (transaction_a, info.head);
  }
  else
  {
    assert (store.block_get (transaction_a, hash_a)->previous ().is_zero ());
    info.open_block = hash_a;
    info.dividend_block = dividend_a;
  }
  if (!hash_a.is_zero ())
  {
    info.head = hash_a;
    info.rep_block = rep_block_a;
    info.balance = balance_a;
    info.modified = chratos::seconds_since_epoch ();
    info.block_count = block_count_a;
    if (exists && info.epoch != epoch_a)
    {
      // otherwise we'd end up with a duplicate
      store.account_del (transaction_a, account_a);
    }
    info.epoch = epoch_a;
    store.account_put (transaction_a, account_a, info);
    if (!(block_count_a % store.block_info_max) && !is_state)
    {
      chratos::block_info block_info;
      block_info.account = account_a;
      block_info.balance = balance_a;
      store.block_info_put (transaction_a, hash_a, block_info);
    }
    checksum_update (transaction_a, hash_a);
  }
  else
  {
    store.account_del (transaction_a, account_a);
  }
}

std::unique_ptr<chratos::block> chratos::ledger::successor (MDB_txn * transaction_a, chratos::uint256_union const & root_a)
{
  chratos::block_hash successor (0);
  if (store.account_exists (transaction_a, root_a))
  {
    chratos::account_info info;
    auto error (store.account_get (transaction_a, root_a, info));
    assert (!error);
    successor = info.open_block;
  }
  else
  {
    successor = store.block_successor (transaction_a, root_a);
  }
  std::unique_ptr<chratos::block> result;
  if (!successor.is_zero ())
  {
    result = store.block_get (transaction_a, successor);
  }
  assert (successor.is_zero () || result != nullptr);
  return result;
}

std::unique_ptr<chratos::block> chratos::ledger::forked_block (MDB_txn * transaction_a, chratos::block const & block_a)
{
  assert (!store.block_exists (transaction_a, block_a.hash ()));
  auto root (block_a.root ());
  assert (store.block_exists (transaction_a, root) || store.account_exists (transaction_a, root));
  std::unique_ptr<chratos::block> result (store.block_get (transaction_a, store.block_successor (transaction_a, root)));
  if (result == nullptr)
  {
    chratos::account_info info;
    auto error (store.account_get (transaction_a, root, info));
    assert (!error);
    result = store.block_get (transaction_a, info.open_block);
    assert (result != nullptr);
  }
  return result;
}
