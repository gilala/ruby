/**********************************************************************

  re.c -

  $Author$
  created at: Mon Aug  9 18:24:49 JST 1993

  Copyright (C) 1993-2001 Yukihiro Matsumoto

**********************************************************************/

#include "ruby.h"
#include "re.h"

static VALUE rb_eRegexpError;

#define BEG(no) regs->beg[no]
#define END(no) regs->end[no]

#define MIN(a,b) (((a)>(b))?(b):(a))
#define REG_CASESTATE  FL_USER0

static void
rb_reg_check(re)
    VALUE re;
{
    if (!RREGEXP(re)->ptr || !RREGEXP(re)->str) {
	rb_raise(rb_eTypeError, "uninitialized Regexp");
    }
}

static VALUE
enc_nth(str, nth)
    VALUE str;
    int nth;
{
    int i = rb_str_sublen(str, nth);
    return INT2NUM(i);
}

extern int ruby_in_compile;

static void
rb_reg_expr_str(str, s, len)
    VALUE str;
    const char *s;
    int len;
{
    m17n_encoding *enc = rb_m17n_get_encoding(str);
    const char *p, *pend;
    int need_escape = 0;

    p = s; pend = p + len;
    while (p<pend) {
	if (*p == '/' || (!m17n_isprint(enc, *p) && !ismbchar(*p))) {
	    need_escape = 1;
	    break;
	}
	p++;
    }
    if (!need_escape) {
	rb_str_cat(str, s, len);
    }
    else {
	p = s; 
	while (p<pend) {
	    if (*p == '/') {
		char c = '\\';
		rb_str_cat(str, &c, 1);
		rb_str_cat(str, p, 1);
	    }
	    else if (ismbchar(*p)) {
	    	rb_str_cat(str, p, mbclen(*p));
		p += mbclen(*p);
		continue;
	    }
	    else if (m17n_isprint(enc, *p)) {
		rb_str_cat(str, p, 1);
	    }
	    else {
		char b[8];
		switch (*p) {
		case '\r':
		    rb_str_cat(str, "\\r", 2);
		    break;
		case '\n':
		    rb_str_cat(str, "\\n", 2);
		    break;
		case '\t':
		    rb_str_cat(str, "\\t", 2);
		    break;
		case '\f':
		    rb_str_cat(str, "\\f", 2);
		    break;
		case 007:
		    rb_str_cat(str, "\\a", 2);
		    break;
		case 013:
		    rb_str_cat(str, "\\v", 2);
		    break;
		case 033:
		    rb_str_cat(str, "\\e", 2);
		    break;
		default:
		    sprintf(b, "\\%03o", *p & 0377);
		    rb_str_cat(str, b, 4);
		    break;
		}
	    }
	    p++;
	}
    }
}

static VALUE
rb_reg_desc(s, len, re)
    const char *s;
    int len;
    VALUE re;
{
    m17n_encoding *enc;
    VALUE str = rb_str_new2("/");

    enc = re ? rb_m17n_get_encoding(re) : ruby_default_encoding;
    rb_m17n_associate_encoding(str, enc);
    rb_reg_expr_str(str, s, len);
    rb_str_cat2(str, "/");
    if (re) {
	rb_reg_check(re);
	/* /p is obsolete; to be removed */
	if ((RREGEXP(re)->ptr->options & RE_OPTION_POSIXLINE) == RE_OPTION_POSIXLINE)
	    rb_str_cat2(str, "p");
	else if (RREGEXP(re)->ptr->options & RE_OPTION_MULTILINE)
	    rb_str_cat2(str, "m");
	if (RREGEXP(re)->ptr->options & RE_OPTION_IGNORECASE)
	    rb_str_cat2(str, "i");
	if (RREGEXP(re)->ptr->options & RE_OPTION_EXTENDED)
	    rb_str_cat2(str, "x");

	if (enc->index == 0) {	/* ascii */
	    rb_str_cat2(str, "n");
	}
    }
    OBJ_INFECT(str, re);
    return str;
}

