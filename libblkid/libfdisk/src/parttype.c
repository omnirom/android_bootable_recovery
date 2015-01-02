
#include <ctype.h>

#include "nls.h"
#include "fdiskP.h"

/**
 * SECTION: parttype
 * @title: Partition types
 * @short_description: abstraction to partition types
 *
 * There are two basic types of parttypes, string based (e.g. GPT)
 * and code/hex based (e.g. MBR).
 */

/**
 * fdisk_new_parttype:
 *
 * It's recommended to use fdisk_label_get_parttype_from_code() or
 * fdisk_label_get_parttype_from_string() for well known types rather
 * than allocate a new instance.
 *
 * Returns: new instance.
 */
struct fdisk_parttype *fdisk_new_parttype(void)
{
	struct fdisk_parttype *t = calloc(1, sizeof(*t));

	t->refcount = 1;
	t->flags = FDISK_PARTTYPE_ALLOCATED;
	DBG(PARTTYPE, ul_debugobj(t, "alloc"));
	return t;
}

/**
 * fdisk_ref_parttype:
 * @t: partition type
 *
 * Incremparts reference counter for allocated types
 */
void fdisk_ref_parttype(struct fdisk_parttype *t)
{
	if (fdisk_parttype_is_allocated(t))
		t->refcount++;
}

/**
 * fdisk_unref_parttype
 * @t: partition pointer
 *
 * De-incremparts reference counter, on zero the @t is automatically
 * deallocated.
 */
void fdisk_unref_parttype(struct fdisk_parttype *t)
{
	if (!fdisk_parttype_is_allocated(t))
		return;

	t->refcount--;
	if (t->refcount <= 0) {
		DBG(PARTTYPE, ul_debugobj(t, "free"));
		free(t->typestr);
		free(t->name);
		free(t);
	}
}

/**
 * fdisk_parttype_set_name:
 * @t: partition type
 * @str: type name
 *
 * Sets type name to allocated partition type, for static types
 * it returns -EINVAL.
 *
 * Return: 0 on success, <0 on error
 */
int fdisk_parttype_set_name(struct fdisk_parttype *t, const char *str)
{
	char *p = NULL;

	if (!t || !fdisk_parttype_is_allocated(t))
		return -EINVAL;
	if (str) {
		p = strdup(str);
		if (!p)
			return -ENOMEM;
	}

	free(t->name);
	t->name = p;
	return 0;
}

/**
 * fdisk_parttype_set_typestr:
 * @t: partition type
 * @str: type identificator (e.g. GUID for GPT)
 *
 * Sets type string to allocated partition type, for static types
 * it returns -EINVAL. Don't use this function for MBR, see
 * fdisk_parttype_set_code().
 *
 * Return: 0 on success, <0 on error
 */
int fdisk_parttype_set_typestr(struct fdisk_parttype *t, const char *str)
{
	char *p = NULL;

	if (!t || !fdisk_parttype_is_allocated(t))
		return -EINVAL;
	if (str) {
		p = strdup(str);
		if (!p)
			return -ENOMEM;
	}

	free(t->typestr);
	t->typestr = p;
	return 0;
}

/**
 * fdisk_parttype_set_code:
 * @t: partition type
 * @code: type identificator (e.g. MBR type codes)
 *
 * Sets type code to allocated partition type, for static types it returns
 * -EINVAL. Don't use this function for GPT, see fdisk_parttype_set_typestr().
 *
 * Return: 0 on success, <0 on error
 */
int fdisk_parttype_set_code(struct fdisk_parttype *t, int code)
{
	if (!t || !fdisk_parttype_is_allocated(t))
		return -EINVAL;
	t->code = code;
	return 0;
}

/**
 * fdisk_label_get_nparttypes:
 * @lb: label
 *
 * Returns: number of types supported by label.
 */
size_t fdisk_label_get_nparttypes(const struct fdisk_label *lb)
{
	if (!lb)
		return 0;
	return lb->nparttypes;
}

/**
 * fdisk_label_get_parttype:
 * @lb: label
 * @n: number
 *
 * Returns: return parttype
 */
struct fdisk_parttype *fdisk_label_get_parttype(const struct fdisk_label *lb, size_t n)
{
	if (!lb || n >= lb->nparttypes)
		return NULL;
	return &lb->parttypes[n];
}

/**
 * fdisk_label_has_code_parttypes:
 * @lb: label
 *
 * Returns: 1 if the label uses code as partition type
 *          identifiers (e.g. MBR) or 0.
 */
int fdisk_label_has_code_parttypes(const struct fdisk_label *lb)
{
	assert(lb);

	if (lb->parttypes && lb->parttypes[0].typestr)
		return 0;
	return 1;
}


/**
 * fdisk_label_get_parttype_from_code:
 * @lb: label
 * @code: code to search for
 *
 * Search for partition type in label-specific table. The result
 * is pointer to static array of label types.
 *
 * Returns: partition type or NULL upon failure or invalid @code.
 */
struct fdisk_parttype *fdisk_label_get_parttype_from_code(
				const struct fdisk_label *lb,
				unsigned int code)
{
	size_t i;

	assert(lb);

	if (!lb->nparttypes)
		return NULL;

	for (i = 0; i < lb->nparttypes; i++)
		if (lb->parttypes[i].code == code)
			return &lb->parttypes[i];
	return NULL;
}

