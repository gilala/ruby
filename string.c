/**********************************************************************

  string.c -

  $Author$
  $Date$
  created at: Mon Aug  9 17:12:58 JST 1993

  Copyright (C) 1993-2001 Yukihiro Matsumoto
  Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
  Copyright (C) 2000  Information-technology Promotion Agency, Japan

**********************************************************************/

#include "ruby.h"
#include "re.h"

#define BEG(no) regs->beg[no]
#define END(no) regs->end[no]

#include <math.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

VALUE rb_cString;

#define STR_NO_ORIG FL_USER0

VALUE rb_fs;

static ID id_encoding = 0;

#define ENCODING_INLINE_MAX 127
#define ENCODING_MASK (FL_USER1|FL_USER2|FL_USER3|FL_USER4|FL_USER5|FL_USER6|FL_USER7)
#define ENCODING_SHIFT (FL_USHIFT+1)
#define ENCODING_SET(obj,i) do {\
    RBASIC(obj)->flags &= ~ENCODING_MASK;\
    RBASIC(obj)->flags |= i << ENCODING_SHIFT;\
} while (0)
#define ENCODING_GET(obj) ((RBASIC(obj)->flags & ENCODING_MASK)>>ENCODING_SHIFT)

void
rb_m17n_associate_encoding(obj, enc)
    VALUE obj;
    m17n_encoding *enc;
{
    int i = enc->index;

    if (i < ENCODING_INLINE_MAX) {
	ENCODING_SET(obj, i);
	return;
    }
    ENCODING_SET(obj, ENCODING_INLINE_MAX);
    if (!id_encoding) {
	id_encoding = rb_intern("encoding");
    }
    rb_ivar_set(obj, id_encoding, INT2NUM(i));
    return;
}

m17n_encoding*
rb_m17n_get_encoding(obj)
    VALUE obj;
{
    int i = ENCODING_GET(obj);

    if (i == ENCODING_INLINE_MAX) {
	VALUE iv;

	if (!id_encoding) {
	    return ruby_default_encoding;
	}
	iv = rb_ivar_get(obj, id_encoding);
	i = NUM2INT(iv);
    }
    return m17n_index_to_encoding(i);
}

static int
str_memcmp(p1, p2, len, enc)
    char *p1, *p2;
    long len;
    m17n_encoding *enc;
{
    if (!ruby_ignorecase) {
	return memcmp(p1, p2, len);
    }
    return m17n_memcmp(p1, p2, len, enc);
}

void
rb_m17n_enc_check(str1, str2, enc)
    VALUE str1, str2;
    m17n_encoding **enc;
{
    m17n_encoding *enc1;
    m17n_encoding *enc2;

    enc1 = rb_m17n_get_encoding(str1);
    enc2 = rb_m17n_get_encoding(str2);

    if (enc1->index == 0 && m17n_asciicompat(enc2)) {
	*enc = enc2;
    }
    else if (enc2->index == 0 && m17n_asciicompat(enc1)) {
	*enc = enc1;
    }
    else if (enc1 != enc2) {
	rb_raise(rb_eArgError, "character encodings differ");
    }
    else {
	*enc = enc1;
    }
}

VALUE
rb_enc_get_encoding(str)
    VALUE str;
{
    m17n_encoding *enc = rb_m17n_get_encoding(str);
    return rb_str_new2(enc->name);
}

VALUE
rb_enc_set_encoding(obj, encoding)
    VALUE obj, encoding;
{
    m17n_encoding *enc = m17n_find_encoding(STR2CSTR(encoding));

    if (!enc) {
	rb_raise(rb_eArgError, "undefined encoding `%s'", STR2CSTR(encoding));
    }
    if (OBJ_FROZEN(obj)) {
	rb_error_frozen("object's encoding");
    }
    rb_m17n_associate_encoding(obj, enc);

    return encoding;
}

void
rb_m17n_copy_encoding(obj1, obj2)
    VALUE obj1, obj2;
{
    rb_m17n_associate_encoding(obj1, rb_m17n_get_encoding(obj2));
}

#define str_ptr(str) RSTRING(str)->ptr
#define str_len(str) RSTRING(str)->len
#define str_end(str) (str_ptr(str)+str_len(str))

VALUE
rb_str_new(ptr, len)
    const char *ptr;
    long len;
{
    NEWOBJ(str, struct RString);
    OBJSETUP(str, rb_cString, T_STRING);

    str->ptr = 0;
    str->len = len;
    str->orig = 0;
    str->ptr = ALLOC_N(char,len+1);
    if (ptr) {
	memcpy(str->ptr, ptr, len);
    }
    str->ptr[len] = '\0';
    rb_m17n_associate_encoding((VALUE)str, ruby_default_encoding);
    return (VALUE)str;
}

VALUE
rb_str_new2(ptr)
    const char *ptr;
{
    return rb_str_new(ptr, strlen(ptr));
}

VALUE
rb_tainted_str_new(ptr, len)
    const char *ptr;
    long len;
{
    VALUE str = rb_str_new(ptr, len);

    OBJ_TAINT(str);
    return str;
}

VALUE
rb_tainted_str_new2(ptr)
    const char *ptr;
{
    VALUE str = rb_str_new2(ptr);

    OBJ_TAINT(str);
    return str;
}

VALUE
rb_str_new3(str)
    VALUE str;
{
    NEWOBJ(str2, struct RString);
    OBJSETUP(str2, rb_cString, T_STRING);

    str2->len = str_len(str);
    str2->ptr = str_ptr(str);
    str2->orig = str;
    OBJ_INFECT(str2, str);
    rb_m17n_associate_encoding((VALUE)str2, rb_m17n_get_encoding(str));

    return (VALUE)str2;
}

VALUE
rb_str_new4(orig)
    VALUE orig;
{
    VALUE klass;

    klass = CLASS_OF(orig);
    while (TYPE(klass) == T_ICLASS || FL_TEST(klass, FL_SINGLETON)) {
	klass = (VALUE)RCLASS(klass)->super;
    }

    if (RSTRING(orig)->orig) {
	VALUE str;

	if (FL_TEST(orig, STR_NO_ORIG)) {
	    str = rb_str_new(str_ptr(orig), str_len(orig));
	}
	else {
	    str = rb_str_new3(RSTRING(orig)->orig);
	}
	OBJ_FREEZE(str);
	RBASIC(str)->klass = klass;
	return str;
    }
    else {
	NEWOBJ(str, struct RString);
	OBJSETUP(str, klass, T_STRING);

	str->len = str_len(orig);
	str->ptr = str_ptr(orig);
	RSTRING(orig)->orig = (VALUE)str;
	str->orig = 0;
	rb_m17n_associate_encoding((VALUE)str, rb_m17n_get_encoding(orig));
	OBJ_INFECT(str, orig);
	OBJ_FREEZE(str);

	return (VALUE)str;
    }
}

VALUE
rb_str_to_str(str)
    VALUE str;
{
    return rb_convert_type(str, T_STRING, "String", "to_str");
}

static void
rb_str_become(str, str2)
    VALUE str, str2;
{
    if (str == str2) return;
    if (NIL_P(str2)) {
	RSTRING(str)->ptr = 0;
	RSTRING(str)->len = 0;
	RSTRING(str)->orig = 0;
	return;
    }
    if ((!RSTRING(str)->orig||FL_TEST(str,STR_NO_ORIG))&&str_ptr(str))
	free(str_ptr(str));
    RSTRING(str)->ptr = str_ptr(str2);
    RSTRING(str)->len = str_len(str2);
    RSTRING(str)->orig = RSTRING(str2)->orig;
    RSTRING(str2)->ptr = 0;	/* abandon str2 */
    RSTRING(str2)->len = 0;
    rb_m17n_copy_encoding(str, str2);
    if (OBJ_TAINTED(str2)) OBJ_TAINT(str);
}

void
rb_str_associate(str, add)
    VALUE str, add;
{
    if (!FL_TEST(str, STR_NO_ORIG)) {
	if (RSTRING(str)->orig) {
	    rb_str_modify(str);
	}
	RSTRING(str)->orig = rb_ary_new();
	FL_SET(str, STR_NO_ORIG);
    }
    rb_ary_push(RSTRING(str)->orig, add);
}

static ID to_str;

VALUE
rb_obj_as_string(obj)
    VALUE obj;
{
    VALUE str;

    if (TYPE(obj) == T_STRING) {
	return obj;
    }
    str = rb_funcall(obj, to_str, 0);
    if (TYPE(str) != T_STRING)
	return rb_any_to_s(obj);
    if (OBJ_TAINTED(obj)) OBJ_TAINT(str);
    return str;
}

VALUE
rb_str_dup(str)
    VALUE str;
{
    VALUE str2;
    VALUE klass;

    if (TYPE(str) != T_STRING) str = rb_str_to_str(str);
    klass = CLASS_OF(str);
    while (TYPE(klass) == T_ICLASS || FL_TEST(klass, FL_SINGLETON)) {
	klass = (VALUE)RCLASS(klass)->super;
    }

    if (OBJ_FROZEN(str)) str2 = rb_str_new3(str);
    else if (FL_TEST(str, STR_NO_ORIG)) {
	str2 = rb_str_new(str_ptr(str), str_len(str));
    }
    else if (RSTRING(str)->orig) {
	str2 = rb_str_new3(RSTRING(str)->orig);
    }
    else {
	str2 = rb_str_new3(rb_str_new4(str));
    }
    OBJ_INFECT(str2, str);
    RBASIC(str2)->klass = klass;
    return str2;
}


static VALUE
rb_str_clone(str)
    VALUE str;
{
    VALUE clone = rb_str_dup(str);
    if (FL_TEST(str, STR_NO_ORIG))
	RSTRING(clone)->orig = RSTRING(str)->orig;
    CLONESETUP(clone, str);

    return clone;
}

static VALUE
rb_str_s_new(argc, argv, klass)
    int argc;
    VALUE *argv;
    VALUE klass;
{
    VALUE str = rb_str_new(0, 0);
    OBJSETUP(str, klass, T_STRING);

    rb_obj_call_init(str, argc, argv);
    return str;
}

static VALUE rb_str_replace_m _((VALUE str, VALUE str2));

static VALUE
rb_str_initialize(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    VALUE str2, encoding;

    rb_scan_args(argc, argv, "11", &str2, &encoding);
    rb_str_replace_m(str, str2);
    if (!NIL_P(encoding)) {
	rb_enc_set_encoding(str, encoding);
    }

    return str;
}

static int
str_strlen(str, enc)
    VALUE str;
    m17n_encoding *enc;
{
    int len;

    if (m17n_mbmaxlen(enc) == 1) return str_len(str);
    len = m17n_strlen(enc, str_ptr(str), str_end(str));
    if (len < 0) {
	rb_raise(rb_eArgError, "invalid mbstring sequence");
    }
    return len;
}

VALUE
rb_str_length(str)
    VALUE str;
{
    m17n_encoding *enc = rb_m17n_get_encoding(str);
    int len;

    len = str_strlen(str, enc);
    return INT2NUM(len);
}

