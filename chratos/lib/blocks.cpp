#include <chratos/lib/blocks.hpp>

#include <boost/endian/conversion.hpp>

/** Compare blocks, first by type, then content. This is an optimization over dynamic_cast, which is very slow on some platforms. */
namespace
{
template <typename T>
bool blocks_equal (T const & first, chratos::block const & second)
{
  static_assert (std::is_base_of<chratos::block, T>::value, "Input parameter is not a block type");
  return (first.type () == second.type ()) && (static_cast<T const &> (second)) == first;
}
}

std::string chratos::to_string_hex (uint64_t value_a)
{
  std::stringstream stream;
  stream << std::hex << std::noshowbase << std::setw (16) << std::setfill ('0');
  stream << value_a;
  return stream.str ();
}

bool chratos::from_string_hex (std::string const & value_a, uint64_t & target_a)
{
  auto error (value_a.empty ());
  if (!error)
  {
    error = value_a.size () > 16;
    if (!error)
    {
      std::stringstream stream (value_a);
      stream << std::hex << std::noshowbase;
      try
      {
        uint64_t number_l;
        stream >> number_l;
        target_a = number_l;
        if (!stream.eof ())
        {
          error = true;
        }
      }
      catch (std::runtime_error &)
      {
        error = true;
      }
    }
  }
  return error;
}

std::string chratos::block::to_json ()
{
  std::string result;
  serialize_json (result);
  return result;
}

chratos::block_hash chratos::block::hash () const
{
  chratos::uint256_union result;
  blake2b_state hash_l;
  auto status (blake2b_init (&hash_l, sizeof (result.bytes)));
  assert (status == 0);
  hash (hash_l);
  status = blake2b_final (&hash_l, result.bytes.data (), sizeof (result.bytes));
  assert (status == 0);
  return result;
}

void chratos::send_block::visit (chratos::block_visitor & visitor_a) const
{
  visitor_a.send_block (*this);
}

void chratos::send_block::hash (blake2b_state & hash_a) const
{
  hashables.hash (hash_a);
}

uint64_t chratos::send_block::block_work () const
{
  return work;
}

void chratos::send_block::block_work_set (uint64_t work_a)
{
  work = work_a;
}

chratos::send_hashables::send_hashables (chratos::block_hash const & previous_a, chratos::account const & destination_a, chratos::amount const & balance_a, const chratos::block_hash & dividend_a) :
previous (previous_a),
destination (destination_a),
balance (balance_a),
dividend (dividend_a)
{
}

chratos::send_hashables::send_hashables (bool & error_a, chratos::stream & stream_a)
{
  error_a = chratos::read (stream_a, previous.bytes);
  if (!error_a)
  {
    error_a = chratos::read (stream_a, destination.bytes);
    if (!error_a)
    {
      error_a = chratos::read (stream_a, balance.bytes);
      if (!error_a)
      {
        error_a = chratos::read (stream_a, dividend.bytes);
      }
    }
  }
}

chratos::send_hashables::send_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
  try
  {
    auto previous_l (tree_a.get<std::string> ("previous"));
    auto destination_l (tree_a.get<std::string> ("destination"));
    auto balance_l (tree_a.get<std::string> ("balance"));
    auto dividend_l (tree_a.get<std::string> ("dividend"));
    error_a = previous.decode_hex (previous_l);
    if (!error_a)
    {
      error_a = destination.decode_account (destination_l);
      if (!error_a)
      {
        error_a = balance.decode_hex (balance_l);
        if (!error_a)
        {
          error_a = dividend.decode_hex (dividend_l);
        }
      }
    }
  }
  catch (std::runtime_error const &)
  {
    error_a = true;
  }
}

void chratos::send_hashables::hash (blake2b_state & hash_a) const
{
  auto status (blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes)));
  assert (status == 0);
  status = blake2b_update (&hash_a, destination.bytes.data (), sizeof (destination.bytes));
  assert (status == 0);
  status = blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
  assert (status == 0);
  status = blake2b_update (&hash_a, dividend.bytes.data (), sizeof (dividend.bytes));
  assert (status == 0);
}

void chratos::send_block::serialize (chratos::stream & stream_a) const
{
  write (stream_a, hashables.previous.bytes);
  write (stream_a, hashables.destination.bytes);
  write (stream_a, hashables.balance.bytes);
  write (stream_a, hashables.dividend.bytes);
  write (stream_a, signature.bytes);
  write (stream_a, work);
}

void chratos::send_block::serialize_json (std::string & string_a) const
{
  boost::property_tree::ptree tree;
  tree.put ("type", "send");
  std::string previous;
  hashables.previous.encode_hex (previous);
  tree.put ("previous", previous);
  tree.put ("destination", hashables.destination.to_account ());
  std::string balance;
  hashables.balance.encode_hex (balance);
  tree.put ("balance", balance);
  std::string signature_l;
  signature.encode_hex (signature_l);
  tree.put ("dividend", hashables.dividend.to_string());
  tree.put ("work", chratos::to_string_hex (work));
  tree.put ("signature", signature_l);
  std::stringstream ostream;
  boost::property_tree::write_json (ostream, tree);
  string_a = ostream.str ();
}

bool chratos::send_block::deserialize (chratos::stream & stream_a)
{
  auto error (false);
  error = read (stream_a, hashables.previous.bytes);
  if (!error)
  {
    error = read (stream_a, hashables.destination.bytes);
    if (!error)
    {
      error = read (stream_a, hashables.balance.bytes);
      if (!error)
      {
        error = read (stream_a, hashables.dividend.bytes);
        if (!error) 
        {
          error = read (stream_a, signature.bytes);
          if (!error)
          {
            error = read (stream_a, work);
          }
        }
      }
    }
  }
  return error;
}

