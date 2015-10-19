#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <errno.h>
#include <stdio.h>

#include "minui.h"

#include <cutils/hashmap.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include <pixelflinger/pixelflinger.h>
#include <pthread.h>

#define STRING_CACHE_MAX_ENTRIES 400
#define STRING_CACHE_TRUNCATE_ENTRIES 150

typedef struct
{
    int size;
    int dpi;
    char *path;
} TrueTypeFontKey;

typedef struct
{
    int type;
    int refcount;
    int size;
    int dpi;
    int max_height;
    int base;
    FT_Face face;
    Hashmap *glyph_cache;
    Hashmap *string_cache;
    struct StringCacheEntry *string_cache_head;
    struct StringCacheEntry *string_cache_tail;
    pthread_mutex_t mutex;
    TrueTypeFontKey *key;
} TrueTypeFont;

typedef struct
{
    FT_BBox bbox;
    FT_BitmapGlyph glyph;
} TrueTypeCacheEntry;

typedef struct
{
    char *text;
    int max_width;
} StringCacheKey;

struct StringCacheEntry
{
    GGLSurface surface;
    int rendered_bytes; // number of bytes from C string rendered, not number of UTF8 characters!
    StringCacheKey *key;
    struct StringCacheEntry *prev;
    struct StringCacheEntry *next;
};

typedef struct StringCacheEntry StringCacheEntry;

typedef struct
{
    FT_Library ft_library;
    Hashmap *fonts;
    pthread_mutex_t mutex;
} FontData;

static FontData font_data = {
    .ft_library = NULL,
    .fonts = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
};

#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))

// 32bit FNV-1a hash algorithm
// http://isthe.com/chongo/tech/comp/fnv/#FNV-1a
static const uint32_t FNV_prime = 16777619U;
static const uint32_t offset_basis = 2166136261U;

static uint32_t fnv_hash(void *data, uint32_t len)
{
    uint8_t *d8 = (uint8_t *)data;
    uint32_t *d32 = (uint32_t *)data;
    uint32_t i, max;
    uint32_t hash = offset_basis;

    max = len/4;

    // 32 bit data
    for(i = 0; i < max; ++i)
    {
        hash ^= *d32++;
        hash *= FNV_prime;
    }

    // last bits
    for(i *= 4; i < len; ++i)
    {
        hash ^= (uint32_t) d8[i];
        hash *= FNV_prime;
    }
    return hash;
}

static inline uint32_t fnv_hash_add(uint32_t cur_hash, uint32_t word)
{
    cur_hash ^= word;
    cur_hash *= FNV_prime;
    return cur_hash;
}

int utf8_to_unicode(const char* pIn, unsigned int *pOut)
{
    int utf_bytes = 1;
    unsigned int unicode = 0;
    unsigned char tmp;
    tmp = (unsigned char)*pIn++;
    if (tmp < 0x80)
    {
        *pOut = tmp;
    }
    else
    {
        unsigned int high_bit_mask = 0x3F;
        unsigned int high_bit_shift = 0;
        int total_bits = 0;
        while((tmp & 0xC0) == 0xC0)
        {
            utf_bytes ++;
            if(utf_bytes > 6)
            {
                *pOut = tmp;
                return 1;
            }
            tmp = 0xFF & (tmp << 1);
            total_bits += 6;
            high_bit_mask >>= 1;
            high_bit_shift++;
            unicode <<= 6;
            unicode |= (*pIn++) & 0x3F;
        }
        unicode |= ((tmp >> high_bit_shift) & high_bit_mask) << total_bits;
        *pOut = unicode;
    }

    return utf_bytes;
}

static bool gr_ttf_string_cache_equals(void *keyA, void *keyB)
{
    StringCacheKey *a = (StringCacheKey *)keyA;
    StringCacheKey *b = (StringCacheKey *)keyB;
    return a->max_width == b->max_width && strcmp(a->text, b->text) == 0;
}

static int gr_ttf_string_cache_hash(void *key)
{
    StringCacheKey *k = (StringCacheKey *)key;
    return fnv_hash(k->text, strlen(k->text));
}

