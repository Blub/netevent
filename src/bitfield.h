/*
 * netevent - low-level event-device sharing
 *
 * Copyright (C) 2017-2019 Wolfgang Bumiller <wry.git@bumiller.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

struct Bits {
	Bits();
	Bits(const Bits&) = delete; // this needs to be explicitly dup()ed
	Bits(Bits&& o);
	~Bits();
	Bits(size_t size);

	static constexpr size_t byte_size(size_t bitcount) noexcept;

	size_t   size() const noexcept;
	size_t   byte_size() const noexcept;
	uint8_t* data() noexcept;
	void     resize(size_t bitcount);
	void     resizeNE1Compat(size_t bitcount);
	void     shrinkTo(size_t bitcount);

	// unsafe interface for temporarily! changing the count
	void setBitCount(size_t bitcount);

	struct BitAccess {
		BitAccess() = delete;

		explicit BitAccess(Bits *owner, size_t index);
		size_t index() const;

		operator bool() const;
		// iterating should include the iterator...
		BitAccess& operator*();
		BitAccess& operator=(bool on);
		// This also serves as iterator:
		BitAccess& operator++();
		BitAccess& operator--();

		bool operator!=(const BitAccess& other);
		bool operator<(const BitAccess& other);

	 private:
		Bits *owner_;
		size_t index_;
	};

	BitAccess operator[](size_t idx);
	BitAccess begin();
	BitAccess end();
	Bits      dup() const;

	Bits& operator=(Bits&& other);

 private:
	size_t   bitcount_;
	uint8_t *data_;
};

inline
Bits::Bits()
	: bitcount_(0)
	, data_(nullptr)
{}

inline
Bits::Bits(Bits&& o)
	: bitcount_(o.bitcount_)
	, data_(o.data_)
{
	o.bitcount_ = 0;
	o.data_ = nullptr;
}

inline
Bits::~Bits()
{
	::free(data_);
}

inline
Bits::Bits(size_t size)
	: Bits()
{
	resize(size);
}

inline size_t
Bits::size() const noexcept
{
	return bitcount_;
}

inline constexpr size_t
Bits::byte_size(size_t bitcount) noexcept
{
	return (bitcount+7) / 8;
}

inline size_t
Bits::byte_size() const noexcept
{
	return byte_size(bitcount_);
}

inline uint8_t*
Bits::data() noexcept
{
	return data_;
}

inline void
Bits::resizeNE1Compat(size_t bitcount)
{
	// this is not the same as 8 + bitcount because of integer
	// math, (bitcount/8)*8 aligns down!
	resize(8 * (1+bitcount/8));
}

inline void
Bits::shrinkTo(size_t bitcount)
{
	if (bitcount < bitcount_)
		bitcount_ = bitcount;
}

// unsafe interface for when you temporarily change the count
inline void
Bits::setBitCount(size_t bitcount)
{
	bitcount_ = bitcount;
}

inline
Bits::BitAccess::BitAccess(Bits *owner, size_t index)
	: owner_(owner)
	, index_(index)
{}

inline size_t
Bits::BitAccess::index() const
{
	return index_;
}

inline
Bits::BitAccess::operator bool() const
{
	return owner_->data_[index_/8] & (1<<(index_&7));
}

// iterating should include the iterator...
inline Bits::BitAccess&
Bits::BitAccess::operator*()
{
	return (*this);
}

inline Bits::BitAccess&
Bits::BitAccess::operator=(bool on)
{
	if (on)
		owner_->data_[index_/8] |= (1<<(index_&7));
	else
		owner_->data_[index_/8] &= ~(1<<(index_&7));
	return (*this);
}

// This also serves as iterator:
inline Bits::BitAccess&
Bits::BitAccess::operator++()
{
	++index_;
	return (*this);
}

inline Bits::BitAccess&
Bits::BitAccess::operator--()
{
	--index_;
	return (*this);
}

inline bool
Bits::BitAccess::operator!=(const BitAccess& other)
{
	return owner_ != other.owner_ ||
	       index_ != other.index_;
}

inline bool
Bits::BitAccess::operator<(const BitAccess& other)
{
	return index_ < other.index_;
}

inline Bits::BitAccess
Bits::operator[](size_t idx)
{
	return BitAccess { this, idx };
}

inline Bits::BitAccess
Bits::begin()
{
	return BitAccess { this, 0 };
}

inline Bits::BitAccess
Bits::end()
{
	return BitAccess { this, bitcount_ };
}

inline Bits&
Bits::operator=(Bits&& other)
{
	bitcount_ = other.bitcount_;
	data_ = other.data_;
	other.bitcount_ = 0;
	other.data_ = nullptr;
	return (*this);
}

inline Bits
Bits::dup() const
{
	Bits d { bitcount_ };
	::memcpy(d.data(), data_, byte_size());
	return d;
}
