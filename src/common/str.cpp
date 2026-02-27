/*
 * str.cpp -- FAKK2 string class implementation
 *
 * This is the shared string class exported by all three FAKK2 binaries.
 * 50 symbols total, all C++ mangled. Must maintain ABI compatibility.
 */

#include "str.h"
#include <cstdlib>
#include <cctype>

static const int STR_ALLOC_GRANULARITY = 20;

void str::Init(void) {
    m_len = 0;
    m_alloced = STR_ALLOC_GRANULARITY;
    m_data = (char *)malloc(m_alloced);
    m_data[0] = '\0';
}

void str::EnsureAlloced(int amount) {
    if (amount >= m_alloced) {
        m_alloced = amount + STR_ALLOC_GRANULARITY;
        char *newbuf = (char *)malloc(m_alloced);
        if (m_data) {
            memcpy(newbuf, m_data, m_len + 1);
            free(m_data);
        }
        m_data = newbuf;
    }
}

void str::EnsureDataWritable(void) {
    /* In the recomp, data is always writable (no COW) */
}

/* ---- Constructors / Destructor ---- */

str::str() {
    Init();
}

str::str(const char *text) {
    if (text) {
        m_len = (int)strlen(text);
        m_alloced = m_len + STR_ALLOC_GRANULARITY;
        m_data = (char *)malloc(m_alloced);
        memcpy(m_data, text, m_len + 1);
    } else {
        Init();
    }
}

str::str(const str &text) {
    m_len = text.m_len;
    m_alloced = text.m_len + STR_ALLOC_GRANULARITY;
    m_data = (char *)malloc(m_alloced);
    memcpy(m_data, text.m_data, m_len + 1);
}

str::~str() {
    if (m_data) {
        free(m_data);
        m_data = NULL;
    }
}

/* ---- Assignment ---- */

str &str::operator=(const char *text) {
    if (!text) {
        EnsureAlloced(1);
        m_data[0] = '\0';
        m_len = 0;
        return *this;
    }
    int len = (int)strlen(text);
    EnsureAlloced(len + 1);
    memcpy(m_data, text, len + 1);
    m_len = len;
    return *this;
}

str &str::operator=(const str &text) {
    if (&text == this) return *this;
    EnsureAlloced(text.m_len + 1);
    memcpy(m_data, text.m_data, text.m_len + 1);
    m_len = text.m_len;
    return *this;
}

/* ---- Concatenation ---- */

str str::operator+(const char *text) const {
    str result(*this);
    result.append(text);
    return result;
}

str str::operator+(const str &text) const {
    str result(*this);
    result.append(text.m_data);
    return result;
}

str &str::operator+=(const char *text) {
    append(text);
    return *this;
}

str &str::operator+=(const str &text) {
    append(text.m_data);
    return *this;
}

str operator+(const char *a, const str &b) {
    str result(a);
    result.append(b.c_str());
    return result;
}

/* ---- Comparison ---- */

int str::operator==(const char *text) const {
    return !strcmp(m_data, text);
}

int str::operator==(const str &text) const {
    return !strcmp(m_data, text.m_data);
}

int str::operator!=(const char *text) const {
    return !!strcmp(m_data, text);
}

int str::operator!=(const str &text) const {
    return !!strcmp(m_data, text.m_data);
}

int str::cmp(const char *text) const {
    return strcmp(m_data, text);
}

int str::cmpn(const char *text, int n) const {
    return strncmp(m_data, text, n);
}

int str::icmp(const char *text) const {
#ifdef _MSC_VER
    return _stricmp(m_data, text);
#else
    return strcasecmp(m_data, text);
#endif
}

int str::icmpn(const char *text, int n) const {
#ifdef _MSC_VER
    return _strnicmp(m_data, text, n);
#else
    return strncasecmp(m_data, text, n);
#endif
}

/* ---- Modification ---- */

void str::append(const char *text) {
    if (text) {
        int len = (int)strlen(text);
        EnsureAlloced(m_len + len + 1);
        memcpy(m_data + m_len, text, len + 1);
        m_len += len;
    }
}

void str::append(const str &text) {
    append(text.m_data);
}

void str::tolower(void) {
    for (int i = 0; i < m_len; i++) {
        m_data[i] = (char)::tolower((unsigned char)m_data[i]);
    }
}

void str::toupper(void) {
    for (int i = 0; i < m_len; i++) {
        m_data[i] = (char)::toupper((unsigned char)m_data[i]);
    }
}

void str::capLength(int len) {
    if (len < m_len) {
        m_data[len] = '\0';
        m_len = len;
    }
}

void str::strip(void) {
    /* Strip leading and trailing whitespace */
    int start = 0;
    while (start < m_len && m_data[start] <= ' ') start++;

    int end = m_len - 1;
    while (end >= start && m_data[end] <= ' ') end--;

    int newlen = end - start + 1;
    if (newlen <= 0) {
        m_data[0] = '\0';
        m_len = 0;
        return;
    }

    if (start > 0) {
        memmove(m_data, m_data + start, newlen);
    }
    m_data[newlen] = '\0';
    m_len = newlen;
}

/* ---- Formatting ---- */

void str::snprintf(const char *fmt, ...) {
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    *this = buffer;
}

/* ---- Query ---- */

int str::isNumeric(void) const {
    if (m_len == 0) return 0;

    int i = 0;
    if (m_data[0] == '-') {
        if (m_len == 1) return 0;
        i = 1;
    }

    int dot = 0;
    for (; i < m_len; i++) {
        if (!isdigit((unsigned char)m_data[i])) {
            if (m_data[i] == '.' && !dot) {
                dot = 1;
            } else {
                return 0;
            }
        }
    }
    return 1;
}
