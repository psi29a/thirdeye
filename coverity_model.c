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
 * arc compiler-state-stream accessor (apps/arc/resource.cpp)
 *
 * CSS_fetch_num pulls a length-prefixed value out of an arc-emitted compiled
 * state stream. The stream is produced by arc itself in the same run, not
 * read from untrusted input. Sanitize so downstream "ndims", "dsize", "vsize"
 * usage doesn't cascade into TAINTED_SCALAR/UNTRUSTED_LOOP_BOUND across
 * SOP_compile_index, SOP_var_declaration, SOP_member_statement, etc.
 * ------------------------------------------------------------------------- */

typedef struct CSS_class CSS_class;

/* Match arc/defs.hpp typedefs: ULONG = unsigned int, LONG = int. Using
 * `unsigned long` here would resolve to a different symbol on LP64 and the
 * model wouldn't bind to the real function. */
unsigned int CSS_fetch_num(CSS_class *CSS)
{
	unsigned int v;
	__coverity_tainted_data_sanitize__(&v);
	return v;
}

/* ------------------------------------------------------------------------- *
 * arc literal-constant fetch (apps/arc/sopcomp.cpp)
 *
 * SOP_fetch_literal_constant returns a LONG (= int) folded from lexer values
 * + the resource-name table; both feeders are already-sanitized in this
 * model. Modeling it as a sanitizer kills the cascade across
 * SOP_case_statement, SOP_member_statement, SOP_statement, SOP_construct,
 * SOP_expr_*, SOP_break_statement, SOP_continue_statement, etc.
 * SOP_resource_name_entry is the related table-index variant.
 * ------------------------------------------------------------------------- */

typedef struct SOP_class SOP_class;

int SOP_fetch_literal_constant(SOP_class *SOP)
{
	int v;
	__coverity_tainted_data_sanitize__(&v);
	return v;
}

unsigned int SOP_resource_name_entry(SOP_class *SOP)
{
	unsigned int v;
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
 * arc source-file slurp (apps/arc/system.cpp)
 *
 * read_file() returns the whole source buffer for the lexer to walk. It's a
 * compiler tool reading its own user-provided source; the 64 MB cap upstream
 * already bounds the size. Sanitizing the returned buffer kills the taint
 * chain at the source -- without this, every downstream SOP_*/RS_*/MAP_*
 * parser function inherits TAINTED_SCALAR / UNTRUSTED_LOOP_BOUND /
 * UNTRUSTED_DIVISOR through the LEX state and the resource tables.
 *
 * Match arc/defs.hpp: ULONG = unsigned int.
 * ------------------------------------------------------------------------- */

unsigned int *read_file(unsigned char *filename)
{
	unsigned int *p;
	(void)filename;
	__coverity_tainted_data_sanitize__(&p);
	return p;
}

/* ------------------------------------------------------------------------- *
 * arc lexer raw-fetch (apps/arc/lexan.cpp)
 *
 * LEX_fetch refills the current lexer state from the source buffer; it
 * applies token-type classification + length checks before any caller sees
 * the result. Modeling the whole LEX state as sanitized after a fetch stops
 * UNTRUSTED_ARRAY_INDEX_READ on the is_whitespace/is_digit/is_namechar/
 * symbol_key lookups inside LEX_fetch itself (all 256-entry tables indexed
 * by an unsigned byte -- the OOB is unreachable).
 * ------------------------------------------------------------------------- */

void LEX_fetch(LEX_class *LEX)
{
	__coverity_writeall__(LEX);
}

/* ------------------------------------------------------------------------- *
 * arc resource-table lookups (apps/arc/rscomp.cpp)
 *
 * RS_get_MSGD_entry / RS_get_OBJD_entry pull a small numeric ID out of the
 * compiler's own symbol tables, built earlier in the same arc run. Treating
 * them as sanitized stops the cascade through SOP_compile_send,
 * SOP_member_statement, etc.
 * ------------------------------------------------------------------------- */

typedef struct RS_class RS_class;

unsigned int RS_get_MSGD_entry(RS_class *RS, char *name)
{
	unsigned int v;
	(void)RS;
	(void)name;
	__coverity_tainted_data_sanitize__(&v);
	return v;
}

unsigned int RS_get_OBJD_entry(RS_class *RS, char *name)
{
	unsigned int v;
	(void)RS;
	(void)name;
	__coverity_tainted_data_sanitize__(&v);
	return v;
}

/* ------------------------------------------------------------------------- *
 * arc pointer-arithmetic helper (apps/arc/system.cpp)
 *
 * ptr_dif returns (a - b) / sizeof(unit) for arc's growable buffers. Both
 * pointers are arc-internal allocations; the result is the element distance
 * within an arc-owned region, not a value derived from external input. The
 * divisor in the lowered code is a sizeof, but the *result* gets used as a
 * loop bound in SOP_break_statement / SOP_continue_statement and Coverity
 * inherits taint through the pointers.
 * ------------------------------------------------------------------------- */

unsigned int ptr_dif(unsigned int *a, unsigned int *b)
{
	unsigned int v;
	(void)a;
	(void)b;
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

/* ------------------------------------------------------------------------- *
 * arc fatal-exit (apps/arc/system.cpp)
 *
 * abend() calls exit(1) and never returns; report(E_FATAL, ...) and
 * report(E_FAULT, ...) flow through it. Modeling it as a panic prevents the
 * stream of "deref after null check" / "use after free" false positives on
 * code that bails through report(E_FATAL, ...).
 * ------------------------------------------------------------------------- */

void abend(void)
{
	__coverity_panic__();
}