bool chratos::send_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
  auto error (false);
  try
  {
    assert (tree_a.get<std::string> ("type") == "send");
    auto previous_l (tree_a.get<std::string> ("previous"));
    auto destination_l (tree_a.get<std::string> ("destination"));
    auto dividend_l (tree_a.get<std::string> ("dividend"));
    auto balance_l (tree_a.get<std::string> ("balance"));
    auto work_l (tree_a.get<std::string> ("work"));
    auto signature_l (tree_a.get<std::string> ("signature"));
    error = hashables.previous.decode_hex (previous_l);
    if (!error)
    {
      error = hashables.destination.decode_account (destination_l);
      if (!error)
      {
        error = hashables.balance.decode_hex (balance_l);
        if (!error)
        {
          error = chratos::from_string_hex (work_l, work);
          if (!error) 
          {
            error = signature.decode_hex (signature_l);
            if (!error)
            {
              error = hashables.dividend.decode_hex (dividend_l);
            }
          }
        }
      }
    }
  }
  catch (std::runtime_error const &)
  {
    error = true;
  }
  return error;
}

chratos::send_block::send_block (chratos::block_hash const & previous_a, chratos::account const & destination_a, chratos::amount const & balance_a, chratos::block_hash const & dividend_a, chratos::raw_key const & prv_a, chratos::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, destination_a, balance_a, dividend_a),
signature (chratos::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

chratos::send_block::send_block (bool & error_a, chratos::stream & stream_a) :
hashables (error_a, stream_a)
{
  if (!error_a)
  {
    error_a = chratos::read (stream_a, signature.bytes);
    if (!error_a)
    {
      error_a = chratos::read (stream_a, work);
    }
  }
}

chratos::send_block::send_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
  if (!error_a)
  {
    try
    {
      auto dividend_l (tree_a.get<std::string> ("dividend"));
      auto signature_l (tree_a.get<std::string> ("signature"));
      auto work_l (tree_a.get<std::string> ("work"));
      error_a = signature.decode_hex (signature_l);
      if (!error_a)
      {
        error_a = chratos::from_string_hex (work_l, work);
        if (!error_a) 
        {
          error_a = hashables.dividend.decode_hex (dividend_l);
        }
      }
    }
    catch (std::runtime_error const &)
    {
      error_a = true;
    }
  }
}

bool chratos::send_block::operator== (chratos::block const & other_a) const
{
  return blocks_equal (*this, other_a);
}

bool chratos::send_block::valid_predecessor (chratos::block const & block_a) const
{
  bool result;
  switch (block_a.type ())
  {
    case chratos::block_type::send:
    case chratos::block_type::receive:
    case chratos::block_type::open:
    case chratos::block_type::change:
    case chratos::block_type::dividend:
      result = true;
      break;
    default:
      result = false;
      break;
  }
  return result;
}

chratos::block_type chratos::send_block::type () const
{
  return chratos::block_type::send;
}

bool chratos::send_block::operator== (chratos::send_block const & other_a) const
{
  auto result (hashables.destination == other_a.hashables.destination && hashables.previous == other_a.hashables.previous && hashables.dividend == other_a.hashables.dividend && hashables.balance == other_a.hashables.balance && work == other_a.work && signature == other_a.signature);
  return result;
}

chratos::block_hash chratos::send_block::previous () const
{
  return hashables.previous;
}

chratos::block_hash chratos::send_block::source () const
{
  return 0;
}

chratos::block_hash chratos::send_block::root () const
{
  return hashables.previous;
}

chratos::block_hash chratos::send_block::dividend () const
{
  return hashables.dividend;
}

chratos::account chratos::send_block::representative () const
{
  return 0;
}

chratos::signature chratos::send_block::block_signature () const
{
  return signature;
}

void chratos::send_block::signature_set (chratos::uint512_union const & signature_a)
{
  signature = signature_a;
}

chratos::open_hashables::open_hashables (chratos::block_hash const & source_a, chratos::account const & representative_a, chratos::account const & account_a, chratos::block_hash const & dividend_a) :
source (source_a),
representative (representative_a),
account (account_a),
dividend (dividend_a)
{
}

chratos::open_hashables::open_hashables (bool & error_a, chratos::stream & stream_a)
{
  error_a = chratos::read (stream_a, source.bytes);
  if (!error_a)
  {
    error_a = chratos::read (stream_a, representative.bytes);
    if (!error_a)
    {
      error_a = chratos::read (stream_a, account.bytes);
      if (!error_a)
      {
        error_a = chratos::read (stream_a, dividend.bytes);
      }
    }
  }
}

chratos::open_hashables::open_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
  try
  {
    auto source_l (tree_a.get<std::string> ("source"));
    auto representative_l (tree_a.get<std::string> ("representative"));
    auto account_l (tree_a.get<std::string> ("account"));
    auto dividend_l (tree_a.get<std::string> ("dividend"));
    error_a = source.decode_hex (source_l);
    if (!error_a)
    {
      error_a = representative.decode_account (representative_l);
      if (!error_a)
      {
        error_a = account.decode_account (account_l);
        if (!error_a) 
        {
          error_a = dividend.decode_hex (dividend_l);
        }
      }
    }
  }
  catch (std::runtime_error const &)
  {
    error_a = true;
  }
}

void chratos::open_hashables::hash (blake2b_state & hash_a) const
{
  blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
  blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
  blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
  blake2b_update (&hash_a, dividend.bytes.data (), sizeof (dividend.bytes));
}

chratos::open_block::open_block (chratos::block_hash const & source_a, chratos::account const & representative_a, chratos::account const & account_a, chratos::block_hash const & dividend_a, chratos::raw_key const & prv_a, chratos::public_key const & pub_a, uint64_t work_a) :
hashables (source_a, representative_a, account_a, dividend_a),
signature (chratos::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
  assert (!representative_a.is_zero ());
}

chratos::open_block::open_block (chratos::block_hash const & source_a, chratos::account const & representative_a, chratos::account const & account_a, chratos::block_hash const & dividend_a, std::nullptr_t) :
hashables (source_a, representative_a, account_a, dividend_a),
work (0)
{
  signature.clear ();
}

chratos::open_block::open_block (bool & error_a, chratos::stream & stream_a) :
hashables (error_a, stream_a)
{
  if (!error_a)
  {
    error_a = chratos::read (stream_a, signature);
    if (!error_a)
    {
      error_a = chratos::read (stream_a, work);
    }
  }
}

