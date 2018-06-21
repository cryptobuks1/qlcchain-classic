
#include <rai/node/common.hpp>

#include <rai/lib/work.hpp>
#include <rai/node/wallet.hpp>

std::array<uint8_t, 2> constexpr rai::message_header::magic_number;
size_t constexpr rai::message_header::ipv4_only_position;
size_t constexpr rai::message_header::bootstrap_server_position;
std::bitset<16> constexpr rai::message_header::block_type_mask;

rai::message_header::message_header (rai::message_type type_a) :
version_max (rai::protocol_version),
version_using (rai::protocol_version),
version_min (rai::protocol_version_min),
type (type_a)
{
}

rai::message_header::message_header (bool & error_a, rai::stream & stream_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void rai::message_header::serialize (rai::stream & stream_a)
{
	rai::write (stream_a, rai::message_header::magic_number);
	rai::write (stream_a, version_max);
	rai::write (stream_a, version_using);
	rai::write (stream_a, version_min);
	rai::write (stream_a, type);
	rai::write (stream_a, static_cast<uint16_t> (extensions.to_ullong ()));
}

bool rai::message_header::deserialize (rai::stream & stream_a)
{
	uint16_t extensions_l;
	std::array<uint8_t, 2> magic_number_l;
	auto result (rai::read (stream_a, magic_number_l));
	result = result || magic_number_l != magic_number;
	result = result || rai::read (stream_a, version_max);
	result = result || rai::read (stream_a, version_using);
	result = result || rai::read (stream_a, version_min);
	result = result || rai::read (stream_a, type);
	result = result || rai::read (stream_a, extensions_l);
	if (!result)
	{
		extensions = extensions_l;
	}
	return result;
}

rai::message::message (rai::message_type type_a) :
header (type_a)
{
}

rai::message::message (rai::message_header const & header_a) :
header (header_a)
{
}

rai::block_type rai::message_header::block_type () const
{
	return static_cast<rai::block_type> (((extensions & block_type_mask) >> 8).to_ullong ());
}

void rai::message_header::block_type_set (rai::block_type type_a)
{
	extensions &= ~block_type_mask;
	extensions |= std::bitset<16> (static_cast<unsigned long long> (type_a) << 8);
}

bool rai::message_header::ipv4_only ()
{
	return extensions.test (ipv4_only_position);
}

void rai::message_header::ipv4_only_set (bool value_a)
{
	extensions.set (ipv4_only_position, value_a);
}

rai::message_parser::message_parser (rai::message_visitor & visitor_a, rai::work_pool & pool_a) :
visitor (visitor_a),
pool (pool_a),
status (parse_status::success)
{
}

void rai::message_parser::deserialize_buffer (uint8_t const * buffer_a, size_t size_a)
{
	status = parse_status::success;
	rai::bufferstream stream (buffer_a, size_a);
	auto error (false);
	rai::message_header header (error, stream);
	if (!error)
	{
		switch (header.type)
		{
			case rai::message_type::keepalive:
			{
				deserialize_keepalive (stream, header);
				break;
			}
			case rai::message_type::publish:
			{
				deserialize_publish (stream, header);
				break;
			}
			case rai::message_type::confirm_req:
			{
				deserialize_confirm_req (stream, header);
				break;
			}
			case rai::message_type::confirm_ack:
			{
				deserialize_confirm_ack (stream, header);
				break;
			}
			default:
			{
				status = parse_status::invalid_message_type;
				break;
			}
		}
	}
	else
	{
		status = parse_status::invalid_header;
	}
}

void rai::message_parser::deserialize_keepalive (rai::stream & stream_a, rai::message_header const & header_a)
{
	auto error (false);
	rai::keepalive incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		visitor.keepalive (incoming);
	}
	else
	{
		status = parse_status::invalid_keepalive_message;
	}
}

void rai::message_parser::deserialize_publish (rai::stream & stream_a, rai::message_header const & header_a)
{
	auto error (false);
	rai::publish incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		if (!rai::work_validate (*incoming.block))
		{
			visitor.publish (incoming);
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_publish_message;
	}
}

void rai::message_parser::deserialize_confirm_req (rai::stream & stream_a, rai::message_header const & header_a)
{
	auto error (false);
	rai::confirm_req incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		if (!rai::work_validate (*incoming.block))
		{
			visitor.confirm_req (incoming);
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_confirm_req_message;
	}
}

void rai::message_parser::deserialize_confirm_ack (rai::stream & stream_a, rai::message_header const & header_a)
{
	auto error (false);
	rai::confirm_ack incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		if (!rai::work_validate (*incoming.vote->block))
		{
			visitor.confirm_ack (incoming);
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_confirm_ack_message;
	}
}

