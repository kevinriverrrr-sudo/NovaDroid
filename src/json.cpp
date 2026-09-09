// ============================================================================
//  NovaDroid - json.cpp  Minimal recursive-descent JSON parser/writer.
// ============================================================================
#include "json.h"
#include <cmath>
#include <cstdio>
#include <sstream>
#include <iomanip>

namespace {

struct Parser {
    const std::wstring& s;
    size_t i = 0;
    std::wstring err;
    int depth = 0;

    explicit Parser(const std::wstring& src) : s(src) {}

    void skipWs() {
        while (i < s.size()) {
            wchar_t c = s[i];
            if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') ++i; else break;
        }
    }
    bool fail(const wchar_t* msg) {
        if (err.empty()) err = std::wstring(msg) + L" (at " + std::to_wstring((long long)i) + L")";
        return false;
    }
    bool peek(wchar_t c) { skipWs(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    bool expect(wchar_t c) { return peek(c) ? true : fail((std::wstring(L"expected '") + c + L"'").c_str()); }

    bool parseValue(JValue& out) {
        if (++depth > 64) return fail(L"too deep");
        skipWs();
        if (i >= s.size()) { --depth; return fail(L"unexpected end"); }
        wchar_t c = s[i];
        bool ok = false;
        if (c == L'{') ok = parseObj(out);
        else if (c == L'[') ok = parseArr(out);
        else if (c == L'"') { out.type = JValue::Str; ok = parseStr(out.str); }
        else if (c == L't') { ok = match(L"true");  if (ok) { out.type = JValue::Bool; out.b = true; } }
        else if (c == L'f') { ok = match(L"false"); if (ok) { out.type = JValue::Bool; out.b = false; } }
        else if (c == L'n') { ok = match(L"null");  if (ok) out.type = JValue::Null; }
        else ok = parseNum(out);
        --depth;
        return ok;
    }
    bool match(const wchar_t* lit) {
        size_t n = wcslen(lit);
        if (s.compare(i, n, lit) == 0) { i += n; return true; }
        return fail(L"literal expected");
    }
    bool parseNum(JValue& out) {
        skipWs();
        size_t start = i;
        if (i < s.size() && (s[i] == L'-' || s[i] == L'+')) ++i;
        bool digits = false;
        while (i < s.size() && ((s[i] >= L'0' && s[i] <= L'9') || s[i] == L'.' ||
              s[i] == L'e' || s[i] == L'E' || s[i] == L'-' || s[i] == L'+')) {
            if (s[i] >= L'0' && s[i] <= L'9') digits = true;
            ++i;
        }
        if (!digits) return fail(L"number expected");
        std::wstring tok = s.substr(start, i - start);
        out.type = JValue::Num;
        out.num = wcstod(tok.c_str(), nullptr);
        return true;
    }
    void utf16Push(std::wstring& out, unsigned cp) {
        if (cp >= 0xD800 && cp <= 0xDBFF) return;      // high surrogate, handled below
        out.push_back((wchar_t)cp);
    }
    bool parseEscape(std::wstring& out) {
        if (i >= s.size()) return fail(L"bad escape");
        wchar_t c = s[i++];
        switch (c) {
            case L'"':  out += L'"';  break;
            case L'\\': out += L'\\'; break;
            case L'/':  out += L'/';  break;
            case L'b':  out += L'\b'; break;
            case L'f':  out += L'\f'; break;
            case L'n':  out += L'\n'; break;
            case L'r':  out += L'\r'; break;
            case L't':  out += L'\t'; break;
            case L'u': {
                if (i + 4 > s.size()) return fail(L"bad \\u");
                unsigned cp = (unsigned)wcstoul(s.substr(i, 4).c_str(), nullptr, 16);
                i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() &&
                    s[i] == L'\\' && s[i + 1] == L'u') {
                    unsigned lo = (unsigned)wcstoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                }
                if (cp < 0x10000) out.push_back((wchar_t)cp);
                else { cp -= 0x10000; out.push_back((wchar_t)(0xD800 + (cp >> 10)));
                       out.push_back((wchar_t)(0xDC00 + (cp & 0x3FF))); }
                break;
            }
            default: return fail(L"bad escape char");
        }
        return true;
    }
    bool parseStr(std::wstring& out) {
        if (i >= s.size() || s[i] != L'"') return fail(L"string expected");
        ++i;
        while (i < s.size()) {
            wchar_t c = s[i++];
            if (c == L'"') return true;
            if (c == L'\\') { if (!parseEscape(out)) return false; }
            else out += c;
        }
        return fail(L"unterminated string");
    }
    bool parseObj(JValue& out) {
        out.type = JValue::Obj;
        ++i; // {
        skipWs();
        if (peek(L'}')) return true;
        while (true) {
            skipWs();
            std::wstring key;
            if (!parseStr(key)) return false;
            if (!expect(L':')) return false;
            JValue v;
            if (!parseValue(v)) return false;
            out.obj[key] = std::move(v);
            if (peek(L',')) continue;
            if (peek(L'}')) return true;
            return fail(L"expected ',' or '}'");
        }
    }
    bool parseArr(JValue& out) {
        out.type = JValue::Arr;
        ++i; // [
        skipWs();
        if (peek(L']')) return true;
        while (true) {
            JValue v;
            if (!parseValue(v)) return false;
            out.arr.push_back(std::move(v));
            if (peek(L',')) continue;
            if (peek(L']')) return true;
            return fail(L"expected ',' or ']'");
        }
    }
};

void writeStr(std::wstring& o, const std::wstring& s) {
    o += L'"';
    for (wchar_t c : s) {
        switch (c) {
            case L'"':  o += L"\\\""; break;
            case L'\\': o += L"\\\\"; break;
            case L'\b': o += L"\\b";  break;
            case L'\f': o += L"\\f";  break;
            case L'\n': o += L"\\n";  break;
            case L'\r': o += L"\\r";  break;
            case L'\t': o += L"\\t";  break;
            default:
                if ((unsigned)c < 0x20) {
                    wchar_t buf[8];
                    swprintf(buf, 8, L"\\u%04x", (unsigned)c);
                    o += buf;
                } else o += c;
        }
    }
    o += L'"';
}

void writeNum(std::wstring& o, double d) {
    if (std::isfinite(d) && std::floor(d) == d && std::fabs(d) < 1e15) {
        wchar_t buf[32];
        swprintf(buf, 32, L"%lld", (long long)d);
        o += buf;
    } else {
        std::wstringstream ws;
        ws << std::setprecision(15) << d;
        o += ws.str();
    }
}

void writeVal(std::wstring& o, const JValue& v) {
    switch (v.type) {
        case JValue::Null: o += L"null"; break;
        case JValue::Bool: o += v.b ? L"true" : L"false"; break;
        case JValue::Num:  writeNum(o, v.num); break;
        case JValue::Str:  writeStr(o, v.str); break;
        case JValue::Arr: {
            o += L'[';
            bool first = true;
            for (auto& e : v.arr) {
                if (!first) o += L',';
                first = false;
                writeVal(o, e);
            }
            o += L']';
            break;
        }
        case JValue::Obj: {
            o += L'{';
            bool first = true;
            for (auto& kv : v.obj) {
                if (!first) o += L',';
                first = false;
                writeStr(o, kv.first);
                o += L':';
                writeVal(o, kv.second);
            }
            o += L'}';
            break;
        }
    }
}

} // namespace

bool JsonParse(const std::wstring& s, JValue& out, std::wstring& err) {
    Parser p(s);
    out = JValue();
    if (!p.parseValue(out)) { err = p.err; return false; }
    p.skipWs();
    if (p.i != s.size()) { err = L"trailing data"; return false; }
    return true;
}

std::wstring JsonWrite(const JValue& v) {
    std::wstring o;
    writeVal(o, v);
    return o;
}
