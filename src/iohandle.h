/*
 * netevent - low-level event-device sharing
 *
 * Copyright (C) 2017-2019 Wolfgang Bumiller <wry.git@bumiller.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <fcntl.h>
#include <unistd.h>

struct IOHandle {
	IOHandle(const IOHandle&) = delete;

	constexpr IOHandle(int fd);
	constexpr IOHandle();
	IOHandle(IOHandle&& o);
	~IOHandle();

	int     fd() const noexcept;
	ssize_t read(void *buf, size_t count);
	ssize_t write(const void *buf, size_t count);
	void    close();
	int     release() noexcept;
	void    cloexec(bool on);

	IOHandle& operator=(IOHandle&& o);

	operator bool() const noexcept;

 private:
	int fd_;
};

inline constexpr
IOHandle::IOHandle(int fd)
	: fd_(fd)
{
}

inline constexpr
IOHandle::IOHandle()
	: fd_(-1)
{
}

inline
IOHandle::IOHandle(IOHandle&& o)
	: IOHandle(o.fd_)
{
	o.fd_ = -1;
}

inline
IOHandle::~IOHandle() {
	if (fd_ != -1)
		::close(fd_);
}

inline int
IOHandle::fd() const noexcept
{
	return fd_;
}

inline ssize_t
IOHandle::read(void *buf, size_t count)
{
	return ::read(fd_, buf, count);
}

inline ssize_t
IOHandle::write(const void *buf, size_t count)
{
	return ::write(fd_, buf, count);
}

inline void
IOHandle::close()
{
	if (fd_ != -1) {
		::close(fd_);
		fd_ = -1;
	}
}

inline int
IOHandle::release() noexcept
{
	int fd = fd_;
	fd_ = -1;
	return fd;
}

inline IOHandle&
IOHandle::operator=(IOHandle&& o)
{
	this->close();
	fd_ = o.fd_;
	o.fd_ = -1;
	return (*this);
}

inline
IOHandle::operator bool() const noexcept
{
	return fd_ != -1;
}

inline void
IOHandle::cloexec(bool on)
{
	int flags = ::fcntl(fd_, F_GETFD);
	if (on)
		flags |= FD_CLOEXEC;
	else
		flags &= ~(FD_CLOEXEC);
	if (::fcntl(fd_, F_SETFD, flags) < 0)
		throw ErrnoException("failed to set FD_CLOEXEC flags");
}