static VALUE
rb_str_size(str)
    VALUE str;
{
    return INT2NUM(str_len(str));
}

static VALUE
rb_str_empty(str)
    VALUE str;
{
    if (str_len(str) == 0)
	return Qtrue;
    return Qfalse;
}

VALUE
rb_str_plus(str1, str2)
    VALUE str1, str2;
{
    VALUE str3;

    if (TYPE(str2) != T_STRING) str2 = rb_str_to_str(str2);
    str3 = rb_str_new(0, str_len(str1)+str_len(str2));
    memcpy(str_ptr(str3), str_ptr(str1), str_len(str1));
    memcpy(str_ptr(str3) + RSTRING(str1)->len,
	   str_ptr(str2), str_len(str2));
    str_ptr(str3)[str_len(str3)] = '\0';

    if (OBJ_TAINTED(str1) || OBJ_TAINTED(str2))
	OBJ_TAINT(str3);
    return str3;
}

VALUE
rb_str_times(str, times)
    VALUE str;
    VALUE times;
{
    VALUE str2;
    long i, len;

    len = NUM2LONG(times);
    if (len == 0) return rb_str_new(0,0);
    if (len < 0) {
	rb_raise(rb_eArgError, "negative argument");
    }
    if (LONG_MAX/len <  str_len(str)) {
	rb_raise(rb_eArgError, "argument too big");
    }

    str2 = rb_str_new(0, str_len(str)*len);
    for (i=0; i<len; i++) {
	memcpy(str_ptr(str2)+(i*str_len(str)),
	       str_ptr(str), str_len(str));
    }
    str_ptr(str2)[str_len(str2)] = '\0';

    if (OBJ_TAINTED(str)) {
	OBJ_TAINT(str2);
    }

    return str2;
}

static VALUE
rb_str_format(str, arg)
    VALUE str, arg;
{
    VALUE *argv;

    if (TYPE(arg) == T_ARRAY) {
	argv = ALLOCA_N(VALUE, RARRAY(arg)->len + 1);
	argv[0] = str;
	MEMCPY(argv+1, RARRAY(arg)->ptr, VALUE, RARRAY(arg)->len);
	return rb_f_sprintf(RARRAY(arg)->len+1, argv);
    }
    
    argv = ALLOCA_N(VALUE, 2);
    argv[0] = str;
    argv[1] = arg;
    return rb_f_sprintf(2, argv);
}

static char*
str_nth(enc, p, e, idx)
    m17n_encoding *enc;
    char *p, *e;
    int idx;
{
    p = m17n_nth(enc, p, e, idx);
    if (p == (char*)-1) {
	rb_raise(rb_eArgError, "invalid mbstring sequence");
    }
    return p;
}

static int
str_sublen(str, len, enc)
    VALUE str;
    int len;
    m17n_encoding *enc;
{
    if (m17n_mbmaxlen(enc) == 1) return len;
    else {
	char *p = str_ptr(str);
	char *e = p + len;
	int i;

	i = 0;
	while (p < e) {
	    p += mbclen(*p);
	    i++;
	}
	return i;
    }
}
    
int
rb_str_sublen(str, len)
    VALUE str;
    int len;
{
    return str_sublen(str, len, rb_m17n_get_encoding(str));
}

VALUE
rb_str_substr(str, beg, len)
    VALUE str;
    long beg, len;
{
    m17n_encoding *enc = rb_m17n_get_encoding(str);
    VALUE str2;
    char *p, *pend = str_end(str);
    int slen = str_strlen(str, enc);

    if (len < 0) return Qnil;
    if (beg > slen) return Qnil;
    if (beg == slen && len > 0) return Qnil;
    if (beg < 0) {
	beg += slen;
	if (beg < 0) return Qnil;
    }
    if (beg + len > slen) {
	len = slen - beg;
    }
    if (len < 0) {
	len = 0;
    }
    if (len == 0) return rb_str_new(0,0);
    p = str_nth(enc, str_ptr(str), pend, beg);
    pend = str_nth(enc, p, pend, len);
    str2 = rb_str_new(p, pend - p);
    if (OBJ_TAINTED(str)) OBJ_TAINT(str2);

    return str2;
}

static int
str_independent(str)
    VALUE str;
{
    if (OBJ_FROZEN(str)) rb_error_frozen("string");
    if (!OBJ_TAINTED(str) && rb_safe_level() >= 4)
	rb_raise(rb_eSecurityError, "Insecure: can't modify string");
    if (!RSTRING(str)->orig || FL_TEST(str, STR_NO_ORIG)) return 1;
    if (TYPE(RSTRING(str)->orig) != T_STRING) rb_bug("non string str->orig");
    RSTRING(str)->orig = 0;
    return 0;
}

void
rb_str_modify(str)
    VALUE str;
{
    char *ptr;

    if (str_independent(str)) return;
    ptr = ALLOC_N(char, str_len(str)+1);
    if (str_ptr(str)) {
	memcpy(ptr, str_ptr(str), str_len(str));
    }
    ptr[str_len(str)] = 0;
    str_ptr(str) = ptr;
}

VALUE
rb_str_freeze(str)
    VALUE str;
{
    return rb_obj_freeze(str);
}

VALUE
rb_str_dup_frozen(str)
    VALUE str;
{
    if (RSTRING(str)->orig && !FL_TEST(str, STR_NO_ORIG)) {
	OBJ_FREEZE(RSTRING(str)->orig);
	return RSTRING(str)->orig;
    }
    if (OBJ_FROZEN(str)) return str;
    str = rb_str_dup(str);
    OBJ_FREEZE(str);
    return str;
}

VALUE
rb_str_resize(str, len)
    VALUE str;
    long len;
{
    rb_str_modify(str);

    if (len >= 0) {
	if (str_len(str) < len || str_len(str) - len > 1024) {
	    REALLOC_N(str_ptr(str), char, len + 1);
	}
	str_len(str) = len;
	str_ptr(str)[len] = '\0';	/* sentinel */
    }
    return str;
}

VALUE
rb_str_cat(str, ptr, len)
    VALUE str;
    const char *ptr;
    long len;
{
    if (len > 0) {
	int poffset = -1;

	rb_str_modify(str);
	if (str_ptr(str) <= ptr &&
	    ptr < str_ptr(str) + str_len(str)) {
	    poffset = ptr - str_ptr(str);
	}
	REALLOC_N(str_ptr(str), char, str_len(str) + len + 1);
	if (ptr) {
	    if (poffset >= 0) ptr = str_ptr(str) + poffset;
	    memcpy(str_ptr(str) + str_len(str), ptr, len);
	}
	str_len(str) += len;
	str_ptr(str)[str_len(str)] = '\0'; /* sentinel */
    }
    return str;
}

VALUE
rb_str_cat2(str, ptr)
    VALUE str;
    const char *ptr;
{
    return rb_str_cat(str, ptr, strlen(ptr));
}

VALUE
rb_str_append(str1, str2)
    VALUE str1, str2;
{
    m17n_encoding *enc;

    if (TYPE(str2) != T_STRING) str2 = rb_str_to_str(str2);
    rb_m17n_enc_check(str1, str2, &enc);
    str1 = rb_str_cat(str1, str_ptr(str2), str_len(str2));
    OBJ_INFECT(str1, str2);

    return str1;
}

VALUE
rb_str_concat(str1, str2)
    VALUE str1, str2;
{
    if (FIXNUM_P(str2)) {
	int i = FIX2INT(str2);
	if (0 <= i && i <= 0xff) { /* byte */
	    char c = i;
	    return rb_str_cat(str1, &c, 1);
	}
    }
    str1 = rb_str_append(str1, str2);

    return str1;
}

int
rb_str_hash(str)
    VALUE str;
{
    m17n_encoding *enc = rb_m17n_get_encoding(str);
    register long len = str_len(str);
    register char *p = str_ptr(str);
    register int key = 0;

#ifdef HASH_ELFHASH
    register unsigned int g;

    while (len--) {
	key = (key << 4) + *p++;
	if (g = key & 0xF0000000)
	    key ^= g >> 24;
	key &= ~g;
    }
#elif HASH_PERL
    if (ruby_ignorecase) {
	while (len--) {
	    key = key*33 + toupper(*p);
	    p++;
	}
    }
    else {
	while (len--) {
	    key = key*33 + *p++;
	}
    }
    key = key + (key>>5);
#else
    if (ruby_ignorecase) {
	while (len--) {
	    key = key*65599 + m17n_toupper(enc, *p);
	    p++;
	}
    }
    else {
	while (len--) {
	    key = key*65599 + *p;
	    p++;
	}
    }
    key = key + (key>>5);
#endif
    return key;
}

static VALUE
rb_str_hash_m(str)
    VALUE str;
{
    int key = rb_str_hash(str);
    return INT2FIX(key);
}

#define lesser(a,b) (((a)>(b))?(b):(a))

int
rb_str_cmp(str1, str2)
    VALUE str1, str2;
{
    long len;
    int retval;
    m17n_encoding *enc;

    rb_m17n_enc_check(str1, str2, &enc);
    len = lesser(str_len(str1), str_len(str2));

    retval = str_memcmp(str_ptr(str1), str_ptr(str2), len, enc);
    if (retval == 0) {
	if (str_len(str1) == str_len(str2)) return 0;
	if (str_len(str1) > str_len(str2)) return 1;
	return -1;
    }
    if (retval == 0) return 0;
    if (retval > 0) return 1;
    return -1;
}

static VALUE
rb_str_equal(str1, str2)
    VALUE str1, str2;
{
    m17n_encoding *enc;
    if (TYPE(str2) != T_STRING)
	return Qfalse;

    enc = rb_m17n_get_encoding(str1);
    if (enc != rb_m17n_get_encoding(str2)) return Qfalse;
    if (str_len(str1) == str_len(str2) && rb_str_cmp(str1, str2) == 0) {
	return Qtrue;
    }
    return Qfalse;
}

static VALUE
rb_str_cmp_m(str1, str2)
    VALUE str1, str2;
{
    int result;

    if (TYPE(str2) != T_STRING) str2 = rb_str_to_str(str2);
    result = rb_str_cmp(str1, str2);
    return INT2FIX(result);
}

static VALUE
rb_str_match(x, y)
    VALUE x, y;
{
    VALUE reg;
    long start;

    switch (TYPE(y)) {
      case T_REGEXP:
	return rb_reg_match(y, x);

      case T_STRING:
	reg = rb_reg_regcomp(y);
	start = rb_reg_search(reg, x, 0, 0);
	if (start == -1) {
	    return Qnil;
	}
	start = rb_str_sublen(x, start);
	return INT2NUM(start);

      default:
	return rb_funcall(y, rb_intern("=~"), 1, x);
    }
}

static VALUE
rb_str_match2(str)
    VALUE str;
{
    return rb_reg_match2(rb_reg_regcomp(str));
}

