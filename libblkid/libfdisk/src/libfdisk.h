/*
 * libfdisk.h - libfdisk API
 *
 * Copyright (C) 2012-2014 Karel Zak <kzak@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _LIBFDISK_H
#define _LIBFDISK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

/**
 * LIBFDISK_VERSION:
 *
 * Library version string
 */
#define LIBFDISK_VERSION   "2.25.0"

/**
 * fdisk_context:
 *
 * Basic library handler.
 */
struct fdisk_context;

/**
 * fdisk_label:
 *
 * Disk label specific driver and setting.
 */
struct fdisk_label;

/**
 * fdisk_parttype:
 *
 * Partition type.
 */
struct fdisk_parttype;

/**
 * fdisk_partition:
 *
 * Partition abstraction (and template).
 */
struct fdisk_partition;

/**
 * fdisk_ask:
 *
 * Ask API handler for dialogs with users.
 */
struct fdisk_ask;

/**
 * fdisk_iter:
 *
 * Unified iterator.
 */
struct fdisk_iter;

/**
 * fdisk_table:
 *
 * Container for fdisk_partition objects
 */
struct fdisk_table;

/**
 * fdisk_field
 *
 * Output field description.
 */
struct fdisk_field;

/**
 * fdisk_script
 *
 * library handler for sfdisk compatible scripts
 */
struct fdisk_script;

/**
 * fdisk_sector_t
 *
 * LBA adresses type
 */
typedef uint64_t fdisk_sector_t;

/**
 * fdisk_labeltype:
 *
 * Supported partition table types (labels)
 */
enum fdisk_labeltype {
	FDISK_DISKLABEL_DOS = (1 << 1),
	FDISK_DISKLABEL_SUN = (1 << 2),
	FDISK_DISKLABEL_SGI = (1 << 3),
	FDISK_DISKLABEL_BSD = (1 << 4),
	FDISK_DISKLABEL_GPT = (1 << 5)
};

/**
 * fdisk_asktype:
 *
 * Ask API dialog types
 */
enum fdisk_asktype {
	FDISK_ASKTYPE_NONE = 0,
	FDISK_ASKTYPE_NUMBER,
	FDISK_ASKTYPE_OFFSET,
	FDISK_ASKTYPE_WARN,
	FDISK_ASKTYPE_WARNX,
	FDISK_ASKTYPE_INFO,
	FDISK_ASKTYPE_YESNO,
	FDISK_ASKTYPE_STRING,
	FDISK_ASKTYPE_MENU
};

/* init.c */
extern void fdisk_init_debug(int mask);

/* context.h */

#define FDISK_PLURAL	0
#define FDISK_SINGULAR	1

struct fdisk_context *fdisk_new_context(void);
struct fdisk_context *fdisk_new_nested_context(struct fdisk_context *parent, const char *name);
void fdisk_unref_context(struct fdisk_context *cxt);
void fdisk_ref_context(struct fdisk_context *cxt);

struct fdisk_context *fdisk_get_parent(struct fdisk_context *cxt);
size_t fdisk_get_npartitions(struct fdisk_context *cxt);

struct fdisk_label *fdisk_get_label(struct fdisk_context *cxt, const char *name);
int fdisk_next_label(struct fdisk_context *cxt, struct fdisk_label **lb);
size_t fdisk_get_nlabels(struct fdisk_context *cxt);

int fdisk_has_label(struct fdisk_context *cxt);
int fdisk_is_labeltype(struct fdisk_context *cxt, enum fdisk_labeltype id);
#define fdisk_is_label(c, x) fdisk_is_labeltype(c, FDISK_DISKLABEL_ ## x)


int fdisk_assign_device(struct fdisk_context *cxt,
			const char *fname, int readonly);
int fdisk_deassign_device(struct fdisk_context *cxt, int nosync);
int fdisk_is_readonly(struct fdisk_context *cxt);

int fdisk_enable_details(struct fdisk_context *cxt, int enable);
int fdisk_is_details(struct fdisk_context *cxt);

int fdisk_enable_listonly(struct fdisk_context *cxt, int enable);
int fdisk_is_listonly(struct fdisk_context *cxt);