static bool gr_ttf_font_cache_equals(void *keyA, void *keyB)
{
    TrueTypeFontKey *a = (TrueTypeFontKey *)keyA;
    TrueTypeFontKey *b = (TrueTypeFontKey *)keyB;
    return (a->size == b->size) && (a->dpi == b->dpi) && !strcmp(a->path, b->path);
}

static int gr_ttf_font_cache_hash(void *key)
{
    TrueTypeFontKey *k = (TrueTypeFontKey *)key;

    uint32_t hash = fnv_hash(k->path, strlen(k->path));
    hash = fnv_hash_add(hash, k->size);
    hash = fnv_hash_add(hash, k->dpi);
    return hash;
}

void *gr_ttf_loadFont(const char *filename, int size, int dpi)
{
    int error;
    TrueTypeFont *res = NULL;
    TrueTypeFontKey *key = NULL;

    pthread_mutex_lock(&font_data.mutex);

    if(font_data.fonts)
    {
        TrueTypeFontKey k = {
            .size = size,
            .dpi = dpi,
            .path = (char*)filename
        };

        res = (TrueTypeFont *)hashmapGet(font_data.fonts, &k);
        if(res)
        {
            ++res->refcount;
            goto exit;
        }
    }

    if(!font_data.ft_library)
    {
        error = FT_Init_FreeType(&font_data.ft_library);
        if(error)
        {
            fprintf(stderr, "Failed to init libfreetype! %d\n", error);
            goto exit;
        }
    }

    FT_Face face;
    error = FT_New_Face(font_data.ft_library, filename, 0, &face);
    if(error)
    {
        fprintf(stderr, "Failed to load truetype face %s: %d\n", filename, error);
        goto exit;
    }

    error = FT_Set_Char_Size(face, 0, size*16, dpi, dpi);
    if(error)
    {
         fprintf(stderr, "Failed to set truetype face size to %d, dpi %d: %d\n", size, dpi, error);
         FT_Done_Face(face);
         goto exit;
    }

    res = (TrueTypeFont *)malloc(sizeof(TrueTypeFont));
    memset(res, 0, sizeof(TrueTypeFont));
    res->type = FONT_TYPE_TTF;
    res->size = size;
    res->dpi = dpi;
    res->face = face;
    res->max_height = -1;
    res->base = -1;
    res->refcount = 1;
    res->glyph_cache = hashmapCreate(32, hashmapIntHash, hashmapIntEquals);
    res->string_cache = hashmapCreate(128, gr_ttf_string_cache_hash, gr_ttf_string_cache_equals);
    pthread_mutex_init(&res->mutex, 0);

    if(!font_data.fonts)
        font_data.fonts = hashmapCreate(4, gr_ttf_font_cache_hash, gr_ttf_font_cache_equals);

    key = (TrueTypeFontKey *)malloc(sizeof(TrueTypeFontKey));
    memset(key, 0, sizeof(TrueTypeFontKey));
    key->path = strdup(filename);
    key->size = size;
    key->dpi = dpi;

    res->key = key;

    hashmapPut(font_data.fonts, key, res);

exit:
    pthread_mutex_unlock(&font_data.mutex);
    return res;
}

void *gr_ttf_scaleFont(void *font, int max_width, int measured_width)
{
    if (!font)
        return NULL;

    TrueTypeFont *f = (TrueTypeFont *)font;
    float scale_value = (float)(max_width) / (float)(measured_width);
    int new_size = ((int)((float)f->size * scale_value)) - 1;
    if (new_size < 1)
        new_size = 1;
    const char* file = f->key->path;
    int dpi = f->dpi;
    return gr_ttf_loadFont(file, new_size, dpi);
}

static bool gr_ttf_freeFontCache(void *key, void *value, void *context __unused)
{
    TrueTypeCacheEntry *e = (TrueTypeCacheEntry *)value;
    FT_Done_Glyph((FT_Glyph)e->glyph);
    free(e);
    free(key);
    return true;
}

static bool gr_ttf_freeStringCache(void *key, void *value, void *context __unused)
{
    StringCacheKey *k = (StringCacheKey *)key;
    free(k->text);
    free(k);

    StringCacheEntry *e = (StringCacheEntry *)value;
    free(e->surface.data);
    free(e);
    return true;
}

