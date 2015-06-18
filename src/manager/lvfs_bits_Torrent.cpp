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
#include "lvfs_bits_sha1.h"

#include <lvfs/IEntry>
#include <lvfs/IStream>
#include <lvfs/IProperties>
#include <lvfs/Module>

#include <efc/ScopedPointer>
#include <brolly/assert.h>

#include <cstring>
#include <cstdio>


namespace LVFS {
namespace BitS {

namespace {

    class Item
    {
    public:
        enum Type
        {
            DictionaryType,
            PairType,
            ListType,
            StringType,
            IntegerType
        };

    public:
        Item(Item *parent) :
            m_parent(parent)
        {}

        virtual ~Item()
        {}

        inline Item *parent() const { return m_parent; }
        virtual Type type() const = 0;

    private:
        Item *m_parent;
    };

    class Pair : public Item
    {
    public:
        Pair(Item *parent) :
            Item(parent)
        {}

        virtual ~Pair()
        {}

        inline const Item *key() const { return m_key.get(); }
        inline void setKey(Item *value) { m_key.reset(value); }

        inline const Item *value() const { return m_value.get(); }
        inline void setValue(Item *value) { m_value.reset(value); }

        virtual Type type() const { return PairType; }

    private:
        EFC::ScopedPointer<Item> m_key;
        EFC::ScopedPointer<Item> m_value;

    };

    class List : public Item
    {
    public:
        typedef EFC::List<EFC::ScopedPointer<Item>> Container;

    public:
        List(Item *parent) :
            Item(parent)
        {}

        virtual ~List()
        {}

        inline const Container &items() const { return m_items; }
        inline bool add(Item *item) { return m_items.push_back(EFC::ScopedPointer<Item>(item)); }

        virtual Type type() const { return ListType; }

    private:
        Container m_items;
    };

    class Dictionary : public List
    {
    public:
        Dictionary(Item *parent) :
            List(parent)
        {}

        virtual ~Dictionary()
        {}

        virtual Type type() const { return DictionaryType; }
    };

    class String : public Item
    {
    public:
        String(Item *parent) :
            Item(parent)
        {}

        virtual ~String()
        {}

        inline const char *value(size_t index) const { return m_value.c_str() + index; };
        inline const EFC::String &value() const { return m_value; };
        inline void setValue(const char *value, int len) { m_value = EFC::String(value, len); };

        virtual Type type() const { return StringType; }

    private:
        EFC::String m_value;
    };

    class Integer : public Item
    {
    public:
        Integer(Item *parent) :
            Item(parent),
            m_sign(1),
            m_value(0)
        {}

        virtual ~Integer()
        {}

        inline uint64_t value() const { return m_value; };
        inline void setValue(uint64_t value) { m_value = value; };

        inline int sign() const { return m_sign; };
        inline void setSign(int sign) { m_sign = sign; };

        virtual Type type() const { return IntegerType; }

    private:
        int m_sign;
        uint64_t m_value;
    };


