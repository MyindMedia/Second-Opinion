#include "MiniJson.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace so::song::json
{

const Value* Value::find (std::string_view key) const
{
    if (type != Type::Object)
        return nullptr;
    for (const auto& [k, v] : object)
        if (k == key)
            return &v;
    return nullptr;
}

namespace
{
    struct Reader
    {
        std::string_view s;
        size_t pos { 0 };
        int depth { 0 };

        void skipWs()
        {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t'))
                ++pos;
        }

        bool consume (char c)
        {
            skipWs();
            if (pos < s.size() && s[pos] == c) { ++pos; return true; }
            return false;
        }

        bool literal (std::string_view word)
        {
            if (s.substr (pos, word.size()) == word) { pos += word.size(); return true; }
            return false;
        }

        static void appendUtf8 (std::string& out, unsigned cp)
        {
            if (cp < 0x80) out += (char) cp;
            else if (cp < 0x800) { out += (char) (0xC0 | (cp >> 6)); out += (char) (0x80 | (cp & 0x3F)); }
            else { out += (char) (0xE0 | (cp >> 12)); out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F)); }
        }

        std::optional<std::string> str()
        {
            if (! consume ('"'))
                return std::nullopt;
            std::string out;
            while (pos < s.size())
            {
                const char c = s[pos++];
                if (c == '"')
                    return out;
                if (c != '\\') { out += c; continue; }
                if (pos >= s.size()) return std::nullopt;
                const char e = s[pos++];
                switch (e)
                {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u':
                    {
                        if (pos + 4 > s.size()) return std::nullopt;
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            const char h = s[pos++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (unsigned) (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (unsigned) (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (unsigned) (h - 'A' + 10);
                            else return std::nullopt;
                        }
                        appendUtf8 (out, cp);
                        break;
                    }
                    default: return std::nullopt;
                }
            }
            return std::nullopt;
        }

        std::optional<Value> value()
        {
            skipWs();
            if (pos >= s.size() || ++depth > 64)
                return std::nullopt;

            std::optional<Value> result;
            const char c = s[pos];

            if (c == '{')
            {
                ++pos;
                Value v = Value::makeObject();
                if (! consume ('}'))
                {
                    do
                    {
                        auto k = str();
                        if (! k || ! consume (':')) return std::nullopt;
                        auto member = value();
                        if (! member) return std::nullopt;
                        v.set (std::move (*k), std::move (*member));
                    } while (consume (','));
                    if (! consume ('}')) return std::nullopt;
                }
                result = std::move (v);
            }
            else if (c == '[')
            {
                ++pos;
                Value v = Value::makeArray();
                if (! consume (']'))
                {
                    do
                    {
                        auto item = value();
                        if (! item) return std::nullopt;
                        v.array.push_back (std::move (*item));
                    } while (consume (','));
                    if (! consume (']')) return std::nullopt;
                }
                result = std::move (v);
            }
            else if (c == '"')
            {
                auto t = str();
                if (! t) return std::nullopt;
                result = Value::makeString (std::move (*t));
            }
            else if (literal ("true"))  { Value v; v.type = Value::Type::Bool; v.boolean = true; result = v; }
            else if (literal ("false")) { Value v; v.type = Value::Type::Bool; result = v; }
            else if (literal ("null"))  { result = Value {}; }
            else
            {
                const size_t start = pos;
                while (pos < s.size() && (std::string_view ("+-.eE0123456789").find (s[pos]) != std::string_view::npos))
                    ++pos;
                if (pos == start) return std::nullopt;
                const std::string num (s.substr (start, pos - start));
                char* end = nullptr;
                const double d = std::strtod (num.c_str(), &end);
                if (end == nullptr || *end != '\0' || ! std::isfinite (d)) return std::nullopt;
                result = Value::makeNumber (d);
            }

            --depth;
            return result;
        }
    };

    std::string_view stripFence (std::string_view t)
    {
        const auto open = t.find ("```");
        if (open == std::string_view::npos)
            return t;
        auto rest = t.substr (open + 3);
        const auto nl = rest.find ('\n');
        if (nl == std::string_view::npos)
            return t;
        rest = rest.substr (nl + 1);
        const auto close = rest.find ("```");
        return close == std::string_view::npos ? rest : rest.substr (0, close);
    }

    std::string number (double d)
    {
        if (std::fabs (d - std::round (d)) < 1e-9)
            return std::to_string ((long long) std::llround (d));
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.3f", d);
        std::string out (buf);
        while (! out.empty() && out.back() == '0') out.pop_back();
        if (! out.empty() && out.back() == '.') out.pop_back();
        if (out == "-0") out = "0";
        return out;
    }
}

std::optional<Value> parse (std::string_view text)
{
    Reader r { stripFence (text) };
    auto v = r.value();
    r.skipWs();
    if (! v || r.pos != r.s.size())
        return std::nullopt;
    return v;
}

std::string escape (std::string_view s)
{
    std::string out = "\"";
    for (char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char) c < 0x20)
                {
                    char buf[8];
                    std::snprintf (buf, sizeof (buf), "\\u%04x", (unsigned) (unsigned char) c);
                    out += buf;
                }
                else
                    out += c;
        }
    }
    return out + "\"";
}

std::string write (const Value& v, int indent)
{
    const std::string pad ((size_t) indent * 2, ' ');
    const std::string inner ((size_t) (indent + 1) * 2, ' ');

    switch (v.type)
    {
        case Value::Type::Null:   return "null";
        case Value::Type::Bool:   return v.boolean ? "true" : "false";
        case Value::Type::Number: return number (v.number);
        case Value::Type::String: return escape (v.string);
        case Value::Type::Array:
        {
            if (v.array.empty()) return "[]";
            bool simple = true;
            for (const auto& a : v.array)
                if (a.isArray() || a.isObject()) simple = false;
            std::string out = "[";
            for (size_t i = 0; i < v.array.size(); ++i)
            {
                out += simple ? (i ? ", " : "") : (i ? ",\n" + inner : "\n" + inner);
                out += write (v.array[i], indent + 1);
            }
            return out + (simple ? "]" : "\n" + pad + "]");
        }
        case Value::Type::Object:
        {
            if (v.object.empty()) return "{}";
            std::string out = "{";
            for (size_t i = 0; i < v.object.size(); ++i)
            {
                out += (i ? ",\n" : "\n") + inner + escape (v.object[i].first) + ": ";
                out += write (v.object[i].second, indent + 1);
            }
            return out + "\n" + pad + "}";
        }
    }
    return "null";
}

} // namespace so::song::json
