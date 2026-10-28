/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2020 Bart Trzynadlowski, Nik Henson, Ian Curtis,
 **                     Harry Tuttle, and Spindizzi
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include "TCPReceive.h"
#include "OSD/Logger.h"
#include "OSD/Thread.h"

#if defined(SUPERMODEL_PLATFORM_SDL3)

#include "NativeSocket.h"

#include <cstdint>

namespace
{
	SocketHandle HandleFromVoid(void* socket)
	{
		return static_cast<SocketHandle>(reinterpret_cast<intptr_t>(socket));
	}

	void* VoidFromHandle(SocketHandle socket)
	{
		return reinterpret_cast<void*>(static_cast<intptr_t>(socket));
	}

	// A packet larger than this is not one of ours; refusing it keeps a corrupt or hostile length
	// word from asking for gigabytes of memory.
	constexpr int kMaxPacketBytes = 16 * 1024 * 1024;

	bool ReceiveAll(SocketHandle socket, void* buffer, int size)
	{
		char* bytes = static_cast<char*>(buffer);
		while (size > 0)
		{
			const int received = NetSocket::ReceiveSome(socket, bytes, size);
			if (received <= 0)
				return false;
			bytes += received;
			size -= received;
		}
		return true;
	}
}

TCPReceive::TCPReceive(int port) :
	m_listenSocket(nullptr),
	m_receiveSocket(nullptr),
	m_socketSet(nullptr),
	m_running(false)
{
	if (!NetSocket::Startup())
	{
		ErrorLog("Net board: socket start-up failed (%d).", NetSocket::LastError());
		return;
	}

	const SocketHandle listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener == kInvalidSocket)
	{
		ErrorLog("Net board: could not create the listening socket (%d).", NetSocket::LastError());
		NetSocket::Cleanup();
		return;
	}

	// Rebind immediately after a restart instead of waiting for TIME_WAIT to expire.
	int reuse = 1;
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
		static_cast<SocketLength>(sizeof(reuse)));

	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(static_cast<unsigned short>(port));

	if (bind(listener, reinterpret_cast<const sockaddr*>(&address), static_cast<SocketLength>(sizeof(address))) != 0 ||
		listen(listener, SOMAXCONN) != 0)
	{
		ErrorLog("Net board: could not listen on port %d (%d).", port, NetSocket::LastError());
		NetSocket::Close(listener);
		NetSocket::Cleanup();
		return;
	}

	// The accept loop polls, so the listener must not block it (and through it, shutdown).
	NetSocket::SetNonBlocking(listener, true);

	m_listenSocket = VoidFromHandle(listener);
	m_running = true;
	m_listenThread = std::thread(&TCPReceive::ListenFunc, this);
}

TCPReceive::~TCPReceive()
{
	m_running = false;

	if (m_listenThread.joinable())
		m_listenThread.join();

	if (m_receiveSocket)
	{
		NetSocket::Close(HandleFromVoid(m_receiveSocket.load()));
		m_receiveSocket = nullptr;
	}

	if (m_listenSocket)
	{
		NetSocket::Close(HandleFromVoid(m_listenSocket));
		m_listenSocket = nullptr;
	}

	NetSocket::Cleanup();
}

bool TCPReceive::CheckDataAvailable(int timeoutMS)
{
	if (!m_receiveSocket)
		return false;

	const SocketHandle socket = HandleFromVoid(m_receiveSocket.load());

	fd_set readSet;
	FD_ZERO(&readSet);
	FD_SET(socket, &readSet);

	// timeoutMS -1 waits forever, 0 polls, 1+ is a millisecond budget - the same contract the
	// SDL_net implementation exposes.
	timeval timeout = {};
	timeval* timeoutPtr = nullptr;
	if (timeoutMS >= 0)
	{
		timeout.tv_sec = timeoutMS / 1000;
		timeout.tv_usec = (timeoutMS % 1000) * 1000;
		timeoutPtr = &timeout;
	}

	const int result = select(static_cast<int>(socket) + 1, &readSet, nullptr, nullptr, timeoutPtr);
	return result > 0;
}

std::vector<char>& TCPReceive::Receive()
{
	if (!m_receiveSocket)
	{
		m_recBuffer.clear();
		return m_recBuffer;
	}

	const SocketHandle socket = HandleFromVoid(m_receiveSocket.load());

	int size = 0;
	if (!ReceiveAll(socket, &size, (int)sizeof(size)) || size < 0 || size > kMaxPacketBytes)
	{
		NetSocket::Close(socket);
		m_receiveSocket = nullptr;
		m_recBuffer.clear();
		return m_recBuffer;
	}

	m_recBuffer.resize(static_cast<size_t>(size));
	if (size > 0 && !ReceiveAll(socket, m_recBuffer.data(), size))
	{
		NetSocket::Close(socket);
		m_receiveSocket = nullptr;
		m_recBuffer.clear();
	}

	return m_recBuffer;
}