    static Item *parseBencode(char *buffer, size_t len, char *info_hash)
    {
        Item *item = NULL;
        Pair *pair2;
        int str_len;
        const char *info_hash_begin = NULL;

        int ignore_e = 0; /* XXX: It is needed to join embedded lists (with 'll' prefix). */

        for (char *p2, *p1 = buffer; len > 0;)
            switch (*p1)
            {
                case 'd':
                {
                    if (item == NULL)
                    {
                        item = new (std::nothrow) Dictionary(item);

                        if (UNLIKELY(item == NULL))
                            return NULL;
                    }
                    else if (item->type() == Item::PairType && static_cast<Pair *>(item)->key())
                    {
                        EFC::ScopedPointer<Item> dictionary(new (std::nothrow) Dictionary(item));

                        if (UNLIKELY(dictionary.get() == NULL))
                        {
                            while (item->parent())
                                item = item->parent();

                            delete item;
                            return NULL;
                        }

                        static_cast<Pair *>(item)->setValue(dictionary.get());
                        item = dictionary.release();
                    }
                    else if (item->type() == Item::ListType)
                    {
                        EFC::ScopedPointer<Item> dictionary(new (std::nothrow) Dictionary(item));

                        if (UNLIKELY(dictionary.get() == NULL))
                        {
                            while (item->parent())
                                item = item->parent();

                            delete item;
                            return NULL;
                        }

                        if (UNLIKELY(static_cast<List *>(item)->add(dictionary.get()) == false))
                        {
                            while (item->parent())
                                item = item->parent();

                            delete item;
                            return NULL;
                        }

                        item = dictionary.release();
                    }
                    else
                    {
                        while (item->parent())
                            item = item->parent();

                        delete item;
                        return NULL;
                    }


                    ++p1;
                    --len;
                    break;
                }

                case 'l':
                {
                    if (item == NULL)
                        return NULL;
                    else if (item->type() == Item::PairType && static_cast<Pair *>(item)->key())
                    {
                        EFC::ScopedPointer<Item> list(new (std::nothrow) List(item));

                        if (UNLIKELY(list.get() == NULL))
                        {
                            while (item->parent())
                                item = item->parent();

                            delete item;
                            return NULL;
                        }

                        static_cast<Pair *>(item)->setValue(list.get());
                        item = list.release();
                    }
                    else if (item->type() == Item::ListType)
                        ++ignore_e;
                    else
                    {
                        while (item->parent())
                            item = item->parent();

                        delete item;
                        return NULL;
                    }

                    ++p1;
                    --len;
                    break;
                }

                case 'e':
                {
                    if (item == NULL)
                        return NULL;

                    if (item->type() == Item::IntegerType)
                    {
                        *p1 = 0;
                        static_cast<Integer *>(item)->setValue(atoll(p2));

                        if (item->parent()->type() == Item::PairType)
                            if (static_cast<Pair *>(item->parent())->value())
                                item = item->parent()->parent();
                            else
                                item = item->parent();
                        else
                            item = item->parent();
                    }
                    else
                        if (ignore_e)
                            --ignore_e;
                        else
                            if (item->parent())
                                if (item->parent()->type() == Item::PairType)
                                {
                                    if (info_hash_begin)
                                    {
                                        int level = 1;
                                        Item *tmp = item->parent();

                                        for (; tmp->parent(); tmp = tmp->parent(), ++level)
                                            continue;

                                        if (level == 2)
                                        {
                                            sha1_context ctx;

                                            sha1_starts(&ctx);
                                            sha1_update(&ctx, reinterpret_cast<const uint8 *>(info_hash_begin), p1 - info_hash_begin + 1);
                                            sha1_finish(&ctx, reinterpret_cast<uint8 *>(info_hash));

                                            info_hash_begin = NULL;
                                        }
                                    }

                                    item = item->parent()->parent();
                                }
                                else if (item->parent()->type() == Item::ListType)
                                    item = item->parent();
                                else
                                {
                                    while (item->parent())
                                        item = item->parent();

                                    delete item;
                                    return NULL;
                                }

                    ++p1;
                    --len;
                    break;
                }

                default:
                {
                    if (item == NULL)
                        return NULL;

                    switch (item->type())
                    {
                        case Item::DictionaryType:
                        {
                            EFC::ScopedPointer<Pair> pair(new (std::nothrow) Pair(item));

                            if (UNLIKELY(pair.get() == NULL))
                            {
                                while (item->parent())
                                    item = item->parent();

                                delete item;
                                return NULL;
                            }

                            if (UNLIKELY(static_cast<Dictionary *>(item)->add(pair.get()) == false))
                            {
                                while (item->parent())
                                    item = item->parent();

                                delete item;
                                return NULL;
                            }

                            EFC::ScopedPointer<Item> key;
                            pair2 = pair.release();

                            if (*p1 == 'i')
                            {
                                p2 = ++p1;
                                --len;
                                key.reset(new (std::nothrow) Integer(pair2));
                            }
                            else if (*p1 >= '1' && *p1 <= '9')
                            {
                                p2 = p1++;
                                --len;
                                key.reset(new (std::nothrow) String(pair2));
                            }

                            if (UNLIKELY(key.get() == NULL))
                            {
                                while (item->parent())
                                    item = item->parent();

                                delete item;
                                return NULL;
                            }

                            pair2->setKey(key.get());
                            item = key.release();

                            break;
                        }

                        case Item::ListType:
                        {
                            EFC::ScopedPointer<Item> value;

                            if (*p1 == 'i')
                            {
                                p2 = ++p1;
                                --len;
                                value.reset(new (std::nothrow) Integer(item));
                            }
                            else if (*p1 >= '1' && *p1 <= '9')
                            {
                                p2 = p1++;
                                --len;
                                value.reset(new (std::nothrow) String(item));
                            }

                            if (UNLIKELY(value.get() == NULL))
                            {
                                while (item->parent())
                                    item = item->parent();

                                delete item;
                                return NULL;
                            }

                            if (UNLIKELY(static_cast<List *>(item)->add(value.get()) == false))
                            {
                                while (item->parent())
                                    item = item->parent();

                                delete item;
                                return NULL;
                            }

                            item = value.release();

                            break;
                        }

                        case Item::PairType:
                        {
                            EFC::ScopedPointer<Item> value;

                            if (*p1 == 'i')
                            {
                                p2 = ++p1;
                                --len;
                                value.reset(new (std::nothrow) Integer(item));
                            }
                            else if (*p1 >= '1' && *p1 <= '9')
                            {
                                p2 = p1++;
                                --len;
                                value.reset(new (std::nothrow) String(item));
                            }

                            if (UNLIKELY(value.get() == NULL))
                            {
                                while (item->parent())
                                    item = item->parent();

                                delete item;
                                return NULL;
                            }

                            static_cast<Pair *>(item)->setValue(value.get());
                            item = value.release();

                            break;
                        }

                        case Item::StringType:
                        {
                            if (*p1 == ':')
                            {
                                *p1++ = 0;
                                --len;
                                static_cast<String *>(item)->setValue(p1, str_len = atoi(p2));
                                p1 += str_len;
                                len -= str_len;

                                if (item->parent()->type() == Item::PairType)
                                    if (static_cast<Pair *>(item->parent())->value())
                                        item = item->parent()->parent();
                                    else
                                    {
                                        if (::strcmp(static_cast<String *>(item)->value().c_str(), "info") == 0)
                                        {
                                            int level = 1;
                                            Item *tmp = item->parent();

                                            for (; tmp->parent(); tmp = tmp->parent(), ++level)
                                                continue;

                                            if (level == 2)
                                                info_hash_begin = p1;
                                        }

                                        item = item->parent();
                                    }
                                else
                                    item = item->parent();
                            }
                            else if (*p1 >= '0' && *p1 <= '9')
                            {
                                ++p1;
                                --len;
                            }
                            else
                            {
                                while (item->parent())
                                    item = item->parent();

                                delete item;
                                return NULL;
                            }

                            break;
                        }

                        case Item::IntegerType:
                        {
                            if (LIKELY(p2 != p1))
                            {
                                if (*p1 < '0' || *p1 > '9')
                                {
                                    while (item->parent())
                                        item = item->parent();

                                    delete item;
                                    return NULL;
                                }
                            }
                            else
                                if (*p1 != '-' && (*p1 < '1' || *p1 > '9') && (len < 2 || *p1 != '0' || p1[1] != 'e'))
                                {
                                    while (item->parent())
                                        item = item->parent();

                                    delete item;
                                    return NULL;
                                }
                                else
                                    static_cast<Integer *>(item)->setSign(*p1 == '-' ? -1 : 1);

                            ++p1;
                            --len;
                            break;
                        }

                        default:
                        {
                            while (item->parent())
                                item = item->parent();

                            delete item;
                            return NULL;
                        }
                    }

                    break;
                }
            }

        return item;
    }

