/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2026 The Supermodel Team
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

#include "NativeSocket.h"

#include <cerrno>
#include <mutex>

#if !defined(_WIN32)
#include <csignal>
#endif

namespace
{
	std::mutex g_startupMutex;
	int g_startupCount = 0;
}

bool NetSocket::Startup()
{
	std::lock_guard<std::mutex> lock(g_startupMutex);

	if (g_startupCount > 0)
	{
		g_startupCount++;
		return true;
	}

#if defined(_WIN32)
	WSADATA data;
	if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
		return false;
#else
	// A peer that disappears between writes would otherwise raise SIGPIPE and terminate the
	// process. MSG_NOSIGNAL covers Linux; ignoring the signal covers platforms without it.
	signal(SIGPIPE, SIG_IGN);
#endif

	g_startupCount = 1;
	return true;
}

void NetSocket::Cleanup()
{
	std::lock_guard<std::mutex> lock(g_startupMutex);

	if (g_startupCount == 0)
		return;

	if (--g_startupCount == 0)
	{
#if defined(_WIN32)
		WSACleanup();
#endif
	}
}

void NetSocket::Close(SocketHandle socket)
{
	if (socket == kInvalidSocket)
		return;

#if defined(_WIN32)
	closesocket(socket);
#else
	close(socket);
#endif
}

bool NetSocket::SetNoDelay(SocketHandle socket)
{
	int one = 1;
	return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
		reinterpret_cast<const char*>(&one), static_cast<SocketLength>(sizeof(one))) == 0;
}

bool NetSocket::SetNonBlocking(SocketHandle socket, bool nonBlocking)
{
#if defined(_WIN32)
	u_long mode = nonBlocking ? 1 : 0;
	return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
	const int flags = fcntl(socket, F_GETFL, 0);
	if (flags < 0)
		return false;
	const int updated = nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
	return fcntl(socket, F_SETFL, updated) == 0;
#endif
}

bool NetSocket::WouldBlock()
{
#if defined(_WIN32)
	const int error = WSAGetLastError();
	return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
	return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
#endif
}

int NetSocket::LastError()
{
#if defined(_WIN32)
	return WSAGetLastError();
#else
	return errno;
#endif
}

int NetSocket::SendSome(SocketHandle socket, const void* data, int length)
{
	for (;;)
	{
#if defined(_WIN32)
		const int sent = send(socket, static_cast<const char*>(data), length, 0);
#elif defined(MSG_NOSIGNAL)
		const int sent = static_cast<int>(send(socket, data, static_cast<size_t>(length), MSG_NOSIGNAL));
#else
		const int sent = static_cast<int>(send(socket, data, static_cast<size_t>(length), 0));
#endif

#if !defined(_WIN32)
		if (sent < 0 && errno == EINTR)
			continue;
#endif
		return sent;
	}
}

int NetSocket::ReceiveSome(SocketHandle socket, void* data, int length)
{
	for (;;)
	{
#if defined(_WIN32)
		const int received = recv(socket, static_cast<char*>(data), length, 0);
#else
		const int received = static_cast<int>(recv(socket, data, static_cast<size_t>(length), 0));
		if (received < 0 && errno == EINTR)
			continue;
#endif
		return received;
	}
}
