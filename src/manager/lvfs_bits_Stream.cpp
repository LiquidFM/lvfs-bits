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

#include "lvfs_bits_Stream.h"
#include "lvfs_bits_Parser.h"

#include <lvfs/Module>

#include <brolly/assert.h>
#include <curl/curl.h>

#include <random>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <errno.h>


namespace LVFS {
namespace BitS {

namespace {

    struct Memory
    {
        char *memory;
        size_t size;
    };

}


Stream::Stream(const char *tracker)
{}

Stream::~Stream()
{

}

Interface::Holder Stream::open(const char *announce, const char *hash, uint64_t length, Error &error)
{
    ASSERT(announce != NULL);
    ASSERT(hash != NULL);

    CURL *handle;
    CURLcode res;
    Memory chunk = { NULL, 0 };
    char peer_id[ITorrent::SizeOfHash];
    char buffer[Module::MaxUriLength];
    size_t size = Module::MaxUriLength;

    curl_global_init(CURL_GLOBAL_ALL);

    /* init the curl session */
    handle = curl_easy_init();

    createId(peer_id);
    createUri(handle, buffer, size, announce, hash, peer_id, 50001, 0, 0, length, None);

    if (buffer[0] == 0)
    {
        /* cleanup curl stuff */
        curl_easy_cleanup(handle);

        /* we're done with libcurl, so clean it up */
        curl_global_cleanup();

        return Interface::Holder();
    }

    /* specify URL to get */
    curl_easy_setopt(handle, CURLOPT_URL, buffer);

    /* send all data to this function  */
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeCallback);

    /* we pass our 'chunk' struct to the callback function */
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, (void *)&chunk);

    /* tell libcurl to follow redirection */
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);

    /* HTTP/2 please */
    curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);

    /* we use a self-signed test server, skip verification during debugging */
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);

    /* some servers don't like requests that are made without a user-agent
       field, so we provide one */
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "lvfs-bits/1.0");

    /* get it! */
    res = curl_easy_perform(handle);

    /* cleanup curl stuff */
    curl_easy_cleanup(handle);

    /* we're done with libcurl, so clean it up */
    curl_global_cleanup();

    /* check for errors */
    if (res != CURLE_OK)
    {
        free(chunk.memory);
        return Interface::Holder();
    }

    Parser parser;
    parser.parse(chunk.memory, chunk.size);
    free(chunk.memory);

    if (parser.isValid())
    {
        const Parser::List *list;
        const Parser::Pair *pair;

        const Parser::Item *peers = NULL;
        const Parser::String *failure = NULL;
        const Parser::String *warning = NULL;
        const Parser::Integer *interval = NULL;
        const Parser::Integer *min_interval = NULL;

        for (auto i = parser.root()->items().begin(), end = parser.root()->items().end(); i != end; ++i)
            if ((*i)->type() == Parser::Item::PairType && (pair = static_cast<Parser::Pair *>((*i).get()))->key()->type() == Parser::Item::StringType)
                if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "interval") == 0)
                    if (interval == NULL && pair->value()->type() == Parser::Item::IntegerType)
                        interval = static_cast<const Parser::Integer *>(pair->value());
                    else
                        return Interface::Holder();
                else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "min interval") == 0)
                    if (min_interval == NULL && pair->value()->type() == Parser::Item::IntegerType)
                        min_interval = static_cast<const Parser::Integer *>(pair->value());
                    else
                        return Interface::Holder();
                else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "warning message") == 0)
                    if (warning == NULL && pair->value()->type() == Parser::Item::StringType)
                        warning = static_cast<const Parser::String *>(pair->value());
                    else
                        return Interface::Holder();
                else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "failure reason") == 0)
                    if (failure == NULL && pair->value()->type() == Parser::Item::StringType)
                        failure = static_cast<const Parser::String *>(pair->value());
                    else
                        return Interface::Holder();
                else if (::strcmp(static_cast<const Parser::String *>(pair->key())->value().c_str(), "peers") == 0)
                    if (peers == NULL)
                        peers = pair->value();
                    else
                        return Interface::Holder();

        parser.test(parser.root(), 0);

        if (failure || interval == NULL || peers == NULL)
            return Interface::Holder();

        if (peers->type() == Parser::Item::StringType)
        {
            const Parser::String *p = static_cast<const Parser::String *>(peers);

            if (p->value().size() % 6)
                return Interface::Holder();

            printf("There are %d peers\n", p->value().size() / 6);

        }
        else if (peers->type() == Parser::Item::ListType)
        {

        }
    }

    return Interface::Holder();
}

