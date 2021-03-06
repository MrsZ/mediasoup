#ifndef MS_RTC_ROOM_H
#define MS_RTC_ROOM_H

#include "common.h"
#include "RTC/RtpDictionaries.h"
#include "RTC/Peer.h"
#include "RTC/RtpReceiver.h"
#include "RTC/RtpSender.h"
#include "RTC/RtpPacket.h"
#include "Channel/Request.h"
#include "Channel/Notifier.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <json/json.h>

namespace RTC
{
	class Room :
		public RTC::Peer::Listener
	{
	public:
		class Listener
		{
		public:
			virtual void onRoomClosed(RTC::Room* room) = 0;
		};

	public:
		static void ClassInit();

	private:
		static RTC::RtpCapabilities supportedCapabilities;
		static std::vector<uint8_t> availablePayloadTypes;

	public:
		Room(Listener* listener, Channel::Notifier* notifier, uint32_t roomId, Json::Value& data);
		virtual ~Room();

		void Close();
		Json::Value toJson();
		void HandleRequest(Channel::Request* request);
		RTC::RtpCapabilities& GetCapabilities();

	private:
		RTC::Peer* GetPeerFromRequest(Channel::Request* request, uint32_t* peerId = nullptr);
		void SetCapabilities(std::vector<RTC::RtpCodecParameters>& mediaCodecs);

	/* Pure virtual methods inherited from RTC::Peer::Listener. */
	public:
		virtual void onPeerClosed(RTC::Peer* peer) override;
		virtual void onPeerCapabilities(RTC::Peer* peer, RTC::RtpCapabilities* capabilities) override;
		virtual void onPeerRtpReceiverParameters(RTC::Peer* peer, RTC::RtpReceiver* rtpReceiver) override;
		virtual void onPeerRtpReceiverClosed(RTC::Peer* peer, RTC::RtpReceiver* rtpReceiver) override;
		virtual void onPeerRtpSenderClosed(RTC::Peer* peer, RTC::RtpSender* rtpSender) override;
		virtual void onPeerRtpPacket(RTC::Peer* peer, RTC::RtpReceiver* rtpReceiver, RTC::RtpPacket* packet) override;
		virtual void onPeerRtcpReceiverReport(RTC::Peer* peer, RTC::RtpSender* rtpSender, RTC::RTCP::ReceiverReport* report) override;
		virtual void onPeerRtcpSenderReport(RTC::Peer* peer, RTC::RtpReceiver* rtpReceiver, RTC::RTCP::SenderReport* report) override;
		virtual void onPeerRtcpSdesChunk(RTC::Peer* peer, RTC::RtpReceiver* rtpReceiver, RTC::RTCP::SdesChunk* chunk) override;
		virtual void onPeerRtcpFeedback(RTC::Peer* peer, RTC::RtpSender* rtpSender, RTC::RTCP::FeedbackPsPacket* packet) override;
		virtual void onPeerRtcpFeedback(RTC::Peer* peer, RTC::RtpSender* rtpSender, RTC::RTCP::FeedbackRtpPacket* packet) override;
		virtual void onPeerRtcpCompleted(RTC::Peer* peer) override;

	public:
		// Passed by argument.
		uint32_t roomId;

	private:
		// Passed by argument.
		Listener* listener = nullptr;
		Channel::Notifier* notifier = nullptr;
		// Others.
		RTC::RtpCapabilities capabilities;
		std::unordered_map<uint32_t, RTC::Peer*> peers;
		std::unordered_map<RTC::RtpReceiver*, std::unordered_set<RTC::RtpSender*>> mapRtpReceiverRtpSenders;
		std::unordered_map<RTC::RtpSender*, RTC::RtpReceiver*> mapRtpSenderRtpReceiver;
		std::vector<RTC::RtpPacket*> rtpRetransmissionContainer;
	};

	/* Inline static methods. */

	inline
	RTC::RtpCapabilities& Room::GetCapabilities()
	{
		return this->capabilities;
	}
}

#endif