void TCPReceive::ListenFunc()
{
	const SocketHandle listener = HandleFromVoid(m_listenSocket);

	while (m_running)
	{
		CThread::Sleep(16);
		if (m_receiveSocket)
			continue;

		sockaddr_storage peer = {};
		SocketLength peerLength = static_cast<SocketLength>(sizeof(peer));
		const SocketHandle accepted = accept(listener, reinterpret_cast<sockaddr*>(&peer), &peerLength);

		if (accepted == kInvalidSocket)
		{
			if (!NetSocket::WouldBlock() && m_running)
				ErrorLog("Net board: accept failed (%d).", NetSocket::LastError());
			continue;
		}

		// The receiver is a single-peer link: a new connection replaces the old one.
		if (m_receiveSocket)
			NetSocket::Close(HandleFromVoid(m_receiveSocket.load()));

		/*
		 * Windows makes an accepted socket inherit the listener's non-blocking mode. The receive
		 * side wants blocking reads: Receive() reads a length word and then exactly that many
		 * bytes, and a packet split across two segments must not be mistaken for a dead peer.
		 */
		NetSocket::SetNonBlocking(accepted, false);
		NetSocket::SetNoDelay(accepted);
		m_receiveSocket = VoidFromHandle(accepted);
		InfoLog("Net board: accepted a connection.");
	}
}

bool TCPReceive::Connected()
{
	return m_receiveSocket != nullptr;
}

#else

#include "SDLIncludes.h"

#if defined(_DEBUG)
#include <cstdio>
#define DPRINTF DebugLog
#else
#define DPRINTF(a, ...)
#endif

TCPReceive::TCPReceive(int port) :
	m_listenSocket(nullptr),
	m_receiveSocket(nullptr),
	m_socketSet(nullptr)
{
	SDLNet_Init();

	m_socketSet = SDLNet_AllocSocketSet(1);

	IPaddress ip;
	int result = SDLNet_ResolveHost(&ip, nullptr, port);

	if (result == 0) {
		m_listenSocket = SDLNet_TCP_Open(&ip);
		if (m_listenSocket) {
			m_running = true;
			m_listenThread = std::thread(&TCPReceive::ListenFunc, this);
		}
	}
}

TCPReceive::~TCPReceive()
{
	m_running = false;

	if (m_listenThread.joinable()) {
		m_listenThread.join();
	}

	if (m_listenSocket) {
		SDLNet_TCP_Close(static_cast<TCPsocket>(m_listenSocket));
		m_listenSocket = nullptr;
	}

	if (m_receiveSocket) {
		SDLNet_TCP_Close(static_cast<TCPsocket>(m_receiveSocket.load()));
		m_receiveSocket = nullptr;
	}

	if (m_socketSet) {
		SDLNet_FreeSocketSet(static_cast<SDLNet_SocketSet>(m_socketSet));
		m_socketSet = nullptr;
	}

	SDLNet_Quit();
}

bool TCPReceive::CheckDataAvailable(int timeoutMS)
{
	if (!m_receiveSocket) {
		return false;
	}

	return SDLNet_CheckSockets(static_cast<SDLNet_SocketSet>(m_socketSet), timeoutMS) > 0;
}

std::vector<char>& TCPReceive::Receive()
{
	if (!m_receiveSocket) {
		DPRINTF("Can't receive because no socket.\n");
		m_recBuffer.clear();
		return m_recBuffer;
	}

	int size = 0;
	int result = SDLNet_TCP_Recv(static_cast<TCPsocket>(m_receiveSocket.load()), &size, sizeof(int));
	DPRINTF("Received %i bytes\n", result);
	if (result <= 0) {
		SDLNet_TCP_Close(static_cast<TCPsocket>(m_receiveSocket.load()));
		m_receiveSocket = nullptr;
	}

	// reserve our space
	m_recBuffer.resize(size);

	while (size) {

		result = SDLNet_TCP_Recv(static_cast<TCPsocket>(m_receiveSocket.load()),
			m_recBuffer.data() + (m_recBuffer.size() - size), size);
		DPRINTF("Received %i bytes\n", result);
		if (result <= 0) {
			SDLNet_TCP_Close(static_cast<TCPsocket>(m_receiveSocket.load()));
			m_receiveSocket = nullptr;
			break;
		}

		size -= result;
	}

	return m_recBuffer;
}

void TCPReceive::ListenFunc()
{
	while (m_running) {

		CThread::Sleep(16);
		if (m_receiveSocket) continue;

		auto socket = SDLNet_TCP_Accept(static_cast<TCPsocket>(m_listenSocket));

		if (socket) {

			// remove old socket if required from socket set
			if (m_receiveSocket) {
				SDLNet_DelSocket(static_cast<SDLNet_SocketSet>(m_socketSet),
					(SDLNet_GenericSocket)m_receiveSocket.load());
			}

			m_receiveSocket = socket;

			SDLNet_AddSocket(static_cast<SDLNet_SocketSet>(m_socketSet),
				(SDLNet_GenericSocket)socket);

			// add socket to socket set
			DPRINTF("Accepted connection.\n");
		}

	}
}

bool TCPReceive::Connected()
{
	return (m_receiveSocket != 0);
}

#endif