int fdisk_set_unit(struct fdisk_context *cxt, const char *str);
const char *fdisk_get_unit(struct fdisk_context *cxt, int n);
int fdisk_use_cylinders(struct fdisk_context *cxt);
unsigned int fdisk_get_units_per_sector(struct fdisk_context *cxt);

unsigned long fdisk_get_optimal_iosize(struct fdisk_context *cxt);
unsigned long fdisk_get_minimal_iosize(struct fdisk_context *cxt);
unsigned long fdisk_get_physector_size(struct fdisk_context *cxt);
unsigned long fdisk_get_sector_size(struct fdisk_context *cxt);
unsigned long fdisk_get_alignment_offset(struct fdisk_context *cxt);
unsigned long fdisk_get_grain_size(struct fdisk_context *cxt);
fdisk_sector_t fdisk_get_first_lba(struct fdisk_context *cxt);
fdisk_sector_t fdisk_set_first_lba(struct fdisk_context *cxt, fdisk_sector_t lba);
fdisk_sector_t fdisk_get_last_lba(struct fdisk_context *cxt);
fdisk_sector_t fdisk_set_last_lba(struct fdisk_context *cxt, fdisk_sector_t lba);
fdisk_sector_t fdisk_get_nsectors(struct fdisk_context *cxt);
const char *fdisk_get_devname(struct fdisk_context *cxt);
int fdisk_get_devfd(struct fdisk_context *cxt);

unsigned int fdisk_get_geom_heads(struct fdisk_context *cxt);
fdisk_sector_t fdisk_get_geom_sectors(struct fdisk_context *cxt);
fdisk_sector_t fdisk_get_geom_cylinders(struct fdisk_context *cxt);



/* parttype.c */
struct fdisk_parttype *fdisk_new_parttype(void);
void fdisk_ref_parttype(struct fdisk_parttype *t);
void fdisk_unref_parttype(struct fdisk_parttype *t);
int fdisk_parttype_set_name(struct fdisk_parttype *t, const char *str);
int fdisk_parttype_set_typestr(struct fdisk_parttype *t, const char *str);
int fdisk_parttype_set_code(struct fdisk_parttype *t, int code);
size_t fdisk_label_get_nparttypes(const struct fdisk_label *lb);
struct fdisk_parttype *fdisk_label_get_parttype(const struct fdisk_label *lb, size_t n);
int fdisk_label_has_code_parttypes(const struct fdisk_label *lb);
struct fdisk_parttype *fdisk_label_get_parttype_from_code(
				const struct fdisk_label *lb,
				unsigned int code);
struct fdisk_parttype *fdisk_label_get_parttype_from_string(
				const struct fdisk_label *lb,
				const char *str);
struct fdisk_parttype *fdisk_new_unknown_parttype(unsigned int code,
						  const char *typestr);
struct fdisk_parttype *fdisk_copy_parttype(const struct fdisk_parttype *type);
struct fdisk_parttype *fdisk_label_parse_parttype(
				const struct fdisk_label *lb,
				const char *str);
const char *fdisk_parttype_get_string(const struct fdisk_parttype *t);
unsigned int fdisk_parttype_get_code(const struct fdisk_parttype *t);
const char *fdisk_parttype_get_name(const struct fdisk_parttype *t);
int fdisk_parttype_is_unknown(const struct fdisk_parttype *t);

/* label.c */

/**
 * fdisk_fieldtype
 *
 * Types of fdisk_field
 */
enum fdisk_fieldtype {
	FDISK_FIELD_NONE = 0,

	/* generic */
	FDISK_FIELD_DEVICE,
	FDISK_FIELD_START,
	FDISK_FIELD_END,
	FDISK_FIELD_SECTORS,
	FDISK_FIELD_CYLINDERS,
	FDISK_FIELD_SIZE,
	FDISK_FIELD_TYPE,
	FDISK_FIELD_TYPEID,

	/* label specific */
	FDISK_FIELD_ATTR,
	FDISK_FIELD_BOOT,
	FDISK_FIELD_BSIZE,
	FDISK_FIELD_CPG,
	FDISK_FIELD_EADDR,
	FDISK_FIELD_FSIZE,
	FDISK_FIELD_NAME,
	FDISK_FIELD_SADDR,
	FDISK_FIELD_UUID,

