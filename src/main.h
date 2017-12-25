/*
 * netevent - low-level event-device sharing
 *
 * Copyright (C) 2017 Wolfgang Bumiller <wry.git@bumiller.com>
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
#ifndef NETEVENT_2_MAIN_H
#define NETEVENT_2_MAIN_H

#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <endian.h>

#include <string>
#include <utility>
#include <exception>
#include <memory>
#include <functional>

#include "../config.h"

using std::string;
using std::move;
template<typename T, typename Deleter = std::default_delete<T>>
using uniq = std::unique_ptr<T, Deleter>;
using std::function;

int cmd_daemon(int argc, char **argv);

// C++ doesn't have designated initializers so this is filled in main()
extern unsigned long kUISetBitIOC[EV_MAX];

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
};

enum class NE2Command : uint16_t {
	KeepAlive    = 0,
	AddDevice    = 1,
	RemoveDevice = 2,
	DeviceEvent  = 3,
};

struct NE2Packet {
	uint16_t cmd;
	// -Wnested-anon-types
	struct Event {
		uint16_t id;
		InputEvent event;
	};
	struct AddDevice {
		uint16_t id;
		uint16_t dev_info_size;
		uint16_t dev_name_size;
	};
	struct RemoveDevice {
		uint16_t id;
	};
	union {
		Event event;
		AddDevice add_device;
		RemoveDevice remove_device;
	};
};

unsigned int String2EV(const char* name, size_t length);

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

struct DeviceException : Exception {
	DeviceException(const char *msg);
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
 private:
	char msgbuf_[4096];
};
#pragma clang diagnostic pop

struct Bits {
	Bits() : bitcount_(0), data_(nullptr) {}
	Bits(const Bits&) = delete; // this needs to be explicitly dup()ed
	Bits(Bits&& o) : bitcount_(o.bitcount_), data_(o.data_) {
		o.bitcount_ = 0;
		o.data_ = nullptr;
	}
	~Bits() {
		::free(data_);
	}

	Bits(size_t size) : Bits() {
		resize(size);
	}

	size_t size() const noexcept {
		return bitcount_;
	}

	static constexpr inline size_t byte_size(size_t bitcount) noexcept {
		return (bitcount+7) / 8;
	}
	size_t byte_size() const noexcept {
		return byte_size(bitcount_);
	}

	uint8_t *data() noexcept {
		return data_;
	}

	void resize(size_t bitcount) {
		if (!bitcount) {
			::free(data_);
			data_ = nullptr;
			bitcount_ = 0;
			return;
		}

		auto np = ::realloc(data_, (bitcount+7)/8);
		if (!np)
			throw ErrnoException("allocation failed");
		data_ = reinterpret_cast<uint8_t*>(np);
		auto oldsize = byte_size();
		auto newsize = byte_size(bitcount);
		if (oldsize < newsize)
			::memset(data_ + oldsize, 0, newsize-oldsize);
		bitcount_ = bitcount;
	}

	void resizeNE1Compat(size_t bitcount) {
		// this is not the same as 8 + bitcount because of integer
		// math, (bitcount/8)*8 aligns down!
		resize(8 * (1+bitcount/8));
	}

	void shrinkTo(size_t bitcount) {
		if (bitcount < bitcount_)
			bitcount_ = bitcount;
	}

	// unsafe interface for when you temporarily change the count
	void setBitCount(size_t bitcount) {
		bitcount_ = bitcount;
	}

	struct BitAccess {
		BitAccess() = delete;

		explicit BitAccess(Bits *owner, size_t index)
			: owner_(owner)
			, index_(index)
		{}

		size_t index() const {
			return index_;
		}

		operator bool() const {
			return owner_->data_[index_/8] & (1<<(index_&7));
		}

		// iterating should include the iterator...
		BitAccess& operator*() {
			return (*this);
		}

		BitAccess& operator=(bool on) {
			if (on)
				owner_->data_[index_/8] |= (1<<(index_&7));
			else
				owner_->data_[index_/8] &= ~(1<<(index_&7));
			return (*this);
		}

		// This also serves as iterator:
		BitAccess& operator++() {
			++index_;
			return (*this);
		}

		BitAccess& operator--() {
			--index_;
			return (*this);
		}

		bool operator!=(const BitAccess& other) {
			return owner_ != other.owner_ ||
			       index_ != other.index_;
		}

		bool operator<(const BitAccess& other) {
			return index_ < other.index_;
		}
	 private:
		Bits *owner_;
		size_t index_;
	};

	BitAccess operator[](size_t idx) {
		return BitAccess { this, idx };
	}

	BitAccess begin() {
		return BitAccess { this, 0 };
	}

	BitAccess end() {
		return BitAccess { this, bitcount_ };
	}

	Bits& operator=(Bits&& other) {
		bitcount_ = other.bitcount_;
		data_ = other.data_;
		other.bitcount_ = 0;
		other.data_ = nullptr;
		return (*this);
	}

	Bits dup() const {
		Bits d { bitcount_ };
		::memcpy(d.data(), data_, byte_size());
		return d;
	}

 private:
	size_t   bitcount_;
	uint8_t *data_;
};

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

	void writeNeteventHeader(int fd);
	void writeNE2AddDevice(int fd, uint16_t id);

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

 private:
	template<typename T>
	int ctl(unsigned long req, T&& data, const char *errmsg, ...) const;

 private:
	int fd_;
	bool eof_ = false;
	bool grabbing_ = false;
	struct uinput_user_dev user_dev_;
	string name_;
	Bits evbits_;
};

struct IOHandle {
	IOHandle(const IOHandle&) = delete;

	constexpr IOHandle(int fd) : fd_(fd) {}

	constexpr IOHandle() : fd_(-1) {}

	IOHandle(IOHandle&& o) : IOHandle(o.fd_) {
		o.fd_ = -1;
	}

	~IOHandle() {
		if (fd_ != -1)
			::close(fd_);
	}

	IOHandle& operator=(IOHandle&& o) {
		this->close();
		fd_ = o.fd_;
		o.fd_ = -1;
		return (*this);
	}

	int fd() const noexcept {
		return fd_;
	}

	ssize_t read(void *buf, size_t count) {
		return ::read(fd_, buf, count);
	}

	ssize_t write(const void *buf, size_t count) {
		return ::write(fd_, buf, count);
	}

	void close() {
		if (fd_ != -1) {
			::close(fd_);
			fd_ = -1;
		}
	}

	int release() noexcept {
		int fd = fd_;
		fd_ = -1;
		return fd;
	}

 private:
	int fd_;
};

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

#endif
