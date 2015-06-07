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

#ifndef LVFS_BITS_TORRENTFILE_H_
#define LVFS_BITS_TORRENTFILE_H_

#include <efc/Map>
#include <efc/String>
#include <efc/ScopedPointer>
#include <lvfs/IDirectory>


namespace LVFS {
namespace BitS {

class PLATFORM_MAKE_PRIVATE TorrentFile : public ExtendsBy<IDirectory>
{
public:
    TorrentFile(const Interface::Holder &file);
    virtual ~TorrentFile();

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
    class PLATFORM_MAKE_PRIVATE Item;
    class PLATFORM_MAKE_PRIVATE List;
    class PLATFORM_MAKE_PRIVATE String;
    class PLATFORM_MAKE_PRIVATE Integer;
    class PLATFORM_MAKE_PRIVATE Pair;
    class PLATFORM_MAKE_PRIVATE Dictionary;

private:
    Item *parseFile(char *buffer, int len, char *info_hash) const;
    void processFile(Dictionary *file);

private:
    struct Deleter { void operator()(Item *item) const; };
    EFC::ScopedPointer<Item, Deleter> m_item;

    void test(Item *item, int pad) const;

private:
    Error m_lastError;
};

}}

#endif /* LVFS_BITS_TORRENTFILE_H_ */
