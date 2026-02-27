/*
 * str.cpp -- FAKK2 string class implementation (copy-on-write)
 *
 * Based on SDK str.cpp. Key behaviors:
 *   - EnsureDataWritable() copies strdata when refcount > 0 (COW trigger)
 *   - EnsureAlloced() creates/resizes strdata with STR_ALLOC_GRAN granularity
 *   - operator=(const char*) handles aliasing (copying from within self)
 *   - All mutation methods call EnsureDataWritable() before modifying data
 */

#include "str.h"
#include <cstdlib>
#include <cctype>

static const int STR_ALLOC_GRAN = 20;

/* =========================================================================
 * EnsureDataWritable -- the COW trigger
 *
 * If the current strdata is shared (refcount > 0), make a private copy.
 * After this call, m_data->refcount == 0 and we own the data exclusively.
 * ========================================================================= */

void str::EnsureDataWritable(void) {
    assert(m_data);

    if (!m_data->refcount)
        return;     /* already writable -- we're the sole owner */

    strdata *olddata = m_data;
    int len = length();

    m_data = new strdata;
    EnsureAlloced(len + 1, false);
    strncpy(m_data->data, olddata->data, len + 1);
    m_data->len = len;

    olddata->DelRef();
}

/* =========================================================================
 * EnsureAlloced -- ensure the data buffer is at least 'amount' bytes
 *
 * Creates strdata if NULL. Calls EnsureDataWritable() to handle COW.
 * Allocates in STR_ALLOC_GRAN chunks to reduce reallocation frequency.
 * ========================================================================= */

void str::EnsureAlloced(int amount, bool keepold) {
    if (!m_data)
        m_data = new strdata;

    EnsureDataWritable();

    if (amount < m_data->alloced)
        return;

    assert(amount);

    int newsize;
    if (amount == 1) {
        newsize = 1;
    } else {
        int mod = amount % STR_ALLOC_GRAN;
        newsize = mod ? amount + STR_ALLOC_GRAN - mod : amount;
    }
    m_data->alloced = newsize;

    char *newbuffer = new char[newsize];

    if (m_data->data) {
        if (keepold)
            strcpy(newbuffer, m_data->data);
        delete[] m_data->data;
    }

    m_data->data = newbuffer;
}

/* =========================================================================
 * Additional constructors
 * ========================================================================= */

str::str(const str &text, int start, int end) : m_data(NULL) {
    int len = end - start;
    if (len < 0) len = 0;

    EnsureAlloced(len + 1);
    if (len > 0 && text.m_data && text.m_data->data) {
        strncpy(m_data->data, text.m_data->data + start, len);
    }
    m_data->data[len] = '\0';
    m_data->len = len;
}

str::str(char ch) : m_data(NULL) {
    EnsureAlloced(2);
    m_data->data[0] = ch;
    m_data->data[1] = '\0';
    m_data->len = 1;
}

str::str(int num) : m_data(NULL) {
    char text[64];
    ::snprintf(text, sizeof(text), "%d", num);
    int len = (int)strlen(text);
    EnsureAlloced(len + 1);
    strcpy(m_data->data, text);
    m_data->len = len;
}

str::str(float num) : m_data(NULL) {
    char text[64];
    ::snprintf(text, sizeof(text), "%f", num);
    /* Strip trailing zeros after decimal point */
    int len = (int)strlen(text);
    if (strchr(text, '.')) {
        while (len > 1 && text[len - 1] == '0') len--;
        if (text[len - 1] == '.') len--;
        text[len] = '\0';
    }
    EnsureAlloced(len + 1);
    strcpy(m_data->data, text);
    m_data->len = len;
}

str::str(unsigned num) : m_data(NULL) {
    char text[64];
    ::snprintf(text, sizeof(text), "%u", num);
    int len = (int)strlen(text);
    EnsureAlloced(len + 1);
    strcpy(m_data->data, text);
    m_data->len = len;
}

/* =========================================================================
 * Assignment from const char*
 *
 * Handles several edge cases:
 *   - NULL input (clears string)
 *   - Self-assignment (text == m_data->data)
 *   - Aliasing (text points into our current buffer)
 * ========================================================================= */

