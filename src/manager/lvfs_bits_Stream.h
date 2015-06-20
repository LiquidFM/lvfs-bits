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

#ifndef LVFS_BITS_STREAM_CPP_
#define LVFS_BITS_STREAM_CPP_

#include <lvfs/IStream>
#include <lvfs-bits/ITorrent>


namespace LVFS {
namespace BitS {

class PLATFORM_MAKE_PRIVATE Stream : public Implements<IStream>
{
public:
    Stream(const char *tracker);
    virtual ~Stream();

    static Interface::Holder open(const char *announce, const char *hash, uint64_t length, Error &error);

public: /* IStream */
    virtual size_t read(void *buffer, size_t size);
    virtual size_t write(const void *buffer, size_t size);
    virtual bool advise(off64_t offset, off64_t len, Advise advise);
    virtual bool seek(off64_t offset, Whence whence = FromCurrent);
    virtual bool flush();

    virtual const Error &lastError() const;

private:
    enum Event
    {
        None,
        Started,
        Completed,
        Stopped
    };

    static void createId(char id[ITorrent::SizeOfHash]);
    static void createUri(void *curl,
                          char *uri, size_t &size,
                          const char *announce,
                          const char info_hash[ITorrent::SizeOfHash],
                          const char peer_id[ITorrent::SizeOfHash],
                          unsigned short port,
                          uint64_t uploaded,
                          uint64_t downloaded,
                          uint64_t left,
                          Event event);

private:
    static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp);

private:
    char m_id[ITorrent::SizeOfHash];
    Error m_lastError;
};

}}

#endif /* LVFS_BITS_STREAM_CPP_ */