	FDISK_NFIELDS		/* must be last */
};

int fdisk_label_get_type(const struct fdisk_label *lb);
const char *fdisk_label_get_name(const struct fdisk_label *lb);
int fdisk_label_require_geometry(const struct fdisk_label *lb);


extern int fdisk_write_disklabel(struct fdisk_context *cxt);
extern int fdisk_verify_disklabel(struct fdisk_context *cxt);
extern int fdisk_create_disklabel(struct fdisk_context *cxt, const char *name);
extern int fdisk_list_disklabel(struct fdisk_context *cxt);
extern int fdisk_locate_disklabel(struct fdisk_context *cxt, int n, const char **name, off_t *offset, size_t *size);

extern int fdisk_get_disklabel_id(struct fdisk_context *cxt, char **id);
extern int fdisk_set_disklabel_id(struct fdisk_context *cxt);

extern int fdisk_get_partition(struct fdisk_context *cxt, size_t partno, struct fdisk_partition **pa);
extern int fdisk_set_partition(struct fdisk_context *cxt, size_t partno, struct fdisk_partition *pa);
extern int fdisk_add_partition(struct fdisk_context *cxt, struct fdisk_partition *pa, size_t *partno);
extern int fdisk_delete_partition(struct fdisk_context *cxt, size_t partno);

extern int fdisk_delete_all_partitions(struct fdisk_context *cxt);

extern int fdisk_set_partition_type(struct fdisk_context *cxt, size_t partnum,
			     struct fdisk_parttype *t);


extern int fdisk_label_get_fields_ids(
			const struct fdisk_label *lb,
			struct fdisk_context *cxt,
			int **ids, size_t *nids);

extern const struct fdisk_field *fdisk_label_get_field(const struct fdisk_label *lb, int id);
extern const struct fdisk_field *fdisk_label_get_field_by_name(
			const struct fdisk_label *lb,
			const char *name);

extern int fdisk_field_get_id(const struct fdisk_field *field);
extern const char *fdisk_field_get_name(const struct fdisk_field *field);
extern double fdisk_field_get_width(const struct fdisk_field *field);
extern int fdisk_field_is_number(const struct fdisk_field *field);


extern void fdisk_label_set_changed(struct fdisk_label *lb, int changed);
extern int fdisk_label_is_changed(const struct fdisk_label *lb);

extern void fdisk_label_set_disabled(struct fdisk_label *lb, int disabled);
extern int fdisk_label_is_disabled(const struct fdisk_label *lb);

extern int fdisk_is_partition_used(struct fdisk_context *cxt, size_t n);

extern int fdisk_toggle_partition_flag(struct fdisk_context *cxt, size_t partnum, unsigned long flag);

extern struct fdisk_partition *fdisk_new_partition(void);
extern void fdisk_reset_partition(struct fdisk_partition *pa);
extern void fdisk_ref_partition(struct fdisk_partition *pa);
extern void fdisk_unref_partition(struct fdisk_partition *pa);
extern int fdisk_partition_is_freespace(struct fdisk_partition *pa);

int fdisk_partition_set_start(struct fdisk_partition *pa, uint64_t off);
int fdisk_partition_unset_start(struct fdisk_partition *pa);
uint64_t fdisk_partition_get_start(struct fdisk_partition *pa);
int fdisk_partition_has_start(struct fdisk_partition *pa);
int fdisk_partition_cmp_start(struct fdisk_partition *a,
			      struct fdisk_partition *b);
int fdisk_partition_start_follow_default(struct fdisk_partition *pa, int enable);
int fdisk_partition_start_is_default(struct fdisk_partition *pa);

int fdisk_partition_set_size(struct fdisk_partition *pa, uint64_t sz);
int fdisk_partition_unset_size(struct fdisk_partition *pa);
uint64_t fdisk_partition_get_size(struct fdisk_partition *pa);
int fdisk_partition_has_size(struct fdisk_partition *pa);
int fdisk_partition_size_explicit(struct fdisk_partition *pa, int enable);