size_t Stream::read(void *buffer, size_t size)
{
    return 0;
}

size_t Stream::write(const void *buffer, size_t size)
{
    m_lastError = Error(EROFS);
    return 0;
}

bool Stream::advise(off64_t offset, off64_t len, Advise advise)
{
    return false;
}

bool Stream::seek(off64_t offset, Whence whence)
{
    return false;
}

bool Stream::flush()
{
    return false;
}

const Error &Stream::lastError() const
{
    return m_lastError;
}

void Stream::createUri(void *curl,
                       char *uri, size_t &size,
                       const char *announce,
                       const char info_hash[ITorrent::SizeOfHash],
                       const char peer_id[ITorrent::SizeOfHash],
                       unsigned short port,
                       uint64_t uploaded,
                       uint64_t downloaded,
                       uint64_t left,
                       Event event)
{
    static const char info_hash_str[] = "info_hash=";
    static const char peer_id_str[] = "&peer_id=";
    static const char port_str[] = "&port=";
    static const char uploaded_str[] = "&uploaded=";
    static const char downloaded_str[] = "&downloaded=";
    static const char left_str[] = "&left=";
    static const char event_None_str[] = "&event=";
    static const char event_Started_str[] = "&event=started";
    static const char event_Completed_str[] = "&event=completed";
    static const char event_Stopped_str[] = "&event=stopped";
    static const char *event_str[] = { event_None_str, event_Started_str, event_Completed_str, event_Stopped_str };
    static const size_t event_str_len[] = { sizeof(event_None_str), sizeof(event_Started_str), sizeof(event_Completed_str), sizeof(event_Stopped_str) };

    enum
    {
        MaxPortLength = 5,
        MinSizeLength = 10,
        MinLength = 1 /* First char '?' or '&' */ +
                    sizeof(info_hash_str)         +
                    ITorrent::SizeOfHash          +
                    sizeof(peer_id_str)           +
                    ITorrent::SizeOfHash          +
                    sizeof(port_str)              +
                    MaxPortLength                 +
                    sizeof(uploaded_str)          +
                    MinSizeLength                 +
                    sizeof(downloaded_str)        +
                    MinSizeLength                 +
                    sizeof(left_str)              +
                    MinSizeLength                 +
                    sizeof(event_Completed_str)
    };

    size_t min = MinLength;
    size_t len = strlen(announce);
    char *escaped;
    int res;

    char *p = uri;
    *p = 0;

    if (len + min > size)
        return;

    ::memcpy(p, announce, len);
    p += len;
    size -= len;

    if (::strrchr(announce, '?'))
    {
        *p++ = '&';
        --size;
        --min;
    }
    else
    {
        *p++ = '?';
        --size;
        --min;
    }

    ::memcpy(p, info_hash_str, sizeof(info_hash_str) - 1);
    p += sizeof(info_hash_str) - 1;
    size -= sizeof(info_hash_str) - 1;
    min -= sizeof(info_hash_str) - 1;

    escaped = curl_easy_escape(curl, info_hash, ITorrent::SizeOfHash);

    if (escaped == NULL)
    {
        uri[0] = 0;
        return;
    }

    len = strlen(escaped);

    if (len + (min + len - ITorrent::SizeOfHash) > size)
    {
        uri[0] = 0;
        return;
    }

    ::memcpy(p, escaped, len);
    p += len;
    size -= len;
    min -= ITorrent::SizeOfHash;
    curl_free(escaped);

    ::memcpy(p, peer_id_str, sizeof(peer_id_str) - 1);
    p += sizeof(peer_id_str) - 1;
    size -= sizeof(peer_id_str) - 1;
    min -= sizeof(peer_id_str) - 1;

    escaped = curl_easy_escape(curl, peer_id, ITorrent::SizeOfHash);

    if (escaped == NULL)
    {
        uri[0] = 0;
        return;
    }

    len = strlen(escaped);

    if (len + (min + len - ITorrent::SizeOfHash) > size)
    {
        uri[0] = 0;
        return;
    }

    ::memcpy(p, escaped, len);
    p += len;
    size -= len;
    min -= ITorrent::SizeOfHash;
    curl_free(escaped);

    ::memcpy(p, port_str, sizeof(port_str) - 1);
    p += sizeof(port_str) - 1;
    size -= sizeof(port_str) - 1;
    min -= sizeof(port_str) - 1;

    if ((res = std::snprintf(p, size, "%" PRIu16, port)) > 0)
    {
        p += res;
        size -= res;
        min -= MaxPortLength;
    }
    else
    {
        uri[0] = 0;
        return;
    }

    ::memcpy(p, uploaded_str, sizeof(uploaded_str) - 1);
    p += sizeof(uploaded_str) - 1;
    size -= sizeof(uploaded_str) - 1;
    min -= sizeof(uploaded_str) - 1;

    if ((res = std::snprintf(p, size, "%" PRIu64, uploaded)) > 0)
    {
        p += res;
        size -= res;
        min -= MinSizeLength;

        if (min > size)
        {
            uri[0] = 0;
            return;
        }
    }
    else
    {
        uri[0] = 0;
        return;
    }

    ::memcpy(p, downloaded_str, sizeof(downloaded_str) - 1);
    p += sizeof(downloaded_str) - 1;
    size -= sizeof(downloaded_str) - 1;
    min -= sizeof(downloaded_str) - 1;

    if ((res = std::snprintf(p, size, "%" PRIu64, downloaded)) > 0)
    {
        p += res;
        size -= res;
        min -= MinSizeLength;

        if (min > size)
        {
            uri[0] = 0;
            return;
        }
    }
    else
    {
        uri[0] = 0;
        return;
    }

    ::memcpy(p, left_str, sizeof(left_str) - 1);
    p += sizeof(left_str) - 1;
    size -= sizeof(left_str) - 1;
    min -= sizeof(left_str) - 1;

    if ((res = std::snprintf(p, size, "%" PRIu64, left)) > 0)
    {
        p += res;
        size -= res;
        min -= MinSizeLength;

        if (min > size)
        {
            uri[0] = 0;
            return;
        }
    }
    else
    {
        uri[0] = 0;
        return;
    }

    ::memcpy(p, event_str[event], event_str_len[event]);
    size -= event_str_len[event];
}

void Stream::createId(char id[ITorrent::SizeOfHash])
{
    // Seed with a real random value, if available
    std::random_device rd;

    // Choose a random mean between 1 and 10
    std::default_random_engine e1(rd());
    std::uniform_int_distribution<int> uniform_dist(1, 10);
    int mean = uniform_dist(e1);

    // Generate a normal distribution around that mean
    std::mt19937 e2(rd());
    std::normal_distribution<> normal_dist(mean, 2);

    for (int n = 0; n < ITorrent::SizeOfHash; ++n)
        id[n] = std::round(normal_dist(e2));
}

size_t Stream::writeCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    Memory *mem = (Memory *)userp;

    if (mem->memory == NULL)
        mem->memory = (char *)malloc(mem->size + realsize + 1);
    else
        mem->memory = (char *)realloc(mem->memory, mem->size + realsize + 1);

    if (mem->memory == NULL)
      return 0;

    memcpy(mem->memory + mem->size, contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

}}
