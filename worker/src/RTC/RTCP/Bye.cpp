#define MS_CLASS "RTC::RTCP::Bye"
// #define MS_LOG_DEV

#include "RTC/RTCP/Bye.h"
#include "Utils.h"
#include "Logger.h"
#include <cstring>

namespace RTC { namespace RTCP
{
	/* Class methods. */

	ByePacket* ByePacket::Parse(const uint8_t* data, size_t len)
	{
		MS_TRACE();

		// Get the header.
		Packet::CommonHeader* header = (Packet::CommonHeader*)data;

		std::auto_ptr<ByePacket> packet(new ByePacket());

		size_t offset = sizeof(Packet::CommonHeader);

		uint8_t count = header->count;
		while ((count--) && (len - offset > 0))
		{
			if (sizeof(uint32_t) > len-offset)
			{
				MS_WARN_TAG(rtcp, "not enough space for SSRC in RTCP Bye message");

				return nullptr;
			}

			packet->AddSsrc(Utils::Byte::Get4Bytes(data+offset, 0));
			offset += sizeof(uint32_t);
		}

		if (count == 0)
		{
			size_t length = size_t(Utils::Byte::Get1Byte(data+offset, 0));
			if (sizeof(uint8_t) + length <= len-offset)
			{
				packet->SetReason(std::string((char*)data+offset, length));
			}
		}

		return packet.release();
	}

	/* Instance methods. */

	size_t ByePacket::Serialize(uint8_t* data)
	{
		MS_TRACE();

		size_t offset = Packet::Serialize(data);

		for (auto ssrc : this->ssrcs)
		{
			std::memcpy(data, &ssrc, sizeof(uint32_t));
			offset += sizeof(uint32_t);
		}

		if (!this->reason.empty())
		{
			Utils::Byte::Set1Byte(data, offset, this->reason.length());
			offset += sizeof(uint8_t); // Length field.
			offset += this->reason.length();
		}

		// 32 bits padding.
		size_t padding = (-offset) & 3;
		for (size_t i = 0; i < padding; i++)
		{
			data[offset+i] = 0;
		}

		return offset+padding;
	}

	void ByePacket::Dump()
	{
		#ifdef MS_LOG_DEV

		MS_TRACE();

		MS_DEBUG_DEV("<ByePacket>");
		for (auto ssrc : this->ssrcs)
		{
			MS_DEBUG_DEV("  ssrc   : %" PRIu32, ssrc);
		}
		if (!this->reason.empty())
			MS_DEBUG_DEV("  reason : %s", this->reason.c_str());
		MS_DEBUG_DEV("</ByePacket>");

		#endif
	}
}}