int fdisk_partition_has_end(struct fdisk_partition *pa);
fdisk_sector_t fdisk_partition_get_end(struct fdisk_partition *pa);

int fdisk_partition_set_partno(struct fdisk_partition *pa, size_t num);
int fdisk_partition_unset_partno(struct fdisk_partition *pa);
size_t fdisk_partition_get_partno(struct fdisk_partition *pa);
int fdisk_partition_has_partno(struct fdisk_partition *pa);
int fdisk_partition_cmp_partno(struct fdisk_partition *a,
	                               struct fdisk_partition *b);
int fdisk_partition_partno_follow_default(struct fdisk_partition *pa, int enable);


extern int fdisk_partition_set_type(struct fdisk_partition *pa, struct fdisk_parttype *type);
extern struct fdisk_parttype *fdisk_partition_get_type(struct fdisk_partition *pa);
extern int fdisk_partition_set_name(struct fdisk_partition *pa, const char *name);
extern const char *fdisk_partition_get_name(struct fdisk_partition *pa);
extern int fdisk_partition_set_uuid(struct fdisk_partition *pa, const char *uuid);
extern int fdisk_partition_set_attrs(struct fdisk_partition *pa, const char *attrs);
extern const char *fdisk_partition_get_uuid(struct fdisk_partition *pa);
extern const char *fdisk_partition_get_attrs(struct fdisk_partition *pa);
extern int fdisk_partition_is_nested(struct fdisk_partition *pa);
extern int fdisk_partition_is_container(struct fdisk_partition *pa);
extern int fdisk_partition_get_parent(struct fdisk_partition *pa, size_t *parent);
extern int fdisk_partition_is_used(struct fdisk_partition *pa);
extern int fdisk_partition_is_bootable(struct fdisk_partition *pa);
extern int fdisk_partition_to_string(struct fdisk_partition *pa,
				     struct fdisk_context *cxt,
				     int id, char **data);

int fdisk_partition_next_partno(struct fdisk_partition *pa,
				       struct fdisk_context *cxt,
				       size_t *n);

extern int fdisk_partition_end_follow_default(struct fdisk_partition *pa, int enable);
extern int fdisk_partition_end_is_default(struct fdisk_partition *pa);

extern int fdisk_reorder_partitions(struct fdisk_context *cxt);

/* table.c */
extern struct fdisk_table *fdisk_new_table(void);
extern int fdisk_reset_table(struct fdisk_table *tb);
extern void fdisk_ref_table(struct fdisk_table *tb);
extern void fdisk_unref_table(struct fdisk_table *tb);
extern size_t fdisk_table_get_nents(struct fdisk_table *tb);
extern int fdisk_table_is_empty(struct fdisk_table *tb);
extern int fdisk_table_add_partition(struct fdisk_table *tb, struct fdisk_partition *pa);
extern int fdisk_table_remove_partition(struct fdisk_table *tb, struct fdisk_partition *pa);

extern int fdisk_get_partitions(struct fdisk_context *cxt, struct fdisk_table **tb);
extern int fdisk_get_freespaces(struct fdisk_context *cxt, struct fdisk_table **tb);

extern int fdisk_table_wrong_order(struct fdisk_table *tb);
extern int fdisk_table_sort_partitions(struct fdisk_table *tb,
			int (*cmp)(struct fdisk_partition *,
				   struct fdisk_partition *));

extern int fdisk_table_next_partition(
			struct fdisk_table *tb,
			struct fdisk_iter *itr,
			struct fdisk_partition **pa);

extern struct fdisk_partition *fdisk_table_get_partition(
			struct fdisk_table *tb,
			size_t n);
extern int fdisk_apply_table(struct fdisk_context *cxt, struct fdisk_table *tb);

/* alignment.c */
#define FDISK_ALIGN_UP		1
#define FDISK_ALIGN_DOWN	2
#define FDISK_ALIGN_NEAREST	3

fdisk_sector_t fdisk_align_lba(struct fdisk_context *cxt, fdisk_sector_t lba, int direction);
fdisk_sector_t fdisk_align_lba_in_range(struct fdisk_context *cxt,
				  fdisk_sector_t lba, fdisk_sector_t start, fdisk_sector_t stop);