static long
rb_str_index(str, sub, offset)
    VALUE str, sub;
    long offset;
{
    char *s, *e, *p;
    long len;
    m17n_encoding *enc;

    rb_m17n_enc_check(str, sub, &enc);
    if (offset < 0) {
	offset += str_strlen(str, enc);
	if (offset < 0) return -1;
    }
    if (str_strlen(str, enc) - offset < str_strlen(sub, enc))
	return -1;
    s = str_nth(enc, str_ptr(str), str_end(str), offset);
    p = str_ptr(sub);
    len = str_len(sub);
    if (len == 0) return offset;
    e = str_ptr(str) + str_len(str) - len + 1;
    while (s < e) {
	if (str_memcmp(s, p, len, enc) == 0) {
	    return offset;
	}
	offset++;
	s += m17n_mbclen(enc, *s);
    }
    return -1;
}

static VALUE
rb_str_index_m(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    VALUE sub;
    VALUE initpos;
    long pos;

    if (rb_scan_args(argc, argv, "11", &sub, &initpos) == 2) {
	pos = NUM2LONG(initpos);
    }
    else {
	pos = 0;
    }
    if (pos < 0) {
	pos += str_len(str);
	if (pos < 0) return Qnil;
    }

    switch (TYPE(sub)) {
      case T_REGEXP:
	pos = rb_reg_adjust_startpos(sub, str, pos, 0);
	pos = rb_reg_search(sub, str, pos, 0);
	pos = rb_str_sublen(str, pos);
	break;

      case T_STRING:
	pos = rb_str_index(str, sub, pos);
	break;

      case T_FIXNUM:
      {
	  int c = FIX2INT(sub);
	  long len = str_len(str);
	  char *p = str_ptr(str);

	  for (;pos<len;pos++) {
	      if (p[pos] == c) return INT2NUM(pos);
	  }
	  return Qnil;
      }

      default:
	rb_raise(rb_eTypeError, "type mismatch: %s given",
		 rb_class2name(CLASS_OF(sub)));
    }

    if (pos == -1) return Qnil;
    return INT2NUM(pos);
}

static VALUE
rb_str_rindex(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    VALUE sub;
    VALUE position;
    int pos, len, len2;
    char *s, *sbeg, *e, *t;
    m17n_encoding *enc;

    if (rb_scan_args(argc, argv, "11", &sub, &position) == 2) {
	pos = NUM2INT(position);
        if (pos < 0) {
	    pos += str_len(str);
	    if (pos < 0) return Qnil;
        }
	if (pos > str_len(str)) pos = str_len(str);
    }
    else {
	pos = str_len(str);
    }

    switch (TYPE(sub)) {
      case T_REGEXP:
	rb_m17n_enc_check(str, sub, &enc);
	if (RREGEXP(sub)->len) {
	    pos = rb_reg_adjust_startpos(sub, str, pos, 1);
	    pos = rb_reg_search(sub, str, pos, 1);
	    pos = str_sublen(str, pos, enc);
	}
	if (pos >= 0) return INT2NUM(pos);
	break;

      case T_STRING:
	rb_m17n_enc_check(str, sub, &enc);
	len = str_strlen(sub);
	len2 = str_strlen(str);
	if (len > len2) return Qnil;
	if (len2 - pos < len) {
	    pos = len2 - len;
	}
	if (len == 0) {
	    return INT2NUM(pos);
	}
	sbeg = str_ptr(str);
	e = str_end(str);
	t = str_ptr(sub);
	do {
	    s = str_nth(enc, sbeg, e, pos);
	    if (str_memcmp(s, t, len, enc) == 0) {
		return INT2NUM(pos);
	    }
	    pos--;
	} while (sbeg <= s);
	break;

      case T_FIXNUM:
      {
	  int c = FIX2INT(sub);
	  char *p = str_ptr(str) + pos;
	  char *pbeg = str_ptr(str);
	  /* get encoding */

	  while (pbeg <= p) {
	      if (*p == c) return INT2NUM(p - str_ptr(str));
	      p--;
	  }
	  return Qnil;
      }

      default:
	rb_raise(rb_eTypeError, "type mismatch: %s given",
		 rb_class2name(CLASS_OF(sub)));
    }
    return Qnil;
}

static char
succ_char(s)
    char *s;
{
    char c = *s;

    /* numerics */
    if ('0' <= c && c < '9') (*s)++;
    else if (c == '9') {
	*s = '0';
	return '1';
    }
    /* small alphabets */
    else if ('a' <= c && c < 'z') (*s)++;
    else if (c == 'z') {
	return *s = 'a';
    }
    /* capital alphabets */
    else if ('A' <= c && c < 'Z') (*s)++;
    else if (c == 'Z') {
	return *s = 'A';
    }
    return 0;
}

static VALUE
rb_str_succ(orig)
    VALUE orig;
{
    m17n_encoding *enc = rb_m17n_get_encoding(orig);
    VALUE str;
    char *sbeg, *s, *e;
    int c = -1;
    int n = 0;

    str = rb_str_new(str_ptr(orig), str_len(orig));
    OBJ_INFECT(str, orig);
    if (str_len(str) == 0) return str;

    sbeg = str_ptr(str); s = sbeg + str_len(str) - 1;
    e = str_end(str);

    while (sbeg <= s) {
	int c = m17n_codepoint(enc, s, e);
	if (m17n_isalnum(enc, c)) {
	    if ((c = succ_char(s)) == 0) break;
	    n = s - sbeg;
	}
	s--;
    }
    if (c == -1) {		/* str contains no alnum */
	sbeg = str_ptr(str); s = sbeg + str_len(str) - 1;
	c = '\001';
	while (sbeg <= s) {
	    *s += 1;
	    if (*s-- != 0) break;
	}
    }
    if (s < sbeg) {
	REALLOC_N(str_ptr(str), char, str_len(str) + 1);
	s = str_ptr(str) + n;
	memmove(s+1, s, str_len(str) - n);
	*s = c;
	str_len(str) += 1;
	str_ptr(str)[str_len(str)] = '\0';
    }

    return str;
}

static VALUE
rb_str_succ_bang(str)
    VALUE str;
{
    rb_str_modify(str);
    rb_str_become(str, rb_str_succ(str));

    return str;
}

VALUE
rb_str_upto(beg, end, excl)
    VALUE beg, end;
    int excl;
{
    VALUE current;
    ID succ = rb_intern("succ");

    if (TYPE(end) != T_STRING) end = rb_str_to_str(end);

    current = beg;
    while (rb_str_cmp(current, end) <= 0) {
	rb_yield(current);
	if (!excl && rb_str_equal(current, end)) break;
	current = rb_funcall(current, succ, 0, 0);
	if (excl && rb_str_equal(current, end)) break;
	if (str_len(current) > str_len(end))
	    break;
    }

    return beg;
}

static VALUE
rb_str_upto_m(beg, end)
    VALUE beg, end;
{
    return rb_str_upto(beg, end, 0);
}

static VALUE
rb_str_aref(str, indx)
    VALUE str;
    VALUE indx;
{
    long idx, len;
    m17n_encoding *enc;

    switch (TYPE(indx)) {
      case T_FIXNUM:
	idx = FIX2LONG(indx);

      num_index:
	enc = rb_m17n_get_encoding(str);
	len = str_strlen(str, enc);
	if (idx < 0) {
	    idx = len + idx;
	}
	if (idx < 0 || len <= idx) {
	    return Qnil;
	}
	idx = m17n_codepoint(enc, str_nth(enc, str_ptr(str), str_end(str), idx),
			     str_end(str));
	return INT2FIX(idx);

      case T_REGEXP:
	if (rb_reg_search(indx, str, 0, 0) >= 0)
	    return rb_reg_last_match(rb_backref_get());
	return Qnil;

      case T_STRING:
	if (rb_str_index(str, indx, 0) != -1) return indx;
	return Qnil;

      default:
	/* check if indx is Range */
	{
	    long beg;

	    enc = rb_m17n_get_encoding(str);
	    len = str_strlen(str, enc);
	    switch (rb_range_beg_len(indx, &beg, &len, len, 0)) {
	      case Qfalse:
		break;
	      case Qnil:
		return Qnil;
	      default:
		return rb_str_substr(str, beg, len);
	    }
	}
	idx = NUM2LONG(indx);
	goto num_index;
    }
    return Qnil;		/* not reached */
}

static VALUE
rb_str_aref_m(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    if (argc == 2) {
	return rb_str_substr(str, NUM2INT(argv[0]), NUM2INT(argv[1]));
    }
    if (argc != 1) {
	rb_raise(rb_eArgError, "wrong # of arguments(%d for 1)", argc);
    }
    return rb_str_aref(str, argv[0]);
}

static void
rb_str_replace(str, beg, len, val)
    VALUE str, val;
    long beg;
    long len;
{
    if (str_len(str) < beg + len) {
	len = str_len(str) - beg;
    }

    if (len < str_len(val)) {
	/* expand string */
	REALLOC_N(str_ptr(str), char, str_len(str)+str_len(val)-len+1);
    }

    if (str_len(val) != len) {
	memmove(str_ptr(str) + beg + str_len(val),
		str_ptr(str) + beg + len,
		str_len(str) - (beg + len));
    }
    if (str_len(str) < beg && len < 0) {
	MEMZERO(str_ptr(str) + str_len(str), char, -len);
    }
    if (str_len(val) > 0) {
	memmove(str_ptr(str)+beg, str_ptr(val), str_len(val));
    }
    RSTRING(str)->len += str_len(val) - len;
    str_ptr(str)[str_len(str)] = '\0';
}

static VALUE rb_str_sub_bang _((int, VALUE*, VALUE));

static VALUE
rb_str_aset(str, indx, val)
    VALUE str;
    VALUE indx, val;
{
    m17n_encoding *enc;
    long idx, beg, len;

    rb_str_modify(str);
    switch (TYPE(indx)) {
      case T_FIXNUM:
      num_index:
        enc = rb_m17n_get_encoding(str);
	idx = NUM2INT(indx);

	if (idx < 0) {
	    idx += str_strlen(str, enc);
	}
	if (idx < 0) {
	  bad_idx:
	    rb_raise(rb_eIndexError, "index %d out of string", idx);
	}

	if (FIXNUM_P(val)) {
	    unsigned int c = NUM2UINT(val);
	    int clen = m17n_codelen(enc, c);
	    char *p = str_nth(enc, str_ptr(str), str_end(str), idx);
	    int plen;

	    if (!p) goto bad_idx;
	    if (p == str_end(str)) plen = 0;
	    else {
		plen = m17n_mbcspan(enc, p, str_end(str));
		if (plen == 0) {
		    rb_raise(rb_eArgError, "invalid mbstring sequence at %d", idx);
		}
	    }

	    if (plen != clen) {
		long offset = p - str_ptr(str);
		char *p0 = str_ptr(str);

		str_ptr(str) = ALLOC_N(char, str_len(str) + clen - plen);
		memcpy(str_ptr(str), p0, offset);
		p = str_ptr(str) + offset;
		memcpy(p + clen, p0 + offset + plen, str_len(str) - offset);
		str_len(str) += clen - plen;
		str_ptr(str)[str_len(str)] = '\0';
	    }
	    m17n_mbcput(enc, c, p);
	}
	else {
	    if (TYPE(val) != T_STRING) val = rb_str_to_str(val);
	    rb_str_replace(str, idx, 1, val);
	}
	return val;

      case T_REGEXP:
        {
	    VALUE args[2];
	    args[0] = indx;
	    args[1] = val;
	    rb_str_sub_bang(2, args, str);
	}
	return val;

      case T_STRING:
	beg = rb_str_index(str, indx, 0);
	if (beg != -1) {
	    if (TYPE(val) != T_STRING) val = rb_str_to_str(val);
	    rb_str_replace(str, beg, str_len(indx), val);
	}
	return val;

      default:
	/* check if indx is Range */
	{
	    if (rb_range_beg_len(indx, &beg, &len, str_len(str), 2)) {
		if (TYPE(val) != T_STRING) val = rb_str_to_str(val);
		rb_str_replace(str, beg, len, val);
		return val;
	    }
	}
	idx = NUM2LONG(indx);
	goto num_index;
    }
}