static VALUE
rb_reg_source(re)
    VALUE re;
{
    VALUE str;

    rb_reg_check(re);
    str = rb_str_new(RREGEXP(re)->str,RREGEXP(re)->len);
    if (OBJ_TAINTED(re)) OBJ_TAINT(str);
    return str;
}

static VALUE
rb_reg_inspect(re)
    VALUE re;
{
    rb_reg_check(re);
    return rb_reg_desc(RREGEXP(re)->str, RREGEXP(re)->len, re);
}

static void
rb_reg_raise(s, len, err, re)
    const char *s;
    int len;
    const char *err;
    VALUE re;
{
    VALUE desc = rb_reg_desc(s, len, re);

    if (ruby_in_compile)
	rb_compile_error("%s: %s", err, RSTRING(desc)->ptr);
    else
	rb_raise(rb_eRegexpError, "%s: %s", err, RSTRING(desc)->ptr);
}

static VALUE
rb_reg_casefold_p(re)
    VALUE re;
{
    rb_reg_check(re);
    if (RREGEXP(re)->ptr->options & RE_OPTION_IGNORECASE) return Qtrue;
    return Qfalse;
}

static VALUE
rb_reg_kcode_m(re)
    VALUE re;
{
    m17n_encoding *enc = rb_m17n_get_encoding(re);

    rb_warn("do not use Regex#kcode; use Regex#encoding instead");
    return rb_str_new2(enc->name);
}

static Regexp*
make_regexp(s, len, flag, enc)
    const char *s;
    int len, flag;
    m17n_encoding *enc;
{
    Regexp *rp;
    char *err;

    /* Handle escaped characters first. */

    /* Build a copy of the string (in dest) with the
       escaped characters translated,  and generate the regex
       from that.
    */

    rp = ALLOC(Regexp);
    MEMZERO((char *)rp, Regexp, 1);
    rp->buffer = ALLOC_N(char, 16);
    rp->allocated = 16;
    rp->fastmap = ALLOC_N(char, 256);
    if (flag) {
	rp->options = flag;
    }
    rp->encoding = enc ? enc : ruby_default_encoding;
    err = re_compile_pattern(s, len, rp);
    if (err != NULL) {
	rb_reg_raise(s, len, err, 0);
    }

    return rp;
}

static VALUE rb_cMatch;

static VALUE
match_alloc()
{
    NEWOBJ(match, struct RMatch);
    OBJSETUP(match, rb_cMatch, T_MATCH);

    match->str = 0;
    match->regs = 0;
    match->regs = ALLOC(struct re_registers);
    MEMZERO(match->regs, struct re_registers, 1);

    return (VALUE)match;
}

static VALUE
match_clone(match)
    VALUE match;
{
    NEWOBJ(clone, struct RMatch);
    CLONESETUP(clone, match);

    clone->str = RMATCH(match)->str;
    clone->regs = 0;

    clone->regs = ALLOC(struct re_registers);
    clone->regs->allocated = 0;
    re_copy_registers(clone->regs, RMATCH(match)->regs);

    return (VALUE)clone;
}

static VALUE
match_size(match)
    VALUE match;
{
    return INT2FIX(RMATCH(match)->regs->num_regs);
}

#define match_nth(m,x,i) enc_nth(RMATCH(match)->str, RMATCH(match)->regs->x[i])

static VALUE
match_offset(match, n)
    VALUE match, n;
{
    int i = NUM2INT(n);

    if (i < 0 || RMATCH(match)->regs->num_regs <= i)
	rb_raise(rb_eIndexError, "index %d out of matches", i);

    if (RMATCH(match)->regs->beg[i] < 0)
	return rb_assoc_new(Qnil, Qnil);

    return rb_assoc_new(INT2FIX(RMATCH(match)->regs->beg[i]),
			INT2FIX(RMATCH(match)->regs->end[i]));
}

static VALUE
match_begin(match, n)
    VALUE match, n;
{
    int i = NUM2INT(n);

    if (i < 0 || RMATCH(match)->regs->num_regs <= i)
	rb_raise(rb_eIndexError, "index %d out of matches", i);

    if (RMATCH(match)->regs->beg[i] < 0)
	return Qnil;

    return match_nth(match,beg,i);
}

