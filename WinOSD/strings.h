#pragma once

#include <cassert>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#ifdef _WIN32
namespace {
#include <Windows.h>

static inline int _wtoa(const wchar_t *w, char *a, int chars, UINT codepage = CP_THREAD_ACP)
{
    return WideCharToMultiByte(codepage, 0, w, -1, a, (int)(a ? chars : 0), 0, 0);
}

static inline int _atow(const char *a, wchar_t *w, int chars, UINT codepage = CP_THREAD_ACP)
{
    return MultiByteToWideChar(codepage, 0, a, -1, w, (int)(w ? chars : 0));
}
};
#else
namespace {
#define HIGH_SURROGATE_START  0xd800
#define HIGH_SURROGATE_END    0xdbff
#define LOW_SURROGATE_START   0xdc00
#define LOW_SURROGATE_END     0xdfff
#define IS_HIGH_SURROGATE(wch) (((wch) >= HIGH_SURROGATE_START) && ((wch) <= HIGH_SURROGATE_END))
#define IS_LOW_SURROGATE(wch)  (((wch) >= LOW_SURROGATE_START) && ((wch) <= LOW_SURROGATE_END))
#define IS_SURROGATE_PAIR(hs, ls) (IS_HIGH_SURROGATE(hs) && IS_LOW_SURROGATE(ls))

static inline int _wtoa(const wchar_t *w, char *a, int chars, UINT codepage = 0)
{
    return wcstombs(a, w, chars - 1) + 1;
}

static inline int _atow(const char *a, wchar_t *w, int chars, UINT codepage = 0)
{
    return mbstowcs(w, a, chars - 1) + 1;
}
};
#endif // _WIN32

static inline void encode_utf16_pair(const uint32_t &character, uint16_t *units)
{
    unsigned int code;
    assert(0x10000 <= character && character <= 0x10FFFF);
    code = (character - 0x10000);
    units[0] = 0xD800 | (code >> 10);
    units[1] = 0xDC00 | (code & 0x3FF);
}

static inline uint32_t decode_utf16_pair(const uint16_t *units)
{
    uint32_t code;
    assert(0xD800 <= units[0] && units[0] <= 0xDBFF);
    assert(0xDC00 <= units[1] && units[1] <= 0xDFFF);
    code = 0x10000;
    code += (units[0] & 0x03FF) << 10;
    code += (units[1] & 0x03FF);
    return code;
}

static inline bool is_emoji(uint32_t ch)
{
    // [0x2100, 0x214F], [0x2300, 0x23FF]
    // [0x2600, 0x27BF], [0x2b50, 0x2b55], [0x2B1B, 0x2B1C]

    // [0x1F000, 0x1FFFF]

    if (0x1FFFF < ch) {
        // (0x1FFFF, ch, inf)
        return false;
    }

    // [0x0000, ch, 0x1FFFF]

    if (0x1F000 <= ch) {
        // [0x1F000, ch, 0x1FFFF]
        return true;
    }

    // [0x0000, ch, 0x1F000)

    if (0x2600 <= ch) {
        // [0x2600, ch, 0x1F000)

        if (ch <= 0x27BF) {
            // [0x2600, ch, 0x27BF]
            return true;
        }

        if (0x2b50 <= ch && ch <= 0x2b55) {
            return true;
        }

        if (0x2B1B <= ch && ch <= 0x2B1C) {
            return true;
        }
    } else if (0x2100 <= ch) {
        // [0x2100, ch, 0x2600)

        if (ch <= 0x214F) {
            // [0x2100, ch, 0x214F]
            return true;
        }

        if (0x2300 <= ch && ch <= 0x23FF) {
            return true;
        }
    }

    return false;
}

static inline char32_t decode_utf16(const wchar_t *units)
{
    if (IS_HIGH_SURROGATE(units[0])) {
        if (IS_LOW_SURROGATE(units[1])) {
            return decode_utf16_pair((const uint16_t *)units);
        }
#ifdef _DEBUG
        // error
        __debugbreak();
#endif
    }
    return units[0];
}