static VALUE
rb_str_aset_m(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    rb_str_modify(str);
    if (argc == 3) {
	m17n_encoding *enc;
	long beg, len, slen, b, l;

	if (TYPE(argv[2]) != T_STRING) argv[2] = rb_str_to_str(argv[2]);
	beg = NUM2INT(argv[0]);
	len = NUM2INT(argv[1]);
	if (len < 0) rb_raise(rb_eIndexError, "negative length %d", len);
	slen = str_strlen(str);
	if (beg < 0) {
	    beg += slen;
	}
	if (beg < 0 || slen < beg) {
	    if (beg < 0) {
		beg -= slen;
	    }
	    rb_raise(rb_eIndexError, "index %d out of string", beg);
	}
	if (beg + len > slen) {
	    len = slen - beg;
	}
	rb_m17n_enc_check(str, argv[2], &enc);
	b = str_nth(enc, str_ptr(str), str_end(str), beg) - str_ptr(str);
	l = str_nth(enc, str_ptr(str), str_end(str), beg+len) - str_ptr(str) - b;
	rb_str_replace(str, b, l, argv[2]);
	return argv[2];
    }
    if (argc != 2) {
	rb_raise(rb_eArgError, "wrong # of arguments(%d for 2)", argc);
    }
    return rb_str_aset(str, argv[0], argv[1]);
}

static VALUE
rb_str_slice_bang(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    VALUE result;
    VALUE buf[3];
    int i;

    if (argc < 1 || 2 < argc) {
	rb_raise(rb_eArgError, "wrong # of arguments(%d for 1)", argc);
    }
    for (i=0; i<argc; i++) {
	buf[i] = argv[i];
    }
    buf[i] = rb_str_new(0,0);
    result = rb_str_aref_m(argc, buf, str);
    rb_str_aset_m(argc+1, buf, str);
    return result;
}

static VALUE
get_pat(pat)
    VALUE pat;
{
    switch (TYPE(pat)) {
      case T_REGEXP:
	break;

      case T_STRING:
	pat = rb_reg_regcomp(pat);
	break;

      default:
	/* type failed */
	Check_Type(pat, T_REGEXP);
    }
    return pat;
}

static VALUE
rb_str_sub_bang(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    VALUE pat, repl, match;
    struct re_registers *regs;
    int iter = 0;
    int tainted = 0;
    long plen;

    if (argc == 1 && rb_block_given_p()) {
	iter = 1;
    }
    else if (argc == 2) {
	repl = rb_obj_as_string(argv[1]);;
	if (OBJ_TAINTED(repl)) tainted = 1;
    }
    else {
	rb_raise(rb_eArgError, "wrong # of arguments(%d for 2)", argc);
    }

    pat = get_pat(argv[0]);
    if (rb_reg_search(pat, str, 0, 0) >= 0) {
	rb_str_modify(str);
	match = rb_backref_get();
	regs = RMATCH(match)->regs;

	if (iter) {
	    rb_match_busy(match);
	    repl = rb_obj_as_string(rb_yield(rb_reg_nth_match(0, match)));
	    rb_backref_set(match);
	}
	else {
	    repl = rb_reg_regsub(repl, str, regs);
	}
	if (OBJ_TAINTED(repl)) tainted = 1;
	plen = END(0) - BEG(0);
	if (str_len(repl) > plen) {
	    REALLOC_N(str_ptr(str), char,
		      str_len(str) + str_len(repl) - plen + 1);
	}
	if (str_len(repl) != plen) {
	    memmove(str_ptr(str) + BEG(0) + str_len(repl),
		    str_ptr(str) + BEG(0) + plen,
		    str_len(str) - BEG(0) - plen);
	}
	memcpy(str_ptr(str) + BEG(0), str_ptr(repl), str_len(repl));
	RSTRING(str)->len += str_len(repl) - plen;
	str_ptr(str)[str_len(str)] = '\0';
	if (tainted) OBJ_TAINT(str);

	return str;
    }
    return Qnil;
}

static VALUE
rb_str_sub(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    str = rb_str_dup(str);
    rb_str_sub_bang(argc, argv, str);
    return str;
}

static VALUE
str_gsub(argc, argv, str, bang)
    int argc;
    VALUE *argv;
    VALUE str;
    int bang;
{
    m17n_encoding *enc;
    VALUE pat, val, repl, match;
    struct re_registers *regs;
    long beg, n;
    long offset, blen, len;
    int iter = 0;
    char *buf, *bp, *cp;
    int tainted = 0;

    if (argc == 1 && rb_block_given_p()) {
	iter = 1;
    }
    else if (argc == 2) {
	repl = rb_obj_as_string(argv[1]);
	if (OBJ_TAINTED(repl)) tainted = 1;
    }
    else {
	rb_raise(rb_eArgError, "wrong # of arguments(%d for 2)", argc);
    }

    pat = get_pat(argv[0]);
    rb_m17n_enc_check(pat, str, &enc);
    offset=0; n=0; 
    beg = rb_reg_search(pat, str, 0, 0);
    if (beg < 0) {
	if (bang) return Qnil;	/* no match, no substitution */
	return rb_str_dup(str);
    }

    blen = str_len(str) + 30; /* len + margin */
    buf = ALLOC_N(char, blen);
    bp = buf;
    cp = str_ptr(str);

    while (beg >= 0) {
	n++;
	match = rb_backref_get();
	regs = RMATCH(match)->regs;
	if (iter) {
	    rb_match_busy(match);
	    val = rb_obj_as_string(rb_yield(rb_reg_nth_match(0, match)));
	    rb_backref_set(match);
	}
	else {
	    val = rb_reg_regsub(repl, str, regs);
	}
	if (OBJ_TAINTED(val)) tainted = 1;
	len = (bp - buf) + (beg - offset) + str_len(val) + 3;
	if (blen < len) {
	    while (blen < len) blen *= 2;
	    len = bp - buf;
	    REALLOC_N(buf, char, blen);
	    bp = buf + len;
	}
	len = beg - offset;	/* copy pre-match substr */
	memcpy(bp, cp, len);
	bp += len;
	memcpy(bp, str_ptr(val), str_len(val));
	bp += RSTRING(val)->len;
	if (BEG(0) == END(0)) {
	    /*
	     * Always consume at least one character of the input string
	     * in order to prevent infinite loops.
	     */
	    len = mbclen(str_ptr(str)[END(0)]);
	    if (str_len(str) > END(0)) {
		memcpy(bp, str_ptr(str)+END(0), len);
		bp += len;
	    }
	    offset = END(0) + len;
	}
	else {
	    offset = END(0);
	}
	cp = str_ptr(str) + offset;
	if (offset > str_len(str)) break;
	beg = rb_reg_search(pat, str, offset, 0);
    }
    if (str_len(str) > offset) {
	len = bp - buf;
	if (blen - len < str_len(str) - offset + 1) {
	    REALLOC_N(buf, char, len + str_len(str) - offset + 1);
	    bp = buf + len;
	}
	memcpy(bp, cp, str_len(str) - offset);
	bp += str_len(str) - offset;
    }
    rb_backref_set(match);
    if (bang) {
	if (str_independent(str)) {
	    free(str_ptr(str));
	}
    }
    else {
	NEWOBJ(dup, struct RString);
	OBJSETUP(dup, rb_cString, T_STRING);
	OBJ_INFECT(dup, str);
	rb_m17n_associate_encoding((VALUE)dup, rb_m17n_get_encoding(str));
	str = (VALUE)dup;
	dup->orig = 0;
    }
    str_ptr(str) = buf;
    str_len(str) = len = bp - buf;
    str_ptr(str)[len] = '\0';

    if (tainted) OBJ_TAINT(str);
    return str;
}

static VALUE
rb_str_gsub_bang(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    return str_gsub(argc, argv, str, 1);
}

static VALUE
rb_str_gsub(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    return str_gsub(argc, argv, str, 0);
}

static VALUE
rb_str_replace_m(str, str2)
    VALUE str, str2;
{
    if (str == str2) return str;
    if (TYPE(str2) != T_STRING) str2 = rb_str_to_str(str2);

    if (RSTRING(str2)->orig && !FL_TEST(str2, STR_NO_ORIG)) {
	if (str_independent(str)) {
	    free(str_ptr(str));
	}
	RSTRING(str)->len = str_len(str2);
	RSTRING(str)->ptr = str_ptr(str2);
	RSTRING(str)->orig = RSTRING(str2)->orig;
    }
    else {
	rb_str_modify(str);
	rb_str_resize(str, str_len(str2));
	memcpy(str_ptr(str), str_ptr(str2), str_len(str2));
    }

    rb_m17n_copy_encoding(str, str2);
    if (OBJ_TAINTED(str2)) OBJ_TAINT(str);
    return str;
}

static VALUE
uscore_get()
{
    VALUE line;

    line = rb_lastline_get();
    if (TYPE(line) != T_STRING) {
	rb_raise(rb_eTypeError, "$_ value need to be String (%s given)",
		 NIL_P(line)?"nil":rb_class2name(CLASS_OF(line)));
    }
    return line;
}

static VALUE
rb_f_sub_bang(argc, argv)
    int argc;
    VALUE *argv;
{
    return rb_str_sub_bang(argc, argv, uscore_get());
}

static VALUE
rb_f_sub(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE str = rb_str_dup(uscore_get());

    if (NIL_P(rb_str_sub_bang(argc, argv, str)))
	return str;
    rb_lastline_set(str);
    return str;
}

static VALUE
rb_f_gsub_bang(argc, argv)
    int argc;
    VALUE *argv;
{
    return rb_str_gsub_bang(argc, argv, uscore_get());
}

static VALUE
rb_f_gsub(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE str = rb_str_dup(uscore_get());

    if (NIL_P(rb_str_gsub_bang(argc, argv, str)))
	return str;
    rb_lastline_set(str);
    return str;
}

static VALUE
rb_str_reverse(str)
    VALUE str;
{
    m17n_encoding *enc = rb_m17n_get_encoding(str);
    VALUE obj;
    char *s, *e, *p;

    if (str_len(str) <= 1) return rb_str_dup(str);

    obj = rb_str_new(0, str_len(str));
    s = str_ptr(str);
    e = str_end(str);
    p = str_end(obj);

    if (m17n_mbmaxlen(enc) == 1) {
	while (s < e) {
	    *--p = *s++;
	}
    }
    else {
	while (s < e) {
	    int c = m17n_codepoint(enc, s, e);
	    int clen = m17n_codelen(enc, c);

	    p -= clen;
	    m17n_mbcput(enc, c, p);
	    s += clen;
	}
    }
    rb_m17n_copy_encoding(obj, str);

    return obj;
}