static VALUE
match_end(match, n)
    VALUE match, n;
{
    int i = NUM2INT(n);

    if (i < 0 || RMATCH(match)->regs->num_regs <= i)
	rb_raise(rb_eIndexError, "index %d out of matches", i);

    if (RMATCH(match)->regs->beg[i] < 0)
	return Qnil;

    return match_nth(match,end,i);
}

#define MATCH_BUSY FL_USER2

void
rb_match_busy(match)
    VALUE match;
{
    FL_SET(match, MATCH_BUSY);
}

int ruby_ignorecase;
static int may_need_recompile;

static void
rb_reg_prepare_re(re)
    VALUE re;
{
    int need_recompile = 0;
    int state;

    rb_reg_check(re);
    state = FL_TEST(re, REG_CASESTATE);
    /* ignorecase status */
    if (ruby_ignorecase && !state) {
	FL_SET(re, REG_CASESTATE);
	RREGEXP(re)->ptr->options |= RE_OPTION_IGNORECASE;
	need_recompile = 1;
    }
    if (!ruby_ignorecase && state) {
	FL_UNSET(re, REG_CASESTATE);
	RREGEXP(re)->ptr->options &= ~RE_OPTION_IGNORECASE;
	need_recompile = 1;
    }

    if (need_recompile) {
	char *err;

	rb_reg_check(re);
	RREGEXP(re)->ptr->fastmap_accurate = 0;
	err = re_compile_pattern(RREGEXP(re)->str, RREGEXP(re)->len, RREGEXP(re)->ptr);
	if (err != NULL) {
	    rb_reg_raise(RREGEXP(re)->str, RREGEXP(re)->len, err, re);
	}
    }
}

int
rb_reg_adjust_startpos(re, str, pos, reverse)
    VALUE re, str;
    int pos, reverse;
{
    int range;

    rb_reg_check(re);
    if (may_need_recompile) rb_reg_prepare_re(re);

    if (reverse) {
	range = -pos;
    }
    else {
	range = RSTRING(str)->len - pos;
    }
    return re_adjust_startpos(RREGEXP(re)->ptr,
			      RSTRING(str)->ptr, RSTRING(str)->len,
			      pos, range);
}

int
rb_reg_search(re, str, pos, reverse)
    VALUE re, str;
    int pos, reverse;
{
    int result;
    VALUE match;
    static struct re_registers regs;
    int range;

    if (pos > RSTRING(str)->len) return -1;

    rb_reg_check(re);
    if (may_need_recompile) rb_reg_prepare_re(re);

    if (reverse) {
	range = -pos;
    }
    else {
	range = RSTRING(str)->len - pos;
    }
    result = re_search(RREGEXP(re)->ptr,RSTRING(str)->ptr,RSTRING(str)->len,
		       pos, range, &regs);

    if (result == -2) {
	rb_reg_raise(RREGEXP(re)->str, RREGEXP(re)->len,
		  "Stack overflow in regexp matcher", re);
    }

    if (result < 0) {
	rb_backref_set(Qnil);
	return result;
    }

    if (rb_thread_scope_shared_p()) {
	match = Qnil;
    }
    else {
	match = rb_backref_get();
    }
    if (NIL_P(match) || FL_TEST(match, MATCH_BUSY)) {
	match = match_alloc();
    }
    re_copy_registers(RMATCH(match)->regs, &regs);
    RMATCH(match)->str = rb_str_new4(str);
    rb_backref_set(match);

    OBJ_INFECT(match, re);
    OBJ_INFECT(match, str);
    return result;
}

VALUE
rb_reg_nth_defined(nth, match)
    int nth;
    VALUE match;
{
    if (NIL_P(match)) return Qnil;
    if (nth >= RMATCH(match)->regs->num_regs) {
	return Qfalse;
    }
    if (RMATCH(match)->BEG(nth) == -1) return Qfalse;
    return Qtrue;
}