chratos::open_block::open_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
  if (!error_a)
  {
    try
    {
      auto work_l (tree_a.get<std::string> ("work"));
      auto signature_l (tree_a.get<std::string> ("signature"));
      error_a = chratos::from_string_hex (work_l, work);
      if (!error_a)
      {
        error_a = signature.decode_hex (signature_l);
      }
    }
    catch (std::runtime_error const &)
    {
      error_a = true;
    }
  }
}

void chratos::open_block::hash (blake2b_state & hash_a) const
{
  hashables.hash (hash_a);
}

uint64_t chratos::open_block::block_work () const
{
  return work;
}

void chratos::open_block::block_work_set (uint64_t work_a)
{
  work = work_a;
}

chratos::block_hash chratos::open_block::previous () const
{
  chratos::block_hash result (0);
  return result;
}

void chratos::open_block::serialize (chratos::stream & stream_a) const
{
  write (stream_a, hashables.source);
  write (stream_a, hashables.representative);
  write (stream_a, hashables.account);
  write (stream_a, hashables.dividend);
  write (stream_a, signature);
  write (stream_a, work);
}

void chratos::open_block::serialize_json (std::string & string_a) const
{
  boost::property_tree::ptree tree;
  tree.put ("type", "open");
  tree.put ("source", hashables.source.to_string ());
  tree.put ("representative", representative ().to_account ());
  tree.put ("account", hashables.account.to_account ());
  tree.put ("dividend", hashables.dividend.to_string());
  std::string signature_l;
  signature.encode_hex (signature_l);
  tree.put ("work", chratos::to_string_hex (work));
  tree.put ("signature", signature_l);
  std::stringstream ostream;
  boost::property_tree::write_json (ostream, tree);
  string_a = ostream.str ();
}

bool chratos::open_block::deserialize (chratos::stream & stream_a)
{
  auto error (read (stream_a, hashables.source));
  if (!error)
  {
    error = read (stream_a, hashables.representative);
    if (!error)
    {
      error = read (stream_a, hashables.account);
      if (!error)
      {
        error = read(stream_a, hashables.dividend);
        if (!error)
        {
          error = read (stream_a, signature);
          if (!error)
          {
            error = read (stream_a, work);
          }
        }
      }
    }
  }
  return error;
}

bool chratos::open_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
  auto error (false);
  try
  {
    assert (tree_a.get<std::string> ("type") == "open");
    auto source_l (tree_a.get<std::string> ("source"));
    auto representative_l (tree_a.get<std::string> ("representative"));
    auto account_l (tree_a.get<std::string> ("account"));
    auto dividend_l (tree_a.get<std::string> ("dividend"));
    auto work_l (tree_a.get<std::string> ("work"));
    auto signature_l (tree_a.get<std::string> ("signature"));
    error = hashables.source.decode_hex (source_l);
    if (!error)
    {
      error = hashables.representative.decode_hex (representative_l);
      if (!error)
      {
        error = hashables.account.decode_hex (account_l);
        if (!error)
        {
          error = chratos::from_string_hex (work_l, work);
          if (!error)
          {
            error = signature.decode_hex (signature_l);
            if (!error)
            {
              error = hashables.dividend.decode_hex (dividend_l);
            }
          }
        }
      }
    }
  }
  catch (std::runtime_error const &)
  {
    error = true;
  }
  return error;
}

void chratos::open_block::visit (chratos::block_visitor & visitor_a) const
{
  visitor_a.open_block (*this);
}

chratos::block_type chratos::open_block::type () const
{
  return chratos::block_type::open;
}

bool chratos::open_block::operator== (chratos::block const & other_a) const
{
  return blocks_equal (*this, other_a);
}

bool chratos::open_block::operator== (chratos::open_block const & other_a) const
{
  return hashables.source == other_a.hashables.source && hashables.representative == other_a.hashables.representative && hashables.account == other_a.hashables.account && hashables.dividend == other_a.hashables.dividend && work == other_a.work && signature == other_a.signature;
}

bool chratos::open_block::valid_predecessor (chratos::block const & block_a) const
{
  return false;
}

chratos::block_hash chratos::open_block::source () const
{
  return hashables.source;
}

chratos::block_hash chratos::open_block::root () const
{
  return hashables.account;
}

chratos::block_hash chratos::open_block::dividend () const
{
  return hashables.dividend;
}

chratos::account chratos::open_block::representative () const
{
  return hashables.representative;
}

chratos::signature chratos::open_block::block_signature () const
{
  return signature;
}

void chratos::open_block::signature_set (chratos::uint512_union const & signature_a)
{
  signature = signature_a;
}

chratos::change_hashables::change_hashables (chratos::block_hash const & previous_a, chratos::account const & representative_a, chratos::block_hash const & dividend_a) :
previous (previous_a),
representative (representative_a),
dividend(dividend_a)
{
}

chratos::change_hashables::change_hashables (bool & error_a, chratos::stream & stream_a)
{
  error_a = chratos::read (stream_a, previous);
  if (!error_a)
  {
    error_a = chratos::read (stream_a, representative);
    if (!error_a)
    {
      error_a = chratos::read (stream_a, dividend.bytes);
    }
  }
}

chratos::change_hashables::change_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
  try
  {
    auto dividend_l (tree_a.get<std::string> ("dividend"));
    auto previous_l (tree_a.get<std::string> ("previous"));
    auto representative_l (tree_a.get<std::string> ("representative"));
    error_a = previous.decode_hex (previous_l);
    if (!error_a)
    {
      error_a = representative.decode_account (representative_l);
      if (!error_a)
      {
        error_a = dividend.decode_hex (dividend_l);
      }
    }
  }
  catch (std::runtime_error const &)
  {
    error_a = true;
  }
}

void chratos::change_hashables::hash (blake2b_state & hash_a) const
{
  blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
  blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
  blake2b_update (&hash_a, dividend.bytes.data (), sizeof (dividend.bytes));
}