static VALUE
rb_str_reverse_bang(str)
    VALUE str;
{
    rb_str_modify(str);
    rb_str_become(str, rb_str_reverse(str));

    return str;
}

static VALUE
rb_str_include(str, arg)
    VALUE str, arg;
{
    long i;

    if (FIXNUM_P(arg)) {
	int c = FIX2INT(arg);
	long len = str_len(str);
	char *p = str_ptr(str);

	for (i=0; i<len; i++) {
	    if (p[i] == c) {
		return Qtrue;
	    }
	}
	return Qfalse;
    }

    if (TYPE(arg) != T_STRING) arg = rb_str_to_str(arg);
    i = rb_str_index(str, arg, 0);

    if (i == -1) return Qfalse;
    return Qtrue;
}

static VALUE
rb_str_to_i(str)
    VALUE str;
{
    return rb_str2inum(str, 10);
}

static VALUE
rb_str_to_f(str)
    VALUE str;
{
    double f = strtod(str_ptr(str), 0);

    return rb_float_new(f);
}

static VALUE
rb_str_to_s(str)
    VALUE str;
{
    return str;
}

VALUE
rb_str_inspect(str)
    VALUE str;
{
    m17n_encoding *enc = rb_m17n_get_encoding(str);
    char *p, *pend;
    VALUE result = rb_str_new2("\"");
    char s[5];

    p = str_ptr(str); pend = p + str_len(str);
    while (p < pend) {
	char c = *p++;
	if (ismbchar(c) && p < pend) {
	    int len = mbclen(c);
	    rb_str_cat(result, p - 1, len);
	    p += len - 1;
	}
	else if (c == '"'|| c == '\\') {
	    s[0] = '\\'; s[1] = c;
	    rb_str_cat(result, s, 2);
	}
	else if (m17n_isprint(enc, c)) {
	    s[0] = c;
	    rb_str_cat(result, s, 1);
	}
	else if (c == '\n') {
	    s[0] = '\\'; s[1] = 'n';
	    rb_str_cat(result, s, 2);
	}
	else if (c == '\r') {
	    s[0] = '\\'; s[1] = 'r';
	    rb_str_cat(result, s, 2);
	}
	else if (c == '\t') {
	    s[0] = '\\'; s[1] = 't';
	    rb_str_cat(result, s, 2);
	}
	else if (c == '\f') {
	    s[0] = '\\'; s[1] = 'f';
	    rb_str_cat(result, s, 2);
	}
	else if (c == '\013') {
	    s[0] = '\\'; s[1] = 'v';
	    rb_str_cat(result, s, 2);
	}
	else if (c == '\007') {
	    s[0] = '\\'; s[1] = 'a';
	    rb_str_cat(result, s, 2);
	}
	else if (c == 033) {
	    s[0] = '\\'; s[1] = 'e';
	    rb_str_cat(result, s, 2);
	}
	else {
	    sprintf(s, "\\%03o", c & 0377);
	    rb_str_cat2(result, s);
	}
    }
    rb_str_cat2(result, "\"");

    OBJ_INFECT(result, str);
    return result;
}

static VALUE
rb_str_dump(str)
    VALUE str;
{
    m17n_encoding *enc = m17n_index_to_encoding(0);
    long len;
    char *p, *pend;
    char *q, *qend;
    VALUE result;

    len = 2;			/* "" */
    p = str_ptr(str); pend = p + str_len(str);
    while (p < pend) {
	char c = *p++;
	switch (c) {
	  case '"':  case '\\':
	  case '\n': case '\r':
	  case '\t': case '\f': case '#':
	  case '\013': case '\007': case '\033': 
	    len += 2;
	    break;

	  default:
	    if (m17n_isprint(enc, c)) {
		len++;
	    }
	    else {
		len += 4;		/* \nnn */
	    }
	    break;
	}
    }

    result = rb_str_new(0, len);
    p = str_ptr(str); pend = p + str_len(str);
    q = str_ptr(result); qend = q + len;

    *q++ = '"';
    while (p < pend) {
	char c = *p++;

	if (c == '"' || c == '\\') {
	    *q++ = '\\';
	    *q++ = c;
	}
	else if (c == '#') {
	    *q++ = '\\';
	    *q++ = '#';
	}
	else if (m17n_isprint(enc, c)) {
	    *q++ = c;
	}
	else if (c == '\n') {
	    *q++ = '\\';
	    *q++ = 'n';
	}
	else if (c == '\r') {
	    *q++ = '\\';
	    *q++ = 'r';
	}
	else if (c == '\t') {
	    *q++ = '\\';
	    *q++ = 't';
	}
	else if (c == '\f') {
	    *q++ = '\\';
	    *q++ = 'f';
	}
	else if (c == '\013') {
	    *q++ = '\\';
	    *q++ = 'v';
	}
	else if (c == '\007') {
	    *q++ = '\\';
	    *q++ = 'a';
	}
	else if (c == '\033') {
	    *q++ = '\\';
	    *q++ = 'e';
	}
	else {
	    *q++ = '\\';
	    sprintf(q, "%03o", c&0xff);
	    q += 3;
	}
    }
    *q++ = '"';

    OBJ_INFECT(result, str);
    return result;
}

static VALUE
rb_str_upcase_bang(str)
    VALUE str;
{
    m17n_encoding *enc = rb_m17n_get_encoding(str);
    char *s, *send;
    int modify = 0;

    rb_str_modify(str);
    s = str_ptr(str); send = s + str_len(str);
    while (s < send) {
	int c = m17n_codepoint(enc, s, send);

	if (m17n_islower(enc, c)) {
	    /* assuming toupper returns codepoint with same size */
	    m17n_mbcput(enc, m17n_toupper(enc, c), s);
	    modify = 1;
	}
	s += m17n_codelen(enc, c);
    }

    if (modify) return str;
    return Qnil;
}

static VALUE
rb_str_upcase(str)
    VALUE str;
{
    str = rb_str_dup(str);
    rb_str_upcase_bang(str);
    return str;
}

static VALUE
rb_str_downcase_bang(str)
    VALUE str;
{
    m17n_encoding *enc = rb_m17n_get_encoding(str);
    char *s, *send;
    int modify = 0;

    rb_str_modify(str);
    s = str_ptr(str); send = s + str_len(str);
    while (s < send) {
	int c = m17n_codepoint(enc, s, send);

	if (m17n_isupper(enc, c)) {
	    /* assuming tolower returns codepoint with same size */
	    m17n_mbcput(enc, m17n_tolower(enc, c), s);
	    modify = 1;
	}
	s += m17n_codelen(enc, c);
    }

    if (modify) return str;
    return Qnil;
}

static VALUE
rb_str_downcase(str)
    VALUE str;
{
    str = rb_str_dup(str);
    rb_str_downcase_bang(str);
    return str;
}

static VALUE
rb_str_capitalize_bang(str)
    VALUE str;
{
    m17n_encoding *enc = rb_m17n_get_encoding(str);
    char *s, *send;
    int c;
    int modify = 0;

    rb_str_modify(str);
    s = str_ptr(str); send = s + str_len(str);
    c = m17n_codepoint(enc, s, send);
    if (m17n_islower(enc, c)) {
	/* assuming toupper returns codepoint with same size */
	m17n_mbcput(enc, m17n_toupper(enc, c), s);
	modify = 1;
	s += m17n_codelen(enc, c);
    }
    while (s < send) {
	c = m17n_codepoint(enc, s, send);

	if (m17n_isupper(enc, c)) {
	    /* assuming tolower returns codepoint with same size */
	    m17n_mbcput(enc, m17n_tolower(enc, c), s);
	    modify = 1;
	}
	s += m17n_codelen(enc, c);
    }
    if (modify) return str;
    return Qnil;
}

static VALUE
rb_str_capitalize(str)
    VALUE str;
{
    str = rb_str_dup(str);
    rb_str_capitalize_bang(str);
    return str;
}

static VALUE
rb_str_swapcase_bang(str)
    VALUE str;
{
    m17n_encoding *enc = rb_m17n_get_encoding(str);
    char *s, *send;
    int modify = 0;

    rb_str_modify(str);
    s = str_ptr(str); send = s + str_len(str);
    while (s < send) {
	int c = m17n_codepoint(enc, s, send);

	if (m17n_isupper(enc, c)) {
	    /* assuming tolower returns codepoint with same size */
	    m17n_mbcput(enc, m17n_tolower(enc, c), s);
	    modify = 1;
	}
	else if (m17n_islower(enc, c)) {
	    /* assuming toupper returns codepoint with same size */
	    m17n_mbcput(enc, m17n_toupper(enc, c), s);
	    modify = 1;
	}
	s += m17n_codelen(enc, c);
    }

    if (modify) return str;
    return Qnil;
}

static VALUE
rb_str_swapcase(str)
    VALUE str;
{
    str = rb_str_dup(str);
    rb_str_swapcase_bang(str);
    return str;
}

typedef unsigned char *USTR;

struct tr {
    int gen, now, max;
    char *p, *pend;
};

static int
trnext(t, enc)
    struct tr *t;
    m17n_encoding *enc;
{
    for (;;) {
	if (!t->gen) {
	    if (t->p == t->pend) return -1;
	    t->now = m17n_codepoint(enc, t->p, t->pend);
	    t->p += m17n_codelen(enc, t->now);
	    if (t->p < t->pend - 1 && *t->p == '-') {
		t->p++;
		if (t->p < t->pend) {
		    int c = m17n_codepoint(enc, t->p, t->pend);
		    t->p += m17n_codelen(enc, c);
		    if (t->now > c) continue;
		    t->gen = 1;
		    t->max = c;
		}
	    }
	    return t->now;
	}
	else if (++t->now < t->max) {
	    return t->now;
	}
	else {
	    t->gen = 0;
	    return t->max;
	}
    }
}

static VALUE rb_str_delete_bang _((int,VALUE*,VALUE));

