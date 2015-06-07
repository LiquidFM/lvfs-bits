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
#include "manager/lvfs_bits_sha1.h"

#include <lvfs/IProperties>

#include <efc/List>
#include <efc/String>
#include <efc/ScopedPointer>

#include <cstdlib>
#include <cstring>

#include <cstdio>


namespace LVFS {
namespace BitS {

class PLATFORM_MAKE_PRIVATE TorrentFile::Item
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

class PLATFORM_MAKE_PRIVATE TorrentFile::Pair : public Item
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

class PLATFORM_MAKE_PRIVATE TorrentFile::List : public Item
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

class PLATFORM_MAKE_PRIVATE TorrentFile::Dictionary : public List
{
public:
    Dictionary(Item *parent) :
        List(parent)
    {}

    virtual ~Dictionary()
    {}

    virtual Type type() const { return DictionaryType; }
};

class PLATFORM_MAKE_PRIVATE TorrentFile::String : public Item
{
public:
    String(Item *parent) :
        Item(parent)
    {}

    virtual ~String()
    {}

    inline const char *value() const { return m_value.c_str(); };
    inline void setValue(const char *value, int len) { m_value = EFC::String(value, len); };

    virtual Type type() const { return StringType; }

private:
    EFC::String m_value;
};

class PLATFORM_MAKE_PRIVATE TorrentFile::Integer : public Item
{
public:
    Integer(Item *parent) :
        Item(parent),
        m_value(0)
    {}

    virtual ~Integer()
    {}

    inline long value() const { return m_value; };
    inline void setValue(long value) { m_value = value; };

    virtual Type type() const { return IntegerType; }

private:
    long m_value;
};


TorrentFile::TorrentFile(const Interface::Holder &file) :
    ExtendsBy(file)
{}

TorrentFile::~TorrentFile()
{}

TorrentFile::const_iterator TorrentFile::begin() const
{
    Interface::Holder fp = original()->as<IEntry>()->open();

    if (fp.isValid())
        if (IProperties *prop = original()->as<IProperties>())
        {
            int len = prop->size();
            char *buffer = new (std::nothrow) char[len];

            if (LIKELY(buffer != NULL))
            {
                char info_hash[20];
                EFC::ScopedPointer<Item> item;

                if (fp->as<IFile>()->read(buffer, len) == len)
                    item.reset(parseFile(buffer, len, info_hash));

                delete [] buffer;

                if (item != NULL && item->type() == Item::DictionaryType)
                    const_cast<TorrentFile *>(this)->processFile(static_cast<Dictionary *>(item.get()));
            }
        }

    return const_iterator();
}

TorrentFile::const_iterator TorrentFile::end() const
{
    return const_iterator();
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

TorrentFile::Item *TorrentFile::parseFile(char *buffer, int len, char *info_hash) const
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
                    static_cast<Integer *>(item)->setValue(atol(p2));

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
                                    if (::strcmp(static_cast<String *>(item)->value(), "info") == 0)
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

void TorrentFile::processFile(Dictionary *file)
{
    const List *list;
    const Pair *pair;

    const Item *encoding = NULL;
    const Item *length = NULL;
    const Item *files = NULL;
    const Item *name = NULL;

    for (auto i = file->items().begin(), end = file->items().end(); i != end; ++i)
        if ((*i)->type() == Item::PairType && (pair = static_cast<Pair *>((*i).get()))->key()->type() == Item::StringType)
            if (::strcmp(static_cast<const String *>(pair->key())->value(), "encoding") == 0)
                if (encoding == NULL)
                    encoding = pair->value();
                else
                {
                    m_item.reset();
                    return;
                }
            else if (::strcmp(static_cast<const String *>(pair->key())->value(), "info") == 0)
            {
                if (pair->value()->type() != Item::DictionaryType || length != NULL || files != NULL || name != NULL)
                {
                    m_item.reset();
                    return;
                }

                list = static_cast<const Dictionary *>(pair->value());

                for (auto i = list->items().begin(), end = list->items().end(); i != end; ++i)
                    if ((*i)->type() == Item::PairType && (pair = static_cast<Pair *>((*i).get()))->key()->type() == Item::StringType)
                        if (::strcmp(static_cast<const String *>(pair->key())->value(), "files") == 0)
                            if (files == NULL)
                                files = pair->value();
                            else
                            {
                                m_item.reset();
                                return;
                            }
                        else if (::strcmp(static_cast<const String *>(pair->key())->value(), "length") == 0)
                            if (length == NULL)
                                length = pair->value();
                            else
                            {
                                m_item.reset();
                                return;
                            }
                        else if (::strcmp(static_cast<const String *>(pair->key())->value(), "name") == 0)
                            if (name == NULL)
                                name = pair->value();
                            else
                            {
                                m_item.reset();
                                return;
                            }
            }

    if (encoding != NULL &&
        (encoding->type() != Item::StringType ||
         ::strcmp(static_cast<const String *>(encoding)->value(), "UTF-8") != 0))
    {
        m_item.reset();
        return;
    }

    if (name == NULL || name->type() != Item::StringType)
    {
        m_item.reset();
        return;
    }

    if ((length != NULL && files != NULL) || (length == NULL && files == NULL))
    {
        m_item.reset();
        return;
    }

}

void TorrentFile::Deleter::operator()(Item *item) const
{
    delete item;
}

void TorrentFile::test(Item *item, int pad) const
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
            ::printf("%s:", static_cast<const String *>(static_cast<Pair *>(item)->key())->value());
        else
            ::printf("%ld:", static_cast<const Integer *>(static_cast<Pair *>(item)->key())->value());

        if (static_cast<Pair *>(item)->value()->type() == Item::StringType)
            ::printf("%s\n", static_cast<const String *>(static_cast<Pair *>(item)->value())->value());
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
        ::printf("%s\n", static_cast<const String *>(item)->value());
    else if (item->type() == Item::IntegerType)
        ::printf("%ld\n", static_cast<const Integer *>(item)->value());
}

}}