chratos::change_block::change_block (chratos::block_hash const & previous_a, chratos::account const & representative_a, chratos::block_hash const & dividend_a, chratos::raw_key const & prv_a, chratos::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, representative_a, dividend_a),
signature (chratos::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

chratos::change_block::change_block (bool & error_a, chratos::stream & stream_a) :
hashables (error_a, stream_a)
{
  if (!error_a)
  {
    error_a = chratos::read (stream_a, signature);
    if (!error_a)
    {
      error_a = chratos::read (stream_a, work);
    }
  }
}

chratos::change_block::change_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
  if (!error_a)
  {
    try
    {
      auto dividend_l (tree_a.get<std::string> ("dividend"));
      auto work_l (tree_a.get<std::string> ("work"));
      auto signature_l (tree_a.get<std::string> ("signature"));
      error_a = chratos::from_string_hex (work_l, work);
      if (!error_a)
      {
        error_a = signature.decode_hex (signature_l);
        if (!error_a)
        {
          error_a = hashables.dividend.decode_hex (dividend_l);
        }
      }
    }
    catch (std::runtime_error const &)
    {
      error_a = true;
    }
  }
}

void chratos::change_block::hash (blake2b_state & hash_a) const
{
  hashables.hash (hash_a);
}

uint64_t chratos::change_block::block_work () const
{
  return work;
}

void chratos::change_block::block_work_set (uint64_t work_a)
{
  work = work_a;
}

chratos::block_hash chratos::change_block::previous () const
{
  return hashables.previous;
}

void chratos::change_block::serialize (chratos::stream & stream_a) const
{
  write (stream_a, hashables.previous);
  write (stream_a, hashables.representative);
  write (stream_a, hashables.dividend);
  write (stream_a, signature);
  write (stream_a, work);
}

void chratos::change_block::serialize_json (std::string & string_a) const
{
  boost::property_tree::ptree tree;
  tree.put ("type", "change");
  tree.put ("previous", hashables.previous.to_string ());
  tree.put ("representative", representative ().to_account ());
  tree.put ("dividend", hashables.dividend.to_string());
  tree.put ("work", chratos::to_string_hex (work));
  std::string signature_l;
  signature.encode_hex (signature_l);
  tree.put ("signature", signature_l);
  std::stringstream ostream;
  boost::property_tree::write_json (ostream, tree);
  string_a = ostream.str ();
}

bool chratos::change_block::deserialize (chratos::stream & stream_a)
{
  auto error (read (stream_a, hashables.previous));
  if (!error)
  {
    error = read (stream_a, hashables.representative);
    if (!error)
    {
      error = read (stream_a, hashables.dividend);
      if (!error)
      {
        error = read (stream_a, signature);
        if (!error)
        {
          error = read (stream_a, work);
        }
      }
    }
  }
  return error;
}

bool chratos::change_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
  auto error (false);
  try
  {
    assert (tree_a.get<std::string> ("type") == "change");
    auto previous_l (tree_a.get<std::string> ("previous"));
    auto representative_l (tree_a.get<std::string> ("representative"));
    auto work_l (tree_a.get<std::string> ("work"));
    auto signature_l (tree_a.get<std::string> ("signature"));
    auto dividend_l (tree_a.get<std::string> ("dividend"));
    error = hashables.previous.decode_hex (previous_l);
    if (!error)
    {
      error = hashables.representative.decode_hex (representative_l);
      if (!error)
      {
        error = chratos::from_string_hex (work_l, work);
        if (!error)
        {
          error = signature.decode_hex (signature_l);
          if (!error) {
            error = hashables.dividend.decode_hex (dividend_l);
          }
        }
      }
    }
  }
  catch (std::runtime_error const &)
  {
    error = true;
  }
  return error;
}

void chratos::change_block::visit (chratos::block_visitor & visitor_a) const
{
  visitor_a.change_block (*this);
}

chratos::block_type chratos::change_block::type () const
{
  return chratos::block_type::change;
}

bool chratos::change_block::operator== (chratos::block const & other_a) const
{
  return blocks_equal (*this, other_a);
}

bool chratos::change_block::operator== (chratos::change_block const & other_a) const
{
  return hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && hashables.dividend == other_a.hashables.dividend && work == other_a.work && signature == other_a.signature;
}

bool chratos::change_block::valid_predecessor (chratos::block const & block_a) const
{
  bool result;
  switch (block_a.type ())
  {
    case chratos::block_type::send:
    case chratos::block_type::receive:
    case chratos::block_type::open:
    case chratos::block_type::change:
      result = true;
      break;
    default:
      result = false;
      break;
  }
  return result;
}

chratos::block_hash chratos::change_block::source () const
{
  return 0;
}

chratos::block_hash chratos::change_block::root () const
{
  return hashables.previous;
}

chratos::block_hash chratos::change_block::dividend () const
{
  return hashables.dividend;
}

chratos::account chratos::change_block::representative () const
{
  return hashables.representative;
}

chratos::signature chratos::change_block::block_signature () const
{
  return signature;
}

void chratos::change_block::signature_set (chratos::uint512_union const & signature_a)
{
  signature = signature_a;
}

chratos::state_hashables::state_hashables (chratos::account const & account_a, chratos::block_hash const & previous_a, chratos::account const & representative_a, chratos::amount const & balance_a, chratos::uint256_union const & link_a, chratos::block_hash const & dividend_a) :
account (account_a),
previous (previous_a),
representative (representative_a),
balance (balance_a),
link (link_a),
dividend (dividend_a)
{
}

chratos::state_hashables::state_hashables (bool & error_a, chratos::stream & stream_a)
{
  error_a = chratos::read (stream_a, account);
  if (!error_a)
  {
    error_a = chratos::read (stream_a, previous);
    if (!error_a)
    {
      error_a = chratos::read (stream_a, representative);
      if (!error_a)
      {
        error_a = chratos::read (stream_a, balance);
        if (!error_a)
        {
          error_a = chratos::read (stream_a, link);
          if (!error_a)
          {
            error_a = chratos::read (stream_a, dividend.bytes);
          }
        }
      }
    }
  }
}

