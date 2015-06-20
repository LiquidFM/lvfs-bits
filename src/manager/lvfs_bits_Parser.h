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

#ifndef LVFS_BITS_PARSER_H_
#define LVFS_BITS_PARSER_H_

#include <efc/List>
#include <efc/String>
#include <efc/ScopedPointer>
#include <lvfs-bits/ITorrent>


namespace LVFS {
namespace BitS {

class PLATFORM_MAKE_PRIVATE Parser
{
public:
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

        virtual ~Item();

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

        virtual ~Pair();

        inline const Item *key() const { return m_key.get(); }
        inline void setKey(Item *value) { m_key.reset(value); }

        inline const Item *value() const { return m_value.get(); }
        inline void setValue(Item *value) { m_value.reset(value); }

        virtual Type type() const;

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

        virtual ~List();

        inline const Container &items() const { return m_items; }
        inline bool add(Item *item) { return m_items.push_back(EFC::ScopedPointer<Item>(item)); }

        virtual Type type() const;

    private:
        Container m_items;
    };

    class Dictionary : public List
    {
    public:
        Dictionary(Item *parent) :
            List(parent)
        {}

        virtual ~Dictionary();

        virtual Type type() const;
    };

    class String : public Item
    {
    public:
        String(Item *parent) :
            Item(parent)
        {}

        virtual ~String();

        inline const char *value(size_t index) const { return m_value.c_str() + index; };
        inline const EFC::String &value() const { return m_value; };
        inline void setValue(const char *value, int len) { m_value = EFC::String(value, len); };

        virtual Type type() const;

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

        virtual ~Integer();

        inline uint64_t value() const { return m_value; };
        inline void setValue(uint64_t value) { m_value = value; };

        inline int sign() const { return m_sign; };
        inline void setSign(int sign) { m_sign = sign; };

        virtual Type type() const;

    private:
        int m_sign;
        uint64_t m_value;
    };

public:
    Parser();
    ~Parser();

    bool isValid() const { return m_item != NULL; }
    const char *infoHash() const { return m_info_hash; }
    const Dictionary *root() const { return m_item.get(); }

    void parse(char *buffer, size_t len);

    static void test(const Item *item, int pad);

private:
    Item *doParse(char *buffer, size_t len);

private:
    EFC::ScopedPointer<Dictionary> m_item;
    char m_info_hash[ITorrent::SizeOfHash];
};

}}

#endif /* LVFS_BITS_PARSER_H_ */
