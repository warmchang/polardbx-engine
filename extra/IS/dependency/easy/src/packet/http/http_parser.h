/*****************************************************************************

Copyright (c) 2023, 2024, Alibaba and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License, version 2.0, as published by the
Free Software Foundation.

This program is also distributed with certain software (including but not
limited to OpenSSL) that is licensed under separate terms, as designated in a
particular file or component or in included license documentation. The authors
of MySQL hereby grant you an additional permission to link the program and
your derivative works with the separately licensed software that they have
included with MySQL.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License, version 2.0,
for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

*****************************************************************************/


/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef http_parser_h
#define http_parser_h
#ifdef __cplusplus
extern "C" {
#endif


#include <sys/types.h>
#if defined(_WIN32) && !defined(__MINGW32__)
typedef __int8          int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16         int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32         int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64         int64_t;
typedef unsigned __int64 uint64_t;

typedef unsigned int    size_t;
typedef int             ssize_t;
#else
#include <stdint.h>
#endif

/* Compile with -DHTTP_PARSER_STRICT=0 to make less checks, but run
 * faster
 */
#ifndef HTTP_PARSER_STRICT
# define HTTP_PARSER_STRICT 1
#else
# define HTTP_PARSER_STRICT 0
#endif


/* Maximium header size allowed */
#define HTTP_MAX_HEADER_SIZE 65535


typedef struct http_parser http_parser;
typedef struct http_parser_settings http_parser_settings;


/* Callbacks should return non-zero to indicate an error. The parser will
 * then halt execution.
 *
 * The one exception is on_headers_complete. In a HTTP_RESPONSE parser
 * returning '1' from on_headers_complete will tell the parser that it
 * should not expect a body. This is used when receiving a response to a
 * HEAD request which may contain 'Content-Length' or 'Transfer-Encoding:
 * chunked' headers that indicate the presence of a body.
 *
 * http_data_cb does not return data chunks. It will be call arbitrarally
 * many times for each string. E.G. you might get 10 callbacks for "on_path"
 * each providing just a few characters more data.
 */
typedef int (*http_data_cb) (http_parser *, const char *at, size_t length);
typedef int (*http_cb) (http_parser *);


/* Request Methods */
enum http_method {
    HTTP_DELETE    = 0
    , HTTP_GET
    , HTTP_HEAD
    , HTTP_POST
    , HTTP_PUT
    , HTTP_PURGE
    /* pathological */
    , HTTP_CONNECT
    , HTTP_OPTIONS
    , HTTP_TRACE
    /* webdav */
    , HTTP_COPY
    , HTTP_LOCK
    , HTTP_MKCOL
    , HTTP_MOVE
    , HTTP_PROPFIND
    , HTTP_PROPPATCH
    , HTTP_UNLOCK
    /* subversion */
    , HTTP_REPORT
    , HTTP_MKACTIVITY
    , HTTP_CHECKOUT
    , HTTP_MERGE
    /* upnp */
    , HTTP_MSEARCH
    , HTTP_NOTIFY
    , HTTP_SUBSCRIBE
    , HTTP_UNSUBSCRIBE
};


enum http_parser_type { HTTP_REQUEST, HTTP_RESPONSE, HTTP_BOTH };


struct http_parser {
    /** PRIVATE **/
    unsigned char           type : 2;
    unsigned char           flags : 6;
    unsigned char           state;
    unsigned char           header_state;
    unsigned char           index;

    int64_t                 content_length;

    /** READ-ONLY **/
    unsigned short          http_major;
    unsigned short          http_minor;
    unsigned short          status_code; /* responses only */
    unsigned char           method;    /* requests only */

    /* 1 = Upgrade header was present and the parser has exited because of that.
     * 0 = No upgrade header present.
     * Should be checked when http_parser_execute() returns in addition to
     * error checking.
     */
    char                    upgrade;

    /* 1 = Ignore case when parsing the HTTP methods. e.g. GET == gEt
     * 0 = Do not ignore case. (default)
     */
    char                    method_case_insensitive;

    /** PUBLIC **/
    void                    *data; /* A pointer to get hook to the "connection" or "socket" object */
};


struct http_parser_settings {
    http_cb                 on_message_begin;
    http_data_cb            on_path;
    http_data_cb            on_query_string;
    http_data_cb            on_url;
    http_data_cb            on_proto;
    http_data_cb            on_host;
    http_data_cb            on_fragment;
    http_data_cb            on_header_field;
    http_data_cb            on_header_value;
    http_cb                 on_headers_complete;
    http_data_cb            on_body;
    http_cb                 on_message_complete;
};

#define method_strings easy_method_strings
extern const char       *easy_method_strings[];

void http_parser_init(http_parser *parser, enum http_parser_type type);


size_t http_parser_execute(http_parser *parser,
                           const http_parser_settings *settings,
                           const char *data,
                           size_t len);


/* If http_should_keep_alive() in the on_headers_complete or
 * on_message_complete callback returns true, then this will be should be
 * the last message on the connection.
 * If you are the server, respond with the "Connection: close" header.
 * If you are the client, close the connection.
 */
int http_should_keep_alive(http_parser *parser);

/* Returns a string version of the HTTP method. */
const char *http_method_str(enum http_method);

int http_parser_has_error(http_parser *parser);

#ifdef __cplusplus
}
#endif
#endif