bool rai::message_parser::at_end (rai::stream & stream_a)
{
	uint8_t junk;
	auto end (rai::read (stream_a, junk));
	return end;
}

rai::keepalive::keepalive () :
message (rai::message_type::keepalive)
{
	rai::endpoint endpoint (boost::asio::ip::address_v6{}, 0);
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
	{
		*i = endpoint;
	}
}

rai::keepalive::keepalive (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void rai::keepalive::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.keepalive (*this);
}

void rai::keepalive::serialize (rai::stream & stream_a)
{
	header.serialize (stream_a);
	for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
	{
		assert (i->address ().is_v6 ());
		auto bytes (i->address ().to_v6 ().to_bytes ());
		write (stream_a, bytes);
		write (stream_a, i->port ());
	}
}

bool rai::keepalive::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::keepalive);
	auto error (false);
	for (auto i (peers.begin ()), j (peers.end ()); i != j && !error; ++i)
	{
		std::array<uint8_t, 16> address;
		uint16_t port;
		if (!read (stream_a, address) && !read (stream_a, port))
		{
			*i = rai::endpoint (boost::asio::ip::address_v6 (address), port);
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool rai::keepalive::operator== (rai::keepalive const & other_a) const
{
	return peers == other_a.peers;
}

rai::publish::publish (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

rai::publish::publish (std::shared_ptr<rai::block> block_a) :
message (rai::message_type::publish),
block (block_a)
{
	header.block_type_set (block->type ());
}

bool rai::publish::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::publish);
	block = rai::deserialize_block (stream_a, header.block_type ());
	auto result (block == nullptr);
	return result;
}

void rai::publish::serialize (rai::stream & stream_a)
{
	assert (block != nullptr);
	header.serialize (stream_a);
	block->serialize (stream_a);
}

void rai::publish::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.publish (*this);
}

bool rai::publish::operator== (rai::publish const & other_a) const
{
	return *block == *other_a.block;
}

rai::confirm_req::confirm_req (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

rai::confirm_req::confirm_req (std::shared_ptr<rai::block> block_a) :
message (rai::message_type::confirm_req),
block (block_a)
{
	header.block_type_set (block->type ());
}

bool rai::confirm_req::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::confirm_req);
	block = rai::deserialize_block (stream_a, header.block_type ());
	auto result (block == nullptr);
	return result;
}

void rai::confirm_req::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.confirm_req (*this);
}

void rai::confirm_req::serialize (rai::stream & stream_a)
{
	assert (block != nullptr);
	header.serialize (stream_a);
	block->serialize (stream_a);
}

bool rai::confirm_req::operator== (rai::confirm_req const & other_a) const
{
	return *block == *other_a.block;
}

rai::confirm_ack::confirm_ack (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a),
vote (std::make_shared<rai::vote> (error_a, stream_a, header.block_type ()))
{
}

rai::confirm_ack::confirm_ack (std::shared_ptr<rai::vote> vote_a) :
message (rai::message_type::confirm_ack),
vote (vote_a)
{
	header.block_type_set (vote->block->type ());
}

bool rai::confirm_ack::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::confirm_ack);
	auto result (vote->deserialize (stream_a));
	return result;
}

void rai::confirm_ack::serialize (rai::stream & stream_a)
{
	assert (header.block_type () == rai::block_type::send || header.block_type () == rai::block_type::receive || header.block_type () == rai::block_type::open || header.block_type () == rai::block_type::change || header.block_type () == rai::block_type::state);
	header.serialize (stream_a);
	vote->serialize (stream_a, header.block_type ());
}

bool rai::confirm_ack::operator== (rai::confirm_ack const & other_a) const
{
	auto result (*vote == *other_a.vote);
	return result;
}

void rai::confirm_ack::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.confirm_ack (*this);
}

rai::frontier_req::frontier_req () :
message (rai::message_type::frontier_req)
{
}

rai::frontier_req::frontier_req (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

bool rai::frontier_req::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::frontier_req);
	auto result (read (stream_a, start.bytes));
	if (!result)
	{
		result = read (stream_a, age);
		if (!result)
		{
			result = read (stream_a, count);
		}
	}
	return result;
}

void rai::frontier_req::serialize (rai::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, start.bytes);
	write (stream_a, age);
	write (stream_a, count);
}

void rai::frontier_req::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.frontier_req (*this);
}

bool rai::frontier_req::operator== (rai::frontier_req const & other_a) const
{
	return start == other_a.start && age == other_a.age && count == other_a.count;
}

