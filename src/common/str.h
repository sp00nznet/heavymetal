/*
 * str.h -- FAKK2 string class (copy-on-write)
 *
 * The `str` class is the primary ABI bridge between fakk2.exe and the game
 * DLLs. All three binaries export 50 identical str class methods. This
 * implementation matches the SDK's copy-on-write pattern using strdata.
 *
 * Key design (from SDK str.h/str.cpp):
 *   - strdata class holds: char *data, int refcount, int alloced, int len
 *   - Copy constructor shares strdata via AddRef() -- no data copy
 *   - Assignment of str& swaps strdata pointers with refcount management
 *   - Write operations call EnsureDataWritable() which copies on refcount > 0
 *   - Destructor calls DelRef() which deletes strdata when refcount < 0
 *   - Allocation granularity: STR_ALLOC_GRAN = 20 bytes
 *
 * Original export list (50 symbols, C++ mangled):
 *   str::str(), str::~str()
 *   str::operator=(const char*), str::operator=(const str&)
 *   str::operator+(const char*), str::operator+(const str&)
 *   str::operator+=(const char*), str::operator+=(const str&)
 *   str::operator[](int), str::operator const char*()
 *   str::operator==(const char*), str::operator==(const str&)
 *   str::operator!=(const char*), str::operator!=(const str&)
 *   str::append(const char*), str::append(const str&)
 *   str::c_str(), str::length()
 *   str::tolower(), str::toupper()
 *   str::snprintf(), str::isNumeric()
 *   str::capLength(), str::strip()
 *   str::BackSlashesToSlashes()
 *   ... and more
 */

#ifndef FAKK_STR_H
#define FAKK_STR_H

#ifdef __cplusplus

#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <cassert>

/* =========================================================================
 * strdata -- reference-counted shared string data
 *
 * Multiple str objects can share the same strdata. When a write operation
 * is attempted on a shared strdata, EnsureDataWritable() copies it first.
 * This is the classic copy-on-write (COW) pattern.
 *
 * Lifecycle:
 *   new strdata       -> refcount = 0 (owned by one str)
 *   AddRef()          -> refcount++ (shared with another str)
 *   DelRef()          -> refcount-- (if < 0, delete this)
 * ========================================================================= */

class strdata {
public:
    strdata() : len(0), refcount(0), data(NULL), alloced(0) {}

    ~strdata() {
        if (data)
            delete[] data;
    }

    void AddRef() {
        refcount++;
    }

    bool DelRef() {     /* returns true if strdata was deleted */
        refcount--;
        if (refcount < 0) {
            delete this;
            return true;
        }
        return false;
    }

    char    *data;
    int     refcount;
    int     alloced;
    int     len;
};

/* =========================================================================
 * str -- FAKK2 string class with copy-on-write semantics
 * ========================================================================= */

class str {
protected:
    strdata *m_data;

    void EnsureAlloced(int amount, bool keepold = true);
    void EnsureDataWritable();

public:
    /* Constructors / destructor */
    str();
    str(const char *text);
    str(const str &text);
    str(const str &text, int start, int end);
    str(char ch);
    str(int num);
    str(float num);
    str(unsigned num);
    ~str();

    /* Core accessors */
    int             length(void) const;
    const char      *c_str(void) const;

    /* Modification */
    void            append(const char *text);
    void            append(const str &text);

    /* Element access */
    char            operator[](int index) const;
    char            &operator[](int index);

    /* Assignment */
    void            operator=(const str &text);
    void            operator=(const char *text);

    /* Concatenation (friend operators) */
    friend str      operator+(const str &a, const str &b);
    friend str      operator+(const str &a, const char *b);
    friend str      operator+(const char *a, const str &b);
    friend str      operator+(const str &a, float b);
    friend str      operator+(const str &a, int b);
    friend str      operator+(const str &a, unsigned b);
    friend str      operator+(const str &a, bool b);
    friend str      operator+(const str &a, char b);

