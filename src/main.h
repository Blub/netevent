/*
 * netevent - low-level event-device sharing
 *
 * Copyright (C) 2017-2021 Wolfgang Bumiller <wry.git@bumiller.com>
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

#include <utility>
#include <exception>
#include <memory>
#include <functional>

using std::move;

#include "config.h"
#include "types.h"
#include "iohandle.h"
#include "socket.h"
#include "bitfield.h"
#include "utils.h"

#define Packed __attribute__((packed))

#define LONG_BITS (sizeof(long) * 8)
#define NLONGS(x) (((x) + LONG_BITS - 1) / LONG_BITS)

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
