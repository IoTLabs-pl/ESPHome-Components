/*
 Copyright (C) 2021-2022 Fredrik Öhrström (gpl-3.0-or-later)

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include"translatebits.h"
#include"util.h"

#include<assert.h>
#include<string.h>

using namespace Translate;

TriggerBits AlwaysTrigger(~(uint64_t)0);
MaskBits AutoMask(0);

void handleBitToString(Rule& rule, std::string &out_s, uint64_t bits)
{
    std::string s;

    if (rule.trigger != AlwaysTrigger && (bits & rule.trigger.intValue()) == 0 )
    {
        // The trigger bits are needed and there are no trigger bits. Ignore this rule.
        return;
    }

    uint64_t mask = rule.mask.intValue();

    if (rule.mask == AutoMask)
    {
        mask = 0;
        for (Map& m : rule.map)
        {
            // Collect all listed bits as the mask.
            mask |= m.from;
        }
    }

    bits = bits & mask;
    for (Map& m : rule.map)
    {
        if ((~mask & m.from) != 0)
        {
            // Check that the match rule does not extend outside of the mask!
            // If mask is 0xff then a match for 0x100 will trigger this bad warning!
            std::string tmp = tostrprintf("BAD_RULE_%s(from=0x%x mask=0x%x)", rule.name.c_str(), m.from, mask);
            s += tmp+" ";
        }

        uint64_t from = m.from & mask; // Better safe than sorry.

        if (m.test == TestBit::Set)
        {
            if ((bits & from) != 0 )
            {
                s += m.to+" ";
                bits = bits & ~m.from; // Remove the handled bit.
            }
        }

        if (m.test == TestBit::NotSet)
        {
            if ((bits & from) == 0)
            {
                s += m.to+" ";
            }
            else
            {
                bits = bits & ~m.from; // Remove the handled bit.
            }
        }
    }
    if (bits != 0)
    {
        // Oups, there are set bits that we have not handled....
        std::string tmp;
        strprintf(&tmp, "%s_%X", rule.name.c_str(), bits);
        s += tmp+" ";
    }

    if (s == "")
    {
        s = rule.default_message.stringValue()+" ";
    }

    out_s += s;
}

void handleIndexToString(Rule& rule, std::string &out_s, uint64_t bits)
{
    std::string s;

    if (rule.trigger != AlwaysTrigger && (bits & rule.trigger.intValue()) == 0 )
    {
        // The trigger bits are needed and there are no trigger bits. Ignore this rule.
        return;
    }

    uint64_t mask = rule.mask.intValue();

    if (rule.mask == AutoMask)
    {
        mask = 0;
        for (Map& m : rule.map)
        {
            // Collect all listed bits as the mask.
            mask |= m.from;
        }
    }

    bits = bits & mask;
    bool found = false;
    for (Map& m : rule.map)
    {
        assert(m.test == TestBit::Set);

        if ((~mask & m.from) != 0)
        {
            std::string tmp;
            strprintf(&tmp, "BAD_RULE_%s(from=0x%x mask=0x%x)", rule.name.c_str(), m.from, rule.mask);
            s += tmp+" ";
        }
        uint64_t from = m.from & mask; // Better safe than sorry.
        if (bits == from)
        {
            s += m.to+" ";
            found = true;
        }
    }
    if (!found)
    {
        // Oups, this index has not been found.
        std::string tmp;
        strprintf(&tmp, "%s_%X", rule.name.c_str(), bits);
        s += tmp+" ";
    }

    out_s += s;
}

void handleDecimalsToString(Rule& rule, std::string &out_s, uint64_t bits)
{
    std::string s;

    if (rule.trigger != AlwaysTrigger && (bits & rule.trigger.intValue()) == 0 )
    {
        // The trigger bits are needed and there are no trigger bits. Ignore this rule.
        return;
    }

    uint64_t mask = rule.mask.intValue();

    if (rule.mask == AutoMask)
    {
        mask = 0;
        for (Map& m : rule.map)
        {
            // Collect all listed bits as the mask.
            mask |= m.from;
        }
    }

    // Switch to signed number here.
    int number = bits % mask;
    if (number == 0)
    {
        s += rule.default_message.stringValue()+" ";
    }
    for (Map& m : rule.map)
    {
        assert(m.test == TestBit::Set);

        if ((m.from - (m.from % mask)) != 0)
        {
            std::string tmp;
            strprintf(&tmp, "BAD_RULE_%s(from=%d modulomask=%d)", rule.name.c_str(), m.from, rule.mask);
            s += tmp+" ";
        }
        int num = m.from % mask; // Better safe than sorry.
        if ((number - num) >= 0)
        {
            s += m.to+" ";
            number -= num;
        }
    }
    if (number > 0)
    {
        // Oups, this number has not been fully understood.
        std::string tmp;
        strprintf(&tmp, "%s_%d", rule.name.c_str(), number);
        s += tmp+" ";
    }

    out_s += s;
}

void handleRule(Rule& rule, std::string &s, uint64_t bits)
{
    switch (rule.type)
    {
    case MapType::BitToString:
        handleBitToString(rule, s, bits);
        break;

    case MapType::IndexToString:
        handleIndexToString(rule, s, bits);
        break;

    case MapType::DecimalsToString:
        handleDecimalsToString(rule, s, bits);
        break;

    default:
        assert(0);
    }
}

std::string Lookup::translate(uint64_t bits)
{
    std::string total = "";

    for (Rule& r : rules)
    {
        std::string s;
        handleRule(r, s, bits);
        total = joinStatusEmptyStrings(total, s);
    }

    while (total.size() > 0 && total.back() == ' ') total.pop_back();

    return sortStatusString(total);
}

std::string Lookup::str()
{
    std::string x = " Lookup {\n";

    for (Rule& r : rules)
    {
        x += "    Rulex {\n";
        x += "        name = "+r.name+"\n";
        x += "    }\n";
    }

    x += "}\n";

    return x;
}

Translate::MapType toMapType(const char *s)
{
    if (!strcmp(s, "BitToString")) return Translate::MapType::BitToString;
    if (!strcmp(s, "IndexToString")) return Translate::MapType::IndexToString;
    if (!strcmp(s, "DecimalsToString")) return Translate::MapType::DecimalsToString;
    return Translate::MapType::Unknown;
}

Lookup NoLookup = {};


Map m = { 123, "howdy" };
std::vector<Map> vm = { { 123, "howdy" } };

Rule r = { "name", Translate::MapType::IndexToString,
    AlwaysTrigger, MaskBits(0xe000),  "", { } };