    /* Concatenation assignment */
    str             &operator+=(const str &a);
    str             &operator+=(const char *a);
    str             &operator+=(float a);
    str             &operator+=(char a);
    str             &operator+=(int a);
    str             &operator+=(unsigned a);
    str             &operator+=(bool a);

    /* Comparison (friend operators) */
    friend bool     operator==(const str &a, const str &b);
    friend bool     operator==(const str &a, const char *b);
    friend bool     operator==(const char *a, const str &b);
    friend bool     operator!=(const str &a, const str &b);
    friend bool     operator!=(const str &a, const char *b);
    friend bool     operator!=(const char *a, const str &b);

    /* Implicit conversion to const char* */
    operator const char *() const;

    /* String comparison (instance methods) */
    int             icmpn(const char *text, int n) const;
    int             icmpn(const str &text, int n) const;
    int             icmp(const char *text) const;
    int             icmp(const str &text) const;
    int             cmpn(const char *text, int n) const;
    int             cmpn(const str &text, int n) const;

    /* Case conversion (instance methods -- modifies in place) */
    void            tolower(void);
    void            toupper(void);

    /* Static utility methods */
    static char     *tolower(char *s1);
    static char     *toupper(char *s1);
    static int      icmpn(const char *s1, const char *s2, int n);
    static int      icmp(const char *s1, const char *s2);
    static int      cmpn(const char *s1, const char *s2, int n);
    static int      cmp(const char *s1, const char *s2);
    static void     snprintf(char *dst, int size, const char *fmt, ...);
    static bool     isNumeric(const char *str);

    /* Query */
    bool            isNumeric(void) const;

    /* Modification */
    void            CapLength(int newlen);
    void            BackSlashesToSlashes(void);
    void            strip(void);
};

/* =========================================================================
 * Inline implementations -- hot path methods
 * ========================================================================= */

inline int str::length(void) const {
    return (m_data != NULL) ? m_data->len : 0;
}

inline const char *str::c_str(void) const {
    assert(m_data);
    return m_data->data;
}

inline str::operator const char *() const {
    return c_str();
}

/* --- Destructor (inline for performance) --- */

inline str::~str() {
    if (m_data) {
        m_data->DelRef();
        m_data = NULL;
    }
}

/* --- Default constructor --- */

inline str::str() : m_data(NULL) {
    EnsureAlloced(1);
    m_data->data[0] = '\0';
}

/* --- const char* constructor --- */

inline str::str(const char *text) : m_data(NULL) {
    if (text) {
        int len = (int)strlen(text);
        EnsureAlloced(len + 1);
        strcpy(m_data->data, text);
        m_data->len = len;
    } else {
        EnsureAlloced(1);
        m_data->data[0] = '\0';
        m_data->len = 0;
    }
}

/* --- Copy constructor (COW: share strdata, bump refcount) --- */

inline str::str(const str &text) : m_data(NULL) {
    m_data = text.m_data;
    m_data->AddRef();
}

/* --- Assignment from str (COW: swap strdata pointers) ---
 * Adding the reference before deleting our current reference prevents
 * us from deleting our string if we are copying from ourself.
 */

inline void str::operator=(const str &text) {
    text.m_data->AddRef();
    m_data->DelRef();
    m_data = text.m_data;
}

/* --- Element access (const -- no copy needed) --- */

inline char str::operator[](int index) const {
    assert(m_data);
    if (!m_data) return 0;
    assert(index >= 0 && index < m_data->len);
    if (index < 0 || index >= m_data->len) return 0;
    return m_data->data[index];
}

/* --- Element access (mutable -- triggers COW) --- */

inline char &str::operator[](int index) {
    static char dummy = 0;
    assert(m_data);
    EnsureDataWritable();
    if (!m_data) return dummy;
    assert(index >= 0 && index < m_data->len);
    if (index < 0 || index >= m_data->len) return dummy;
    return m_data->data[index];
}

#endif /* __cplusplus */

#endif /* FAKK_STR_H */