static inline char32_t decode_utf8(const char *units)
{
    char c = units[0];
    const char payload = 0b00111111;

    // Zero continuation (0 to 0x7F), 0xxxxxxx
    if (c <= 0b01111111) {
        return c & 0x7f;
    }

    // One continuation (0x80 to 0x7FF) 110xxxxx 10xxxxxx
    if (c <= 0b11011111) {
        return (c & 0b00011111) << 6
            | units[1] & payload;
    }

    // Two continuations(0x800 to 0xD7FF and 0xE000 to 0xFFFF) 1110xxxx 10xxxxxx 10xxxxxx
    if (c <= 0b11101111) {
#ifdef _DEBUG // UTF-16 surrogates
        uint32_t tmp = (c & 0b00001111) << 12
            | units[1] & payload << 6
            | units[2] & payload;

        assert(0xD7FF < tmp && tmp < 0xE000);
#endif

        return (c & 0b00001111) << 12
            | units[1] & payload << 6
            | units[2] & payload;
    }

    // Three continuations(0x10000 to 0x10FFFF) 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if (c <= 0b11110111) {
        return (c & 0b00000111) << 18
            | units[1] & payload << 12
            | units[2] & payload << 6
            | units[3] & payload;
    }

#ifdef _DEBUG // double check if we have valid utf-8 char
    __debugbreak();
#endif

    if (c <= 0b11111011) {
        return (c & 0b00000011) << 24
            | units[1] & payload << 18
            | units[2] & payload << 12
            | units[3] & payload << 6
            | units[4] & payload;
    }

    if (c <= 0b11111011) {
        return (c & 0b00000011) << 24
            | units[1] & payload << 18
            | units[2] & payload << 12
            | units[3] & payload << 6
            | units[4] & payload;
    }

    if (c <= 0b11111101) {
        return (c & 0b00000011) << 30
            | units[1] & payload << 24
            | units[2] & payload << 18
            | units[3] & payload << 12
            | units[4] & payload << 6
            | units[5] & payload;
    }
}

static inline bool has_utf8_bom(const char *in_char)
{
    uint8_t *in = (uint8_t *)in_char;
    return (in && in[0] == 0xef && in[1] == 0xbb && in[2] == 0xbf);
}

static inline size_t utf8_to_wchar(const char *in, size_t insize, wchar_t *out,
                                   size_t outsize, int flags)
{
    int i_insize = (int)insize;
    int ret;

    if (i_insize == 0)
        i_insize = (int)strlen(in);

    /* prevent bom from being used in the string */
    if (has_utf8_bom(in)) {
        if (i_insize >= 3) {
            in += 3;
            insize -= 3;
        }
    }

    ret = MultiByteToWideChar(CP_UTF8, 0, in, i_insize, out, (int)outsize);

    return (ret > 0) ? (size_t)ret : 0;
}

static inline size_t os_utf8_to_wcs(const char *str, size_t len, wchar_t *dst, size_t dst_size)
{
    size_t in_len;
    size_t out_len;

    if (!str)
        return 0;

    in_len = len ? len : strlen(str);
    out_len = dst ? (dst_size - 1) : utf8_to_wchar(str, in_len, NULL, 0, 0);

    if (dst) {
        if (!dst_size)
            return 0;

        if (out_len)
            out_len = utf8_to_wchar(str, in_len,
                                    dst, out_len + 1, 0);

        dst[out_len] = 0;
    }

    return out_len;
}

static inline size_t os_utf8_to_wcs_ptr(const char *str, size_t len, wchar_t **pstr)
{
    if (str) {
        size_t out_len = os_utf8_to_wcs(str, len, NULL, 0);

        *pstr = (wchar_t *)malloc((out_len + 1) * sizeof(wchar_t));
        return os_utf8_to_wcs(str, len, *pstr, out_len + 1);
    } else {
        *pstr = NULL;
        return 0;
    }
}

#ifndef _WIN32
#error assuming wchar_t is utf-16le
#endif // !_WIN32

class wstring_char
{
public:
    template<typename _Val, typename _Base>
    struct _iterator
    {
        size_t _cursor = 0;
        const _Base *_base;

        _Val operator*() const
        {
            return (*_base)[_cursor];
        }
        auto &operator++()
        {
            _cursor += 1;
            return *this;
        }
        auto operator!=(const _iterator &other) const
        {
            return _cursor != other._cursor;
        }
    };

    template<typename _Val, typename _Base>
    struct _iterator_proxy;

    template<typename _Val, typename _Base>
    struct _iterator_proxy
    {
        const _Base *_base;

        std::tuple<const wchar_t *, bool> operator[](size_t idx) const
        {
            return { (*_base)[idx], _base->isEmoji[idx] };
        }

        auto begin()
        {
            return _iterator<_Val, _iterator_proxy>{ 0, this };
        }

