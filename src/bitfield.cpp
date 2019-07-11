/*
 * netevent - low-level event-device sharing
 *
 * Copyright (C) 2017-2019 Wolfgang Bumiller <wry.git@bumiller.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "main.h"
#include "bitfield.h"

void
Bits::resize(size_t bitcount)
{
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

