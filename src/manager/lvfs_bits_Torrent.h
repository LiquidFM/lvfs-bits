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

#ifndef LVFS_BITS_TORRENT_H_
#define LVFS_BITS_TORRENT_H_

#include <efc/Map>
#include <efc/List>
#include <efc/Vector>
#include <efc/String>
#include <lvfs/IDirectory>
#include <lvfs-bits/ITorrent>


namespace LVFS {
namespace BitS {

class PLATFORM_MAKE_PRIVATE Torrent : public Implements<ITorrent, IDirectory>
{
public:
    struct Piece
    {
        struct File
        {
            uint64_t offset;
            uint64_t length;
            Interface::Holder file;
        };
        typedef EFC::List<File> Files;

        uint64_t index;
        char hash[SizeOfHash];
        Files files;
    };

    typedef EFC::Vector<Piece>                       Pieces;
    typedef EFC::Map<EFC::String, Interface::Holder> Files;

public:
    virtual ~Torrent();

    static Interface::Holder fromFile(const Interface::Holder &file, Error &error);

public: /* ITorrent */
    virtual const char *announce() const;
    virtual const char *comment() const;
    virtual const char *createdBy() const;
    virtual time_t creationDate() const;
    virtual const char *publisher() const;
    virtual const char *publisherUrl() const;
    virtual const char *hash() const;
    virtual uint64_t size() const;

public: /* IDirectory */
    virtual const_iterator begin() const;
    virtual const_iterator end() const;

    virtual bool exists(const char *name) const;
    virtual Interface::Holder entry(const char *name, const IType *type = NULL, bool create = false);

    virtual bool copy(const Progress &callback, const Interface::Holder &file, bool move = false);
    virtual bool rename(const Interface::Holder &file, const char *name);
    virtual bool remove(const Interface::Holder &file);

    virtual const Error &lastError() const;

private:
    Torrent(const char hash[SizeOfHash]);
    void init(const void *state);

private:
    EFC::String m_announce;
    EFC::String m_comment;
    EFC::String m_created_by;
    time_t m_creation_date;
    EFC::String m_publisher;
    EFC::String m_publisher_url;
    char m_info_hash[SizeOfHash];
    uint64_t m_size;
    Files m_files;
    Pieces m_pieces;
    Error m_lastError;
};

}}

#endif /* LVFS_BITS_TORRENT_H_ */