VALUE
rb_reg_nth_match(nth, match)
    int nth;
    VALUE match;
{
    VALUE str;
    int start, end, len;

    if (NIL_P(match)) return Qnil;
    if (nth >= RMATCH(match)->regs->num_regs) {
	return Qnil;
    }
    start = RMATCH(match)->BEG(nth);
    if (start == -1) return Qnil;
    end = RMATCH(match)->END(nth);
    len = end - start;
    str = rb_str_new(RSTRING(RMATCH(match)->str)->ptr + start, len);
    if (OBJ_TAINTED(match)) OBJ_TAINT(str);
    return str;
}

VALUE
rb_reg_last_match(match)
    VALUE match;
{
    return rb_reg_nth_match(0, match);
}

VALUE
rb_reg_match_pre(match)
    VALUE match;
{
    if (NIL_P(match)) return Qnil;
    if (RMATCH(match)->BEG(0) == -1) return Qnil;
    return rb_str_new(RSTRING(RMATCH(match)->str)->ptr, RMATCH(match)->BEG(0));
}

VALUE
rb_reg_match_post(match)
    VALUE match;
{
    if (NIL_P(match)) return Qnil;
    if (RMATCH(match)->BEG(0) == -1) return Qnil;
    return rb_str_new(RSTRING(RMATCH(match)->str)->ptr+RMATCH(match)->END(0),
		      RSTRING(RMATCH(match)->str)->len-RMATCH(match)->END(0));
}

VALUE
rb_reg_match_last(match)
    VALUE match;
{
    int i;

    if (NIL_P(match)) return Qnil;
    if (RMATCH(match)->BEG(0) == -1) return Qnil;

    for (i=RMATCH(match)->regs->num_regs-1; RMATCH(match)->BEG(i) == -1 && i > 0; i--)
	;
    if (i == 0) return Qnil;
    return rb_reg_nth_match(i, match);
}

static VALUE
last_match_getter()
{
    return rb_reg_last_match(rb_backref_get());
}

static VALUE
rb_reg_s_last_match(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE nth;

    if (rb_scan_args(argc, argv, "01", &nth) == 1) {
	rb_reg_nth_match(NUM2INT(nth), rb_backref_get());
    }
    return rb_reg_last_match(rb_backref_get());
}

static VALUE
prematch_getter()
{
    return rb_reg_match_pre(rb_backref_get());
}

static VALUE
postmatch_getter()
{
    return rb_reg_match_post(rb_backref_get());
}

static VALUE
last_paren_match_getter()
{
    return rb_reg_match_last(rb_backref_get());
}

static VALUE
match_to_a(match)
    VALUE match;
{
    struct re_registers *regs = RMATCH(match)->regs;
    VALUE ary = rb_ary_new2(regs->num_regs);
    char *ptr = RSTRING(RMATCH(match)->str)->ptr;
    int i;

    for (i=0; i<regs->num_regs; i++) {
	if (regs->beg[i] == -1) rb_ary_push(ary, Qnil);
	else rb_ary_push(ary, rb_str_new(ptr+regs->beg[i],
				   regs->end[i]-regs->beg[i]));
    }
    return ary;
}

static VALUE
match_aref(argc, argv, match)
    int argc;
    VALUE *argv;
    VALUE match;
{
    VALUE idx, rest;

    rb_scan_args(argc, argv, "11", &idx, &rest);

    if (!NIL_P(rest) || !FIXNUM_P(idx) || FIX2INT(idx) < 0) {
	return rb_ary_aref(argc, argv, match_to_a(match));
    }
    return rb_reg_nth_match(FIX2INT(idx), match);
}

static VALUE
match_to_s(match)
    VALUE match;
{
    VALUE str = rb_reg_last_match(match);

    if (NIL_P(str)) str = rb_str_new(0,0);
    if (OBJ_TAINTED(match)) OBJ_TAINT(str);
    if (OBJ_TAINTED(RMATCH(match)->str)) OBJ_TAINT(str);
    return str;
}

static VALUE
match_string(match)
    VALUE match;
{
    return RMATCH(match)->str;	/* str is frozen */
}

VALUE rb_cRegexp;

