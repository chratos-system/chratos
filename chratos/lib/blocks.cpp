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
	tree.put ("dividend", hashables.dividend.to_string ());
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

chratos::block_hash chratos::state_block::link () const
{
	return hashables.link;
}

chratos::account chratos::state_block::representative () const
{
	return hashables.representative;
}

chratos::account chratos::state_block::account () const
{
  return hashables.account;
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
		if (type == "state")
		{
			bool error (false);
			std::unique_ptr<chratos::state_block> obj (new chratos::state_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "dividend")
		{
			bool error (false);
			std::unique_ptr<chratos::dividend_block> obj (new chratos::dividend_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "claim")
		{
			bool error (false);
			std::unique_ptr<chratos::claim_block> obj (new chratos::claim_block (error, tree_a));
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
		case chratos::block_type::dividend:
		{
			bool error (false);
			std::unique_ptr<chratos::dividend_block> obj (new chratos::dividend_block (error, stream_a));
			if (!error)
			{
				result = std::move (obj);
			}
			break;
		}
		case chratos::block_type::claim:
		{
			bool error (false);
			std::unique_ptr<chratos::claim_block> obj (new chratos::claim_block (error, stream_a));
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
void chratos::dividend_block::visit (chratos::block_visitor & visitor_a) const
{
	visitor_a.dividend_block (*this);
}

void chratos::dividend_block::hash (blake2b_state & hash_a) const
{
	chratos::uint256_union preamble (static_cast<uint64_t> (chratos::block_type::dividend));
	blake2b_update (&hash_a, preamble.bytes.data (), preamble.bytes.size ());
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

chratos::dividend_hashables::dividend_hashables (chratos::account const & account_a, chratos::block_hash const & previous_a, chratos::account const & representative_a, chratos::amount const & balance_a, const chratos::block_hash & dividend_a) :
account (account_a),
previous (previous_a),
representative (representative_a),
balance (balance_a),
dividend (dividend_a)
{
}

chratos::dividend_hashables::dividend_hashables (bool & error_a, chratos::stream & stream_a)
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
					error_a = chratos::read (stream_a, dividend);
				}
			}
		}
	}
}

chratos::dividend_hashables::dividend_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
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
						error_a = dividend.decode_hex (dividend_l);
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

void chratos::dividend_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	blake2b_update (&hash_a, dividend.bytes.data (), sizeof (dividend.bytes));
}

void chratos::dividend_block::serialize (chratos::stream & stream_a) const
{
	write (stream_a, hashables.account);
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.balance);
	write (stream_a, hashables.dividend);
	write (stream_a, signature);
	write (stream_a, boost::endian::native_to_big (work));
}

void chratos::dividend_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	tree.put ("type", "dividend");
	tree.put ("account", hashables.account.to_account ());
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("balance", hashables.balance.to_string_dec ());
	tree.put ("dividend", hashables.dividend.to_string ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
	tree.put ("work", chratos::to_string_hex (work));
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

bool chratos::dividend_block::deserialize (chratos::stream & stream_a)
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
	return error;
}

bool chratos::dividend_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "dividend");
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
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
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

chratos::dividend_block::dividend_block (chratos::account const & account_a, chratos::block_hash const & previous_a, chratos::account const & representative_a, chratos::amount const & balance_a, chratos::block_hash const & dividend_a, chratos::raw_key const & prv_a, chratos::public_key const & pub_a, uint64_t work_a) :
hashables (account_a, previous_a, representative_a, balance_a, dividend_a),
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
			boost::endian::big_to_native_inplace (work);
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

