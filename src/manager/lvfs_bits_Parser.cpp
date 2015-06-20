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

#include "lvfs_bits_Parser.h"
#include "lvfs_bits_sha1.h"

#include <brolly/assert.h>

#include <cstdio>
#include <cstring>


namespace LVFS {
namespace BitS {

Parser::Item::~Item()
{}

Parser::Pair::~Pair()
{}

Parser::Item::Type Parser::Pair::type() const
{
    return PairType;
}

Parser::List::~List()
{}

Parser::Item::Type Parser::List::type() const
{
    return ListType;
}

Parser::Dictionary::~Dictionary()
{}

Parser::Item::Type Parser::Dictionary::type() const
{
    return DictionaryType;
}

Parser::String::~String()
{}

Parser::Item::Type Parser::String::type() const
{
    return StringType;
}

Parser::Integer::~Integer()
{}

Parser::Item::Type Parser::Integer::type() const
{
    return IntegerType;
}

Parser::Parser()
{}

Parser::~Parser()
{}

void Parser::parse(char *buffer, size_t len)
{
    ASSERT(buffer != NULL);
    m_item.reset();

    if (Item *item = doParse(buffer, len))
        if (item->type() == Item::DictionaryType)
            m_item.reset(static_cast<Dictionary *>(item));
        else
            delete item;
}

Parser::Item *Parser::doParse(char *buffer, size_t len)
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
                                        sha1_finish(&ctx, reinterpret_cast<uint8 *>(m_info_hash));

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

void Parser::test(const Item *item, int pad)
{
    for (int i = 0; i < pad; ++i)
        ::printf("\t");

    if (item->type() == Item::DictionaryType || item->type() == Item::ListType)
    {
        for (auto i = static_cast<const List *>(item)->items().begin(), end = static_cast<const List *>(item)->items().end(); i != end; ++i)
            test(i->get(), pad + 1);
    }
    else if (item->type() == Item::PairType)
    {
        if (static_cast<const Pair *>(item)->key()->type() == Item::StringType)
            ::printf("%s:", static_cast<const String *>(static_cast<const Pair *>(item)->key())->value().c_str());
        else
            ::printf("%ld:", static_cast<const Integer *>(static_cast<const Pair *>(item)->key())->value());

        if (static_cast<const Pair *>(item)->value()->type() == Item::StringType)
            ::printf("%s\n", static_cast<const String *>(static_cast<const Pair *>(item)->value())->value().c_str());
        else if (static_cast<const Pair *>(item)->value()->type() == Item::IntegerType)
            ::printf("%ld\n", static_cast<const Integer *>(static_cast<const Pair *>(item)->value())->value());
        else if (static_cast<const Pair *>(item)->value()->type() == Item::DictionaryType ||
                 static_cast<const Pair *>(item)->value()->type() == Item::ListType)
        {
            ::printf("[\n");

            for (auto i = static_cast<const List *>(static_cast<const Pair *>(item)->value())->items().begin(), end = static_cast<const List *>(static_cast<const Pair *>(item)->value())->items().end(); i != end; ++i)
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

}}