chratos::state_hashables::state_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
  try
  {
    auto account_l (tree_a.get<std::string> ("account"));
    auto previous_l (tree_a.get<std::string> ("previous"));
    auto representative_l (tree_a.get<std::string> ("representative"));
    auto balance_l (tree_a.get<std::string> ("balance"));
    auto link_l (tree_a.get<std::string> ("link"));
    auto dividend_l (tree_a.get<std::string> ("dividend"));
    error_a = account.decode_account (account_l);
    if (!error_a)
    {
      error_a = previous.decode_hex (previous_l);
      if (!error_a)
      {
        error_a = representative.decode_account (representative_l);
        if (!error_a)
        {
          error_a = balance.decode_dec (balance_l);
          if (!error_a)
          {
            error_a = link.decode_account (link_l) && link.decode_hex (link_l);
            if (!error_a) 
            {
              error_a = dividend.decode_hex (dividend_l);
            }
          }
        }
      }
    }
  }
  catch (std::runtime_error const &)
  {
    error_a = true;
  }
}

void chratos::state_hashables::hash (blake2b_state & hash_a) const
{
  blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
  blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
  blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
  blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
  blake2b_update (&hash_a, link.bytes.data (), sizeof (link.bytes));
  blake2b_update (&hash_a, dividend.bytes.data (), sizeof (dividend.bytes));
}

chratos::state_block::state_block (chratos::account const & account_a, chratos::block_hash const & previous_a, chratos::account const & representative_a, chratos::amount const & balance_a, chratos::uint256_union const & link_a, chratos::block_hash const & dividend_a, chratos::raw_key const & prv_a, chratos::public_key const & pub_a, uint64_t work_a) :
hashables (account_a, previous_a, representative_a, balance_a, link_a, dividend_a),
signature (chratos::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

chratos::state_block::state_block (bool & error_a, chratos::stream & stream_a) :
hashables (error_a, stream_a)
{
  if (!error_a)
  {
    error_a = chratos::read (stream_a, signature);
    if (!error_a)
    {
      error_a = chratos::read (stream_a, work);
      boost::endian::big_to_native_inplace (work);
    }
  }
}

chratos::state_block::state_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
  if (!error_a)
  {
    try
    {
      auto type_l (tree_a.get<std::string> ("type"));
      auto signature_l (tree_a.get<std::string> ("signature"));
      auto work_l (tree_a.get<std::string> ("work"));
      error_a = type_l != "state";
      if (!error_a)
      {
        error_a = chratos::from_string_hex (work_l, work);
        if (!error_a)
        {
          error_a = signature.decode_hex (signature_l);
        }
      }
    }
    catch (std::runtime_error const &)
    {
      error_a = true;
    }
  }
}

void chratos::state_block::hash (blake2b_state & hash_a) const
{
  chratos::uint256_union preamble (static_cast<uint64_t> (chratos::block_type::state));
  blake2b_update (&hash_a, preamble.bytes.data (), preamble.bytes.size ());
  hashables.hash (hash_a);
}

uint64_t chratos::state_block::block_work () const
{
  return work;
}

void chratos::state_block::block_work_set (uint64_t work_a)
{
  work = work_a;
}

chratos::block_hash chratos::state_block::previous () const
{
  return hashables.previous;
}

void chratos::state_block::serialize (chratos::stream & stream_a) const
{
  write (stream_a, hashables.account);
  write (stream_a, hashables.previous);
  write (stream_a, hashables.representative);
  write (stream_a, hashables.balance);
  write (stream_a, hashables.link);
  write (stream_a, hashables.dividend);
  write (stream_a, signature);
  write (stream_a, boost::endian::native_to_big (work));
}

void chratos::state_block::serialize_json (std::string & string_a) const
{
  boost::property_tree::ptree tree;
  tree.put ("type", "state");
  tree.put ("account", hashables.account.to_account ());
  tree.put ("previous", hashables.previous.to_string ());
  tree.put ("representative", representative ().to_account ());
  tree.put ("balance", hashables.balance.to_string_dec ());
  tree.put ("link", hashables.link.to_string ());
  tree.put ("link_as_account", hashables.link.to_account ());
  tree.put ("dividend", hashables.dividend.to_string());
  std::string signature_l;
  signature.encode_hex (signature_l);
  tree.put ("signature", signature_l);
  tree.put ("work", chratos::to_string_hex (work));
  std::stringstream ostream;
  boost::property_tree::write_json (ostream, tree);
  string_a = ostream.str ();
}

bool chratos::state_block::deserialize (chratos::stream & stream_a)
{
  auto error (read (stream_a, hashables.account));
  if (!error)
  {
    error = read (stream_a, hashables.previous);
    if (!error)
    {
      error = read (stream_a, hashables.representative);
      if (!error)
      {
        error = read (stream_a, hashables.balance);
        if (!error)
        {
          error = read (stream_a, hashables.link);
          if (!error)
          {
            error = read (stream_a, hashables.dividend);
            if (!error)
            {
              error = read (stream_a, signature);
              if (!error)
              {
                error = read (stream_a, work);
                boost::endian::big_to_native_inplace (work);
              }
            }
          }
        }
      }
    }
  }
  return error;
}

bool chratos::state_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
  auto error (false);
  try
  {
    assert (tree_a.get<std::string> ("type") == "state");
    auto account_l (tree_a.get<std::string> ("account"));
    auto previous_l (tree_a.get<std::string> ("previous"));
    auto representative_l (tree_a.get<std::string> ("representative"));
    auto balance_l (tree_a.get<std::string> ("balance"));
    auto link_l (tree_a.get<std::string> ("link"));
    auto dividend_l (tree_a.get<std::string> ("dividend"));
    auto work_l (tree_a.get<std::string> ("work"));
    auto signature_l (tree_a.get<std::string> ("signature"));
    error = hashables.account.decode_account (account_l);
    if (!error)
    {
      error = hashables.previous.decode_hex (previous_l);
      if (!error)
      {
        error = hashables.representative.decode_account (representative_l);
        if (!error)
        {
          error = hashables.balance.decode_dec (balance_l);
          if (!error)
          {
            error = hashables.link.decode_account (link_l) && hashables.link.decode_hex (link_l);
            if (!error)
            {
              error = chratos::from_string_hex (work_l, work);
              if (!error)
              {
                error = signature.decode_hex (signature_l);
                if (!error)
                {
                  error = hashables.dividend.decode_hex (dividend_l);
                }
              }
            }
          }
        }
      }
    }
  }
  catch (std::runtime_error const &)
  {
    error = true;
  }
  return error;
}