bool chratos::dividend_block::operator== (chratos::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool chratos::dividend_block::valid_predecessor (chratos::block const & block_a) const
{
	return true;
}

chratos::block_type chratos::dividend_block::type () const
{
	return chratos::block_type::dividend;
}

bool chratos::dividend_block::operator== (chratos::dividend_block const & other_a) const
{
	auto result (hashables.account == other_a.hashables.account && hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance && hashables.dividend == other_a.hashables.dividend && work == other_a.work && signature == other_a.signature);
	return result;
}

chratos::block_hash chratos::dividend_block::previous () const
{
	return hashables.previous;
}

chratos::block_hash chratos::dividend_block::source () const
{
	return hashables.account;
}

chratos::block_hash chratos::dividend_block::root () const
{
	return hashables.previous;
}

chratos::block_hash chratos::dividend_block::dividend () const
{
	return hashables.dividend;
}

chratos::block_hash chratos::dividend_block::link () const
{
	return 0;
}

chratos::account chratos::dividend_block::representative () const
{
	return hashables.representative;
}

chratos::account chratos::dividend_block::account () const
{
  return hashables.account;
}

chratos::signature chratos::dividend_block::block_signature () const
{
	return signature;
}

void chratos::dividend_block::signature_set (chratos::uint512_union const & signature_a)
{
	signature = signature_a;
}

void chratos::claim_block::visit (chratos::block_visitor & visitor_a) const
{
	visitor_a.claim_block (*this);
}

void chratos::claim_block::hash (blake2b_state & hash_a) const
{
	chratos::uint256_union preamble (static_cast<uint64_t> (chratos::block_type::claim));
	blake2b_update (&hash_a, preamble.bytes.data (), preamble.bytes.size ());
	hashables.hash (hash_a);
}

uint64_t chratos::claim_block::block_work () const
{
	return work;
}

void chratos::claim_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

chratos::claim_hashables::claim_hashables (chratos::account const & account_a, chratos::block_hash const & previous_a, chratos::account const & representative_a, chratos::amount const & balance_a, const chratos::block_hash & dividend_a) :
account (account_a),
previous (previous_a),
representative (representative_a),
balance (balance_a),
dividend (dividend_a)
{
}

chratos::claim_hashables::claim_hashables (bool & error_a, chratos::stream & stream_a)
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
					error_a = chratos::read (stream_a, dividend);
				}
			}
		}
	}
}

chratos::claim_hashables::claim_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
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
						error_a = dividend.decode_hex (dividend_l);
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

void chratos::claim_hashables::hash (blake2b_state & hash_a) const
{
	auto status (blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes)));
	assert (status == 0);
	status = blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	assert (status == 0);
	status = blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	assert (status == 0);
	status = blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	assert (status == 0);
	status = blake2b_update (&hash_a, dividend.bytes.data (), sizeof (dividend.bytes));
	assert (status == 0);
}

void chratos::claim_block::serialize (chratos::stream & stream_a) const
{
	write (stream_a, hashables.account);
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.balance);
	write (stream_a, hashables.dividend);
	write (stream_a, signature);
	write (stream_a, boost::endian::native_to_big (work));
}

void chratos::claim_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	tree.put ("type", "claim");
	tree.put ("account", hashables.account.to_account ());
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("balance", hashables.balance.to_string_dec ());
	tree.put ("dividend", hashables.dividend.to_string ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
	tree.put ("work", chratos::to_string_hex (work));
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

bool chratos::claim_block::deserialize (chratos::stream & stream_a)
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
	return error;
}

bool chratos::claim_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "dividend");
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
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
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

chratos::claim_block::claim_block (chratos::account const & account_a, chratos::block_hash const & previous_a, chratos::account const & representative_a, chratos::amount const & balance_a, chratos::block_hash const & dividend_a, chratos::raw_key const & prv_a, chratos::public_key const & pub_a, uint64_t work_a) :
hashables (account_a, previous_a, representative_a, balance_a, dividend_a),
signature (chratos::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

chratos::claim_block::claim_block (bool & error_a, chratos::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		error_a = chratos::read (stream_a, signature.bytes);
		if (!error_a)
		{
			error_a = chratos::read (stream_a, work);
			boost::endian::big_to_native_inplace (work);
		}
	}
}

chratos::claim_block::claim_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
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

bool chratos::claim_block::operator== (chratos::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool chratos::claim_block::valid_predecessor (chratos::block const & block_a) const
{
	return true;
}

chratos::block_type chratos::claim_block::type () const
{
	return chratos::block_type::claim;
}

bool chratos::claim_block::operator== (chratos::claim_block const & other_a) const
{
	auto result (hashables.account == other_a.hashables.account && hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance && hashables.dividend == other_a.hashables.dividend && work == other_a.work && signature == other_a.signature);
	return result;
}

chratos::block_hash chratos::claim_block::previous () const
{
	return hashables.previous;
}

chratos::block_hash chratos::claim_block::source () const
{
	return hashables.account;
}

chratos::block_hash chratos::claim_block::root () const
{
	return hashables.previous;
}

chratos::block_hash chratos::claim_block::dividend () const
{
	return hashables.dividend;
}

chratos::block_hash chratos::claim_block::link () const
{
	return 0;
}

chratos::account chratos::claim_block::representative () const
{
	return hashables.representative;
}

chratos::account chratos::claim_block::account () const
{
  return hashables.account;
}

chratos::signature chratos::claim_block::block_signature () const
{
	return signature;
}

void chratos::claim_block::signature_set (chratos::uint512_union const & signature_a)
{
	signature = signature_a;
}