void gr_ttf_freeFont(void *font)
{
    pthread_mutex_lock(&font_data.mutex);

    TrueTypeFont *d = (TrueTypeFont *)font;

    if(--d->refcount == 0)
    {
        hashmapRemove(font_data.fonts, d->key);

        if(hashmapSize(font_data.fonts) == 0)
        {
            hashmapFree(font_data.fonts);
            font_data.fonts = NULL;
        }

        free(d->key->path);
        free(d->key);

        FT_Done_Face(d->face);
        hashmapForEach(d->string_cache, gr_ttf_freeStringCache, NULL);
        hashmapFree(d->string_cache);
        hashmapForEach(d->glyph_cache, gr_ttf_freeFontCache, NULL);
        hashmapFree(d->glyph_cache);
        pthread_mutex_destroy(&d->mutex);
        free(d);
    }

    pthread_mutex_unlock(&font_data.mutex);
}

static TrueTypeCacheEntry *gr_ttf_glyph_cache_peek(TrueTypeFont *font, int char_index)
{
    return (TrueTypeCacheEntry *)hashmapGet(font->glyph_cache, &char_index);
}

static TrueTypeCacheEntry *gr_ttf_glyph_cache_get(TrueTypeFont *font, int char_index)
{
    TrueTypeCacheEntry *res = (TrueTypeCacheEntry *)hashmapGet(font->glyph_cache, &char_index);
    if(!res)
    {
        int error = FT_Load_Glyph(font->face, char_index, FT_LOAD_RENDER);
        if(error)
        {
            fprintf(stderr, "Failed to load glyph idx %d: %d\n", char_index, error);
            return NULL;
        }

        FT_BitmapGlyph glyph;
        error = FT_Get_Glyph(font->face->glyph, (FT_Glyph*)&glyph);
        if(error)
        {
            fprintf(stderr, "Failed to copy glyph %d: %d\n", char_index, error);
            return NULL;
        }

        res = (TrueTypeCacheEntry *)malloc(sizeof(TrueTypeCacheEntry));
        memset(res, 0, sizeof(TrueTypeCacheEntry));
        res->glyph = glyph;
        FT_Glyph_Get_CBox((FT_Glyph)glyph, FT_GLYPH_BBOX_PIXELS, &res->bbox);

        int *key = (int *)malloc(sizeof(int));
        *key = char_index;

        hashmapPut(font->glyph_cache, key, res);
    }

    return res;
}