static VALUE
tr_trans(str, src, repl, sflag)
    VALUE str, src, repl;
    int sflag;
{
    m17n_encoding *enc;
    struct tr trsrc, trrepl;
    int cflag = 0;
    int c, last, modify = 0;
    char *s, *send;
    VALUE hash;
    
    rb_str_modify(str);
    if (TYPE(src) != T_STRING) src = rb_str_to_str(src);
    trsrc.p = str_ptr(src); trsrc.pend = trsrc.p + str_len(src);
    if (str_len(src) >= 2 && str_ptr(src)[0] == '^') {
	cflag++;
	trsrc.p++;
    }
    if (TYPE(repl) != T_STRING) repl = rb_str_to_str(repl);
    if (str_len(repl) == 0) {
	return rb_str_delete_bang(1, &src, str);
    }
    rb_m17n_enc_check(str, src, &enc);
    rb_m17n_enc_check(str, repl, &enc);
    trrepl.p = str_ptr(repl);
    trrepl.pend = trrepl.p + str_len(repl);
    trsrc.gen = trrepl.gen = 0;
    trsrc.now = trrepl.now = 0;
    trsrc.max = trrepl.max = 0;
    hash = rb_hash_new();

    if (cflag) {
	while ((c = trnext(&trsrc, enc)) >= 0) {
	    rb_hash_aset(hash, INT2NUM(c), Qtrue);
	}
	while ((c = trnext(&trrepl, enc)) >= 0)
	    /* retrieve last replacer */;
	last = trrepl.now;
    }
    else {
	int r;

	while ((c = trnext(&trsrc, enc)) >= 0) {
	    r = trnext(&trrepl, enc);
	    if (r == -1) r = trrepl.now;
	    rb_hash_aset(hash, INT2NUM(c), INT2NUM(r));
	}
    }

    s = str_ptr(str); send = s + str_len(str);
    if (sflag) {
	int clen, tlen, max = str_len(str);
	int offset, save = -1;
	char *buf = ALLOC_N(char, max), *t = buf;
	VALUE v;

	if (cflag) tlen = m17n_codelen(enc, last);
	while (s < send) {
	    c = m17n_codepoint(enc, s, send);
	    tlen = clen = m17n_codelen(enc, c);

	    s += clen;
	    v = rb_hash_aref(hash, INT2NUM(c));
	    if (!NIL_P(v)) {
		if (!cflag) {
		    c = NUM2INT(v);
		    if (save == c) continue;
		    save = c;
		    tlen = m17n_codelen(enc, c);
		    modify = 1;
		}
	    }
	    else if (cflag) {
		save = c = last;
		modify = 1;
	    }
	    else {
		save = -1;
	    }
	    while (t - buf + tlen >= max) {
		offset = t - buf;
		max *= 2;
		REALLOC_N(buf, char, max);
		t = buf + offset;
	    }
	    m17n_mbcput(enc, c, t);
	    t += tlen;
	}
	free(str_ptr(str));
	str_ptr(str) = buf;
	str_len(str) = t - buf;
	str_ptr(str)[str_len(str)] = '\0';
    }
    else if (m17n_mbmaxlen(enc) == 1) {
	while (s < send) {
	    VALUE v = rb_hash_aref(hash, INT2FIX(*s));
	    if (!NIL_P(v)) {
		if (cflag) {
		    *s = last;
		}
		else {
		    c = FIX2INT(v);
		    *s = c & 0xff;
		}
		modify = 1;
	    }
	    s++;
	}
    }
    else {
	int clen, tlen, max = str_len(str) * 1.2;
	int offset;
	char *buf = ALLOC_N(char, max), *t = buf;
	VALUE v;

	if (cflag) tlen = m17n_codelen(enc, last);
	while (s < send) {
	    c = m17n_codepoint(enc, s, send);
	    tlen = clen = m17n_codelen(enc, c);

	    v = rb_hash_aref(hash, INT2NUM(c));
	    if (!NIL_P(v)) {
		if (!cflag) {
		    c = NUM2INT(v);
		    tlen = m17n_codelen(enc, c);
		    modify = 1;
		}
	    }
	    else if (cflag) {
		c = last;
		modify = 1;
	    }
	    while (t - buf + tlen >= max) {
		offset = t - buf;
		max *= 2;
		REALLOC_N(buf, char, max);
		t = buf + offset;
	    }
	    if (s != t) m17n_mbcput(enc, c, t);
	    s += clen;
	    t += tlen;
	}
	free(str_ptr(str));
	str_ptr(str) = buf;
	str_len(str) = t - buf;
	str_ptr(str)[str_len(str)] = '\0';
    }
    rb_gc_force_recycle(hash);

    if (modify) return str;
    return Qnil;
}

static VALUE
rb_str_tr_bang(str, src, repl)
    VALUE str, src, repl;
{
    return tr_trans(str, src, repl, 0);
}

static VALUE
rb_str_tr(str, src, repl)
    VALUE str, src, repl;
{
    str = rb_str_dup(str);
    tr_trans(str, src, repl, 0);
    return str;
}

static void
tr_setup_table(str, tablep, ctablep, enc)
    VALUE str;
    VALUE *tablep;
    VALUE *ctablep;
    m17n_encoding *enc;
{
    struct tr tr;
    int c;
    VALUE table;

    tr.p = str_ptr(str); tr.pend = tr.p + str_len(str);
    tr.gen = tr.now = tr.max = 0;
    if (str_len(str) > 1 && str_ptr(str)[0] == '^') {
	tr.p++;
	if (!*ctablep) *ctablep = rb_hash_new();
	table = *ctablep;
    }
    else {
	if (!*tablep) *tablep = rb_hash_new();
	table = *tablep;
    }

    while ((c = trnext(&tr, enc)) >= 0) {
	rb_hash_aset(table, INT2NUM(c), Qtrue);
    }
}

static VALUE
rb_str_delete_bang(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    m17n_encoding *enc;
    char *s, *send, *t;
    VALUE del = 0, nodel = 0;
    int modify = 0;
    int i;

    for (i=0; i<argc; i++) {
	VALUE s = argv[i];

	if (TYPE(s) != T_STRING) 
	    s = rb_str_to_str(s);
	rb_m17n_enc_check(str, s, &enc);
	tr_setup_table(s, &del, &nodel, enc);
    }

    rb_str_modify(str);
    s = t = str_ptr(str);
    send = str_end(str);
    while (s < send) {
	int c = m17n_codepoint(enc, s, send);
	int clen = m17n_codelen(enc, c);
	VALUE v = INT2NUM(c);

	if ((del && !NIL_P(rb_hash_aref(del, v))) &&
	    (!nodel || NIL_P(rb_hash_aref(nodel, v)))) {
	    modify = 1;
	}
	else {
	    if (t != s) m17n_mbcput(enc, c, t);
	    t += clen;
	}
	s += clen;
    }
    *t = '\0';
    str_len(str) = t - str_ptr(str);

    if (modify) return str;
    return Qnil;
}

static VALUE
rb_str_delete(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    str = rb_str_dup(str);
    rb_str_delete_bang(argc, argv, str);
    return str;
}

static VALUE
rb_str_squeeze_bang(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    m17n_encoding *enc;
    VALUE del = 0, nodel = 0;
    char *s, *send, *t;
    int save, modify = 0;
    int i;

    if (argc == 0) {
	enc = rb_m17n_get_encoding(str);
    }
    else {
	for (i=0; i<argc; i++) {
	    VALUE s = argv[i];

	    if (TYPE(s) != T_STRING) 
		s = rb_str_to_str(s);
	    rb_m17n_enc_check(str, s, &enc);
	    tr_setup_table(s, &del, &nodel, enc);
	}
    }

    rb_str_modify(str);
    s = t = str_ptr(str);
    send = str_end(str);
    save = -1;
    while (s < send) {
	int c = m17n_codepoint(enc, s, send);
	int clen = m17n_codelen(enc, c);
	VALUE v = INT2NUM(c);

	if (c != save &&
	    ((del && NIL_P(rb_hash_aref(del, v))) ||
	     (!nodel || NIL_P(rb_hash_aref(nodel, v))))) {
	    if (t != s) m17n_mbcput(enc, c, t);
	    save = c;
	    t += clen;
	}
	s += clen;
    }
    *t = '\0';
    if (t - str_ptr(str) != str_len(str)) {
	str_len(str) = t - str_ptr(str);
	modify = 1;
    }

    if (modify) return str;
    return Qnil;
}

static VALUE
rb_str_squeeze(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    str = rb_str_dup(str);
    rb_str_squeeze_bang(argc, argv, str);
    return str;
}

static VALUE
rb_str_tr_s_bang(str, src, repl)
    VALUE str, src, repl;
{
    return tr_trans(str, src, repl, 1);
}

static VALUE
rb_str_tr_s(str, src, repl)
    VALUE str, src, repl;
{
    str = rb_str_dup(str);
    tr_trans(str, src, repl, 1);
    return str;
}

static VALUE
rb_str_count(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    m17n_encoding *enc;
    VALUE del = 0, nodel = 0;
    char *s, *send;
    int i;

    if (argc < 1) {
	rb_raise(rb_eArgError, "wrong # of arguments");
    }
    for (i=0; i<argc; i++) {
	VALUE s = argv[i];

	if (TYPE(s) != T_STRING) 
	    s = rb_str_to_str(s);
	rb_m17n_enc_check(str, s, &enc);
	tr_setup_table(s, &del, &nodel, enc);
    }

    s = str_ptr(str);
    send = s + str_len(str);
    i = 0;
    while (s < send) {
	int c = m17n_codepoint(enc, s, send);
	int clen = m17n_codelen(enc, c);
	VALUE v = INT2NUM(c);

	if ((del && !NIL_P(rb_hash_aref(del, v))) &&
	    (!nodel || NIL_P(rb_hash_aref(nodel, v)))) {
	    i++;
	}
	s += clen;
    }
    return INT2NUM(i);
}