void chratos::state_block::visit (chratos::block_visitor & visitor_a) const
{
  visitor_a.state_block (*this);
}

chratos::block_type chratos::state_block::type () const
{
  return chratos::block_type::state;
}

bool chratos::state_block::operator== (chratos::block const & other_a) const
{
  return blocks_equal (*this, other_a);
}

bool chratos::state_block::operator== (chratos::state_block const & other_a) const
{
  return hashables.account == other_a.hashables.account && hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && hashables.balance == other_a.hashables.balance && hashables.link == other_a.hashables.link && hashables.dividend == other_a.hashables.dividend && signature == other_a.signature && work == other_a.work;
}

bool chratos::state_block::valid_predecessor (chratos::block const & block_a) const
{
  return true;
}

chratos::block_hash chratos::state_block::source () const
{
  return 0;
}

chratos::block_hash chratos::state_block::root () const
{
  return !hashables.previous.is_zero () ? hashables.previous : hashables.account;
}

chratos::block_hash chratos::state_block::dividend () const
{
  return hashables.dividend;
}

chratos::account chratos::state_block::representative () const
{
  return hashables.representative;
}

chratos::signature chratos::state_block::block_signature () const
{
  return signature;
}

void chratos::state_block::signature_set (chratos::uint512_union const & signature_a)
{
  signature = signature_a;
}

std::unique_ptr<chratos::block> chratos::deserialize_block_json (boost::property_tree::ptree const & tree_a)
{
  std::unique_ptr<chratos::block> result;
  try
  {
    auto type (tree_a.get<std::string> ("type"));
    if (type == "receive")
    {
      bool error (false);
      std::unique_ptr<chratos::receive_block> obj (new chratos::receive_block (error, tree_a));
      if (!error)
      {
        result = std::move (obj);
      }
    }
    else if (type == "send")
    {
      bool error (false);
      std::unique_ptr<chratos::send_block> obj (new chratos::send_block (error, tree_a));
      if (!error)
      {
        result = std::move (obj);
      }
    }
    else if (type == "open")
    {
      bool error (false);
      std::unique_ptr<chratos::open_block> obj (new chratos::open_block (error, tree_a));
      if (!error)
      {
        result = std::move (obj);
      }
    }
    else if (type == "change")
    {
      bool error (false);
      std::unique_ptr<chratos::change_block> obj (new chratos::change_block (error, tree_a));
      if (!error)
      {
        result = std::move (obj);
      }
    }
    else if (type == "state")
    {
      bool error (false);
      std::unique_ptr<chratos::state_block> obj (new chratos::state_block (error, tree_a));
      if (!error)
      {
        result = std::move (obj);
      }
    }
  }
  catch (std::runtime_error const &)
  {
  }
  return result;
}

std::unique_ptr<chratos::block> chratos::deserialize_block (chratos::stream & stream_a)
{
  chratos::block_type type;
  auto error (read (stream_a, type));
  std::unique_ptr<chratos::block> result;
  if (!error)
  {
    result = chratos::deserialize_block (stream_a, type);
  }
  return result;
}

std::unique_ptr<chratos::block> chratos::deserialize_block (chratos::stream & stream_a, chratos::block_type type_a)
{
  std::unique_ptr<chratos::block> result;
  switch (type_a)
  {
    case chratos::block_type::receive:
    {
      bool error (false);
      std::unique_ptr<chratos::receive_block> obj (new chratos::receive_block (error, stream_a));
      if (!error)
      {
        result = std::move (obj);
      }
      break;
    }
    case chratos::block_type::send:
    {
      bool error (false);
      std::unique_ptr<chratos::send_block> obj (new chratos::send_block (error, stream_a));
      if (!error)
      {
        result = std::move (obj);
      }
      break;
    }
    case chratos::block_type::open:
    {
      bool error (false);
      std::unique_ptr<chratos::open_block> obj (new chratos::open_block (error, stream_a));
      if (!error)
      {
        result = std::move (obj);
      }
      break;
    }
    case chratos::block_type::change:
    {
      bool error (false);
      std::unique_ptr<chratos::change_block> obj (new chratos::change_block (error, stream_a));
      if (!error)
      {
        result = std::move (obj);
      }
      break;
    }
    case chratos::block_type::state:
    {
      bool error (false);
      std::unique_ptr<chratos::state_block> obj (new chratos::state_block (error, stream_a));
      if (!error)
      {
        result = std::move (obj);
      }
      break;
    }
    default:
      assert (false);
      break;
  }
  return result;
}

void chratos::receive_block::visit (chratos::block_visitor & visitor_a) const
{
  visitor_a.receive_block (*this);
}

bool chratos::receive_block::operator== (chratos::receive_block const & other_a) const
{
  auto result (hashables.previous == other_a.hashables.previous && hashables.source == other_a.hashables.source && work == other_a.work && hashables.dividend == other_a.hashables.dividend && signature == other_a.signature);
  return result;
}

bool chratos::receive_block::deserialize (chratos::stream & stream_a)
{
  auto error (false);
  error = read (stream_a, hashables.previous.bytes);
  if (!error)
  {
    error = read (stream_a, hashables.source.bytes);
    if (!error)
    {
      error = read (stream_a, hashables.dividend.bytes);
      if (!error)
      {
        error = read (stream_a, signature.bytes);
        if (!error)
        {
          error = read (stream_a, work);
        }
      }
    }
  }
  return error;
}

bool chratos::receive_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
  auto error (false);
  try
  {
    assert (tree_a.get<std::string> ("type") == "receive");
    auto previous_l (tree_a.get<std::string> ("previous"));
    auto source_l (tree_a.get<std::string> ("source"));
    auto work_l (tree_a.get<std::string> ("work"));
    auto signature_l (tree_a.get<std::string> ("signature"));
    auto dividend_l (tree_a.get<std::string> ("dividend"));
    error = hashables.previous.decode_hex (previous_l);
    if (!error)
    {
      error = hashables.source.decode_hex (source_l);
      if (!error)
      {
        error = chratos::from_string_hex (work_l, work);
        if (!error)
        {
          error = signature.decode_hex (signature_l);
          if (!error)
          {
            error = hashables.dividend.decode_hex (dividend_l);
          }
        }
      }
    }
  }
  catch (std::runtime_error const &)
  {
    error = true;
  }
  return error;
}