static int gr_ttf_copy_glyph_to_surface(GGLSurface *dest, FT_BitmapGlyph glyph, int offX, int offY, int base)
{
    unsigned y;
    uint8_t *src_itr = glyph->bitmap.buffer;
    uint8_t *dest_itr = dest->data;

    if(glyph->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
    {
        fprintf(stderr, "Unsupported pixel mode in FT_BitmapGlyph %d\n", glyph->bitmap.pixel_mode);
        return -1;
    }

    dest_itr += (offY + base - glyph->top)*dest->stride + (offX + glyph->left);

    // FIXME: if glyph->left is negative and everything else is 0 (e.g. letter 'j' in Roboto-Regular),
    // the result might end up being before the buffer - I'm not sure how to properly handle this.
    if(dest_itr < dest->data)
        dest_itr = dest->data;

    for(y = 0; y < glyph->bitmap.rows; ++y)
    {
        memcpy(dest_itr, src_itr, glyph->bitmap.width);
        src_itr += glyph->bitmap.pitch;
        dest_itr += dest->stride;
    }
    return 0;
}

static void gr_ttf_calcMaxFontHeight(TrueTypeFont *f)
{
    char c;
    int char_idx;
    int error;
    FT_Glyph glyph;
    FT_BBox bbox;
    FT_BBox bbox_glyph;
    TrueTypeCacheEntry *ent;

    bbox.yMin = bbox_glyph.yMin = LONG_MAX;
    bbox.yMax = bbox_glyph.yMax = LONG_MIN;

    for(c = '!'; c <= '~'; ++c)
    {
        char_idx = FT_Get_Char_Index(f->face, c);
        ent = gr_ttf_glyph_cache_peek(f, char_idx);
        if(ent)
        {
            bbox.yMin = MIN(bbox.yMin, ent->bbox.yMin);
            bbox.yMax = MAX(bbox.yMax, ent->bbox.yMax);
        }
        else
        {
            error = FT_Load_Glyph(f->face, char_idx, 0);
            if(error)
                continue;

            error = FT_Get_Glyph(f->face->glyph, &glyph);
            if(error)
                continue;

            FT_Glyph_Get_CBox(glyph, FT_GLYPH_BBOX_PIXELS, &bbox_glyph);
            bbox.yMin = MIN(bbox.yMin, bbox_glyph.yMin);
            bbox.yMax = MAX(bbox.yMax, bbox_glyph.yMax);

            FT_Done_Glyph(glyph);
        }
    }

    if(bbox.yMin > bbox.yMax)
        bbox.yMin = bbox.yMax = 0;

    f->max_height = bbox.yMax - bbox.yMin;
    f->base = bbox.yMax;

    // FIXME: twrp fonts have some padding on top, I'll add it here
    // Should be fixed in the themes
    f->max_height += f->size / 4;
    f->base += f->size / 4;
}

// returns number of bytes from const char *text rendered to fit max_width, not number of UTF8 characters!
static int gr_ttf_render_text(TrueTypeFont *font, GGLSurface *surface, const char *text, int max_width)
{
    TrueTypeFont *f = font;
    TrueTypeCacheEntry *ent;
    int bytes_rendered = 0, total_w = 0;
    int utf_bytes = 0;
    unsigned int unicode = 0;
    int i, x, diff, char_idx, prev_idx = 0;
    int height, base;
    FT_Vector delta;
    uint8_t *data = NULL;
    const char *text_itr = text;
    int *char_idxs;
    int char_idxs_len = 0;

    char_idxs = (int *)malloc(strlen(text) * sizeof(int));

    while(*text_itr)
    {
        utf_bytes = utf8_to_unicode(text_itr, &unicode);
        text_itr += utf_bytes;
        bytes_rendered += utf_bytes;

        char_idx = FT_Get_Char_Index(f->face, unicode);
        char_idxs[char_idxs_len] = char_idx;

        ent = gr_ttf_glyph_cache_get(f, char_idx);
        if(ent)
        {
            diff = ent->glyph->root.advance.x >> 16;

            if(FT_HAS_KERNING(f->face) && prev_idx && char_idx)
            {
                FT_Get_Kerning(f->face, prev_idx, char_idx, FT_KERNING_DEFAULT, &delta);
                diff += delta.x >> 6;
            }

            if(max_width != -1 && total_w + diff > max_width)
                break;

            total_w += diff;
        }
        prev_idx = char_idx;
        ++char_idxs_len;
    }

    if(font->max_height == -1)
        gr_ttf_calcMaxFontHeight(font);

    if(font->max_height == -1)
    {
        free(char_idxs);
        return -1;
    }

    height = font->max_height;

    data = (uint8_t *)malloc(total_w*height);
    memset(data, 0, total_w*height);
    x = 0;
    prev_idx = 0;

    surface->version = sizeof(*surface);
    surface->width = total_w;
    surface->height = height;
    surface->stride = total_w;
    surface->data = (GGLubyte*)data;
    surface->format = GGL_PIXEL_FORMAT_A_8;

    for(i = 0; i < char_idxs_len; ++i)
    {
        char_idx = char_idxs[i];
        if(FT_HAS_KERNING(f->face) && prev_idx && char_idx)
        {
            FT_Get_Kerning(f->face, prev_idx, char_idx, FT_KERNING_DEFAULT, &delta);
            x += delta.x >> 6;
        }

        ent = gr_ttf_glyph_cache_get(f, char_idx);
        if(ent)
        {
            gr_ttf_copy_glyph_to_surface(surface, ent->glyph, x, 0, font->base);
            x += ent->glyph->root.advance.x >> 16;
        }

        prev_idx = char_idx;
    }

    free(char_idxs);
    return bytes_rendered;
}

static StringCacheEntry *gr_ttf_string_cache_peek(TrueTypeFont *font, const char *text, int max_width)
{
    StringCacheEntry *res;
    StringCacheKey k = {
        .text = (char*)text,
        .max_width = max_width
    };

    return (StringCacheEntry *)hashmapGet(font->string_cache, &k);
}

static StringCacheEntry *gr_ttf_string_cache_get(TrueTypeFont *font, const char *text, int max_width)
{
    StringCacheEntry *res;
    StringCacheKey k = {
        .text = (char*)text,
        .max_width = max_width
    };

    res = (StringCacheEntry *)hashmapGet(font->string_cache, &k);
    if(!res)
    {
        res = (StringCacheEntry *)malloc(sizeof(StringCacheEntry));
        memset(res, 0, sizeof(StringCacheEntry));
        res->rendered_bytes = gr_ttf_render_text(font, &res->surface, text, max_width);
        if(res->rendered_bytes < 0)
        {
            free(res);
            return NULL;
        }

        StringCacheKey *new_key = (StringCacheKey *)malloc(sizeof(StringCacheKey));
        memset(new_key, 0, sizeof(StringCacheKey));
        new_key->max_width = max_width;
        new_key->text = strdup(text);

        res->key = new_key;

        if(font->string_cache_tail)
        {
            res->prev = font->string_cache_tail;
            res->prev->next = res;
        }
        else
            font->string_cache_head = res;
        font->string_cache_tail = res;

        hashmapPut(font->string_cache, new_key, res);
    }
    else if(res->next)
    {
        // move this entry to the tail of the linked list
        // if it isn't already there
        if(res->prev)
            res->prev->next = res->next;

        res->next->prev = res->prev;

        if(!res->prev)
            font->string_cache_head = res->next;

        res->next = NULL;
        res->prev = font->string_cache_tail;
        res->prev->next = res;
        font->string_cache_tail = res;

        // truncate old entries
        if(hashmapSize(font->string_cache) >= STRING_CACHE_MAX_ENTRIES)
        {
            printf("Truncating string cache entries.\n");
            int i;
            StringCacheEntry *ent;
            for(i = 0; i < STRING_CACHE_TRUNCATE_ENTRIES; ++i)
            {
                ent = font->string_cache_head;
                font->string_cache_head = ent->next;
                font->string_cache_head->prev = NULL;

                hashmapRemove(font->string_cache, ent->key);

                gr_ttf_freeStringCache(ent->key, ent, NULL);
            }
        }
    }
    return res;
}

int gr_ttf_measureEx(const char *s, void *font)
{
    TrueTypeFont *f = (TrueTypeFont *)font;
    int res = -1;

    pthread_mutex_lock(&f->mutex);
    StringCacheEntry *e = gr_ttf_string_cache_get(f, s, -1);
    if(e)
        res = e->surface.width;
    pthread_mutex_unlock(&f->mutex);

    return res;
}

int gr_ttf_maxExW(const char *s, void *font, int max_width)
{
    TrueTypeFont *f = (TrueTypeFont *)font;
    TrueTypeCacheEntry *ent;
    int max_bytes = 0, total_w = 0;
    int utf_bytes, prev_utf_bytes = 0;
    unsigned int unicode = 0;
    int char_idx, prev_idx = 0;
    FT_Vector delta;
    StringCacheEntry *e;

    pthread_mutex_lock(&f->mutex);

    e = gr_ttf_string_cache_peek(f, s, max_width);
    if(e)
    {
        max_bytes = e->rendered_bytes;
        pthread_mutex_unlock(&f->mutex);
        return max_bytes;
    }

    while(*s)
    {
        utf_bytes = utf8_to_unicode(s, &unicode);
        s += utf_bytes;

        char_idx = FT_Get_Char_Index(f->face, unicode);
        if(FT_HAS_KERNING(f->face) && prev_idx && char_idx)
        {
            FT_Get_Kerning(f->face, prev_idx, char_idx, FT_KERNING_DEFAULT, &delta);
            total_w += delta.x >> 6;
        }
        prev_idx = char_idx;

        if(total_w > max_width)
        {
            max_bytes -= prev_utf_bytes;
            break;
        }
        prev_utf_bytes = utf_bytes;

        ent = gr_ttf_glyph_cache_get(f, char_idx);
        if(!ent)
            continue;

        total_w += ent->glyph->root.advance.x >> 16;
        max_bytes += utf_bytes;
    }
    pthread_mutex_unlock(&f->mutex);
    return max_bytes;
}

int gr_ttf_textExWH(void *context, int x, int y, const char *s, void *pFont, int max_width, int max_height)
{
    GGLContext *gl = (GGLContext *)context;
    TrueTypeFont *font = (TrueTypeFont *)pFont;

    // not actualy max width, but max_width + x
    if(max_width != -1)
    {
        max_width -= x;
        if(max_width <= 0)
            return 0;
    }

    pthread_mutex_lock(&font->mutex);

    StringCacheEntry *e = gr_ttf_string_cache_get(font, s, max_width);
    if(!e)
    {
        pthread_mutex_unlock(&font->mutex);
        return -1;
    }

    int y_bottom = y + e->surface.height;
    int res = e->rendered_bytes;

    if(max_height != -1 && max_height < y_bottom)
    {
        y_bottom = max_height;
        if(y_bottom <= y)
        {
            pthread_mutex_unlock(&font->mutex);
            return 0;
        }
    }

    gl->bindTexture(gl, &e->surface);
    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);

    gl->enable(gl, GGL_TEXTURE_2D);
    gl->texCoord2i(gl, -x, -y);
    gl->recti(gl, x, y, x + e->surface.width, y_bottom);
    gl->disable(gl, GGL_TEXTURE_2D);

    pthread_mutex_unlock(&font->mutex);
    return res;
}

