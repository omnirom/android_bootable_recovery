#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <cutils/properties.h>
#include <cutils/hashmap.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

static int str_hash(void *key)
{
    return hashmapHash(key, strlen(key));
}

static bool str_equals(void *keyA, void *keyB)
{
    return strcmp(keyA, keyB) == 0;
}

static void announce(char *name, char *value)
{
    unsigned char *x;
    
    for(x = (unsigned char *)value; *x; x++) {
        if((*x < 32) || (*x > 127)) *x = '.';
    }

    fprintf(stderr,"%10d %s = '%s'\n", (int) time(0), name, value);
}

static void add_to_watchlist(Hashmap *watchlist, const char *name,
        const prop_info *pi)
{
    char *key = strdup(name);
    unsigned *value = malloc(sizeof(unsigned));
    if (!key || !value)
        exit(1);

    *value = __system_property_serial(pi);
    hashmapPut(watchlist, key, value);
}

static void populate_watchlist(const prop_info *pi, void *cookie)
{
    Hashmap *watchlist = cookie;
    char name[PROP_NAME_MAX];
    char value_unused[PROP_VALUE_MAX];

    __system_property_read(pi, name, value_unused);
    add_to_watchlist(watchlist, name, pi);
}

static void update_watchlist(const prop_info *pi, void *cookie)
{
    Hashmap *watchlist = cookie;
    char name[PROP_NAME_MAX];
    char value[PROP_VALUE_MAX];
    unsigned *serial;

    __system_property_read(pi, name, value);
    serial = hashmapGet(watchlist, name);
    if (!serial) {
        add_to_watchlist(watchlist, name, pi);
        announce(name, value);
    } else {
        unsigned tmp = __system_property_serial(pi);
        if (*serial != tmp) {
            *serial = tmp;
            announce(name, value);
        }
    }
}

int watchprops_main(int argc, char *argv[])
{
    unsigned serial;
    
    Hashmap *watchlist = hashmapCreate(1024, str_hash, str_equals);
    if (!watchlist)
        exit(1);

    __system_property_foreach(populate_watchlist, watchlist);

    for(serial = 0;;) {
        serial = __system_property_wait_any(serial);
        __system_property_foreach(update_watchlist, watchlist);
    }
    return 0;
}