void str::operator=(const char *text) {
    if (!text) {
        EnsureAlloced(1, false);
        m_data->data[0] = '\0';
        m_data->len = 0;
        return;
    }

    if (!m_data) {
        int len = (int)strlen(text);
        EnsureAlloced(len + 1, false);
        strcpy(m_data->data, text);
        m_data->len = len;
        return;
    }

    if (text == m_data->data)
        return;     /* copying same thing -- punt */

    EnsureDataWritable();

    /* Check for aliasing: text points into our buffer */
    if (m_data->data && text >= m_data->data &&
        text <= m_data->data + m_data->len) {
        int diff = (int)(text - m_data->data);
        int i;
        for (i = 0; text[i]; i++) {
            m_data->data[i] = text[i];
        }
        m_data->data[i] = '\0';
        m_data->len -= diff;
        return;
    }

    int len = (int)strlen(text);
    EnsureAlloced(len + 1, false);
    strcpy(m_data->data, text);
    m_data->len = len;
}

/* =========================================================================
 * Append
 * ========================================================================= */

void str::append(const char *text) {
    if (text && *text) {
        int len = (int)strlen(text);
        EnsureAlloced(m_data->len + len + 1);
        strcpy(m_data->data + m_data->len, text);
        m_data->len += len;
    }
}

void str::append(const str &text) {
    append(text.c_str());
}

/* =========================================================================
 * Concatenation operators (friend functions)
 * ========================================================================= */

str operator+(const str &a, const str &b) {
    str result(a);
    result.append(b);
    return result;
}

str operator+(const str &a, const char *b) {
    str result(a);
    result.append(b);
    return result;
}

str operator+(const char *a, const str &b) {
    str result(a);
    result.append(b);
    return result;
}

str operator+(const str &a, float b) {
    char text[64];
    ::snprintf(text, sizeof(text), "%f", b);
    str result(a);
    result.append(text);
    return result;
}

str operator+(const str &a, int b) {
    char text[64];
    ::snprintf(text, sizeof(text), "%d", b);
    str result(a);
    result.append(text);
    return result;
}

str operator+(const str &a, unsigned b) {
    char text[64];
    ::snprintf(text, sizeof(text), "%u", b);
    str result(a);
    result.append(text);
    return result;
}

str operator+(const str &a, bool b) {
    str result(a);
    result.append(b ? "true" : "false");
    return result;
}

str operator+(const str &a, char b) {
    char text[2] = { b, '\0' };
    str result(a);
    result.append(text);
    return result;
}

/* =========================================================================
 * Concatenation assignment
 * ========================================================================= */

str &str::operator+=(const str &a) {
    append(a);
    return *this;
}

str &str::operator+=(const char *a) {
    append(a);
    return *this;
}

str &str::operator+=(float a) {
    char text[64];
    ::snprintf(text, sizeof(text), "%f", a);
    append(text);
    return *this;
}

str &str::operator+=(char a) {
    char text[2] = { a, '\0' };
    append(text);
    return *this;
}

str &str::operator+=(int a) {
    char text[64];
    ::snprintf(text, sizeof(text), "%d", a);
    append(text);
    return *this;
}

str &str::operator+=(unsigned a) {
    char text[64];
    ::snprintf(text, sizeof(text), "%u", a);
    append(text);
    return *this;
}

str &str::operator+=(bool a) {
    append(a ? "true" : "false");
    return *this;
}

/* =========================================================================
 * Comparison operators
 * ========================================================================= */

bool operator==(const str &a, const str &b) {
    return !strcmp(a.c_str(), b.c_str());
}

bool operator==(const str &a, const char *b) {
    assert(b);
    return !strcmp(a.c_str(), b);
}

bool operator==(const char *a, const str &b) {
    assert(a);
    return !strcmp(a, b.c_str());
}

bool operator!=(const str &a, const str &b) {
    return !!strcmp(a.c_str(), b.c_str());
}

bool operator!=(const str &a, const char *b) {
    return !!strcmp(a.c_str(), b);
}

bool operator!=(const char *a, const str &b) {
    return !!strcmp(a, b.c_str());
}