void chratos::receive_block::serialize (chratos::stream & stream_a) const
{
  write (stream_a, hashables.previous.bytes);
  write (stream_a, hashables.source.bytes);
  write (stream_a, hashables.dividend.bytes);
  write (stream_a, signature.bytes);
  write (stream_a, work);
}

void chratos::receive_block::serialize_json (std::string & string_a) const
{
  boost::property_tree::ptree tree;
  tree.put ("type", "receive");
  std::string previous;
  hashables.previous.encode_hex (previous);
  tree.put ("previous", previous);
  std::string source;
  hashables.source.encode_hex (source);
  tree.put ("source", source);
  std::string signature_l;
  signature.encode_hex (signature_l);
  tree.put ("dividend", hashables.dividend.to_string());
  tree.put ("work", chratos::to_string_hex (work));
  tree.put ("signature", signature_l);
  std::stringstream ostream;
  boost::property_tree::write_json (ostream, tree);
  string_a = ostream.str ();
}

chratos::receive_block::receive_block (chratos::block_hash const & previous_a, chratos::block_hash const & source_a, chratos::block_hash const & dividend_a, chratos::raw_key const & prv_a, chratos::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, source_a, dividend_a),
signature (chratos::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

chratos::receive_block::receive_block (bool & error_a, chratos::stream & stream_a) :
hashables (error_a, stream_a)
{
  if (!error_a)
  {
    error_a = chratos::read (stream_a, signature);
    if (!error_a)
    {
      error_a = chratos::read (stream_a, work);
    }
  }
}

chratos::receive_block::receive_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
  if (!error_a)
  {
    try
    {
      auto signature_l (tree_a.get<std::string> ("signature"));
      auto work_l (tree_a.get<std::string> ("work"));
      error_a = signature.decode_hex (signature_l);
      if (!error_a)
      {
        error_a = chratos::from_string_hex (work_l, work);
      }
    }
    catch (std::runtime_error const &)
    {
      error_a = true;
    }
  }
}

void chratos::receive_block::hash (blake2b_state & hash_a) const
{
  hashables.hash (hash_a);
}

uint64_t chratos::receive_block::block_work () const
{
  return work;
}

void chratos::receive_block::block_work_set (uint64_t work_a)
{
  work = work_a;
}

bool chratos::receive_block::operator== (chratos::block const & other_a) const
{
  return blocks_equal (*this, other_a);
}

bool chratos::receive_block::valid_predecessor (chratos::block const & block_a) const
{
  bool result;
  switch (block_a.type ())
  {
    case chratos::block_type::send:
    case chratos::block_type::receive:
    case chratos::block_type::open:
    case chratos::block_type::change:
    case chratos::block_type::dividend:
      result = true;
      break;
    default:
      result = false;
      break;
  }
  return result;
}

chratos::block_hash chratos::receive_block::previous () const
{
  return hashables.previous;
}

chratos::block_hash chratos::receive_block::source () const
{
  return hashables.source;
}

chratos::block_hash chratos::receive_block::root () const
{
  return hashables.previous;
}

chratos::block_hash chratos::receive_block::dividend () const
{
  return hashables.dividend;
}

chratos::account chratos::receive_block::representative () const
{
  return 0;
}

chratos::signature chratos::receive_block::block_signature () const
{
  return signature;
}

void chratos::receive_block::signature_set (chratos::uint512_union const & signature_a)
{
  signature = signature_a;
}

chratos::block_type chratos::receive_block::type () const
{
  return chratos::block_type::receive;
}

chratos::receive_hashables::receive_hashables (chratos::block_hash const & previous_a, chratos::block_hash const & source_a, chratos::block_hash const & dividend_a) :
previous (previous_a),
source (source_a),
dividend (dividend_a)
{
}

chratos::receive_hashables::receive_hashables (bool & error_a, chratos::stream & stream_a)
{
  error_a = chratos::read (stream_a, previous.bytes);
  if (!error_a)
  {
    error_a = chratos::read (stream_a, source.bytes);
    if (!error_a)
    {
      error_a = chratos::read (stream_a, dividend.bytes);
    }
  }
}

chratos::receive_hashables::receive_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
  try
  {
    auto previous_l (tree_a.get<std::string> ("previous"));
    auto source_l (tree_a.get<std::string> ("source"));
    auto dividend_l (tree_a.get<std::string> ("dividend"));
    error_a = previous.decode_hex (previous_l);
    if (!error_a)
    {
      error_a = source.decode_hex (source_l);
      if (!error_a) {
        error_a = dividend.decode_hex (dividend_l);
      }
    }
  }
  catch (std::runtime_error const &)
  {
    error_a = true;
  }
}

void chratos::receive_hashables::hash (blake2b_state & hash_a) const
{
  blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
  blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
  blake2b_update (&hash_a, dividend.bytes.data (), sizeof (dividend.bytes));
}

void chratos::dividend_block::visit (chratos::block_visitor & visitor_a) const
{
  visitor_a.dividend_block (*this);
}

void chratos::dividend_block::hash (blake2b_state & hash_a) const
{
  hashables.hash (hash_a);
}

uint64_t chratos::dividend_block::block_work () const
{
  return work;
}

void chratos::dividend_block::block_work_set (uint64_t work_a)
{
  work = work_a;
}

chratos::dividend_hashables::dividend_hashables (chratos::block_hash const & previous_a, chratos::amount const & balance_a, const chratos::block_hash & dividend_a) :
previous (previous_a),
balance (balance_a),
dividend (dividend_a)
{
}

chratos::dividend_hashables::dividend_hashables (bool & error_a, chratos::stream & stream_a)
{
  error_a = chratos::read (stream_a, previous.bytes);
  if (!error_a)
  {
    error_a = chratos::read (stream_a, balance.bytes);
    if (!error_a)
    {
      error_a = chratos::read (stream_a, dividend.bytes);
    }
  }
}