        auto end()
        {
            return _iterator<_Val, _iterator_proxy>{ _base->size(), this };
        }
    };

    using const_iterator = _iterator<const wchar_t *, wstring_char>;
    using emoji_iterator = _iterator_proxy<std::tuple<const wchar_t *, bool>, wstring_char>;

    wstring_char() : data(nullptr), cap(0), used(0) {}

    wstring_char(const wchar_t *wstr, size_t len = 0) :
        wstring_char()
    {
        append(wstr, len);
    }

    wstring_char(const std::wstring &str) :
        wstring_char(str.data(), str.size())
    {}

    wstring_char(const wstring_char &other) :
        wstring_char()
    {
        if (other.data == nullptr) {
            return;
        }

        cap = other.used;
        used = other.used;

        data = new wchar_t[used];
        memcpy(data, other.data, used * sizeof(wchar_t));
    }

    wstring_char &operator=(const wstring_char &other)
    {
        if (other.data == nullptr) {
            used = 0;
            indices.clear();
            if (data) {
                data[0] = 0;
            }
        } else {
            used = other.used;
            indices = other.indices;

            if (cap < used) {
                if (data) {
                    delete data;
                }

                cap = used;
                data = new wchar_t[used];
            }

            memcpy(data, other.data, used * sizeof(wchar_t));
        }

        return *this;
    }

    wstring_char(wstring_char &&other) noexcept :
        wstring_char()
    {
        if (other.data == nullptr) {
            return;
        }

        cap = other.cap;
        used = other.used;
        data = other.data;
        indices = std::move(other.indices);

        other.data = nullptr;
    }

    wstring_char &operator=(wstring_char &&other) noexcept
    {
        if (other.data == nullptr) {
            used = 0;
            indices.clear();
            if (data) {
                data[0] = 0;
            }
        } else {
            used = other.used;
            indices = other.indices;

            if (data) {
                delete[] data;
            }
            data = other.data;
            other.data = nullptr;

            indices = std::move(other.indices);
        }

        return *this;
    }

    ~wstring_char()
    {
        if (data) {
            delete[] data;
        }
    }

    // TODO: find selector and correctly split emoji
    void append(const wchar_t *wstr, size_t len = 0)
    {
        if (len == 0) {
            len = wcslen(wstr);
            if (len == 0) {
                return;
            }
        }

        // worse case, utf16 2 wchar + 1 null
        if (cap < len * 3 + used) {
            auto *buf = new wchar_t[used + len * 3];
            if (data != nullptr) {
                memcpy(buf, data, used * sizeof(wchar_t));
                delete[] data;
            }

            data = buf;
            cap = used + len * 3;
        }

        size_t p = used;
        for (int i = 0; i < len; data[p++] = 0) {
            indices.push_back(p);

            bool bmp = IS_SURROGATE_PAIR(wstr[i], wstr[i + 1]);
            uint32_t u32 = bmp ? decode_utf16_pair((uint16_t *)(wstr + i)) : wstr[i];

            if (bmp) {
                data[p++] = wstr[i++];
                data[p++] = wstr[i++];
            } else {
                data[p++] = wstr[i++];
            }

            if (!is_emoji(u32)) {
                isEmoji.push_back(false);
                continue;
            } else {
                isEmoji.push_back(true);
            }

            // if first char is emoji, then it is emoji until it is not
            // to correctly split emoji, we need to check next code type (ZWJ and selectors)
            while (true) {
                if (i == len) {
                    break;
                }

                bmp = IS_SURROGATE_PAIR(wstr[i], wstr[i + 1]);
                u32 = bmp ? decode_utf16_pair((uint16_t *)(wstr + i)) : wstr[i];

                // Variation Selector-16, Zero Width Joiner, emoji,
                // Combining Enclosing Keycap, Tags & Variation Selectors Supplement
                if (u32 == 0xFE0F || u32 == 0x200D || is_emoji(u32)
                    || u32 == 0x20E3 || (0xE0000 <= u32 && u32 <= 0xE01EF)) {
                    if (bmp) {
                        data[p++] = wstr[i++];
                        data[p++] = wstr[i++];
                    } else {
                        data[p++] = wstr[i++];
                    }
                } else {
                    break;
                }
            }
        }

        used += p;
    }

    wstring_char &operator=(const wchar_t *wstr)
    {
        clear();
        append(wstr);
        return *this;
    }

