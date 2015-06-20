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

#include "lvfs_bits_Torrent.h"
#include "lvfs_bits_Stream.h"
#include "lvfs_bits_Parser.h"

#include <lvfs/IEntry>
#include <lvfs/IStream>
#include <lvfs/IProperties>
#include <lvfs/Module>

#include <brolly/assert.h>

#include <cstring>
#include <cstdio>


namespace LVFS {
namespace BitS {

namespace {

    class Entry : public Implements<IEntry, IProperties>
    {
    public:
        Entry(const char *location, off64_t size, time_t ctime, const Interface::Holder &torrent) :
            m_location(::strdup(location)),
            m_title(::strrchr(m_location, '/') + 1),
            m_size(size),
            m_ctime(ctime),
            m_torrent(torrent)
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
            Error error;
            return Stream::open(m_torrent->announce(), m_torrent->hash(), m_torrent->size(), error);
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
        Interface::Adaptor<ITorrent> m_torrent;
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

        Torrent::Files *entries() { return &m_entries; }

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
        virtual const_iterator begin() const { return std_iterator<Torrent::Files>(m_entries.begin()); }
        virtual const_iterator end() const { return std_iterator<Torrent::Files>(m_entries.end()); }

        virtual bool exists(const char *name) const { return false; }
        virtual Interface::Holder entry(const char *name, const IType *type = NULL, bool create = false) { return Interface::Holder(); }

        virtual bool copy(const Progress &callback, const Interface::Holder &file, bool move = false) { return false; }
        virtual bool rename(const Interface::Holder &file, const char *name) { return false; }
        virtual bool remove(const Interface::Holder &file) { return false; }

        virtual const Error &lastError() const { return m_error; }

    private:
        char *m_location;
        const char *m_title;
        Torrent::Files m_entries;
        Interface::Adaptor<IType> m_type;
        mutable Error m_error;
    };


    struct GlobalState
    {
        time_t cTime;
        EFC::String announce;
        EFC::String comment;
        EFC::String created_by;
        time_t creation_date;
        EFC::String publisher;
        EFC::String publisher_url;
        uint64_t total_length;
        Torrent::Files files;
        Torrent::Pieces pieces;
        const Interface::Holder *torrent;
    };

    struct ProcessEntryState
    {
        GlobalState &global;
        time_t ctime;
        uint64_t length;
        const Parser::String *pieces;
        const EFC::String *name;
        const uint64_t pieces_count;
        const uint64_t piece_length;
        uint64_t current_piece_length;
        Torrent::Piece piece;
        char location[Module::MaxUriLength];
    };


    static bool processEntry(Torrent::Files *entries, ProcessEntryState &state)
    {
        Interface::Holder entry;
        Interface::Holder entry2;
        Torrent::Pieces::size_type index;

        if (::snprintf(state.location + strlen(state.location),
                       sizeof(state.location) - strlen(state.location),
                       "/%s", state.name->c_str()) >= sizeof(state.location) - strlen(state.location))
            return false;

        entry.reset(new (std::nothrow) Entry(state.location, state.length, state.ctime, *state.global.torrent));

        if (UNLIKELY(entry.isValid() == false))
            return false;

        if ((entry2 = Module::open(entry)).isValid())
            entry = entry2;

        entries->insert(Torrent::Files::value_type(*state.name, entry));

        for (uint64_t len = state.length; len > 0;)
            if (len >= state.current_piece_length)
            {
                index = state.piece.index + 1;

                if (index >= state.pieces_count)
                    return false;

                state.piece.files.push_back({ state.piece_length - state.current_piece_length, state.current_piece_length, entry });
                state.global.pieces.push_back(std::move(state.piece));

                state.piece.index = index;
                ::memcpy(state.piece.hash, state.pieces->value().c_str() + index * Torrent::SizeOfHash, Torrent::SizeOfHash);
                state.piece.files.clear();

                len -= state.current_piece_length;
                state.current_piece_length = state.piece_length;
            }
            else
            {
                state.piece.files.push_back({ state.piece_length - state.current_piece_length, len, entry });
                state.current_piece_length -= len;
                break;
            }

        return true;
    }

