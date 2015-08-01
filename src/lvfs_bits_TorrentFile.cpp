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

#include <lvfs/IEntry>
#include <lvfs/IStream>
#include <lvfs/IProperties>
#include <lvfs/Module>

#include <efc/ScopedPointer>
#include <brolly/assert.h>

#include <libtorrent/lazy_entry.hpp>
#include <libtorrent/torrent_info.hpp>
#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <cstring>
#include <cstdio>


namespace LVFS {
namespace BitS {

namespace {

    class Entry : public Implements<IEntry, IProperties>
    {
    public:
        Entry(const char *location, off64_t size, time_t ctime, int index, const boost::intrusive_ptr<libtorrent::torrent_info> &ti) :
            m_location(::strdup(location)),
            m_title(::strrchr(m_location, '/') + 1),
            m_size(size),
            m_ctime(ctime),
            m_index(index),
            m_ti(ti)
        {
            m_type = Module::desktop().typeOfFile(m_title);
        }

        virtual ~Entry()
        {
            ::free(m_location);
        }

    public: /* IEntry */
        virtual const char *title() const { return m_title; }
        virtual const char *schema() const { return "file"; }
        virtual const char *location() const { return m_location; }
        virtual const IType *type() const { return m_type; }
        virtual Interface::Holder open(IStream::Mode mode = IStream::Read) const
        {
            return Interface::Holder();
        }

    public: /* IProperties */
        virtual off64_t size() const { return m_size; }
        virtual time_t cTime() const { return m_ctime; }
        virtual time_t mTime() const { return m_ctime; }
        virtual time_t aTime() const { return m_ctime; }
        virtual int permissions() const { return Read | Write; }

    private:
        char *m_location;
        const char *m_title;
        off64_t m_size;
        time_t m_ctime;
        Interface::Adaptor<IType> m_type;

    private:
        int m_index;
        boost::intrusive_ptr<libtorrent::torrent_info> m_ti;
    };

    class Dir : public Implements<IEntry, IDirectory>
    {
    public:
        Dir(const char *location) :
            m_location(::strdup(location)),
            m_title(::strrchr(m_location, '/') + 1),
            m_type(Module::desktop().typeOfDirectory())
        {}

        virtual ~Dir()
        {
            ::free(m_location);
        }

        TorrentFile::Files *entries() { return &m_entries; }

    public: /* IEntry */
        virtual const char *title() const { return m_title; }
        virtual const char *schema() const { return "file"; }
        virtual const char *location() const { return m_location; }
        virtual const IType *type() const { return m_type; }
        virtual Interface::Holder open(IStream::Mode mode = IStream::Read) const
        {
            m_error = Error(EISDIR);
            return Interface::Holder();
        }

    public: /* IDirectory */
        virtual const_iterator begin() const { return std_iterator<TorrentFile::Files>(m_entries.begin()); }
        virtual const_iterator end() const { return std_iterator<TorrentFile::Files>(m_entries.end()); }

        virtual bool exists(const char *name) const { return false; }
        virtual Interface::Holder entry(const char *name, const IType *type = NULL, bool create = false) { return Interface::Holder(); }

        virtual bool copy(const Progress &callback, const Interface::Holder &file, bool move = false) { return false; }
        virtual bool rename(const Interface::Holder &file, const char *name) { return false; }
        virtual bool remove(const Interface::Holder &file) { return false; }

        virtual const Error &lastError() const { return m_error; }

    private:
        char *m_location;
        const char *m_title;
        TorrentFile::Files m_entries;
        Interface::Adaptor<IType> m_type;
        mutable Error m_error;
    };


    struct GlobalState
    {
        time_t cTime;
        time_t creation_date;
        TorrentFile::Files files;
        boost::intrusive_ptr<libtorrent::torrent_info> ti;
    };

    struct ProcessEntryState
    {
        GlobalState &global;
        time_t ctime;
        uint32_t index;
        uint64_t length;
        EFC::String name;
        char location[Module::MaxUriLength];
    };


    static bool processEntry(TorrentFile::Files *entries, ProcessEntryState &state)
    {
        Interface::Holder entry;
        Interface::Holder entry2;
        size_t len = sizeof(state.location) - strlen(state.location);

        if (::snprintf(state.location + sizeof(state.location) - len, len, "/%s", state.name.c_str()) >= len)
            return false;

        entry.reset(new (std::nothrow) Entry(state.location, state.length, state.ctime, state.index, state.global.ti));

        if (UNLIKELY(entry.isValid() == false))
            return false;

        if ((entry2 = Module::open(entry)).isValid())
            entry = entry2;

        entries->insert(TorrentFile::Files::value_type(state.name, entry));

        return true;
    }