int gr_ttf_getMaxFontHeight(void *font)
{
    int res;
    TrueTypeFont *f = (TrueTypeFont *)font;

    pthread_mutex_lock(&f->mutex);

    if(f->max_height == -1)
        gr_ttf_calcMaxFontHeight(f);
    res = f->max_height;

    pthread_mutex_unlock(&f->mutex);
    return res;
}

static bool gr_ttf_dump_stats_count_string_cache(void *key __unused, void *value, void *context)
{
    int *string_cache_size = (int *)context;
    StringCacheEntry *e = (StringCacheEntry *)value;
    *string_cache_size += e->surface.height*e->surface.width + sizeof(StringCacheEntry);
    return true;
}

static bool gr_ttf_dump_stats_font(void *key, void *value, void *context)
{
    TrueTypeFontKey *k = (TrueTypeFontKey *)key;
    TrueTypeFont *f = (TrueTypeFont *)value;
    int *total_string_cache_size = (int *)context;
    int string_cache_size = 0;

    pthread_mutex_lock(&f->mutex);

    hashmapForEach(f->string_cache, gr_ttf_dump_stats_count_string_cache, &string_cache_size);

    printf("  Font %s (size %d, dpi %d):\n"
            "    refcount: %d\n"
            "    max_height: %d\n"
            "    base: %d\n"
            "    glyph_cache: %d entries\n"
            "    string_cache: %d entries (%.2f kB)\n",
            k->path, k->size, k->dpi,
            f->refcount, f->max_height, f->base,
            hashmapSize(f->glyph_cache),
            hashmapSize(f->string_cache), ((double)string_cache_size)/1024);

    pthread_mutex_unlock(&f->mutex);

    *total_string_cache_size += string_cache_size;
    return true;
}

void gr_ttf_dump_stats(void)
{
    pthread_mutex_lock(&font_data.mutex);

    printf("TrueType fonts system stats: ");
    if(!font_data.fonts)
        printf("no truetype fonts loaded.\n");
    else
    {
        int total_string_cache_size = 0;
        printf("%d fonts loaded.\n", hashmapSize(font_data.fonts));
        hashmapForEach(font_data.fonts, gr_ttf_dump_stats_font, &total_string_cache_size);
        printf("  Total string cache size: %.2f kB\n", ((double)total_string_cache_size)/1024);
    }

    pthread_mutex_unlock(&font_data.mutex);
}
