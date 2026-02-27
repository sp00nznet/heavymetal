/*
 * str.h -- FAKK2 string class
 *
 * The `str` class is the primary ABI bridge between fakk2.exe and the game
 * DLLs. All three binaries export 50 identical str class methods. This
 * implementation must be binary-compatible with the original.
 *
 * Original export list (50 symbols, C++ mangled):
 *   str::str(), str::~str()
 *   str::operator=(const char*), str::operator=(const str&)
 *   str::operator+(const char*), str::operator+(const str&)
 *   str::operator+=(const char*), str::operator+=(const str&)
 *   str::operator[](int)
 *   str::operator==(const char*), str::operator==(const str&)
 *   str::operator!=(const char*), str::operator!=(const str&)
 *   str::append(const char*), str::append(const str&)
 *   str::c_str(), str::length()
 *   str::cmp(const char*), str::cmpn(const char*, int)
 *   str::icmp(const char*), str::icmpn(const char*, int)
 *   str::tolower(), str::toupper()
 *   str::snprintf(const char*, ...)
 *   str::isNumeric()
 *   str::capLength(int)
 *   str::strip()
 *   ... and more
 */

#ifndef FAKK_STR_H
#define FAKK_STR_H

#ifdef __cplusplus

#include <cstdarg>
#include <cstring>
#include <cstdio>

class str {
private:
    char    *m_data;
    int     m_len;
    int     m_alloced;

    void    Init(void);
    void    EnsureAlloced(int amount);
    void    EnsureDataWritable(void);

public:
    /* Constructors / destructor */
    str();
    str(const char *text);
    str(const str &text);
    ~str();

    /* Assignment */
    str &operator=(const char *text);
    str &operator=(const str &text);

    /* Concatenation */
    str operator+(const char *text) const;
    str operator+(const str &text) const;
    str &operator+=(const char *text);
    str &operator+=(const str &text);
    friend str operator+(const char *a, const str &b);

    /* Access */
    char operator[](int index) const;
    char &operator[](int index);
    const char *c_str(void) const;
    int length(void) const;

    /* Comparison */
    int operator==(const char *text) const;
    int operator==(const str &text) const;
    int operator!=(const char *text) const;
    int operator!=(const str &text) const;
    int cmp(const char *text) const;
    int cmpn(const char *text, int n) const;
    int icmp(const char *text) const;
    int icmpn(const char *text, int n) const;

    /* Modification */
    void append(const char *text);
    void append(const str &text);
    void tolower(void);
    void toupper(void);
    void capLength(int len);
    void strip(void);

    /* Formatting */
    void snprintf(const char *fmt, ...);

    /* Query */
    int isNumeric(void) const;
};

/* Inline implementations for hot-path methods */
inline const char *str::c_str(void) const {
    return m_data;
}

inline int str::length(void) const {
    return m_len;
}

inline char str::operator[](int index) const {
    return m_data[index];
}

inline char &str::operator[](int index) {
    return m_data[index];
}

#endif /* __cplusplus */

#endif /* FAKK_STR_H */