static void
rb_reg_initialize(obj, s, len, options, enc)
    VALUE obj;
    const char *s;
    int len;
    int options;		/* CASEFOLD  = 1 */
				/* EXTENDED  = 2 */
				/* MULTILINE = 4 */
				/* CODE_NONE = 16 */
				/* CODE_EUC  = 32 */
				/* CODE_SJIS = 48 */
				/* CODE_UTF8 = 64 */
    m17n_encoding *enc;
{
    struct RRegexp *re = RREGEXP(obj);

    if (re->ptr) re_free_pattern(re->ptr);
    if (re->str) free(re->str);
    re->ptr = 0;
    re->str = 0;

    if (!enc) {
	 switch (options & ~0xf) {
	   case 0:
	   default:
	     enc = ruby_default_encoding;
	     break;
	   case 16:
	     enc = m17n_find_encoding("ascii");
	     break;
	   case 32:
	     enc = m17n_find_encoding("euc-jp");
	     break;
	   case 48:
	     enc = m17n_find_encoding("sjis");
	     break;
	   case 64:
	     enc = m17n_find_encoding("utf-8");
	     break;
	 }
    }

    if (ruby_ignorecase) {
	options |= RE_OPTION_IGNORECASE;
	FL_SET(re, REG_CASESTATE);
    }
    re->ptr = make_regexp(s, len, options, enc);
    re->str = ALLOC_N(char, len+1);
    memcpy(re->str, s, len);
    re->str[len] = '\0';
    re->len = len;
    rb_m17n_associate_encoding((VALUE)re, enc);
}

VALUE
rb_reg_new(s, len, options)
    const char *s;
    long len;
    int options;
{
    NEWOBJ(re, struct RRegexp);
    OBJSETUP(re, rb_cRegexp, T_REGEXP);

    re->ptr = 0; re->len = 0; re->str = 0;

    rb_reg_initialize(re, s, len, options, 0);
    return (VALUE)re;
}

static int case_cache;
static VALUE reg_cache;

VALUE
rb_reg_regcomp(str)
    VALUE str;
{
    if (reg_cache && RREGEXP(reg_cache)->len == RSTRING(str)->len
	&& case_cache == ruby_ignorecase
	&& memcmp(RREGEXP(reg_cache)->str, RSTRING(str)->ptr, RSTRING(str)->len) == 0)
	return reg_cache;

    case_cache = ruby_ignorecase;
    return reg_cache = rb_reg_new(RSTRING(str)->ptr, RSTRING(str)->len,
				  ruby_ignorecase);
}

static VALUE
rb_reg_equal(re1, re2)
    VALUE re1, re2;
{
    int min;

    if (re1 == re2) return Qtrue;
    if (TYPE(re2) != T_REGEXP) return Qfalse;
    rb_reg_check(re1); rb_reg_check(re2);
    if (RREGEXP(re1)->len != RREGEXP(re2)->len) return Qfalse;
    min = RREGEXP(re1)->len;
    if (min > RREGEXP(re2)->len) min = RREGEXP(re2)->len;
    if (memcmp(RREGEXP(re1)->str, RREGEXP(re2)->str, min) == 0 &&
	rb_m17n_get_encoding(re1) == rb_m17n_get_encoding(re2) &&
	!((RREGEXP(re1)->ptr->options & RE_OPTION_IGNORECASE) ^
	  (RREGEXP(re2)->ptr->options & RE_OPTION_IGNORECASE))) {
	return Qtrue;
    }
    return Qfalse;
}

VALUE
rb_reg_match(re, str)
    VALUE re, str;
{
    int start;

    if (NIL_P(str)) return Qnil;
    str = rb_str_to_str(str);
    start = rb_reg_search(re, str, 0, 0);
    if (start < 0) {
	return Qnil;
    }
    return enc_nth(str, start);
}

VALUE
rb_reg_match2(re)
    VALUE re;
{
    int start;
    VALUE line = rb_lastline_get();

    if (TYPE(line) != T_STRING)
	return Qnil;

    start = rb_reg_search(re, line, 0, 0);
    if (start < 0) {
	return Qnil;
    }
    return enc_nth(line, start);
}

