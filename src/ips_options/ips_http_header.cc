/*
** Copyright (C) 2014 Cisco and/or its affiliates. All rights reserved.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
// ips_http_header.cc author Russ Combs <rucombs@cisco.com>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string>
using namespace std;

#include "snort_types.h"
#include "parser.h"
#include "snort_debug.h"
#include "snort.h"
#include "profiler.h"
#include "flow/flow.h"
#include "detection/detection_defines.h"
#include "framework/ips_option.h"
#include "framework/cursor.h"
#include "framework/inspector.h"
#include "framework/module.h"

static const char* s_name = "http_header";

static THREAD_LOCAL ProfileStats httpHeaderPerfStats;

static const Parameter hh_params[] =
{
    { "~name", Parameter::PT_STRING, nullptr, nullptr,
      "restrict to given header" },

    { nullptr, Parameter::PT_MAX, nullptr, nullptr, nullptr }
};

//-------------------------------------------------------------------------
// module
//-------------------------------------------------------------------------

class HttpHeaderModule : public Module
{
public:
    HttpHeaderModule() : Module(s_name, hh_params) { };

    bool begin(const char*, int, SnortConfig*);
    bool set(const char*, Value&, SnortConfig*);

    ProfileStats* get_profile() const
    { return &httpHeaderPerfStats; };

public:
    string name;
};

bool HttpHeaderModule::begin(const char*, int, SnortConfig*)
{
    name.clear();
    return true;
}

bool HttpHeaderModule::set(const char*, Value& v, SnortConfig*)
{
    if ( v.is("~name") )
        name = v.get_string();

    else
        return false;

    return true;
}

//-------------------------------------------------------------------------
// generic header getter
//-------------------------------------------------------------------------

class HttpHeaderOption : public IpsOption
{
public:
    HttpHeaderOption(string& s) : IpsOption(s_name)
    { name = s; };

    CursorActionType get_cursor_type() const
    { return CAT_SET_OTHER; };

    int eval(Cursor&, Packet*);

private:
    string name;
};

static bool find(
    const string& s, const InspectionBuffer& b, Cursor& c)
{
    const char* h = s.c_str();
    unsigned k = s.size();

    const uint8_t* t = b.data;
    unsigned n = b.len;

    // find the start of header
    do
    {
        if ( n < k )
            return false;

        if ( !strncasecmp(h, (char*)t, k) &&
             !strncmp((char*)t+k, ": ", 2) )
            break;

        t = (uint8_t*)memchr(t, '\n', n);

        if ( !t )
            return false;

        n = b.len - (++t - b.data);
    }
    while ( true );

    // skip over the keyword to the data
    t += k + 2;

    // now find the end of header
    const uint8_t* z = (uint8_t*)memchr(t, '\n', n);

    if ( z )
    {
        while ( isspace(z[-1]) && (z > t) )
            --z;
        n = z - t;
    }
    c.set(h, t, n);
    return true;
}

int HttpHeaderOption::eval(Cursor& c, Packet* p)
{
    PROFILE_VARS;
    MODULE_PROFILE_START(httpHeaderPerfStats);

    int rval;
    InspectionBuffer hb;

    if ( !p->flow || !p->flow->gadget )
        rval = DETECTION_OPTION_NO_MATCH;

    // FIXIT-P cache id at parse time for runtime use
    else if ( !p->flow->gadget->get_buf(s_name, p, hb) )
        rval = DETECTION_OPTION_NO_MATCH;

    else if ( !name.size() )
    {
        c.set(s_name, hb.data, hb.len);
        rval = DETECTION_OPTION_MATCH;
    }
    else if ( find(name, hb, c) )
        rval = DETECTION_OPTION_MATCH;

    else
        rval = DETECTION_OPTION_NO_MATCH;

    MODULE_PROFILE_END(httpHeaderPerfStats);
    return rval;
}

//-------------------------------------------------------------------------
// api
//-------------------------------------------------------------------------

static Module* mod_ctor()
{
    return new HttpHeaderModule;
}

static void mod_dtor(Module* m)
{
    delete m;
}

static IpsOption* hh_ctor(Module* m, OptTreeNode*)
{
    HttpHeaderModule* mod = (HttpHeaderModule*)m;
    return new HttpHeaderOption(mod->name);
}

static void hh_dtor(IpsOption* p)
{
    delete p;
}

static const IpsApi header_api =
{
    {
        PT_IPS_OPTION,
        s_name,
        IPSAPI_PLUGIN_V0,
        0,
        mod_ctor,
        mod_dtor
    },
    OPT_TYPE_DETECTION,
    0, PROTO_BIT__TCP,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    hh_ctor,
    hh_dtor,
    nullptr
};

#ifdef BUILDING_SO
SO_PUBLIC const BaseApi* snort_plugins[] =
{
    &header_api.base,
    nullptr
};
#else
const BaseApi* ips_http_header = &header_api.base;
#endif