    static void test(Item *item, int pad)
    {
        for (int i = 0; i < pad; ++i)
            ::printf("\t");

        if (item->type() == Item::DictionaryType || item->type() == Item::ListType)
        {
            for (auto i = static_cast<List *>(item)->items().begin(), end = static_cast<List *>(item)->items().end(); i != end; ++i)
                test(i->get(), pad + 1);
        }
        else if (item->type() == Item::PairType)
        {
            if (static_cast<Pair *>(item)->key()->type() == Item::StringType)
                ::printf("%s:", static_cast<const String *>(static_cast<Pair *>(item)->key())->value().c_str());
            else
                ::printf("%ld:", static_cast<const Integer *>(static_cast<Pair *>(item)->key())->value());

            if (static_cast<Pair *>(item)->value()->type() == Item::StringType)
                ::printf("%s\n", static_cast<const String *>(static_cast<Pair *>(item)->value())->value().c_str());
            else if (static_cast<Pair *>(item)->value()->type() == Item::IntegerType)
                ::printf("%ld\n", static_cast<const Integer *>(static_cast<Pair *>(item)->value())->value());
            else if (static_cast<Pair *>(item)->value()->type() == Item::DictionaryType ||
                     static_cast<Pair *>(item)->value()->type() == Item::ListType)
            {
                ::printf("[\n");

                for (auto i = static_cast<const List *>(static_cast<Pair *>(item)->value())->items().begin(), end = static_cast<const List *>(static_cast<Pair *>(item)->value())->items().end(); i != end; ++i)
                    test(i->get(), pad + 1);

                for (int i = 0; i < pad; ++i)
                    ::printf("\t");

                ::printf("]\n");
            }
        }
        else if (item->type() == Item::StringType)
            ::printf("%s\n", static_cast<const String *>(item)->value().c_str());
        else if (item->type() == Item::IntegerType)
            ::printf("%ld\n", static_cast<const Integer *>(item)->value());
    }


