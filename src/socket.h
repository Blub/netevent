/*
 * netevent - low-level event-device sharing
 *
 * Copyright (C) 2017-2019 Wolfgang Bumiller <wry.git@bumiller.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "types.h"
#include "iohandle.h"

struct Socket {
	Socket();
	Socket(const Socket&) = delete;
	~Socket();

	void openUnixStream();
	void close();
	template<bool Abstract> void bindUnix(const std::string& path);
	template<bool Abstract> void listenUnix(const std::string& path);
	void listen();
	template<bool Abstract> void connectUnix(const std::string& path);
	IOHandle accept();
	void shutdown(bool read_end);

	int      fd() const noexcept;
	int      release() noexcept;
	IOHandle intoIOHandle() noexcept;

	operator bool() const noexcept;

 private:
	int fd_;
	std::string path_;
	bool unlink_ = false;
};
extern template void Socket::bindUnix<true>(const std::string& path);
extern template void Socket::bindUnix<false>(const std::string& path);
extern template void Socket::connectUnix<true>(const std::string& path);
extern template void Socket::connectUnix<false>(const std::string& path);

inline
Socket::Socket()
	: fd_(-1)
	, path_()
{}

inline int
Socket::fd() const noexcept
{
	return fd_;
}

inline
Socket::operator bool() const noexcept
{
	return fd_ != -1;
}

inline int
Socket::release() noexcept
{
	int fd = fd_;
	fd_ = -1;
	return fd;
}

inline IOHandle
Socket::intoIOHandle() noexcept
{
	return { release() };
}

template<bool Abstract>
inline void
Socket::listenUnix(const std::string& path) {
	bindUnix<Abstract>(path);
	return listen();
}
