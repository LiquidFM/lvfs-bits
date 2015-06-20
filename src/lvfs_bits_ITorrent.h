/**
 * This file is part of lvfs-bits.
 *
 * Copyright (C) 2011-2015 Dmitriy Vilkov, <dav.daemon@gmail.com>
 *
 * lvfs-bits is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * lvfs-bits is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with lvfs-bits. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LVFS_BITS_ITORRENT_H_
#define LVFS_BITS_ITORRENT_H_

#include <time.h>
#include <lvfs/Interface>


namespace LVFS {
namespace BitS {

class PLATFORM_MAKE_PUBLIC ITorrent
{
    DECLARE_INTERFACE(LVFS::BitS::ITorrent)

public:
    enum { SizeOfHash = 20 };

public:
    virtual ~ITorrent();

    virtual const char *announce() const = 0;
    virtual const char *comment() const = 0;
    virtual const char *createdBy() const = 0;
    virtual time_t creationDate() const = 0;
    virtual const char *publisher() const = 0;
    virtual const char *publisherUrl() const = 0;
    virtual const char *hash() const = 0;
    virtual uint64_t size() const = 0;
};

}}

#endif /* LVFS_BITS_ITORRENT_H_ */
