/**
 * This file is part of lvfs-bits.
 *
 * Copyright (C) 2015 Dmitriy Vilkov, <dav.daemon@gmail.com>
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

#include "lvfs_bits_Plugin.h"
#include "lvfs_bits_TorrentFile.h"


namespace LVFS {
namespace BitS {

Plugin::Plugin()
{}

Plugin::~Plugin()
{}

Interface::Holder Plugin::open(const Interface::Holder &file) const
{
    return Interface::Holder(new (std::nothrow) TorrentFile(file));
}

const Error &Plugin::lastError() const
{
    return m_error;
}

void Plugin::registered()
{

}

}}
