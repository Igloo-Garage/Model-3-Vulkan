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

/*
 * NativeSocket.h
 *
 * The small socket seam shared by TCPSend and TCPReceive on builds that do not
 * link SDL_net.
 *
 * SDL3 dropped SDL_net from the default target, and the network board only
 * needs four operations (connect, listen, send, receive). Wrapping them here
 * keeps the two protocol files free of platform #ifdefs: WinSock2 on Windows,
 * BSD sockets elsewhere. The wire format is unchanged from the SDL_net path -
 * a host-order int length followed by the payload - so an SDL3 executable can
 * link with an SDL2 one.
 */

#ifndef INCLUDED_NATIVESOCKET_H
#define INCLUDED_NATIVESOCKET_H

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
using SocketLength = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
using SocketHandle = int;
using SocketLength = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace NetSocket
{
	/*
	 * Reference-counted process start-up. Both network classes call Startup() in their constructor
	 * and Cleanup() in their destructor, so a process that creates a sender and a receiver (the
	 * usual master/slave configuration) initialises the stack once.
	 */
	bool Startup();
	void Cleanup();

	void Close(SocketHandle socket);

	// TCP_NODELAY, so a 57 Hz input packet is not held by Nagle's algorithm waiting for an ACK.
	bool SetNoDelay(SocketHandle socket);

	// Non-blocking mode for the listener, whose accept loop must not stall the shutdown path.
	bool SetNonBlocking(SocketHandle socket, bool nonBlocking);

	bool WouldBlock();
	int LastError();

	/*
	 * send()/recv() with EINTR retried. SendSome() suppresses SIGPIPE on platforms that allow it,
	 * because a peer that disconnects mid-packet must close the connection, not kill the process.
	 * A return value of 0 means the peer closed; -1 means an error, check WouldBlock().
	 */
	int SendSome(SocketHandle socket, const void* data, int length);
	int ReceiveSome(SocketHandle socket, void* data, int length);
}

#endif // INCLUDED_NATIVESOCKET_H