    static bool processFiles(TorrentFile::Files *entries, const libtorrent::lazy_entry &files, ProcessEntryState &state, const char *path_buf)
    {
        Interface::Holder entry;
        TorrentFile::Files *local_entries;

        const libtorrent::lazy_entry *path;
        const libtorrent::lazy_entry *current_file;

        for (int i = 0; i < files.list_size(); ++i)
        {
            path = files.list_at(i)->dict_find("path");
            current_file = path->list_at(0);

            local_entries = entries;
            ::strcpy(state.location, path_buf);

            if (path->list_size() > 1)
            {
                char *local_buf = state.location + strlen(state.location);

                for (int q = 0; q < path->list_size() - 1; ++q, current_file = path->list_at(q))
                {
                    if (::snprintf(local_buf,
                                   sizeof(state.location) - (local_buf - state.location),
                                   "/%s",
                                   current_file->string_cstr()) >= sizeof(state.location) - (local_buf - state.location))
                        return false;

                    local_buf += current_file->string_length() + 1;

                    EFC::String key(current_file->string_cstr());
                    TorrentFile::Files::iterator lb = local_entries->lower_bound(key);

                    if (lb != local_entries->end() && !(local_entries->key_comp()(key, lb->first)))
                        local_entries = lb->second.as<Dir>()->entries();
                    else
                    {
                        entry.reset(new (std::nothrow) Dir(state.location));

                        if (UNLIKELY(entry.isValid() == false))
                            return false;

                        local_entries->insert(lb, TorrentFile::Files::value_type(key, entry));
                        local_entries = entry.as<Dir>()->entries();
                    }
                }
            }

            state.name = current_file->string_cstr();
            state.length = files.list_at(i)->dict_find("length")->int_value();

            if (!processEntry(local_entries, state))
                return false;
        }

        return true;
    }

    static bool processFile(GlobalState &global_state, const libtorrent::lazy_entry &entry)
    {
        const libtorrent::lazy_entry *info = entry.dict_find("info");
        const libtorrent::lazy_entry *name = info->dict_find("name");
        const libtorrent::lazy_entry *files = info->dict_find("files");

        ProcessEntryState state =
        {
                global_state,
                entry.dict_find_int_value("creation date", global_state.creation_date),
                0,
                0,
                {},
                {}
        };

        if (files != NULL)
        {
            char buf[Module::MaxUriLength];

            if (::snprintf(buf, sizeof(buf), "/%s", name->string_cstr()) >= sizeof(buf))
                return false;

            Interface::Holder entry(new (std::nothrow) Dir(buf));

            if (UNLIKELY(entry.isValid() == false))
                return false;

            state.global.files.insert(TorrentFile::Files::value_type(name->string_cstr(), entry));

            if (!processFiles(entry.as<Dir>()->entries(), *files, state, buf))
                return false;
        }
        else
        {
            state.name = name->string_cstr();
            state.length = info->dict_find_int_value("length");

            if (!processEntry(&state.global.files, state))
                return false;
        }

        return true;
    }

}


TorrentFile::TorrentFile(const Interface::Holder &file) :
    ExtendsBy(file)
{}

TorrentFile::~TorrentFile()
{}

TorrentFile::const_iterator TorrentFile::begin() const
{
    if (m_files.empty())
    {
        Interface::Holder fp = original()->as<IEntry>()->open();

        if (fp.isValid())
            if (IProperties *prop = original()->as<IProperties>())
            {
                size_t len = prop->size();
                EFC::ScopedPointer<char> buffer(new (std::nothrow) char[len]);

                if (LIKELY(buffer.get() != NULL))
                {
                    if (fp->as<IStream>()->read(buffer.get(), len) != len)
                        return std_iterator<Files>(m_files.begin());

                    libtorrent::lazy_entry e;
                    libtorrent::error_code ec;

                    if (libtorrent::lazy_bdecode(buffer.get(), buffer.get() + len, e, ec) == 0)
                    {
                        GlobalState state;

                        state.cTime = prop->cTime();
                        state.ti.reset(new (std::nothrow) libtorrent::torrent_info(e, ec));

                        if (UNLIKELY(state.ti.get() == NULL))
                            return std_iterator<Files>(m_files.begin());

                        if (UNLIKELY(state.ti->is_valid() == false))
                            return std_iterator<Files>(m_files.begin());

                        if (!processFile(state, e))
                            return std_iterator<Files>(m_files.begin());

                        m_files = std::move(state.files);
                    }
                }
            }
    }

    return std_iterator<Files>(m_files.begin());
}

TorrentFile::const_iterator TorrentFile::end() const
{
    return std_iterator<Files>(m_files.end());
}

bool TorrentFile::exists(const char *name) const
{
    return false;
}

Interface::Holder TorrentFile::entry(const char *name, const IType *type, bool create)
{
    return Interface::Holder();
}

bool TorrentFile::copy(const Progress &callback, const Interface::Holder &file, bool move)
{
    return false;
}

bool TorrentFile::rename(const Interface::Holder &file, const char *name)
{
    return false;
}

bool TorrentFile::remove(const Interface::Holder &file)
{
    return false;
}

const Error &TorrentFile::lastError() const
{
    return m_lastError;
}

}}