    wstring_char &operator=(const std::wstring &wstr)
    {
        clear();
        append(wstr.data(), wstr.size());
        return *this;
    }

    wstring_char &operator+=(const wchar_t *wstr)
    {
        append(wstr);
        return *this;
    }

    wstring_char &operator+=(const std::wstring &wstr)
    {
        append(wstr.data(), wstr.size());
        return *this;
    }

    std::u32string to_u32string() const
    {
        std::u32string u32str;
        for (const auto &idx : indices) {
            if (*(data + idx + 1) == 0) {
                u32str.push_back(data[idx]);
            } else {
                u32str.push_back(decode_utf16_pair((uint16_t *)(data + idx)));
            }
        }
        return u32str;
    }

    std::wstring to_wstring() const
    {
        std::wstring str;
        for (const auto &idx : indices) {
            str.append(data + idx);
        }
        return str;
    }

    std::vector<std::wstring> to_wstrings() const
    {
        std::vector<std::wstring> strs;
        for (const auto &idx : indices) {
            strs.emplace_back(data + idx);
        }
        return strs;
    }

    const_iterator begin() const
    {
        return const_iterator{ 0, const_cast<wstring_char *>(this) };
    }

    const_iterator end() const
    {
        return const_iterator{ size(), const_cast<wstring_char *>(this) };
    }

    emoji_iterator items() const
    {
        return emoji_iterator{ this };
    }

    void clear()
    {
        used = 0;
        indices.clear();
        data[0] = 0;
    }

    size_t size() const
    {
        return indices.size();
    }

    size_t length() const
    {
        return indices.size();
    }

    size_t capacity() const
    {
        return cap;
    }

    // copy data to a smaller buffer
    void shrink_to_fit()
    {
        if (used == cap || data == nullptr) {
            return;
        }

        auto *buf = new wchar_t[used]; // additional null-char included
        memcpy(buf, data, used);
        delete data;

        data = buf;
        cap = used;
    }

    // unsafe method, memory management hand-off to caller
    wchar_t *at(size_t idx) const
    {
        return &data[indices[idx]];
    }

    const wchar_t *operator[] (size_t idx) const
    {
        return &data[indices[idx]];
    }

private:
    size_t cap;
    size_t used;
    wchar_t *data;

    std::vector<bool> isEmoji;
    std::vector<size_t> indices;
};

