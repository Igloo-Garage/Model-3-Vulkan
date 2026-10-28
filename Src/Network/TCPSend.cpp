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

#include "TCPSend.h"
#include "OSD/Logger.h"

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

	bool SendAll(SocketHandle socket, const void* buffer, int size)
	{
		const char* bytes = static_cast<const char*>(buffer);
		while (size > 0)
		{
			const int sent = NetSocket::SendSome(socket, bytes, size);
			if (sent <= 0)
				return false;
			bytes += sent;
			size -= sent;
		}
		return true;
	}
}

TCPSend::TCPSend(std::string& ip, int port) :
	m_ip(ip),
	m_port(port),
	m_socket(nullptr)
{
	NetSocket::Startup();
}

TCPSend::~TCPSend()
{
	if (m_socket)
	{
		NetSocket::Close(HandleFromVoid(m_socket));
		m_socket = nullptr;
	}

	NetSocket::Cleanup();
}

bool TCPSend::Send(const void* data, int length)
{
	if (!Connected())
		return false;

	/*
	 * Length-prefixed, host order, exactly as the SDL_net path writes it. The original protocol
	 * already assumed both ends agree on byte order, and every supported platform is little-endian.
	 */
	const SocketHandle socket = HandleFromVoid(m_socket);
	if (!SendAll(socket, &length, (int)sizeof(length)) ||
		(length > 0 && !SendAll(socket, data, length)))
	{
		NetSocket::Close(socket);
		m_socket = nullptr;
		return false;
	}

	return true;
}

bool TCPSend::Connected()
{
	return m_socket != nullptr;
}

bool TCPSend::Connect()
{
	if (Connected())
		return true;

	addrinfo hints = {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	const std::string port = std::to_string(m_port);

	addrinfo* results = nullptr;
	if (getaddrinfo(m_ip.c_str(), port.c_str(), &hints, &results) != 0)
		return false;

	for (addrinfo* entry = results; entry != nullptr; entry = entry->ai_next)
	{
		const SocketHandle socket = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
		if (socket == kInvalidSocket)
			continue;

		if (::connect(socket, entry->ai_addr, static_cast<SocketLength>(entry->ai_addrlen)) == 0)
		{
			// A pending 57 Hz packet must not wait behind Nagle's algorithm.
			NetSocket::SetNoDelay(socket);
			m_socket = VoidFromHandle(socket);
			break;
		}

		NetSocket::Close(socket);
	}

	freeaddrinfo(results);
	return Connected();
}

#else

#include "SDLIncludes.h"

#if defined(_DEBUG)
#include <stdio.h>
#define DPRINTF DebugLog
#else
#define DPRINTF(a, ...)
#endif

TCPSend::TCPSend(std::string& ip, int port) :
	m_ip(ip),
	m_port(port),
	m_socket(nullptr)
{
	SDLNet_Init();
}

TCPSend::~TCPSend()
{
	if (m_socket) {
		SDLNet_TCP_Close(static_cast<TCPsocket>(m_socket));
		m_socket = nullptr;
	}

	SDLNet_Quit();	// unload lib (winsock dll for windows)
}

bool TCPSend::Send(const void * data, int length)
{
	// If we failed bail out
	if (!Connected()) {
		DPRINTF("Not connected\n");
		return false;
	}

	DPRINTF("Sending %i bytes\n", length);

	int sent = SDLNet_TCP_Send(static_cast<TCPsocket>(m_socket), &length, sizeof(int));		// pack the length at the start of transmission.

	if (!length)
		return true;		// 0 sized packet will blow our connex

	sent = SDLNet_TCP_Send(static_cast<TCPsocket>(m_socket), data, length);

	if (sent < length) {
		SDLNet_TCP_Close(static_cast<TCPsocket>(m_socket));
		m_socket = nullptr;
	}

	return true;
}

bool TCPSend::Connected()
{
	return m_socket != 0;
}

bool TCPSend::Connect()
{
	IPaddress ip;
	int result = SDLNet_ResolveHost(&ip, m_ip.c_str(), m_port);

	if (result == 0) {
		m_socket = SDLNet_TCP_Open(&ip);
	}

	return Connected();
}

#endif