chratos::dividend_hashables::dividend_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
  try
  {
    auto previous_l (tree_a.get<std::string> ("previous"));
    auto balance_l (tree_a.get<std::string> ("balance"));
    auto dividend_l (tree_a.get<std::string> ("dividend"));
    error_a = previous.decode_hex (previous_l);
    if (!error_a)
    {
      error_a = balance.decode_hex (balance_l);
      if (!error_a)
      {
        error_a = dividend.decode_hex (dividend_l);
      }
    }
  }
  catch (std::runtime_error const &)
  {
    error_a = true;
  }
}

void chratos::dividend_hashables::hash (blake2b_state & hash_a) const
{
  auto status (blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes)));
  assert (status == 0);
  status = blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
  assert (status == 0);
  status = blake2b_update (&hash_a, dividend.bytes.data (), sizeof (dividend.bytes));
  assert (status == 0);
}

void chratos::dividend_block::serialize (chratos::stream & stream_a) const
{
  write (stream_a, hashables.previous.bytes);
  write (stream_a, hashables.balance.bytes);
  write (stream_a, hashables.dividend.bytes);
  write (stream_a, signature.bytes);
  write (stream_a, work);
}

void chratos::dividend_block::serialize_json (std::string & string_a) const
{
  boost::property_tree::ptree tree;
  tree.put ("type", "dividend");
  std::string previous;
  hashables.previous.encode_hex (previous);
  tree.put ("previous", previous);
  std::string balance;
  hashables.balance.encode_hex (balance);
  tree.put ("balance", balance);
  std::string signature_l;
  signature.encode_hex (signature_l);
  tree.put ("dividend", hashables.dividend.to_string());
  tree.put ("work", chratos::to_string_hex (work));
  tree.put ("signature", signature_l);
  std::stringstream ostream;
  boost::property_tree::write_json (ostream, tree);
  string_a = ostream.str ();
}

bool chratos::dividend_block::deserialize (chratos::stream & stream_a)
{
  auto error (false);
  error = read (stream_a, hashables.previous.bytes);
  if (!error)
  {
    error = read (stream_a, hashables.balance.bytes);
    if (!error)
    {
      error = read (stream_a, hashables.dividend.bytes);
      if (!error) 
      {
        error = read (stream_a, signature.bytes);
        if (!error)
        {
          error = read (stream_a, work);
        }
      }
    }
  }
  return error;
}

bool chratos::dividend_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
  auto error (false);
  try
  {
    assert (tree_a.get<std::string> ("type") == "dividend");
    auto previous_l (tree_a.get<std::string> ("previous"));
    auto dividend_l (tree_a.get<std::string> ("dividend"));
    auto balance_l (tree_a.get<std::string> ("balance"));
    auto work_l (tree_a.get<std::string> ("work"));
    auto signature_l (tree_a.get<std::string> ("signature"));
    error = hashables.previous.decode_hex (previous_l);
    if (!error)
    {
      error = hashables.balance.decode_hex (balance_l);
      if (!error)
      {
        error = chratos::from_string_hex (work_l, work);
        if (!error) 
        {
          error = signature.decode_hex (signature_l);
          if (!error)
          {
            error = hashables.dividend.decode_hex (dividend_l);
          }
        }
      }
    }
  }
  catch (std::runtime_error const &)
  {
    error = true;
  }
  return error;
}

chratos::dividend_block::dividend_block (chratos::block_hash const & previous_a, chratos::amount const & balance_a, chratos::block_hash const & dividend_a, chratos::raw_key const & prv_a, chratos::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, balance_a, dividend_a),
signature (chratos::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

chratos::dividend_block::dividend_block (bool & error_a, chratos::stream & stream_a) :
hashables (error_a, stream_a)
{
  if (!error_a)
  {
    error_a = chratos::read (stream_a, signature.bytes);
    if (!error_a)
    {
      error_a = chratos::read (stream_a, work);
    }
  }
}

chratos::dividend_block::dividend_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
  if (!error_a)
  {
    try
    {
      auto dividend_l (tree_a.get<std::string> ("dividend"));
      auto signature_l (tree_a.get<std::string> ("signature"));
      auto work_l (tree_a.get<std::string> ("work"));
      error_a = signature.decode_hex (signature_l);
      if (!error_a)
      {
        error_a = chratos::from_string_hex (work_l, work);
        if (!error_a) 
        {
          error_a = hashables.dividend.decode_hex (dividend_l);
        }
      }
    }
    catch (std::runtime_error const &)
    {
      error_a = true;
    }
  }
}

bool chratos::dividend_block::operator== (chratos::block const & other_a) const
{
  return blocks_equal (*this, other_a);
}

bool chratos::dividend_block::valid_predecessor (chratos::block const & block_a) const
{
  bool result;
  switch (block_a.type ())
  {
    case chratos::block_type::send:
    case chratos::block_type::receive:
    case chratos::block_type::open:
    case chratos::block_type::change:
    case chratos::block_type::dividend:
      result = true;
      break;
    default:
      result = false;
      break;
  }
  return result;
}

chratos::block_type chratos::dividend_block::type () const
{
  return chratos::block_type::dividend;
}

bool chratos::dividend_block::operator== (chratos::dividend_block const & other_a) const
{
  auto result (hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance && hashables.dividend == other_a.hashables.dividend && work == other_a.work && signature == other_a.signature);
  return result;
}

chratos::block_hash chratos::dividend_block::previous () const
{
  return hashables.previous;
}

chratos::block_hash chratos::dividend_block::source () const
{
  return 0;
}

chratos::block_hash chratos::dividend_block::root () const
{
  return hashables.previous;
}

chratos::block_hash chratos::dividend_block::dividend () const
{
  return hashables.dividend;
}

chratos::account chratos::dividend_block::representative () const
{
  return 0;
}

chratos::signature chratos::dividend_block::block_signature () const
{
  return signature;
}

void chratos::dividend_block::signature_set (chratos::uint512_union const & signature_a)
{
  signature = signature_a;
}