namespace std {
template<class _Elem, class _Traits, class _Alloc>
inline std::vector<std::basic_string<_Elem, _Traits, _Alloc>> split(
    const std::basic_string<_Elem, _Traits, _Alloc> &_Str, const _Elem _Delim)
{
    std::vector<std::basic_string<_Elem, _Traits, _Alloc>> elems;
    std::basic_stringstream<_Elem, _Traits, _Alloc> ss(_Str);
    std::basic_string<_Elem, _Traits, _Alloc> item;
    while (std::getline(ss, item, _Delim)) {
        elems.push_back(item);
    }
    return elems;
}

template<class _Elem, class _Traits, class _Alloc, class T2, class T3>
inline std::basic_string<_Elem, _Traits, _Alloc>
replace(const std::basic_string<_Elem, _Traits, _Alloc> &str,
        const T2 &old_str, const T3 &new_str)
{
#if _HAS_CXX17
    const std::basic_string_view<_Elem, _Traits> _Old(old_str), _New(new_str);
#else
    const std::basic_string<_Elem, _Traits, _Alloc> _Old(old_str), _New(new_str);
#endif
    std::basic_string<_Elem, _Traits, _Alloc> _Dest(str);
    typename std::basic_string<_Elem, _Traits, _Alloc>::size_type start_pos = 0;

    const auto _End = std::basic_string<_Elem, _Traits, _Alloc>::npos;
    while ((start_pos = _Dest.find(_Old, start_pos)) != _End) {
        _Dest.replace(start_pos, _Old.length(), _New);
        start_pos += _New.length();
    }
    return _Dest;
}

// convert ANSI to utf-16le
static inline std::wstring atow(const std::string &string)
{
    int len = _atow(string.data(), 0, 0);
    wchar_t *buffer = new wchar_t[len];
    memset(buffer, 0, len * sizeof(*buffer));
    _atow(string.data(), buffer, (int)len);
    std::wstring s = buffer;
    delete[] buffer;
    return s;
}

// on windows, wchar_t should be stored as UTF-16LE
static inline std::wstring u8tow(const std::string &string)
{
    int len = _atow(string.data(), 0, 0, CP_UTF8);
    wchar_t *buffer = new wchar_t[len];
    memset(buffer, 0, len * sizeof(*buffer));
    _atow(string.data(), buffer, (int)len, CP_UTF8);
    std::wstring s = buffer;
    delete[] buffer;
    return s;
}

// convert utf-16le to ANSI
static inline std::string wtoa(const std::wstring &string)
{
    int len = _wtoa(string.data(), 0, 0);
    char *buffer = new char[len];
    memset(buffer, 0, len * sizeof(*buffer));
    _wtoa(string.data(), buffer, (int)len);
    std::string s = buffer;
    delete[] buffer;
    return s;
}

// convert utf-16le to utf-8
static inline std::string wtou8(const std::wstring &string)
{
    int len = _wtoa(string.data(), 0, 0, CP_UTF8);
    char *buffer = new char[len];
    memset(buffer, 0, len * sizeof(*buffer));
    _wtoa(string.data(), buffer, (int)len, CP_UTF8);
    std::string s = buffer;
    delete[] buffer;
    return s;
}

static inline std::u32string wtou32(const std::wstring &string)
{
    std::u32string u32str;

    size_t len = string.size();
    if (len == 1) {
        u32str.push_back(string[0]);
        return u32str;
    }

    u32str.reserve(len);

    auto *data = (const uint16_t *)string.data();
    for (size_t i = 0; i < len; ) {
        if (IS_SURROGATE_PAIR(data[i], data[i + 1])) {
            u32str.push_back(decode_utf16_pair(data + i));
            i += 2;
        } else {
            u32str.push_back(data[i]);
            i += 1;
        }
    }

    return u32str;
}

static inline std::u32string atou32(const std::string &string)
{
    std::u32string str;
    str.reserve(string.size()); // expect all one byte

    const char payload = 0b00111111;

    size_t i;
    uint32_t tmp;
    for (i = 0; i < string.size(); str.push_back(tmp)) {
        char c = string[i];

        // Zero continuation (0 to 0x7F), 0xxxxxxx
        if (c <= 0b01111111) {
            tmp = c & 0x7f;
            i += 1;
            continue;
        }

        // One continuation (0x80 to 0x7FF) 110xxxxx 10xxxxxx
        if (c <= 0b11011111) {
            tmp = (c & 0b00011111) << 6
                | string[i + 1] & payload;
            i += 2;

            assert(0x80 <= tmp && tmp <= 0x7FF);
            continue;
        }

        // Two continuations(0x800 to 0xD7FF and 0xE000 to 0xFFFF) 1110xxxx 10xxxxxx 10xxxxxx
        if (c <= 0b11101111) {
            tmp = (c & 0b00001111) << 12
                | string[i + 1] & payload << 6
                | string[i + 2] & payload;
            i += 3;

            if (0xD7FF < tmp && tmp < 0xE000) { // UTF-16 surrogates
                throw new std::runtime_error("invalid UTF-8 string");
            }

            assert(0x800 <= tmp && tmp <= 0xFFFF);
            continue;
        }

        // Three continuations(0x10000 to 0x10FFFF) 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        if (c <= 0b11110111) {
            tmp = (c & 0b00000111) << 18
                | string[i + 1] & payload << 12
                | string[i + 2] & payload << 6
                | string[i + 3] & payload;
            i += 4;

            assert(0x10000 <= tmp && tmp <= 0x10FFFF);
            continue;
        }

        throw new std::runtime_error("invalid UTF-8 string");
    }

    assert(i == string.size());
    assert(str == wtou32(atow(string)));

    return str;
}

// usually no need to convert wchat_t other than default encoding (UTF-16LE), there is no wstring version
static inline std::string encoding(const std::string &string, unsigned int targetCp, unsigned int srcCp)
{
    int len = _atow(string.data(), 0, 0, srcCp);
    wchar_t *wbuffer = new wchar_t[len];
    memset(wbuffer, 0, len * sizeof(*wbuffer));
    _atow(string.data(), wbuffer, (int)len, srcCp);

    len = _wtoa(wbuffer, 0, 0, targetCp);
    char *buffer = new char[len];
    memset(buffer, 0, len * sizeof(*buffer));
    _wtoa(wbuffer, buffer, (int)len, targetCp);
    std::string s = buffer;
    delete[] wbuffer;
    delete[] buffer;
    return s;
}
}
