//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "ice_port_observer.h"
#include "stun/stun_message.h"

#include <vector>
#include <memory>

#include <rtp_rtcp/rtp_packet.h>
#include <rtp_rtcp/rtcp_packet.h>
#include <physical_port/physical_port_manager.h>

class IcePort : protected PhysicalPortObserver
{
protected:
	// 각각의 client들에 대한 접속 정보를 추적하기 위한 구조체
	struct IcePortInfo
	{
		// client에 연결되어 있는 세션 정보
		std::shared_ptr<SessionInfo> session_info;

		std::shared_ptr<SessionDescription> offer_sdp;
		std::shared_ptr<SessionDescription> peer_sdp;

		std::shared_ptr<ov::Socket> remote;
		ov::SocketAddress address;

		IcePortConnectionState state;

		std::chrono::time_point<std::chrono::system_clock> expire_time;

		void UpdateBindingTime()
		{
			expire_time = std::chrono::system_clock::now() + std::chrono::milliseconds(30 * 1000);
		}

		bool IsExpired() const
		{
			return (std::chrono::system_clock::now() > expire_time);
		}
	};

public:
	IcePort();
	virtual ~IcePort();

	bool Create(ov::SocketType type, const ov::SocketAddress &address);
	bool Close();

	const ov::SocketType GetType() const
	{
		return _physical_port->GetType();
	}

	const ov::SocketAddress &GetAddress() const
	{
		return _physical_port->GetAddress();
	}

	IcePortConnectionState GetState(const std::shared_ptr<SessionInfo> &session_info) const
	{
		OV_ASSERT2(session_info != nullptr);

		auto item = _session_table.find(session_info->GetId());

		if(item == _session_table.end())
		{
			OV_ASSERT(false, "Invalid session_id: %d", session_info->GetId());
			return IcePortConnectionState::Failed;
		}

		return item->second->state;
	}

	ov::String GenerateUfrag();

	bool AddObserver(std::shared_ptr<IcePortObserver> observer);
	bool RemoveObserver(std::shared_ptr<IcePortObserver> observer);
	bool RemoveObservers();

	bool HasObserver() const noexcept
	{
		return (_observers.size() > 0);
	}

	void AddSession(const std::shared_ptr<SessionInfo> &session_info, std::shared_ptr<SessionDescription> offer_sdp, std::shared_ptr<SessionDescription> peer_sdp);
	bool RemoveSession(const std::shared_ptr<SessionInfo> &session_info);

	bool Send(const std::shared_ptr<SessionInfo> &session_info, std::unique_ptr<RtpPacket> packet);
	bool Send(const std::shared_ptr<SessionInfo> &session_info, std::unique_ptr<RtcpPacket> packet);
	bool Send(const std::shared_ptr<SessionInfo> &session_info, const std::shared_ptr<const ov::Data> &data);

	ov::String ToString() const;

protected:
	//--------------------------------------------------------------------
	// Implementation of PhysicalPortObserver
	//--------------------------------------------------------------------
	void OnConnected(const std::shared_ptr<ov::Socket> &remote) override;
	void OnDataReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<const ov::Data> &data) override;
	void OnDisconnected(const std::shared_ptr<ov::Socket> &remote, PhysicalPortDisconnectReason reason, const std::shared_ptr<const ov::Error> &error) override;
	//--------------------------------------------------------------------

	void SetIceState(std::shared_ptr<IcePortInfo> &info, IcePortConnectionState state);

	// STUN 오류를 반환함
	void ResponseError(const std::shared_ptr<ov::Socket> &remote);

private:
	void CheckTimedoutItem();

	// STUN nego order:
	// (State: New)
	// [Server] <-- 1. Binding Request          --- [Player]
	// (State: Checking)
	// [Server] --- 2. Binding Success Response --> [Player]
	// [Server] --- 3. Binding Request          --> [Player]
	// [Server] <-- 4. Binding Success Response --- [Player]
	// (State: Connected)
	bool ProcessBindingRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const StunMessage &request_message);
	bool SendBindingResponse(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const StunMessage &request_message, const std::shared_ptr<IcePortInfo> &info);
	bool SendBindingRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<IcePortInfo> &info);
	bool ProcessBindingResponse(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const StunMessage &response_message);

	std::shared_ptr<PhysicalPort> _physical_port;

	// IcePort로 부터 데이터가 들어오면 이벤트를 받을 옵져버 목록
	std::vector<std::shared_ptr<IcePortObserver>> _observers;

	// STUN binding이 될 때까지 관련 정보를 담고 있는 mapping table
	// binding이 완료되면 이후로는 destination ip & port로 구분하기 때문에 필요 없어짐
	// key: offer ufrag
	// value: IcePortInfo
	std::map<const ov::String, std::shared_ptr<IcePortInfo>> _user_mapping_table;
	std::mutex _user_mapping_table_mutex;

	// STUN nego가 완료되면 생성되는 mapping table

	// 상대방의 ip:port로 IcePortInfo를 바로 찾을 수 있게 함
	// key: SocketAddress
	// value: IcePortInfo
	std::mutex _ice_port_info_mutex;
	std::map<ov::SocketAddress, std::shared_ptr<IcePortInfo>> _ice_port_info;
	// session_id로 IcePortInfo를 바로 찾을 수 있게 함
	std::map<session_id_t, std::shared_ptr<IcePortInfo>> _session_table;

	// 마지막으로 STUN 메시지가 온 시점을 기억함
	ov::DelayQueue _timer;
};
