/*
 * netevent - low-level event-device sharing
 *
 * Copyright (C) 2017-2019 Wolfgang Bumiller <wry.git@bumiller.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <string>

using std::string;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wweak-vtables"
struct Exception : std::exception {
	Exception(const char *msg) : msg_(msg) {}
	const char *what() const noexcept override;
 protected:
	Exception() : Exception(nullptr) {}
 private:
	const char *msg_;
};

struct MsgException : Exception {
	MsgException(const MsgException&) = delete;
	MsgException(MsgException&& o);
	MsgException(const char *msg, ...);
 private:
	char msgbuf_[4096];
};

struct ErrnoException : Exception {
	ErrnoException(const ErrnoException&) = delete;
	ErrnoException(ErrnoException&& o);
	ErrnoException(const char *msg, ...);
	int error() const noexcept { return errno_; }
 private:
	int errno_;
	char msgbuf_[4096];
};
#pragma clang diagnostic pop