static VALUE
rb_reg_match_m(re, str)
    VALUE re, str;
{
    VALUE result = rb_reg_match(re, str);

    if (NIL_P(result)) return Qnil;
    result = rb_backref_get();
    rb_match_busy(result);
    return result;
}

static VALUE
rb_reg_initialize_m(argc, argv, self)
    int argc;
    VALUE *argv;
    VALUE self;
{
    m17n_encoding *enc;
    VALUE src;
    int flag = 0;

    if (argc == 0 || argc > 3) {
	rb_raise(rb_eArgError, "wrong # of argument");
    }
    if (argc >= 2) {
	if (FIXNUM_P(argv[1])) flag = FIX2INT(argv[1]);
	else if (RTEST(argv[1])) flag = RE_OPTION_IGNORECASE;
    }
    if (argc == 3) {
	char *kcode = STR2CSTR(argv[2]);

	if (kcode[1] == 0) {
	    switch (kcode[0]) {
	      case 'n': case 'N':
		kcode = "ascii";
		break;
	      case 'e': case 'E':
		kcode = "euc-jp";
		break;
	      case 's': case 'S':
		kcode = "sjis";
		break;
	      case 'u': case 'U':
		kcode = "utf-8";
		break;
	      default:
		break;
	    }
	}
	enc = m17n_find_encoding(kcode);
	if (!enc) {
	    rb_raise(rb_eArgError, "unknow encoding %s", kcode);
	}
    }

    src = argv[0];
    if (TYPE(src) == T_REGEXP) {
	rb_reg_check(src);
	rb_reg_initialize(self, RREGEXP(src)->str, RREGEXP(src)->len, flag, enc);
    }
    else {
	char *p;
	int len;

	p = rb_str2cstr(src, &len);
	rb_reg_initialize(self, p, len, flag, enc);
    }
    return self;
}

static VALUE
rb_reg_s_new(argc, argv, klass)
    int argc;
    VALUE *argv;
    VALUE klass;
{
    NEWOBJ(re, struct RRegexp);
    OBJSETUP(re, klass, T_REGEXP);
    re->ptr = 0; re->len = 0; re->str = 0;
    rb_obj_call_init((VALUE)re, argc, argv);
    return (VALUE)re;
}

static VALUE
rb_reg_s_quote(argc, argv)
    int argc;
    VALUE *argv;
{
    m17n_encoding *enc;
    VALUE str, kcode;
    char *s, *send, *t;
    VALUE tmp;
    int len;

    rb_scan_args(argc, argv, "11", &str, &kcode);
    if (!NIL_P(kcode)) {
	enc = m17n_find_encoding(STR2CSTR(kcode));
    }
    else {
	enc = rb_m17n_get_encoding(str);
    }
    s = rb_str2cstr(str, &len);
    send = s + len;
    tmp = rb_str_new(0, len*2);
    t = RSTRING(tmp)->ptr;

    for (; s < send; s++) {
	if (ismbchar(*s)) {
	    int n = mbclen(*s);

	    while (n-- && s < send)
		*t++ = *s++;
	    s--;
	    continue;
	}
	if (*s == '[' || *s == ']'
	    || *s == '{' || *s == '}'
	    || *s == '(' || *s == ')'
	    || *s == '|' || *s == '-'
	    || *s == '*' || *s == '.' || *s == '\\'
	    || *s == '?' || *s == '+'
	    || *s == '^' || *s == '$') {
	    *t++ = '\\';
	}
	*t++ = *s;
    }
    rb_str_resize(tmp, t - RSTRING(tmp)->ptr);

    return tmp;
}

int
rb_reg_options(re)
    VALUE re;
{
    int options = 0;

    rb_reg_check(re);
    if (RREGEXP(re)->ptr->options & RE_OPTION_IGNORECASE)
	options |= RE_OPTION_IGNORECASE;
    return options;
}