    static bool processFiles(Torrent::Files *entries, const Parser::List *files, ProcessEntryState &state, const char *path_buf)
    {
        Interface::Holder entry;
        Torrent::Files *local_entries;

        const Parser::Pair *pair;
        const Parser::Integer *length;
        const Parser::List *path;
        const Parser::String *name;

        for (auto i = files->items().begin(), end = files->items().end(); i != end; ++i)
            if ((*i)->type() == Parser::Item::DictionaryType)
            {
                length = NULL;
                path = NULL;
                local_entries = entries;
                ::strcpy(state.location, path_buf);

                for (auto q = static_cast<const Parser::Dictionary *>((*i).get())->items().begin(),
                          end = static_cast<const Parser::Dictionary *>((*i).get())->items().end();
                     q != end; ++q)
                {
                    if ((*q)->type() == Parser::Item::PairType && (pair = static_cast<const Parser::Pair *>((*q).get()))->key()->type() == Parser::Item::StringType)
                        if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "length") == 0)
                            if (length == NULL && pair->value()->type() == Parser::Item::IntegerType)
                                length = static_cast<const Parser::Integer *>(pair->value());
                            else
                                return false;
                        else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "path") == 0)
                            if (path == NULL && pair->value()->type() == Parser::Item::ListType)
                                path = static_cast<const Parser::List *>(pair->value());
                            else
                                return false;
                }

                if (length == NULL || path == NULL || path->items().size() == 0)
                    return false;

                auto current_file = path->items().begin();

                if (path->items().size() > 1)
                {
                    auto end = --path->items().end();
                    char *local_buf = state.location + strlen(state.location);

                    for (; current_file != end; ++current_file)
                    {
                        name = static_cast<const Parser::String *>((*current_file).get());

                        if (name->type() != Parser::Item::StringType)
                            return false;

                        if (::snprintf(local_buf,
                                       sizeof(state.location) - (local_buf - state.location),
                                       "/%s",
                                       name->value().c_str()) >= sizeof(state.location) - (local_buf - state.location))
                            return false;

                        local_buf += name->value().size() + 1;

                        Torrent::Files::iterator lb = local_entries->lower_bound(name->value());

                        if (lb != local_entries->end() && !(local_entries->key_comp()(name->value(), lb->first)))
                            local_entries = lb->second.as<Dir>()->entries();
                        else
                        {
                            entry.reset(new (std::nothrow) Dir(state.location));

                            if (UNLIKELY(entry.isValid() == false))
                                return false;

                            local_entries->insert(lb, Torrent::Files::value_type(name->value(), entry));
                            local_entries = entry.as<Dir>()->entries();
                        }
                    }
                }

                name = static_cast<const Parser::String *>((*current_file).get());

                if (name->type() != Parser::Item::StringType)
                    return false;

                state.name = &name->value();
                state.length = length->value();
                state.global.total_length += length->value();