int fdisk_lba_is_phy_aligned(struct fdisk_context *cxt, fdisk_sector_t lba);

int fdisk_override_geometry(struct fdisk_context *cxt,
			    unsigned int cylinders,
			    unsigned int heads,
			    unsigned int sectors);
int fdisk_save_user_geometry(struct fdisk_context *cxt,
			    unsigned int cylinders,
			    unsigned int heads,
			    unsigned int sectors);
int fdisk_save_user_sector_size(struct fdisk_context *cxt,
				unsigned int phy,
				unsigned int log);
int fdisk_has_user_device_properties(struct fdisk_context *cxt);
int fdisk_reset_alignment(struct fdisk_context *cxt);
int fdisk_reset_device_properties(struct fdisk_context *cxt);
int fdisk_reread_partition_table(struct fdisk_context *cxt);

/* iter.c */
enum {

	FDISK_ITER_FORWARD = 0,
	FDISK_ITER_BACKWARD
};
extern struct fdisk_iter *fdisk_new_iter(int direction);
extern void fdisk_free_iter(struct fdisk_iter *itr);
extern void fdisk_reset_iter(struct fdisk_iter *itr, int direction);
extern int fdisk_iter_get_direction(struct fdisk_iter *itr);


/* dos.c */
#define DOS_FLAG_ACTIVE	1

extern int fdisk_dos_move_begin(struct fdisk_context *cxt, size_t i);
extern int fdisk_dos_enable_compatible(struct fdisk_label *lb, int enable);
extern int fdisk_dos_is_compatible(struct fdisk_label *lb);

/* sun.h */
extern int fdisk_sun_set_alt_cyl(struct fdisk_context *cxt);
extern int fdisk_sun_set_xcyl(struct fdisk_context *cxt);
extern int fdisk_sun_set_ilfact(struct fdisk_context *cxt);
extern int fdisk_sun_set_rspeed(struct fdisk_context *cxt);
extern int fdisk_sun_set_pcylcount(struct fdisk_context *cxt);

/* bsd.c */
extern int fdisk_bsd_edit_disklabel(struct fdisk_context *cxt);
extern int fdisk_bsd_write_bootstrap(struct fdisk_context *cxt);
extern int fdisk_bsd_link_partition(struct fdisk_context *cxt);

/* sgi.h */
#define SGI_FLAG_BOOT	1
#define SGI_FLAG_SWAP	2
extern int fdisk_sgi_set_bootfile(struct fdisk_context *cxt);
extern int fdisk_sgi_create_info(struct fdisk_context *cxt);

/* gpt */

/* GPT partition attributes */
enum {
	/* System partition (disk partitioning utilities must preserve the
	 * partition as is) */
	GPT_FLAG_REQUIRED = 1,

	/* EFI firmware should ignore the content of the partition and not try
	 * to read from it */
	GPT_FLAG_NOBLOCK,

	/* Legacy BIOS bootable  */
	GPT_FLAG_LEGACYBOOT,

	/* bites 48-63, Defined and used by the individual partition type.
	 *
	 * The flag GPT_FLAG_GUIDSPECIFIC forces libfdisk to ask (by ask API)
	 * for a bit number. If you want to toggle specific bit and avoid any
	 * dialog, then use the bit number (in range 48..63). For example:
	 *
	 * // start dialog to ask for bit number
	 * fdisk_toggle_partition_flag(cxt, n, GPT_FLAG_GUIDSPECIFIC);
	 *
	 * // toggle bit 60
	 * fdisk_toggle_partition_flag(cxt, n, 60);
	 */
	GPT_FLAG_GUIDSPECIFIC
};

extern int fdisk_gpt_is_hybrid(struct fdisk_context *cxt);


/* script.c */
struct fdisk_script *fdisk_new_script(struct fdisk_context *cxt);
struct fdisk_script *fdisk_new_script_from_file(struct fdisk_context *cxt,
						 const char *filename);
void fdisk_ref_script(struct fdisk_script *dp);
void fdisk_unref_script(struct fdisk_script *dp);

const char *fdisk_script_get_header(struct fdisk_script *dp, const char *name);
int fdisk_script_set_header(struct fdisk_script *dp, const char *name, const char *data);
struct fdisk_table *fdisk_script_get_table(struct fdisk_script *dp);
int fdisk_script_get_nlines(struct fdisk_script *dp);