static VALUE
rb_reg_clone(re)
    VALUE re;
{
    m17n_encoding *enc = rb_m17n_get_encoding(re);
    NEWOBJ(clone, struct RRegexp);
    CLONESETUP(clone, re);
    rb_reg_check(re);
    clone->ptr = 0; clone->len = 0; clone->str = 0;
    rb_reg_initialize(clone, RREGEXP(re)->str, RREGEXP(re)->len,
		      rb_reg_options(re), enc);
    return (VALUE)re;
}

VALUE
rb_reg_regsub(str, src, regs)
    VALUE str, src;
    struct re_registers *regs;
{
    m17n_encoding *enc = rb_m17n_get_encoding(str);
    VALUE val = 0;
    char *p, *s, *e, c;
    int no;

    p = s = RSTRING(str)->ptr;
    e = s + RSTRING(str)->len;

    while (s < e) {
	char *ss = s;

	c = *s++;
	if (ismbchar(c)) {
	    s += mbclen(c) - 1;
	    continue;
	}
	if (c != '\\' || s == e) continue;

	if (!val) val = rb_str_new(p, ss-p);
	else      rb_str_cat(val, p, ss-p);

	c = *s++;
	p = s;
	switch (c) {
	  case '0': case '1': case '2': case '3': case '4':
	  case '5': case '6': case '7': case '8': case '9':
	    no = c - '0';
	    break;
	  case '&':
	    no = 0;
	    break;

	  case '`':
	    rb_str_cat(val, RSTRING(src)->ptr, BEG(0));
	    continue;

	  case '\'':
	    rb_str_cat(val, RSTRING(src)->ptr+END(0), RSTRING(src)->len-END(0));
	    continue;

	  case '+':
	    no = regs->num_regs-1;
	    while (BEG(no) == -1 && no > 0) no--;
	    if (no == 0) continue;
	    break;

	  case '\\':
	    rb_str_cat(val, s-1, 1);
	    continue;

	  default:
	    rb_str_cat(val, s-2, 2);
	    continue;
	}

	if (no >= 0) {
	    if (no >= regs->num_regs) continue;
	    if (BEG(no) == -1) continue;
	    rb_str_cat(val, RSTRING(src)->ptr+BEG(no), END(no)-BEG(no));
	}
    }

    if (p < e) {
	if (!val) val = rb_str_new(p, e-p);
	else      rb_str_cat(val, p, e-p);
    }
    if (!val) return str;

    return val;
}

static VALUE
kcode_getter()
{
    rb_warn("$KCODE is obsolete");
    return rb_str_new2(ruby_default_encoding->name);
}

void
rb_set_kcode(code)
    const char *code;
{
    m17n_encoding *enc;

    if (code == 0) {
	code = "ascii";
    }

    if (code[1] == 0) {
	switch (code[0]) {
	  case 'E':
	  case 'e':
	    code = "euc-jp";
	    break;
	  case 'S':
	  case 's':
	    code = "sjis";
	    break;
	  case 'U':
	  case 'u':
	    code = "utf-8";
	    break;
	  case 'N':
	  case 'n':
	  case 'A':
	  case 'a':
	    code = "ascii";
	    break;
	}
    }
    enc = m17n_find_encoding(code);
    if (!enc) {
	rb_raise(rb_eArgError, "unknow encoding %s", code);
    }
    ruby_default_encoding = enc;
}

static void
kcode_setter(val)
    struct RString *val;
{
    rb_warn("changing default encoding on the fly is not recommended");
    rb_set_kcode(STR2CSTR(val));
}

static VALUE
ignorecase_getter()
{
    return ruby_ignorecase?Qtrue:Qfalse;
}

static void
ignorecase_setter(val)
    VALUE val;
{
    may_need_recompile = 1;
    ruby_ignorecase = RTEST(val);
}

static VALUE
match_getter()
{
    VALUE match = rb_backref_get();

    if (NIL_P(match)) return Qnil;
    rb_match_busy(match);
    return match;
}

static void
match_setter(val)
    VALUE val;
{
    Check_Type(val, T_MATCH);
    rb_backref_set(val);
}