                if (!processEntry(local_entries, state))
                    return false;
            }
            else
                return false;

        return true;
    }

    static bool processFile(GlobalState &global_state, const Parser::Dictionary *file)
    {
        const Parser::List *list;
        const Parser::Pair *pair;

        const Parser::String *announce = NULL;
        const Parser::String *comment = NULL;
        const Parser::String *created_by = NULL;
        const Parser::Integer *creation_date = NULL;
        const Parser::String *publisher = NULL;
        const Parser::String *publisher_url = NULL;

        const Parser::String *encoding = NULL;
        const Parser::List *files = NULL;
        const Parser::Integer *length = NULL;
        const Parser::String *name = NULL;
        const Parser::String *pieces = NULL;
        const Parser::Integer *piece_length = NULL;

        for (auto i = file->items().begin(), end = file->items().end(); i != end; ++i)
            if ((*i)->type() == Parser::Item::PairType && (pair = static_cast<Parser::Pair *>((*i).get()))->key()->type() == Parser::Item::StringType)
                if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "announce") == 0)
                    if (announce == NULL && pair->value()->type() == Parser::Item::StringType)
                        announce = static_cast<const Parser::String *>(pair->value());
                    else
                        return false;
                else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "comment") == 0)
                    if (comment == NULL && pair->value()->type() == Parser::Item::StringType)
                        comment = static_cast<const Parser::String *>(pair->value());
                    else
                        return false;
                else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "created by") == 0)
                    if (created_by == NULL && pair->value()->type() == Parser::Item::StringType)
                        created_by = static_cast<const Parser::String *>(pair->value());
                    else
                        return false;
                else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "creation date") == 0)
                    if (creation_date == NULL && pair->value()->type() == Parser::Item::IntegerType)
                        creation_date = static_cast<const Parser::Integer *>(pair->value());
                    else
                        return false;
                else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "encoding") == 0)
                    if (encoding == NULL && pair->value()->type() == Parser::Item::StringType)
                        encoding = static_cast<const Parser::String *>(pair->value());
                    else
                        return false;
                else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "info") == 0)
                {
                    if (pair->value()->type() != Parser::Item::DictionaryType || length != NULL || files != NULL || name != NULL)
                        return false;

                    list = static_cast<const Parser::Dictionary *>(pair->value());

                    for (auto i = list->items().begin(), end = list->items().end(); i != end; ++i)
                        if ((*i)->type() == Parser::Item::PairType && (pair = static_cast<Parser::Pair *>((*i).get()))->key()->type() == Parser::Item::StringType)
                            if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "files") == 0)
                                if (files == NULL && pair->value()->type() == Parser::Item::ListType)
                                    files = static_cast<const Parser::List *>(pair->value());
                                else
                                    return false;
                            else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "name") == 0)
                                if (name == NULL && pair->value()->type() == Parser::Item::StringType)
                                    name = static_cast<const Parser::String *>(pair->value());
                                else
                                    return false;
                            else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "piece length") == 0)
                                if (piece_length == NULL && pair->value()->type() == Parser::Item::IntegerType)
                                    piece_length = static_cast<const Parser::Integer *>(pair->value());
                                else
                                    return false;
                            else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "pieces") == 0)
                                if (pieces == NULL && pair->value()->type() == Parser::Item::StringType)
                                    pieces = static_cast<const Parser::String *>(pair->value());
                                else
                                    return false;
                            else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "length") == 0)
                                if (length == NULL && pair->value()->type() == Parser::Item::IntegerType)
                                    length = static_cast<const Parser::Integer *>(pair->value());
                                else
                                    return false;
                }
                else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "publisher") == 0)
                    if (publisher == NULL && pair->value()->type() == Parser::Item::StringType)
                        publisher = static_cast<const Parser::String *>(pair->value());
                    else
                        return false;
                else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "publisher-url") == 0)
                    if (publisher_url == NULL && pair->value()->type() == Parser::Item::StringType)
                        publisher_url = static_cast<const Parser::String *>(pair->value());
                    else
                        return false;

        if (announce == NULL)
            return false;
        else
            global_state.announce = std::move(announce->value());

        if (comment != NULL)
            global_state.comment = std::move(comment->value());

        if (created_by != NULL)
            global_state.created_by = std::move(created_by->value());

        if (creation_date == NULL)
            global_state.creation_date = global_state.cTime;
        else
            global_state.creation_date = creation_date->value();

        if (publisher != NULL)
            global_state.publisher = std::move(publisher->value());

        if (publisher_url != NULL)
            global_state.publisher_url = std::move(publisher_url->value());

        if (piece_length == NULL)
            return false;

        if (pieces == NULL)
            return false;

        if (name == NULL)
            return false;

        if ((length != NULL && files != NULL) || (length == NULL && files == NULL))
            return false;

        if (encoding != NULL && ::strcmp(encoding->value().c_str(), "UTF-8") != 0)
            return false;

        if (pieces->value().size() % Torrent::SizeOfHash)
            return false;

        ProcessEntryState state =
        {
                global_state,
                global_state.creation_date,
                0,
                pieces,
                NULL,
                pieces->value().size() / Torrent::SizeOfHash,
                piece_length->value(),
                piece_length->value(),
                { 0, {}, {} },
                {}
        };

        state.piece.index = 0;
        ::memcpy(state.piece.hash, pieces->value().c_str(), Torrent::SizeOfHash);

        state.global.pieces.reserve(state.pieces_count);

        if (files != NULL)
        {
            char buf[Module::MaxUriLength];

            if (::snprintf(buf, sizeof(buf), "/%s", name->value().c_str()) >= sizeof(buf))
                return false;

            Interface::Holder entry(new (std::nothrow) Dir(buf));

            if (UNLIKELY(entry.isValid() == false))
                return false;

            state.global.files.insert(Torrent::Files::value_type(name->value(), entry));

            if (!processFiles(entry.as<Dir>()->entries(), files, state, buf))
                return false;
        }
        else
        {
            state.name = &name->value();
            state.length = length->value();

            if (::snprintf(state.location, sizeof(state.location), "/%s", name->value().c_str()) >= sizeof(state.location))
                return false;

            if (!processEntry(&state.global.files, state))
                return false;
        }

        return true;
    }

}