    class Entry : public Implements<IEntry, IProperties>
    {
    public:
        Entry(const char *location, off64_t size, time_t ctime) :
            m_location(::strdup(location)),
            m_title(::strrchr(m_location, '/') + 1),
            m_size(size),
            m_ctime(ctime)
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
        virtual Interface::Holder open(IStream::Mode mode = IStream::Read) const { return Interface::Holder(); }

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
        Torrent::Files files;
        Torrent::Pieces pieces;
    };

    struct ProcessEntryState
    {
        GlobalState &global;
        time_t ctime;
        uint64_t length;
        const String *pieces;
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

        entry.reset(new (std::nothrow) Entry(state.location, state.length, state.ctime));

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

    static bool processFiles(Torrent::Files *entries, const List *files, ProcessEntryState &state, const char *path_buf)
    {
        Interface::Holder entry;
        Torrent::Files *local_entries;

        const Pair *pair;
        const Integer *length;
        const List *path;
        const String *name;

        for (auto i = files->items().begin(), end = files->items().end(); i != end; ++i)
            if ((*i)->type() == Item::DictionaryType)
            {
                length = NULL;
                path = NULL;
                local_entries = entries;
                ::strcpy(state.location, path_buf);

                for (auto q = static_cast<const Dictionary *>((*i).get())->items().begin(),
                          end = static_cast<const Dictionary *>((*i).get())->items().end();
                     q != end; ++q)
                {
                    if ((*q)->type() == Item::PairType && (pair = static_cast<const Pair *>((*q).get()))->key()->type() == Item::StringType)
                        if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "length") == 0)
                            if (length == NULL && pair->value()->type() == Item::IntegerType)
                                length = static_cast<const Integer *>(pair->value());
                            else
                                return false;
                        else if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "path") == 0)
                            if (path == NULL && pair->value()->type() == Item::ListType)
                                path = static_cast<const List *>(pair->value());
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
                        name = static_cast<const String *>((*current_file).get());

                        if (name->type() != Item::StringType)
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

                name = static_cast<const String *>((*current_file).get());

                if (name->type() != Item::StringType)
                    return false;

                state.name = &name->value();
                state.length = length->value();

                if (!processEntry(local_entries, state))
                    return false;
            }
            else
                return false;

        return true;
    }