void
Init_Regexp()
{
    rb_eRegexpError = rb_define_class("RegexpError", rb_eStandardError);

    re_set_default_encoding(ruby_default_encoding);
    rb_define_virtual_variable("$~", match_getter, match_setter);
    rb_define_virtual_variable("$&", last_match_getter, 0);
    rb_define_virtual_variable("$`", prematch_getter, 0);
    rb_define_virtual_variable("$'", postmatch_getter, 0);
    rb_define_virtual_variable("$+", last_paren_match_getter, 0);

    rb_define_virtual_variable("$=", ignorecase_getter, ignorecase_setter);
    rb_define_virtual_variable("$KCODE", kcode_getter, kcode_setter);
    rb_define_virtual_variable("$-K", kcode_getter, kcode_setter);

    rb_cRegexp = rb_define_class("Regexp", rb_cObject);
    rb_define_singleton_method(rb_cRegexp, "new", rb_reg_s_new, -1);
    rb_define_singleton_method(rb_cRegexp, "compile", rb_reg_s_new, -1);
    rb_define_singleton_method(rb_cRegexp, "quote", rb_reg_s_quote, -1);
    rb_define_singleton_method(rb_cRegexp, "escape", rb_reg_s_quote, -1);
    rb_define_singleton_method(rb_cRegexp, "last_match", rb_reg_s_last_match, 0);

    rb_define_method(rb_cRegexp, "initialize", rb_reg_initialize_m, -1);
    rb_define_method(rb_cRegexp, "clone", rb_reg_clone, 0);
    rb_define_method(rb_cRegexp, "==", rb_reg_equal, 1);
    rb_define_method(rb_cRegexp, "=~", rb_reg_match, 1);
    rb_define_method(rb_cRegexp, "===", rb_reg_match, 1);
    rb_define_method(rb_cRegexp, "~", rb_reg_match2, 0);
    rb_define_method(rb_cRegexp, "match", rb_reg_match_m, 1);
    rb_define_method(rb_cRegexp, "inspect", rb_reg_inspect, 0);
    rb_define_method(rb_cRegexp, "source", rb_reg_source, 0);
    rb_define_method(rb_cRegexp, "casefold?", rb_reg_casefold_p, 0);
    rb_define_method(rb_cRegexp, "kcode", rb_reg_kcode_m, 0);

    rb_define_method(rb_cRegexp, "encoding", rb_enc_get_encoding, 0);
    rb_define_method(rb_cRegexp, "encoding=", rb_enc_set_encoding, 1);

    rb_define_const(rb_cRegexp, "IGNORECASE", INT2FIX(RE_OPTION_IGNORECASE));
    rb_define_const(rb_cRegexp, "EXTENDED", INT2FIX(RE_OPTION_EXTENDED));
    rb_define_const(rb_cRegexp, "MULTILINE", INT2FIX(RE_OPTION_MULTILINE));

    rb_global_variable(&reg_cache);

    rb_cMatch  = rb_define_class("MatchData", rb_cObject);
    rb_define_global_const("MatchingData", rb_cMatch);
    rb_undef_method(CLASS_OF(rb_cMatch), "new");
    rb_define_method(rb_cMatch, "clone", match_clone, 0);
    rb_define_method(rb_cMatch, "size", match_size, 0);
    rb_define_method(rb_cMatch, "length", match_size, 0);
    rb_define_method(rb_cMatch, "offset", match_offset, 1);
    rb_define_method(rb_cMatch, "begin", match_begin, 1);
    rb_define_method(rb_cMatch, "end", match_end, 1);
    rb_define_method(rb_cMatch, "to_a", match_to_a, 0);
    rb_define_method(rb_cMatch, "[]", match_aref, -1);
    rb_define_method(rb_cMatch, "pre_match", rb_reg_match_pre, 0);
    rb_define_method(rb_cMatch, "post_match", rb_reg_match_post, 0);
    rb_define_method(rb_cMatch, "to_s", match_to_s, 0);
    rb_define_method(rb_cMatch, "string", match_string, 0);
    rb_define_method(rb_cMatch, "inspect", rb_any_to_s, 0);
}