static VALUE
rb_str_split_m(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    m17n_encoding *enc = rb_m17n_get_encoding(str);
    VALUE spat;
    VALUE limit;
    int char_sep = -1;
    long beg, end, i;
    int lim = 0;
    VALUE result, tmp;

    if (rb_scan_args(argc, argv, "02", &spat, &limit) == 2) {
	lim = NUM2INT(limit);
	if (lim <= 0) limit = Qnil;
	else if (lim == 1) return rb_ary_new3(1, str);
	i = 1;
    }

    if (argc == 0) {
	if (!NIL_P(rb_fs)) {
	    spat = rb_fs;
	    goto fs_set;
	}
	char_sep = ' ';
    }
    else {
      fs_set:
	switch (TYPE(spat)) {
	  case T_STRING:
	    if (str_strlen(spat, enc) == 1) {
		rb_m17n_enc_check(str, spat, &enc);
		char_sep = m17n_codepoint(enc, str_ptr(spat), str_end(spat));
	    }
	    else {
		spat = rb_reg_regcomp(spat);
	    }
	    break;
	  case T_REGEXP:
	    break;
	  default:
	    rb_raise(rb_eArgError, "bad separator");
	}
    }

    result = rb_ary_new();
    beg = 0;
    if (char_sep >= 0) {
	char *ptr = str_ptr(str);
	long len = str_len(str);
	char *eptr = ptr + len;
	int c;

	end = beg = 0;
	if (char_sep == ' ') {	/* AWK emulation */
	    int skip = 1;

	    while (ptr < eptr) {
		c = m17n_codepoint(enc, ptr, eptr);

		if (skip) {
		    if (m17n_isspace(enc,c)) {
			beg++;
		    }
		    else {
			end = beg+1;
			skip = 0;
		    }
		}
		else {
		    if (m17n_isspace(enc, c)) {
			rb_ary_push(result, rb_str_substr(str, beg, end-beg));
			skip = 1;
			beg = end + 1;
			if (!NIL_P(limit) && lim <= ++i) break;
		    }
		    else {
			end++;
		    }
		}
		ptr += m17n_codelen(enc, c);
	    }
	}
	else {
	    while (ptr < eptr) {
		c = m17n_codepoint(enc, ptr, eptr);
		if (c == (char)char_sep) {
		    rb_ary_push(result, rb_str_substr(str, beg, end-beg));
		    beg = end + 1;
		    if (!NIL_P(limit) && lim <= ++i) break;
		}
		end++;
		ptr += m17n_codelen(enc, c);
	    }
	}
    }
    else {
	long start = beg;
	long idx;
	int last_null = 0;
	struct re_registers *regs;

	rb_m17n_enc_check(str, spat, &enc);
	while ((end = rb_reg_search(spat, str, start, 0)) >= 0) {
	    regs = RMATCH(rb_backref_get())->regs;
	    if (start == end && BEG(0) == END(0)) {
		if (last_null == 1) {
		    tmp = rb_str_new(str_ptr(str)+beg, mbclen(str_ptr(str)[beg]));
		    rb_m17n_copy_encoding(tmp, str);
		    rb_ary_push(result, tmp);
		    beg = start;
		}
		else {
		    start += mbclen(str_ptr(str)[start]);
		    last_null = 1;
		    continue;
		}
	    }
	    else {
		tmp = rb_str_new(str_ptr(str)+beg, end-beg);
		rb_m17n_copy_encoding(tmp, str);
		rb_ary_push(result, tmp);
		beg = start = END(0);
	    }
	    last_null = 0;

	    for (idx=1; idx < regs->num_regs; idx++) {
		if (BEG(idx) == -1) continue;
		if (BEG(idx) == END(idx))
		    tmp = rb_str_new(0, 0);
		else
		    tmp = rb_reg_nth_match(idx, rb_backref_get());
		rb_m17n_copy_encoding(tmp, str);
		rb_ary_push(result, tmp);
	    }
	    if (!NIL_P(limit) && lim <= ++i) break;
	}
	beg = str_sublen(str, beg, enc);
    }
    if (!NIL_P(limit) || str_len(str) > beg || lim < 0) {
	if (str_strlen(str, enc) == beg)
	    tmp = rb_str_new(0, 0);
	else {
	    char *p = str_nth(enc, str_ptr(str), str_end(str), beg);
	    tmp = rb_str_new(p, str_end(str)-p);
	}
	rb_m17n_copy_encoding(tmp, str);
	rb_ary_push(result, tmp);
    }
    if (NIL_P(limit) && lim == 0) {
	while (RARRAY(result)->len > 0 &&
	       str_len(RARRAY(result)->ptr[RARRAY(result)->len-1]) == 0)
	    rb_ary_pop(result);
    }

    return result;
}

VALUE
rb_str_split(str, sep0)
    VALUE str;
    const char *sep0;
{
    VALUE sep;

    if (TYPE(str) != T_STRING) str = rb_str_to_str(str);
    sep = rb_str_new2(sep0);
    return rb_str_split_m(1, &sep, str);
}

static VALUE
rb_f_split(argc, argv)
    int argc;
    VALUE *argv;
{
    return rb_str_split_m(argc, argv, uscore_get());
}

static VALUE
rb_str_each_line(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    VALUE rs;
    int newline;
    int rslen;
    char *p = str_ptr(str), *pend = p + str_len(str), *s;
    char *ptr = p;
    long len = str_len(str);
    VALUE line;

    if (rb_scan_args(argc, argv, "01", &rs) == 0) {
	rs = rb_rs;
    }

    if (NIL_P(rs)) {
	rb_yield(str);
	return str;
    }
    if (TYPE(rs) != T_STRING) {
	rs = rb_str_to_str(rs);
    }

    rslen = str_len(rs);
    if (rslen == 0) {
	newline = '\n';
    }
    else {
	newline = str_ptr(rs)[rslen-1];
    }

    for (s = p, p += rslen; p < pend; p++) {
	if (rslen == 0 && *p == '\n') {
	    if (*++p != '\n') continue;
	    while (*p == '\n') p++;
	}
	if (p[-1] == newline &&
	    (rslen <= 1 ||
	     memcmp(str_ptr(rs), p-rslen, rslen) == 0)) {
	    line = rb_str_new(s, p - s);
	    rb_yield(line);
	    if (str_ptr(str) != ptr || str_len(str) != len)
		rb_raise(rb_eArgError, "string modified");
	    s = p;
	}
    }

    if (s != pend) {
        if (p > pend) p = pend;
	line = rb_str_new(s, p - s);
	OBJ_INFECT(line, str);
	rb_yield(line);
    }

    return str;
}

static VALUE
rb_str_each_byte(str)
    VALUE str;
{
    long i;

    for (i=0; i<str_len(str); i++) {
	rb_yield(INT2FIX(str_ptr(str)[i] & 0xff));
    }
    return str;
}

static VALUE
rb_str_chop_bang(str)
    VALUE str;
{
    if (str_len(str) > 0) {
	rb_str_modify(str);
	str_len(str)--;
	if (str_ptr(str)[str_len(str)] == '\n') {
	    if (str_len(str) > 0 &&
		str_ptr(str)[str_len(str)-1] == '\r') {
		str_len(str)--;
	    }
	}
	str_ptr(str)[str_len(str)] = '\0';
	return str;
    }
    return Qnil;
}

static VALUE
rb_str_chop(str)
    VALUE str;
{
    str = rb_str_dup(str);
    rb_str_chop_bang(str);
    return str;
}

static VALUE
rb_f_chop_bang(str)
    VALUE str;
{
    return rb_str_chop_bang(uscore_get());
}

static VALUE
rb_f_chop()
{
    VALUE str = uscore_get();

    if (str_len(str) > 0) {
	str = rb_str_dup(str);
	rb_str_chop_bang(str);
	rb_lastline_set(str);
    }
    return str;
}

static VALUE
rb_str_chomp_bang(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    VALUE rs;
    int newline;
    int rslen;
    char *p = str_ptr(str);
    long len = str_len(str);

    if (rb_scan_args(argc, argv, "01", &rs) == 0) {
	rs = rb_rs;
    }
    if (NIL_P(rs)) return Qnil;

    if (TYPE(rs) != T_STRING) rs = rb_str_to_str(rs);
    rslen = str_len(rs);
    if (rslen == 0) {
	while (len>0 && p[len-1] == '\n') {
	    len--;
	}
	if (len < str_len(str)) {
	    rb_str_modify(str);
	    str_len(str) = len;
	    str_ptr(str)[len] = '\0';
	    return str;
	}
	return Qnil;
    }
    if (rslen > len) return Qnil;
    newline = str_ptr(rs)[rslen-1];

    if (p[len-1] == newline &&
	(rslen <= 1 ||
	 memcmp(str_ptr(rs), p+len-rslen, rslen) == 0)) {
	rb_str_modify(str);
	str_len(str) -= rslen;
	str_ptr(str)[str_len(str)] = '\0';
	return str;
    }
    return Qnil;
}

static VALUE
rb_str_chomp(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    str = rb_str_dup(str);
    rb_str_chomp_bang(argc, argv, str);
    return str;
}

static VALUE
rb_f_chomp_bang(argc, argv)
    int argc;
    VALUE *argv;
{
    return rb_str_chomp_bang(argc, argv, uscore_get());
}

static VALUE
rb_f_chomp(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE str = uscore_get();
    VALUE dup = rb_str_dup(str);

    if (NIL_P(rb_str_chomp_bang(argc, argv, dup)))
	return str;
    rb_lastline_set(dup);
    return dup;
}

static VALUE
rb_str_strip_bang(str)
    VALUE str;
{
    m17n_encoding *enc = rb_m17n_get_encoding(str);
    char *s, *t, *e;

    rb_str_modify(str);
    s = str_ptr(str);
    e = t = s + str_len(str);
    /* remove spaces at head */
    while (s < t && m17n_isspace(enc,*s)) s++;

    /* remove trailing spaces */
    t--;
    while (s <= t && m17n_isspace(enc,*t)) t--;
    t++;

    str_len(str) = t-s;
    if (s > str_ptr(str)) { 
	char *p = str_ptr(str);

	str_ptr(str) = ALLOC_N(char, str_len(str)+1);
	memcpy(str_ptr(str), s, str_len(str));
	str_ptr(str)[str_len(str)] = '\0';
	free(p);
    }
    else if (t < e) {
	str_ptr(str)[str_len(str)] = '\0';
    }
    else {
	return Qnil;
    }

    return str;
}

static VALUE
rb_str_strip(str)
    VALUE str;
{
    str = rb_str_dup(str);
    rb_str_strip_bang(str);
    return str;
}

static VALUE
scan_once(str, pat, start)
    VALUE str, pat;
    long *start;
{
    m17n_encoding *enc;
    VALUE result, match;
    struct re_registers *regs;
    long i;

    rb_m17n_enc_check(pat, str, &enc);
    if (rb_reg_search(pat, str, *start, 0) >= 0) {
	match = rb_backref_get();
	regs = RMATCH(match)->regs;
	if (BEG(0) == END(0)) {
	    /*
	     * Always consume at least one character of the input string
	     */
	    *start = END(0)+mbclen(str_ptr(str)[END(0)]);
	}
	else {
	    *start = END(0);
	}
	if (regs->num_regs == 1) {
	    return rb_reg_nth_match(0, match);
	}
	result = rb_ary_new2(regs->num_regs);
	for (i=1; i < regs->num_regs; i++) {
	    rb_ary_push(result, rb_reg_nth_match(i, match));
	}

	return result;
    }
    return Qnil;
}

static VALUE
rb_str_scan(str, pat)
    VALUE str, pat;
{
    VALUE result;
    long start = 0;
    VALUE match = Qnil;

    pat = get_pat(pat);
    if (!rb_block_given_p()) {
	VALUE ary = rb_ary_new();

	while (!NIL_P(result = scan_once(str, pat, &start))) {
	    match = rb_backref_get();
	    rb_ary_push(ary, result);
	}
	rb_backref_set(match);
	return ary;
    }
    
    while (!NIL_P(result = scan_once(str, pat, &start))) {
	match = rb_backref_get();
	rb_match_busy(match);
	rb_yield(result);
	rb_backref_set(match);	/* restore $~ value */
    }
    rb_backref_set(match);
    return str;
}

static VALUE
rb_f_scan(self, pat)
    VALUE self, pat;
{
    return rb_str_scan(uscore_get(), pat);
}

static VALUE
rb_str_hex(str)
    VALUE str;
{
    return rb_str2inum(str, 16);
}

static VALUE
rb_str_oct(str)
    VALUE str;
{
    int base = 8;

    if (str_len(str) > 2 && str_ptr(str)[0] == '0') {
	switch (str_ptr(str)[1]) {
	  case 'x':
	  case 'X':
	    base = 16;
	    break;
	  case 'b':
	  case 'B':
	    base = 2;
	    break;
	}
    }
    return rb_str2inum(str, base);
}

