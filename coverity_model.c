/*
 * Coverity Scan modeling file for psi29a/thirdeye.
 *
 * Upload via https://scan.coverity.com -> Project Settings -> Analysis Settings
 * -> "Modeling file" (or pass --model-file=coverity_model.c to cov-analyze for
 * local runs). The file is analyzed by Coverity instead of compiled, so it
 * never goes through the project build.
 *
 * What this file does:
 *
 *  1. arc/ is a compiler. Coverity flags every byte read from the source file
 *     as TAINTED_SCALAR, so the entire SOP/RS parser tree trips on LEX_* call
 *     sites that *correctly* use the lexer's outputs. The lexer validates,
 *     length-checks, and keyword-matches before returning; downstream callers
 *     are using sanitized data, not raw bytes. We model the LEX_* accessors
 *     as returning sanitized values.
 *
 *  2. arc/report() is the legacy varargs error printer. Some call sites pass
 *     a trailing NULL sentinel for safety. Those are harmless but trip
 *     PRINTF_ARGS. Modeling report() without a format-string sink turns the
 *     check off for it.
 *
 *  3. Local taint guards added in code (e.g. the loColumns < 1024 cap in
 *     daesop convert.cpp and the resource-count cap in dict.cpp) handle the
 *     daesop side; no models needed there.
 *
 * Do NOT add models to silence real bugs -- only false positives on patterns
 * Coverity can't see across.
 */

/* ------------------------------------------------------------------------- *
 * arc lexer accessors (apps/arc/lexan.cpp)
 *
 * The lexer's outputs are products of its own validation, not raw file
 * contents. Treating them as sanitized stops the false-positive cascade
 * through SOP_expr_*, SOP_var_declaration, RS_compile, etc.
 * ------------------------------------------------------------------------- */

typedef struct LEX_class LEX_class;

short LEX_type(LEX_class *LEX, unsigned short select)
{
	short v;
	__coverity_tainted_data_sanitize__(&v);
	return v;
}

short LEX_token(LEX_class *LEX, unsigned short select)
{
	short v;
	__coverity_tainted_data_sanitize__(&v);
	return v;
}

unsigned int LEX_value(LEX_class *LEX, unsigned short select)
{
	unsigned int v;
	__coverity_tainted_data_sanitize__(&v);
	return v;
}

char *LEX_lexeme(LEX_class *LEX, unsigned short select)
{
	char *p;
	__coverity_tainted_data_sanitize__(&p);
	return p;
}

char *LEX_line(LEX_class *LEX, unsigned short select)
{
	char *p;
	__coverity_tainted_data_sanitize__(&p);
	return p;
}

short LEX_next_comma(LEX_class *LEX)
{
	short v;
	__coverity_tainted_data_sanitize__(&v);
	return v;
}

short LEX_next_constant(LEX_class *LEX)
{
	short v;
	__coverity_tainted_data_sanitize__(&v);
	return v;
}

/* ------------------------------------------------------------------------- *
 * daesop dictionary-array length (apps/daesop/dict.cpp)
 *
 * getNumberOfItems walks a NULL-terminated array and caps its own result at
 * MAX_NUMBER_OF_DICTIONARY_ITEMS. The returned count is therefore bounded
 * and safe for "+1, * sizeof(struct)" sizing math. Modeling it as a
 * sanitized small non-negative integer cuts the INTEGER_OVERFLOW class
 * across every caller that does `count + 1` then multiplies by a struct
 * size for malloc.
 *
 * Forward-declare the entry pointer; the model file isn't compiled so we
 * don't need the real struct definition.
 * ------------------------------------------------------------------------- */

struct INTERNAL_DICTIONARY_ENTRY;
typedef struct INTERNAL_DICTIONARY_ENTRY *DICTENTRYPOINTER;

int getNumberOfItems(DICTENTRYPOINTER *aArray)
{
	int v;
	__coverity_tainted_data_sanitize__(&v);
	return v;
}

/* ------------------------------------------------------------------------- *
 * arc varargs reporter (apps/arc/system.cpp)
 *
 * report() is a custom error printer. Modeling it without a
 * __coverity_format_string_sink__ tells Coverity not to enforce printf-style
 * arg-count matching here, which silences PRINTF_ARGS on the legacy
 * trailing-NULL call style.
 * ------------------------------------------------------------------------- */

void report(unsigned short errtype, char *prefix, char *msg, ...)
{
	(void)errtype;
	(void)prefix;
	(void)msg;
}