Torrent::~Torrent()
{}

Interface::Holder Torrent::fromFile(const Interface::Holder &file, Error &error)
{
    Interface::Holder fp = file->as<IEntry>()->open();

    if (fp.isValid())
        if (IProperties *prop = file->as<IProperties>())
        {
            size_t len = prop->size();
            char *buffer = new (std::nothrow) char[len];

            if (LIKELY(buffer != NULL))
            {
                Parser parser;

                if (fp->as<IStream>()->read(buffer, len) == len)
                    parser.parse(buffer, len);

                delete [] buffer;

                if (parser.isValid())
                {
                    Interface::Holder torrent(new (std::nothrow) Torrent(parser.infoHash()));

                    if (UNLIKELY(torrent.isValid() == false))
                        return Interface::Holder();

                    GlobalState state;
                    state.cTime = prop->cTime();
                    state.total_length = 0;
                    state.torrent = &torrent;

                    if (!processFile(state, parser.root()))
                        return Interface::Holder();

                    torrent.as<Torrent>()->init(&state);
                    return torrent;
                }
            }
        }

    return Interface::Holder();
}

const char *Torrent::announce() const
{
    return m_announce.c_str();
}

const char *Torrent::comment() const
{
    return m_comment.c_str();
}

const char *Torrent::createdBy() const
{
    return m_created_by.c_str();
}

time_t Torrent::creationDate() const
{
    return m_creation_date;
}

const char *Torrent::publisher() const
{
    return m_publisher.c_str();
}

const char *Torrent::publisherUrl() const
{
    return m_publisher_url.c_str();
}

const char *Torrent::hash() const
{
    return m_info_hash;
}

uint64_t Torrent::size() const
{
    return m_size;
}

Torrent::const_iterator Torrent::begin() const
{
    return std_iterator<Files>(m_files.begin());
}

Torrent::const_iterator Torrent::end() const
{
    return std_iterator<Files>(m_files.end());
}

bool Torrent::exists(const char *name) const
{
    return false;
}

Interface::Holder Torrent::entry(const char *name, const IType *type, bool create)
{
    return Interface::Holder();
}

bool Torrent::copy(const Progress &callback, const Interface::Holder &file, bool move)
{
    return false;
}

bool Torrent::rename(const Interface::Holder &file, const char *name)
{
    return false;
}

bool Torrent::remove(const Interface::Holder &file)
{
    return false;
}

const Error &Torrent::lastError() const
{
    return m_lastError;
}

Torrent::Torrent(const char hash[SizeOfHash]) :
    m_creation_date(0),
    m_size(0)
{
    ::memcpy(m_info_hash, hash, SizeOfHash);
}

void Torrent::init(const void *state)
{
    m_announce = std::move(static_cast<const GlobalState *>(state)->announce);
    m_comment = std::move(static_cast<const GlobalState *>(state)->comment);
    m_created_by = std::move(static_cast<const GlobalState *>(state)->created_by);
    m_creation_date = std::move(static_cast<const GlobalState *>(state)->creation_date);
    m_publisher = std::move(static_cast<const GlobalState *>(state)->publisher);
    m_publisher_url = std::move(static_cast<const GlobalState *>(state)->publisher_url);
    m_size = std::move(static_cast<const GlobalState *>(state)->total_length);
    m_files = std::move(static_cast<const GlobalState *>(state)->files);
    m_pieces = std::move(static_cast<const GlobalState *>(state)->pieces);
}

}}
