/*
 * netevent - low-level event-device sharing
 *
 * Copyright (C) 2017-2019 Wolfgang Bumiller <wry.git@bumiller.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <errno.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-id-macro"
#pragma clang diagnostic ignored "-Wdocumentation-unknown-command"
#include <linux/input.h>
#include <linux/uinput.h>
#pragma clang diagnostic pop
#if defined(__FreeBSD__)
#include <sys/endian.h>
#else
#include <endian.h>
#endif

#include <string>
#include <utility>
#include <exception>
#include <memory>
#include <functional>

#include "../config.h"
#include "types.h"
#include "iohandle.h"
#include "bitfield.h"

#define Packed __attribute__((packed))

#define LONG_BITS (sizeof(long) * 8)
#define NLONGS(x) (((x) + LONG_BITS - 1) / LONG_BITS)

using std::string;
using std::move;
template<typename T, typename Deleter = std::default_delete<T>>
using uniq = std::unique_ptr<T, Deleter>;
using std::function;

int cmd_daemon(int argc, char **argv);

// C++ doesn't have designated initializers so this is filled in main()
extern bool gUse_UI_DEV_SETUP;
extern unsigned long kUISetBitIOC[EV_MAX];
extern unsigned long kBitLength[EV_MAX];

// "Internal" input event, equal to the usual 64 bit struct input_event
// because struct timeval varies between architectures
struct InputEvent {
	uint64_t tv_sec;
	uint32_t tv_usec;
	uint16_t type;
	uint16_t code;
	int32_t value;
	uint32_t padding = 0;

	inline void toHost() {
		tv_sec  = be64toh(tv_sec);
		tv_usec = be32toh(tv_usec);
		type    = be16toh(type);
		code    = be16toh(code);
		value   = static_cast<int32_t>(be32toh(value));
	}
	inline void toNet() {
		tv_sec  = htobe64(tv_sec);
		tv_usec = htobe32(tv_usec);
		type    = htobe16(type);
		code    = htobe16(code);
		value   = static_cast<int32_t>(htobe32(value));
	}
} Packed;

static const char kNE2Hello[8] = { 'N', 'E', '2', 'H',
                                   'e', 'l', 'l', 'o', };
static const uint16_t kNE2Version = 2;

enum class NE2Command : uint16_t {
	KeepAlive    = 0,
	AddDevice    = 1,
	RemoveDevice = 2,
	DeviceEvent  = 3,
	Hello        = 4,
};

struct NE2Packet {
	// -Wnested-anon-types
	struct Event {
		uint16_t cmd;
		uint16_t id;
		InputEvent event;
	} Packed;
	struct AddDevice {
		uint16_t cmd;
		uint16_t id;
		uint16_t dev_info_size;
		uint16_t dev_name_size;
	} Packed;
	struct RemoveDevice {
		uint16_t cmd;
		uint16_t id;
	} Packed;
	struct Hello {
		uint16_t cmd;
		uint16_t version;
		char magic[8];
	} Packed;
	union {
		uint16_t cmd;
		Event event;
		AddDevice add_device;
		RemoveDevice remove_device;
		Hello hello;
	} Packed;
};

void writeHello(int fd);

unsigned int String2EV(const char* name, size_t length);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wweak-vtables"
struct DeviceException : Exception {
	DeviceException(const char *msg);
};
#pragma clang diagnostic pop

struct OutDevice {
	OutDevice() = delete;
	OutDevice(OutDevice&&) = delete;
	OutDevice(const OutDevice&) = delete;
	OutDevice(const string& name, struct input_id id);
	~OutDevice();

	void create();
	void setupAbsoluteAxis(uint16_t code, const struct input_absinfo& ai);
	void setEventBit(uint16_t type);
	void setGenericBit(unsigned long what, uint16_t bit,
	                   const char *errmsg, ...);

	static uniq<OutDevice> newFromNeteventStream(int fd);
	static uniq<OutDevice> newFromNE2AddCommand(int fd, NE2Packet&);
	static void skipNE2AddCommand(int fd, NE2Packet&);

	int fd() const noexcept {
		return fd_;
	}

	void write(const InputEvent& ev);

 private:
	static uniq<OutDevice> newFromNE2AddCommand(int, NE2Packet&, bool);
	void assertNotCreated(const char *errmsg) const;

	template<typename T>
	void ctl(unsigned long req, T&& data, const char *errmsg, ...) const;

 private:
	int fd_;
	struct uinput_user_dev user_dev_;
	bool created_ = false;
};

struct InDevice {
	InDevice() = delete;
	InDevice(InDevice&&);
	InDevice(const InDevice&) = delete;
	InDevice(const string& path);
	~InDevice();

	void writeNeteventHeader(int fd);
	void writeNE2AddDevice(int fd, uint16_t id);

	void setName(const string&);
	void resetName(); // Set to original name (remembered in name_)

	void persistent(bool on) noexcept;
	bool persistent() const noexcept {
		return persistent_;
	}

	void grab(bool on);
	bool grab() const noexcept {
		return grabbing_;
	}

	bool read(InputEvent *out);
	bool eof() const noexcept {
		return eof_;
	}

	int fd() const noexcept {
		return fd_;
	}

	const char* name() const noexcept {
		return user_dev_.name;
	}

	const string& realName() const noexcept {
		return name_;
	}

 private:
	template<typename T>
	int ctl(unsigned long req, T&& data, const char *errmsg, ...) const;

 private:
	int fd_;
	bool eof_ = false;
	bool grabbing_ = false;
	bool persistent_ = false;
	struct uinput_user_dev user_dev_;
	string name_;
	Bits evbits_;
};

inline void
InDevice::persistent(bool on) noexcept {
	persistent_ = on;
}

struct Socket {
	inline Socket() : fd_(-1), path_() {}
	Socket(const Socket&) = delete;
	~Socket();

	void openUnixStream();
	void close();
	template<bool Abstract> void bindUnix(const string& path);
	template<bool Abstract> void listenUnix(const string& path);
	void listen();
	template<bool Abstract> void connectUnix(const string& path);
	IOHandle accept();
	void shutdown(bool read_end);

	int fd() const noexcept {
		return fd_;
	}

	operator bool() const noexcept {
		return fd_ != -1;
	}

	int release() noexcept {
		int fd = fd_;
		fd_ = -1;
		return fd;
	}

	IOHandle intoIOHandle() noexcept {
		return { release() };
	}


 private:
	int fd_;
	string path_;
	bool unlink_ = false;
};
extern template void Socket::bindUnix<true>(const string& path);
extern template void Socket::bindUnix<false>(const string& path);
extern template void Socket::connectUnix<true>(const string& path);
extern template void Socket::connectUnix<false>(const string& path);

template<bool Abstract>
inline void
Socket::listenUnix(const string& path) {
	bindUnix<Abstract>(path);
	return listen();
}

struct ScopeGuard {
	ScopeGuard() = delete;
	ScopeGuard(ScopeGuard&& o) : f_(move(o.f_)) {}
	ScopeGuard(const ScopeGuard&) = delete;
	ScopeGuard(function<void()> f) : f_(f) {}
	~ScopeGuard() { f_(); }
 private:
	function<void()> f_;
};
struct ScopeGuardHelper { // actually gets rid of an unused-variable warning
	constexpr ScopeGuardHelper() {}
	ScopeGuard operator+(function<void()> f) {
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

struct {
	unsigned int      num;
	const char *const name;
} static const
kEVMap[] = {
	{ EV_SYN,       "SYN" },
	{ EV_KEY,       "KEY" },
	{ EV_REL,       "REL" },
	{ EV_ABS,       "ABS" },
	{ EV_MSC,       "MSC" },
	{ EV_SW ,       "SW " },
	{ EV_LED,       "LED" },
	{ EV_SND,       "SND" },
	{ EV_REP,       "REP" },
	{ EV_FF,        "FF" },
	{ EV_PWR,       "PWR" },
	{ EV_FF_STATUS, "FF_STATUS" },
};

static inline constexpr const char*
EV2String(unsigned int ev)
{
	return (ev < sizeof(kEVMap)/sizeof(kEVMap[0]))
		? kEVMap[ev].name
		: "<Unknown>";
}
