/*
 * netevent - low-level event-device sharing
 *
 * Copyright (C) 2017-2021 Wolfgang Bumiller <wry.git@bumiller.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

struct ScopeGuard {
	ScopeGuard() = delete;
	ScopeGuard(ScopeGuard&& o) : f_(move(o.f_)) {}
	ScopeGuard(const ScopeGuard&) = delete;
	ScopeGuard(std::function<void()> f) : f_(f) {}
	~ScopeGuard() { f_(); }
 private:
	std::function<void()> f_;
};
struct ScopeGuardHelper { // actually gets rid of an unused-variable warning
	constexpr ScopeGuardHelper() {}
	ScopeGuard operator+(std::function<void()> f) {
		return {move(f)};
	}
};
#define NE2_CPP_CAT_INDIR(X,Y) X ## Y
#define NE2_CPP_CAT(X,Y) NE2_CPP_CAT_INDIR(X, Y)
#define NE2_CPP_ADDLINE(X) NE2_CPP_CAT(X, __LINE__)
#define scope(exit) \
  auto NE2_CPP_ADDLINE(ne2_scopeguard) = ScopeGuardHelper{}+[&]()

static inline
bool
mustRead(int fd, void *buf, size_t length)
{
	while (length) {
		auto got = ::read(fd, buf, length);
		if (got == 0)
			errno = 0;
		if (got <= 0)
			return false;
		if (size_t(got) > length) {
			errno = EFAULT;
			return false;
		}
		buf = reinterpret_cast<void*>(
		    reinterpret_cast<uint8_t*>(buf) + got);
		length -= size_t(got);
	}
	return true;
}

static inline
bool
mustWrite(int fd, const void *buf, size_t length)
{
	return ::write(fd, buf, length) == ssize_t(length);
}

bool parseULong(unsigned long *out, const char *s, size_t maxlen);
bool parseLong(long *out, const char *s, size_t maxlen);
bool parseBool(bool *out, const char *s);

template<typename Iter>
static inline string
join(char c, Iter&& i, Iter&& end)
{
	string s;
	for (; i != end; ++i) {
		if (s.length())
			s.append(1, c);
		s.append(*i);
	}
	return s;
}