    static bool processFile(GlobalState &global_state, const Dictionary *file)
    {
        const List *list;
        const Pair *pair;

        const String *announce = NULL;
        const String *comment = NULL;
        const String *created_by = NULL;
        const Integer *creation_date = NULL;
        const String *publisher = NULL;
        const String *publisher_url = NULL;

        const String *encoding = NULL;
        const List *files = NULL;
        const Integer *length = NULL;
        const String *name = NULL;
        const String *pieces = NULL;
        const Integer *piece_length = NULL;

        for (auto i = file->items().begin(), end = file->items().end(); i != end; ++i)
            if ((*i)->type() == Item::PairType && (pair = static_cast<Pair *>((*i).get()))->key()->type() == Item::StringType)
                if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "announce") == 0)
                    if (announce == NULL && pair->value()->type() == Item::StringType)
                        announce = static_cast<const String *>(pair->value());
                    else
                        return false;
                else if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "comment") == 0)
                    if (comment == NULL && pair->value()->type() == Item::StringType)
                        comment = static_cast<const String *>(pair->value());
                    else
                        return false;
                else if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "created by") == 0)
                    if (created_by == NULL && pair->value()->type() == Item::StringType)
                        created_by = static_cast<const String *>(pair->value());
                    else
                        return false;
                else if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "creation date") == 0)
                    if (creation_date == NULL && pair->value()->type() == Item::IntegerType)
                        creation_date = static_cast<const Integer *>(pair->value());
                    else
                        return false;
                else if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "encoding") == 0)
                    if (encoding == NULL && pair->value()->type() == Item::StringType)
                        encoding = static_cast<const String *>(pair->value());
                    else
                        return false;
                else if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "info") == 0)
                {
                    if (pair->value()->type() != Item::DictionaryType || length != NULL || files != NULL || name != NULL)
                        return false;

                    list = static_cast<const Dictionary *>(pair->value());

                    for (auto i = list->items().begin(), end = list->items().end(); i != end; ++i)
                        if ((*i)->type() == Item::PairType && (pair = static_cast<Pair *>((*i).get()))->key()->type() == Item::StringType)
                            if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "files") == 0)
                                if (files == NULL && pair->value()->type() == Item::ListType)
                                    files = static_cast<const List *>(pair->value());
                                else
                                    return false;
                            else if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "name") == 0)
                                if (name == NULL && pair->value()->type() == Item::StringType)
                                    name = static_cast<const String *>(pair->value());
                                else
                                    return false;
                            else if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "piece length") == 0)
                                if (piece_length == NULL && pair->value()->type() == Item::IntegerType)
                                    piece_length = static_cast<const Integer *>(pair->value());
                                else
                                    return false;
                            else if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "pieces") == 0)
                                if (pieces == NULL && pair->value()->type() == Item::StringType)
                                    pieces = static_cast<const String *>(pair->value());
                                else
                                    return false;
                            else if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "length") == 0)
                                if (length == NULL && pair->value()->type() == Item::IntegerType)
                                    length = static_cast<const Integer *>(pair->value());
                                else
                                    return false;
                }
                else if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "publisher") == 0)
                    if (publisher == NULL && pair->value()->type() == Item::StringType)
                        publisher = static_cast<const String *>(pair->value());
                    else
                        return false;
                else if (::strcmp(static_cast<const String *>(pair->key())->value().c_str(), "publisher-url") == 0)
                    if (publisher_url == NULL && pair->value()->type() == Item::StringType)
                        publisher_url = static_cast<const String *>(pair->value());
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
                char info_hash[Torrent::SizeOfHash];
                EFC::ScopedPointer<Item> item;

                if (fp->as<IStream>()->read(buffer, len) == len)
                    item.reset(parseBencode(buffer, len, info_hash));

                delete [] buffer;

                if (item != NULL && item->type() == Item::DictionaryType)
                {
                    GlobalState state;
                    state.cTime = prop->cTime();

                    if (processFile(state, static_cast<Dictionary *>(item.get())))
                        return Interface::Holder(new (std::nothrow) Torrent(&state, info_hash));
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

Torrent::Torrent(const void *state, const char hash[SizeOfHash]) :
    m_announce(std::move(static_cast<const GlobalState *>(state)->announce)),
    m_comment(std::move(static_cast<const GlobalState *>(state)->comment)),
    m_created_by(std::move(static_cast<const GlobalState *>(state)->created_by)),
    m_creation_date(std::move(static_cast<const GlobalState *>(state)->creation_date)),
    m_publisher(std::move(static_cast<const GlobalState *>(state)->publisher)),
    m_publisher_url(std::move(static_cast<const GlobalState *>(state)->publisher_url)),
    m_files(std::move(static_cast<const GlobalState *>(state)->files)),
    m_pieces(std::move(static_cast<const GlobalState *>(state)->pieces))
{
    ::memcpy(m_info_hash, hash, SizeOfHash);
}

}}
