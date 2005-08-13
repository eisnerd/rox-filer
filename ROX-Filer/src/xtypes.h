/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 *
 * Extended filesystem attribute support, particularly for MIME types
 */

#ifndef _XTYPES_H
#define _XTYPES_H

/* Prototypes */
void xattr_init(void);

/* path may be NULL to test for support in libc */
int xattr_supported(const char *path);

int xattr_have(const char *path);
gchar *xattr_get(const char *path, const char *attr, int *len);
int xattr_set(const char *path, const char *attr,
	      const char *value, int value_len);

MIME_type *xtype_get(const char *path);
int xtype_set(const char *path, const MIME_type *type);

#endif