/**
 * fdisk_label_get_parttype_from_string:
 * @lb: label
 * @str: string to search for
 *
 * Search for partition type in label-specific table. The result
 * is pointer to static array of label types.
 *
 * Returns: partition type or NULL upon failure or invalid @str.
 */
struct fdisk_parttype *fdisk_label_get_parttype_from_string(
				const struct fdisk_label *lb,
				const char *str)
{
	size_t i;

	assert(lb);

	if (!lb->nparttypes)
		return NULL;

	for (i = 0; i < lb->nparttypes; i++)
		if (lb->parttypes[i].typestr
		    && strcasecmp(lb->parttypes[i].typestr, str) == 0)
			return &lb->parttypes[i];

	return NULL;
}

/**
 * fdisk_new_unknown_parttype:
 * @code: type as number
 * @typestr: type as string

 * Allocates new 'unknown' partition type. Use fdisk_unref_parttype() to
 * deallocate.
 *
 * Returns: newly allocated partition type, or NULL upon failure.
 */
struct fdisk_parttype *fdisk_new_unknown_parttype(unsigned int code,
						  const char *typestr)
{
	struct fdisk_parttype *t = fdisk_new_parttype();

	if (!t)
		return NULL;

	fdisk_parttype_set_name(t, _("unknown"));
	fdisk_parttype_set_code(t, code);
	fdisk_parttype_set_typestr(t, typestr);
	t->flags |= FDISK_PARTTYPE_UNKNOWN;

	return t;
}

/**
 * fdisk_copy_parttype:
 * @type: type to copy
 *
 * Use fdisk_unref_parttype() to deallocate.
 *
 * Returns: newly allocated partition type, or NULL upon failure.
 */
struct fdisk_parttype *fdisk_copy_parttype(const struct fdisk_parttype *type)
{
	struct fdisk_parttype *t = fdisk_new_parttype();

	if (!t)
		return NULL;

	fdisk_parttype_set_name(t, type->name);
	fdisk_parttype_set_code(t, type->code);
	fdisk_parttype_set_typestr(t, type->typestr);

	return t;
}

/**
 * fdisk_label_parse_parttype:
 * @lb: label
 * @str: string to parse from
 *
 * Parses partition type from @str according to the label. Thefunction returns
 * a pointer to static table of the partition types, or newly allocated
 * partition type for unknown types (see fdisk_parttype_is_unknown(). It's
 * safe to call fdisk_unref_parttype() for all results.
 *
 * Returns: pointer to type or NULL on error.
 */
struct fdisk_parttype *fdisk_label_parse_parttype(
				const struct fdisk_label *lb,
				const char *str)
{
	struct fdisk_parttype *types, *ret;
	unsigned int code = 0;
	char *typestr = NULL, *end = NULL;

	assert(lb);

	if (!lb->nparttypes)
		return NULL;

	DBG(LABEL, ul_debugobj((void *) lb, "parsing '%s' (%s) partition type",
				str, lb->name));
	types = lb->parttypes;

	if (types[0].typestr == NULL && isxdigit(*str)) {

		errno = 0;
		code = strtol(str, &end, 16);

		if (errno || *end != '\0') {
			DBG(LABEL, ul_debugobj((void *) lb, "parsing failed: %m"));
			return NULL;
		}
		ret = fdisk_label_get_parttype_from_code(lb, code);
		if (ret)
			goto done;
	} else {
		int i;

		/* maybe specified by type string (e.g. UUID) */
		ret = fdisk_label_get_parttype_from_string(lb, str);
		if (ret)
			goto done;

		/* maybe specified by order number */
		errno = 0;
		i = strtol(str, &end, 0);
		if (errno == 0 && *end == '\0' && i > 0
		    && i - 1 < (int) lb->nparttypes) {
			ret = &types[i - 1];
			goto done;
		}
	}

	ret = fdisk_new_unknown_parttype(code, typestr);
done:
	DBG(PARTTYPE, ul_debugobj(ret, "returns parsed '%s' partition type", ret->name));
	return ret;
}

/**
 * fdisk_parttype_get_string:
 * @t: type
 *
 * Returns: partition type string (e.g. GUID for GPT)
 */
const char *fdisk_parttype_get_string(const struct fdisk_parttype *t)
{
	assert(t);
	return t->typestr && *t->typestr ? t->typestr : NULL;
}

/**
 * fdisk_parttype_get_code:
 * @t: type
 *
 * Returns: partition type code (e.g. for MBR)
 */
unsigned int fdisk_parttype_get_code(const struct fdisk_parttype *t)
{
	assert(t);
	return t->code;
}

/**
 * fdisk_parttype_get_name:
 * @t: type
 *
 * Returns: partition type human readable name
 */
const char *fdisk_parttype_get_name(const struct fdisk_parttype *t)
{
	assert(t);
	return t->name;
}

/**
 * fdisk_parttype_is_unknown:
 * @t: type
 *
 * Checks for example result from fdisk_label_parse_parttype().
 *
 * Returns: 1 is type is "unknonw" or 0.
 */
int fdisk_parttype_is_unknown(const struct fdisk_parttype *t)
{
	return t && (t->flags & FDISK_PARTTYPE_UNKNOWN) ? 1 : 0;
}
