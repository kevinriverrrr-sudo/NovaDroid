// ============================================================================
//  NovaDroid - json.h  Minimal self-contained JSON parser/writer (UTF-8 in/out
//  as std::wstring for values). Pure C++17, no Win32 - host-testable.
// ============================================================================
#pragma once
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <cstdint>

struct JValue {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0.0;
    std::wstring str;
    std::vector<JValue> arr;
    std::map<std::wstring, JValue> obj;

    JValue() = default;
    static JValue MakeStr(const std::wstring& s) { JValue v; v.type = Str; v.str = s; return v; }
    static JValue MakeNum(double d)              { JValue v; v.type = Num; v.num = d; return v; }
    static JValue MakeBool(bool b2)              { JValue v; v.type = Bool; v.b = b2; return v; }

    bool isStr() const { return type == Str; }
    bool isNum() const { return type == Num; }
    const JValue* find(const std::wstring& k) const {
        if (type != Obj) return nullptr;
        auto it = obj.find(k);
        return it == obj.end() ? nullptr : &it->second;
    }
    int  asInt(int def = 0) const { return type == Num ? (int)num : def; }
    long long asInt64(long long def = 0) const { return type == Num ? (long long)num : def; }
    bool asBool(bool def = false) const {
        if (type == Bool) return b;
        if (type == Num) return num != 0;
        return def;
    }
    std::wstring asStr(const std::wstring& def = L"") const { return type == Str ? str : def; }
};

// Parse UTF-8-less wide text (values are wide). Returns false + err.
bool JsonParse(const std::wstring& s, JValue& out, std::wstring& err);
// Serialize. Strings escaped, numbers integral when whole.
std::wstring JsonWrite(const JValue& v);