int fdisk_script_read_context(struct fdisk_script *dp, struct fdisk_context *cxt);
int fdisk_script_write_file(struct fdisk_script *dp, FILE *f);
int fdisk_script_read_file(struct fdisk_script *dp, FILE *f);
int fdisk_script_read_line(struct fdisk_script *dp, FILE *f, char *buf, size_t bufsz);

int fdisk_set_script(struct fdisk_context *cxt, struct fdisk_script *dp);
struct fdisk_script *fdisk_get_script(struct fdisk_context *cxt);

int fdisk_apply_script_headers(struct fdisk_context *cxt, struct fdisk_script *dp);
int fdisk_apply_script(struct fdisk_context *cxt, struct fdisk_script *dp);


/* ask.c */
#define fdisk_is_ask(a, x) (fdisk_ask_get_type(a) == FDISK_ASKTYPE_ ## x)

int fdisk_set_ask(struct fdisk_context *cxt,
		int (*ask_cb)(struct fdisk_context *, struct fdisk_ask *, void *),
		void *data);


void fdisk_ref_ask(struct fdisk_ask *ask);
void fdisk_unref_ask(struct fdisk_ask *ask);
const char *fdisk_ask_get_query(struct fdisk_ask *ask);
int fdisk_ask_get_type(struct fdisk_ask *ask);
const char *fdisk_ask_number_get_range(struct fdisk_ask *ask);
uint64_t fdisk_ask_number_get_default(struct fdisk_ask *ask);
uint64_t fdisk_ask_number_get_low(struct fdisk_ask *ask);
uint64_t fdisk_ask_number_get_high(struct fdisk_ask *ask);
uint64_t fdisk_ask_number_get_result(struct fdisk_ask *ask);
int fdisk_ask_number_set_result(struct fdisk_ask *ask, uint64_t result);
uint64_t fdisk_ask_number_get_base(struct fdisk_ask *ask);
uint64_t fdisk_ask_number_get_unit(struct fdisk_ask *ask);
int fdisk_ask_number_set_relative(struct fdisk_ask *ask, int relative);
int fdisk_ask_number_inchars(struct fdisk_ask *ask);
int fdisk_ask_partnum(struct fdisk_context *cxt, size_t *partnum, int wantnew);

int fdisk_ask_number(struct fdisk_context *cxt,
		     uintmax_t low,
		     uintmax_t dflt,
		     uintmax_t high,
		     const char *query,
		     uintmax_t *result);
char *fdisk_ask_string_get_result(struct fdisk_ask *ask);
int fdisk_ask_string_set_result(struct fdisk_ask *ask, char *result);
int fdisk_ask_string(struct fdisk_context *cxt,
		     const char *query,
		     char **result);
int fdisk_ask_yesno(struct fdisk_context *cxt,
		     const char *query,
		     int *result);
int fdisk_ask_yesno_get_result(struct fdisk_ask *ask);
int fdisk_ask_yesno_set_result(struct fdisk_ask *ask, int result);
int fdisk_ask_menu_get_default(struct fdisk_ask *ask);
int fdisk_ask_menu_set_result(struct fdisk_ask *ask, int key);
int fdisk_ask_menu_get_result(struct fdisk_ask *ask, int *key);
int fdisk_ask_menu_get_item(struct fdisk_ask *ask, size_t idx, int *key,
			    const char **name, const char **desc);
size_t fdisk_ask_menu_get_nitems(struct fdisk_ask *ask);
int fdisk_ask_print_get_errno(struct fdisk_ask *ask);
const char *fdisk_ask_print_get_mesg(struct fdisk_ask *ask);

int fdisk_info(struct fdisk_context *cxt, const char *fmt, ...);
int fdisk_warn(struct fdisk_context *cxt, const char *fmt, ...);
int fdisk_warnx(struct fdisk_context *cxt, const char *fmt, ...);

/* utils.h */
extern char *fdisk_partname(const char *dev, size_t partno);

#ifdef __cplusplus
}
#endif

#endif /* _LIBFDISK_H */