/* =========================================================================
 * String comparison (instance methods)
 * ========================================================================= */

int str::icmpn(const char *text, int n) const {
    return str::icmpn(c_str(), text, n);
}

int str::icmpn(const str &text, int n) const {
    return str::icmpn(c_str(), text.c_str(), n);
}

int str::icmp(const char *text) const {
    return str::icmp(c_str(), text);
}

int str::icmp(const str &text) const {
    return str::icmp(c_str(), text.c_str());
}

int str::cmpn(const char *text, int n) const {
    return str::cmpn(c_str(), text, n);
}

int str::cmpn(const str &text, int n) const {
    return str::cmpn(c_str(), text.c_str(), n);
}

/* =========================================================================
 * Static string comparison
 * ========================================================================= */

int str::icmpn(const char *s1, const char *s2, int n) {
#ifdef _MSC_VER
    return _strnicmp(s1, s2, n);
#else
    return strncasecmp(s1, s2, n);
#endif
}

int str::icmp(const char *s1, const char *s2) {
#ifdef _MSC_VER
    return _stricmp(s1, s2);
#else
    return strcasecmp(s1, s2);
#endif
}

int str::cmpn(const char *s1, const char *s2, int n) {
    return strncmp(s1, s2, n);
}

int str::cmp(const char *s1, const char *s2) {
    return strcmp(s1, s2);
}

/* =========================================================================
 * Case conversion
 * ========================================================================= */

void str::tolower(void) {
    EnsureDataWritable();
    for (int i = 0; i < m_data->len; i++) {
        m_data->data[i] = (char)::tolower((unsigned char)m_data->data[i]);
    }
}

void str::toupper(void) {
    EnsureDataWritable();
    for (int i = 0; i < m_data->len; i++) {
        m_data->data[i] = (char)::toupper((unsigned char)m_data->data[i]);
    }
}

char *str::tolower(char *s1) {
    char *s = s1;
    while (*s) {
        *s = (char)::tolower((unsigned char)*s);
        s++;
    }
    return s1;
}

char *str::toupper(char *s1) {
    char *s = s1;
    while (*s) {
        *s = (char)::toupper((unsigned char)*s);
        s++;
    }
    return s1;
}

/* =========================================================================
 * Formatting
 * ========================================================================= */

void str::snprintf(char *dst, int size, const char *fmt, ...) {
    char buffer[0x10000];
    va_list argptr;
    va_start(argptr, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, argptr);
    va_end(argptr);
    strncpy(dst, buffer, size - 1);
    dst[size - 1] = '\0';
}

/* =========================================================================
 * Query
 * ========================================================================= */

bool str::isNumeric(const char *s) {
    if (*s == '-') s++;

    bool dot = false;
    int len = (int)strlen(s);
    for (int i = 0; i < len; i++) {
        if (!isdigit((unsigned char)s[i])) {
            if (s[i] == '.' && !dot) {
                dot = true;
                continue;
            }
            return false;
        }
    }
    return len > 0;
}

bool str::isNumeric(void) const {
    return str::isNumeric(c_str());
}

/* =========================================================================
 * Modification
 * ========================================================================= */

void str::CapLength(int newlen) {
    assert(m_data);
    if (length() <= newlen)
        return;

    EnsureDataWritable();
    m_data->data[newlen] = '\0';
    m_data->len = newlen;
}

void str::BackSlashesToSlashes(void) {
    EnsureDataWritable();
    for (int i = 0; i < m_data->len; i++) {
        if (m_data->data[i] == '\\')
            m_data->data[i] = '/';
    }
}

void str::strip(void) {
    EnsureDataWritable();

    int start = 0;
    while (start < m_data->len && m_data->data[start] <= ' ')
        start++;

    int end = m_data->len - 1;
    while (end >= start && m_data->data[end] <= ' ')
        end--;

    int newlen = end - start + 1;
    if (newlen <= 0) {
        m_data->data[0] = '\0';
        m_data->len = 0;
        return;
    }

    if (start > 0) {
        memmove(m_data->data, m_data->data + start, newlen);
    }
    m_data->data[newlen] = '\0';
    m_data->len = newlen;
}
