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

#include "lvfs_bits_TorrentFile.h"
#include "lvfs_bits_Torrent.h"


namespace LVFS {
namespace BitS {

TorrentFile::TorrentFile(const Interface::Holder &file) :
    ExtendsBy(file)
{}

TorrentFile::~TorrentFile()
{}

TorrentFile::const_iterator TorrentFile::begin() const
{
    if (!m_torrent.isValid())
        m_torrent = Torrent::fromFile(original(), m_lastError);

    if (m_torrent.isValid())
        return m_torrent->begin();
    else
        return const_iterator();
}

TorrentFile::const_iterator TorrentFile::end() const
{
    if (m_torrent.isValid())
        return m_torrent->end();
    else
        return const_iterator();
}

bool TorrentFile::exists(const char *name) const
{
    if (m_torrent.isValid())
        return m_torrent->exists(name);
    else
        return false;
}

Interface::Holder TorrentFile::entry(const char *name, const IType *type, bool create)
{
    if (m_torrent.isValid())
        return m_torrent->entry(name, type, create);
    else
        return Interface::Holder();
}

bool TorrentFile::copy(const Progress &callback, const Interface::Holder &file, bool move)
{
    if (m_torrent.isValid())
        return m_torrent->copy(callback, file, move);
    else
        return false;
}

bool TorrentFile::rename(const Interface::Holder &file, const char *name)
{
    if (m_torrent.isValid())
        return m_torrent->rename(file, name);
    else
        return false;
}

bool TorrentFile::remove(const Interface::Holder &file)
{
    if (m_torrent.isValid())
        return m_torrent->remove(file);
    else
        return false;
}

const Error &TorrentFile::lastError() const
{
    if (m_torrent.isValid())
        return m_torrent->lastError();
    else
        return m_lastError;
}

}}