static VALUE
rb_str_crypt(str, salt)
    VALUE str, salt;
{
    extern char *crypt();

    if (TYPE(salt) != T_STRING) salt = rb_str_to_str(salt);
    if (str_len(salt) < 2)
	rb_raise(rb_eArgError, "salt too short(need >=2 bytes)");
    return rb_str_new2(crypt(str_ptr(str), str_ptr(salt)));
}

static VALUE
rb_str_intern(str)
    VALUE str;
{
    ID id;

    if (strlen(str_ptr(str)) != str_len(str))
	rb_raise(rb_eArgError, "string contains `\\0'");
    id = rb_intern(str_ptr(str));
    return ID2SYM(id);
}

static VALUE
rb_str_sum(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    VALUE vbits;
    int   bits;
    char *p, *pend;

    if (rb_scan_args(argc, argv, "01", &vbits) == 0) {
	bits = 16;
    }
    else bits = NUM2INT(vbits);

    p = str_ptr(str); pend = p + str_len(str);
    if (bits > sizeof(long)*CHAR_BIT) {
	VALUE res = INT2FIX(0);
	VALUE mod;

	mod = rb_funcall(INT2FIX(1), rb_intern("<<"), 1, INT2FIX(bits));
	mod = rb_funcall(mod, '-', 1, INT2FIX(1));

	while (p < pend) {
	    res = rb_funcall(res, '+', 1, INT2FIX((unsigned int)*p));
	    p++;
	}
	res = rb_funcall(res, '&', 1, mod);
	return res;
    }
    else {
	unsigned int res = 0;
	unsigned int mod = (1<<bits)-1;

	if (mod == 0) {
	    mod = -1;
	}
	while (p < pend) {
	    res += (unsigned int)*p;
	    p++;
	}
	res &= mod;
	return rb_int2inum(res);
    }
}

static VALUE
rb_str_ljust(str, w)
    VALUE str;
    VALUE w;
{
    long width = NUM2LONG(w);
    VALUE res;
    char *p, *pend;

    if (width < 0 || str_len(str) >= width) return str;
    res = rb_str_new(0, width);
    memcpy(str_ptr(res), str_ptr(str), str_len(str));
    p = str_end(res); pend = str_ptr(res) + width;
    while (p < pend) {
	*p++ = ' ';
    }
    return res;
}

static VALUE
rb_str_rjust(str, w)
    VALUE str;
    VALUE w;
{
    long width = NUM2LONG(w);
    VALUE res;
    char *p, *pend;

    if (width < 0 || str_len(str) >= width) return str;
    res = rb_str_new(0, width);
    p = str_ptr(res); pend = p + width - str_len(str);
    while (p < pend) {
	*p++ = ' ';
    }
    memcpy(pend, str_ptr(str), str_len(str));
    return res;
}

static VALUE
rb_str_center(str, w)
    VALUE str;
    VALUE w;
{
    long width = NUM2LONG(w);
    VALUE res;
    char *p, *pend;
    long n;

    if (width < 0 || str_len(str) >= width) return str;
    res = rb_str_new(0, width);
    n = (width - str_len(str))/2;
    p = str_ptr(res); pend = p + n;
    while (p < pend) {
	*p++ = ' ';
    }
    memcpy(pend, str_ptr(str), str_len(str));
    p = pend + str_len(str); pend = str_ptr(res) + width;
    while (p < pend) {
	*p++ = ' ';
    }
    return res;
}

void
rb_str_setter(val, id, var)
    VALUE val;
    ID id;
    VALUE *var;
{
    if (!NIL_P(val) && TYPE(val) != T_STRING) {
	rb_raise(rb_eTypeError, "value of %s must be String", rb_id2name(id));
    }
    *var = val;
}

void
Init_String()
{
    rb_cString  = rb_define_class("String", rb_cObject);
    rb_include_module(rb_cString, rb_mComparable);
    rb_include_module(rb_cString, rb_mEnumerable);
    rb_define_singleton_method(rb_cString, "new", rb_str_s_new, -1);
    rb_define_method(rb_cString, "initialize", rb_str_initialize, 1);
    rb_define_method(rb_cString, "clone", rb_str_clone, 0);
    rb_define_method(rb_cString, "dup", rb_str_dup, 0);
    rb_define_method(rb_cString, "<=>", rb_str_cmp_m, 1);
    rb_define_method(rb_cString, "==", rb_str_equal, 1);
    rb_define_method(rb_cString, "===", rb_str_equal, 1);
    rb_define_method(rb_cString, "eql?", rb_str_equal, 1);
    rb_define_method(rb_cString, "hash", rb_str_hash_m, 0);
    rb_define_method(rb_cString, "+", rb_str_plus, 1);
    rb_define_method(rb_cString, "*", rb_str_times, 1);
    rb_define_method(rb_cString, "%", rb_str_format, 1);
    rb_define_method(rb_cString, "[]", rb_str_aref_m, -1);
    rb_define_method(rb_cString, "[]=", rb_str_aset_m, -1);
    rb_define_method(rb_cString, "length", rb_str_length, 0);
    rb_define_method(rb_cString, "size", rb_str_size, 0);
    rb_define_method(rb_cString, "empty?", rb_str_empty, 0);
    rb_define_method(rb_cString, "=~", rb_str_match, 1);
    rb_define_method(rb_cString, "~", rb_str_match2, 0);
    rb_define_method(rb_cString, "succ", rb_str_succ, 0);
    rb_define_method(rb_cString, "succ!", rb_str_succ_bang, 0);
    rb_define_method(rb_cString, "next", rb_str_succ, 0);
    rb_define_method(rb_cString, "next!", rb_str_succ_bang, 0);
    rb_define_method(rb_cString, "upto", rb_str_upto_m, 1);
    rb_define_method(rb_cString, "index", rb_str_index_m, -1);
    rb_define_method(rb_cString, "rindex", rb_str_rindex, -1);
    rb_define_method(rb_cString, "replace", rb_str_replace_m, 1);

    rb_define_method(rb_cString, "to_i", rb_str_to_i, 0);
    rb_define_method(rb_cString, "to_f", rb_str_to_f, 0);
    rb_define_method(rb_cString, "to_s", rb_str_to_s, 0);
    rb_define_method(rb_cString, "to_str", rb_str_to_s, 0);
    rb_define_method(rb_cString, "inspect", rb_str_inspect, 0);
    rb_define_method(rb_cString, "dump", rb_str_dump, 0);

    rb_define_method(rb_cString, "upcase", rb_str_upcase, 0);
    rb_define_method(rb_cString, "downcase", rb_str_downcase, 0);
    rb_define_method(rb_cString, "capitalize", rb_str_capitalize, 0);
    rb_define_method(rb_cString, "swapcase", rb_str_swapcase, 0);

    rb_define_method(rb_cString, "upcase!", rb_str_upcase_bang, 0);
    rb_define_method(rb_cString, "downcase!", rb_str_downcase_bang, 0);
    rb_define_method(rb_cString, "capitalize!", rb_str_capitalize_bang, 0);
    rb_define_method(rb_cString, "swapcase!", rb_str_swapcase_bang, 0);

    rb_define_method(rb_cString, "hex", rb_str_hex, 0);
    rb_define_method(rb_cString, "oct", rb_str_oct, 0);
    rb_define_method(rb_cString, "split", rb_str_split_m, -1);
    rb_define_method(rb_cString, "reverse", rb_str_reverse, 0);
    rb_define_method(rb_cString, "reverse!", rb_str_reverse_bang, 0);
    rb_define_method(rb_cString, "concat", rb_str_concat, 1);
    rb_define_method(rb_cString, "<<", rb_str_concat, 1);
    rb_define_method(rb_cString, "crypt", rb_str_crypt, 1);
    rb_define_method(rb_cString, "intern", rb_str_intern, 0);

    rb_define_method(rb_cString, "include?", rb_str_include, 1);

    rb_define_method(rb_cString, "scan", rb_str_scan, 1);

    rb_define_method(rb_cString, "ljust", rb_str_ljust, 1);
    rb_define_method(rb_cString, "rjust", rb_str_rjust, 1);
    rb_define_method(rb_cString, "center", rb_str_center, 1);

    rb_define_method(rb_cString, "sub", rb_str_sub, -1);
    rb_define_method(rb_cString, "gsub", rb_str_gsub, -1);
    rb_define_method(rb_cString, "chop", rb_str_chop, 0);
    rb_define_method(rb_cString, "chomp", rb_str_chomp, -1);
    rb_define_method(rb_cString, "strip", rb_str_strip, 0);

    rb_define_method(rb_cString, "sub!", rb_str_sub_bang, -1);
    rb_define_method(rb_cString, "gsub!", rb_str_gsub_bang, -1);
    rb_define_method(rb_cString, "strip!", rb_str_strip_bang, 0);
    rb_define_method(rb_cString, "chop!", rb_str_chop_bang, 0);
    rb_define_method(rb_cString, "chomp!", rb_str_chomp_bang, -1);

    rb_define_method(rb_cString, "tr", rb_str_tr, 2);
    rb_define_method(rb_cString, "tr_s", rb_str_tr_s, 2);
    rb_define_method(rb_cString, "delete", rb_str_delete, -1);
    rb_define_method(rb_cString, "squeeze", rb_str_squeeze, -1);
    rb_define_method(rb_cString, "count", rb_str_count, -1);

    rb_define_method(rb_cString, "tr!", rb_str_tr_bang, 2);
    rb_define_method(rb_cString, "tr_s!", rb_str_tr_s_bang, 2);
    rb_define_method(rb_cString, "delete!", rb_str_delete_bang, -1);
    rb_define_method(rb_cString, "squeeze!", rb_str_squeeze_bang, -1);

    rb_define_method(rb_cString, "each_line", rb_str_each_line, -1);
    rb_define_method(rb_cString, "each", rb_str_each_line, -1);
    rb_define_method(rb_cString, "each_byte", rb_str_each_byte, 0);

    rb_define_method(rb_cString, "sum", rb_str_sum, -1);

    rb_define_global_function("sub", rb_f_sub, -1);
    rb_define_global_function("gsub", rb_f_gsub, -1);

    rb_define_global_function("sub!", rb_f_sub_bang, -1);
    rb_define_global_function("gsub!", rb_f_gsub_bang, -1);

    rb_define_global_function("chop", rb_f_chop, 0);
    rb_define_global_function("chop!", rb_f_chop_bang, 0);

    rb_define_global_function("chomp", rb_f_chomp, -1);
    rb_define_global_function("chomp!", rb_f_chomp_bang, -1);

    rb_define_global_function("split", rb_f_split, -1);
    rb_define_global_function("scan", rb_f_scan, 1);

    rb_define_method(rb_cString, "slice", rb_str_aref_m, -1);
    rb_define_method(rb_cString, "slice!", rb_str_slice_bang, -1);

    rb_define_method(rb_cString, "encoding", rb_enc_get_encoding, 0);
    rb_define_method(rb_cString, "encoding=", rb_enc_set_encoding, 1);

    to_str = rb_intern("to_s");

    rb_fs = Qnil;
    rb_define_hooked_variable("$;", &rb_fs, 0, rb_str_setter);
    rb_define_hooked_variable("$-F", &rb_fs, 0, rb_str_setter);
}
