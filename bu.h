#include <utils/String8.h>

#include <lib/libtar.h>
#include <zlib.h>

extern "C" {
#include <openssl/sha.h>
#include <openssl/md5.h>
#ifndef MD5_DIGEST_STRING_LENGTH
#define MD5_DIGEST_STRING_LENGTH (MD5_DIGEST_LENGTH*2+1)
#endif
#ifndef SHA_DIGEST_STRING_LENGTH
#define SHA_DIGEST_STRING_LENGTH (SHA_DIGEST_LENGTH*2+1)
#endif
}

#define HASH_MAX_LENGTH SHA_DIGEST_LENGTH
#define HASH_MAX_STRING_LENGTH SHA_DIGEST_STRING_LENGTH

#define PROP_LINE_LEN (PROPERTY_KEY_MAX+1+PROPERTY_VALUE_MAX+1+1)

#define PATHNAME_SOD "/tmp/sod"
#define PATHNAME_EOD "/tmp/eod"

extern int sockfd;
extern TAR* tar;
extern gzFile gzf;

extern char* hash_name;
extern size_t hash_datalen;
extern SHA_CTX sha_ctx;
extern MD5_CTX md5_ctx;

extern void logmsg(const char* fmt, ...);
extern int create_tar(const char* compress, const char* mode);

extern int do_backup(int argc, char** argv);
extern int do_restore(int argc, char** argv);

