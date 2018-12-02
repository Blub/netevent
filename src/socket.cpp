/*
 * netevent - low-level event-device sharing
 *
 * Copyright (C) 2017,2018 Wolfgang Bumiller <wry.git@bumiller.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "main.h"
#include <sys/socket.h>
#include <sys/un.h>

Socket::~Socket()
{
	this->close();
}

void
Socket::close()
{
	if (fd_ != -1) {
		::close(fd_);
		fd_ = -1;
		if (path_.length()) {
			if (unlink_) {
				unlink_ = false;
				::unlink(path_.c_str());
			}
			path_.clear();
		}
	}
}

void
Socket::openUnixStream()
{
	close();
	fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd_ < 0)
		throw ErrnoException("failed to open socket");
}

template<bool Abstract>
void
Socket::bindUnix(const string& path)
{
	openUnixStream();

	struct sockaddr_un addr;
	if (path.length() >= sizeof(addr.sun_path))
		throw MsgException("path too long (%zu >= %zu): '%s'",
		                   path.length(), sizeof(addr.sun_path),
		                   path.c_str());

	addr.sun_family = AF_UNIX;
	uint8_t *data;
	if (Abstract) {
		addr.sun_path[0] = 0;
		data = reinterpret_cast<uint8_t*>(&addr.sun_path[1]);
	} else {
		data = reinterpret_cast<uint8_t*>(&addr.sun_path[0]);
	}
	::memcpy(data, path.c_str(), path.length());
	data[path.length()] = 0;

	auto beg = reinterpret_cast<const uint8_t*>(&addr);
	auto end = data + path.length();

	if (!Abstract)
		(void)::unlink(path.c_str());
	if (::bind(fd_, reinterpret_cast<const struct sockaddr*>(&addr),
	           Abstract ? socklen_t(end-beg) : socklen_t(sizeof(addr)))
	    != 0)
		throw ErrnoException("failed to bind to %s%s",
		                     (Abstract ? "@" : ""), path.c_str());
	if (!Abstract) {
		path_ = path;
		unlink_ = true;
	}
}
template void Socket::bindUnix<true>(const string& path);
template void Socket::bindUnix<false>(const string& path);

void
Socket::listen()
{
	if (::listen(fd_, 5) != 0)
		throw ErrnoException("failed to listen on %s%s",
		                     path_.c_str());
}

template<bool Abstract>
void
Socket::connectUnix(const string& path)
{
	openUnixStream();

	struct sockaddr_un addr;
	if (path.length() >= sizeof(addr.sun_path))
		throw MsgException("path too long (%zu >= %zu): '%s'",
		                   path.length(), sizeof(addr.sun_path),
		                   path.c_str());

	addr.sun_family = AF_UNIX;
	uint8_t *data;
	if (Abstract) {
		addr.sun_path[0] = 0;
		data = reinterpret_cast<uint8_t*>(&addr.sun_path[1]);
	} else {
		data = reinterpret_cast<uint8_t*>(&addr.sun_path[0]);
	}
	::memcpy(data, path.c_str(), path.length());
	data[path.length()] = 0;

	auto beg = reinterpret_cast<const uint8_t*>(&addr);
	auto end = data + path.length();

	if (!Abstract)
		path_ = path;
	if (::connect(fd_, reinterpret_cast<const struct sockaddr*>(&addr),
	              Abstract ? socklen_t(end-beg) : socklen_t(sizeof(addr)))
	    != 0)
		throw ErrnoException("failed to connect to %s%s",
		                     (Abstract ? "@" : ""), path.c_str());
}
template void Socket::connectUnix<true>(const string& path);
template void Socket::connectUnix<false>(const string& path);

IOHandle
Socket::accept()
{
	struct sockaddr_un un;
	socklen_t slen = sizeof(un);
	int client = ::accept4(fd_, reinterpret_cast<struct sockaddr*>(&un),
	                       &slen, SOCK_CLOEXEC);
	if (client < 0)
		throw ErrnoException("failed to accept client");
	return {client};
}

void
Socket::shutdown(bool read_end)
{
	if (::shutdown(fd_, read_end ? SHUT_RD : SHUT_WR) != 0)
		throw ErrnoException("shutdown() on socket failed");
}