rai::bulk_pull::bulk_pull () :
message (rai::message_type::bulk_pull)
{
}

rai::bulk_pull::bulk_pull (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void rai::bulk_pull::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull (*this);
}

bool rai::bulk_pull::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::bulk_pull);
	auto result (read (stream_a, start));
	if (!result)
	{
		result = read (stream_a, end);
	}
	return result;
}

//QLINK
rai::smart_contract_req::smart_contract_req () :
message (rai::message_type::smart_contract_req)
{
}

//QLINK
bool rai::smart_contract_req::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
	assert (rai::message_type::smart_contract_req == type);
	if (!result)
	{
		assert (type == rai::message_type::smart_contract_req);
		result = read (stream_a, token_type);
	}
	return result;
}

void rai::smart_contract_req::serialize (rai::stream & stream_a)
{
	write_header (stream_a);
	write (stream_a, token_type);
}

void rai::smart_contract_req::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.smart_contract_req (*this);
}

rai::smart_contract_msg::smart_contract_msg () :
message (rai::message_type::smart_contract)
{
}

rai::smart_contract_msg::smart_contract_msg (std::shared_ptr<rai::smart_contract_block> block_a) :
message (rai::message_type::smart_contract),
len (sizeof (rai::account) + sizeof (rai::account) + block_a->hashables.abi_length.number () + sizeof (rai::amount) + sizeof (rai::block_hash) + sizeof (rai::signature) + sizeof (uint64_t)),
smart_contract (block_a)
{
	block_type_set (smart_contract->type ());
}

bool rai::smart_contract_msg::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
	assert (type == rai::message_type::smart_contract);
	if (!result)
	{
		//assert (type == rai::message_type::smart_contract);
		result = read (stream_a, len);
		if (!result)
		{
			//QLINK
			auto block (rai::deserialize_block (stream_a, block_type ()));
			smart_contract.reset (static_cast<rai::smart_contract_block *> (block.release ()));
			result = smart_contract == nullptr;
		}
	}
	return result;
}

void rai::smart_contract_msg::serialize (rai::stream & stream_a)
{
	assert (block_type () == rai::block_type::smart_contract);
	write_header (stream_a);
	write (stream_a, len);
	smart_contract->serialize (stream_a);
}

void rai::smart_contract_msg::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.smart_contract (*this);
}

rai::smart_contract_ack::smart_contract_ack (smart_contract_result const & result_a) :
message (rai::message_type::smart_contract_ack)
{
	result = result_a;
}

bool rai::smart_contract_ack::deserialize (rai::stream & stream_a)
{
	auto result_1 (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result_1);
	assert (rai::message_type::smart_contract_ack == type);
	if (!result_1)
	{
		assert (type == rai::message_type::smart_contract_ack);
		result_1 = read (stream_a, result);
	}
	return result_1;
}

void rai::smart_contract_ack::serialize (rai::stream & stream_a)
{
	write_header (stream_a);
	write (stream_a, result);
}

void rai::smart_contract_ack::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.smart_contract_ack (*this);
}

bool rai::smart_contract_ack::operator== (rai::smart_contract_ack const & other_a) const
{
	return result == other_a.result;
}

void rai::bulk_pull::serialize (rai::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, start);
	write (stream_a, end);
}

rai::bulk_pull_blocks::bulk_pull_blocks () :
message (rai::message_type::bulk_pull_blocks)
{
}

rai::bulk_pull_blocks::bulk_pull_blocks (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void rai::bulk_pull_blocks::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull_blocks (*this);
}

bool rai::bulk_pull_blocks::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::bulk_pull_blocks);
	auto result (read (stream_a, min_hash));
	if (!result)
	{
		result = read (stream_a, max_hash);
		if (!result)
		{
			result = read (stream_a, mode);
			if (!result)
			{
				result = read (stream_a, max_count);
			}
		}
	}
	return result;
}

void rai::bulk_pull_blocks::serialize (rai::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, min_hash);
	write (stream_a, max_hash);
	write (stream_a, mode);
	write (stream_a, max_count);
}

rai::bulk_push::bulk_push () :
message (rai::message_type::bulk_push)
{
}

rai::bulk_push::bulk_push (rai::message_header const & header_a) :
message (header_a)
{
}

bool rai::bulk_push::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::bulk_push);
	return false;
}

void rai::bulk_push::serialize (rai::stream & stream_a)
{
	header.serialize (stream_a);
}

void rai::bulk_push::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.bulk_push (*this);
}

rai::message_visitor::~message_visitor ()
{
}
