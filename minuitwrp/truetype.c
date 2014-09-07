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

typedef struct
{
    int type;
    int size;
    int dpi;
    int max_height;
    int base;
    FT_Face face;
    Hashmap *glyph_cache;
    Hashmap *string_cache;
    pthread_mutex_t mutex;
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

typedef struct
{
    GGLSurface surface;
    int rendered_len;
} StringCacheEntry;

static FT_Library ft_library = NULL;

#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))

static bool gr_ttf_string_cache_equals(void *keyA, void *keyB)
{
    StringCacheKey *a = keyA;
    StringCacheKey *b = keyB;
    return a->max_width == b->max_width && strcmp(a->text, b->text) == 0;
}

// 32bit FNV-1a hash algorithm
// http://isthe.com/chongo/tech/comp/fnv/#FNV-1a
static int gr_ttf_string_cache_hash(void *key)
{
    StringCacheKey *k = key;
    const uint32_t FNV_prime = 16777619U;
    const uint32_t offset_basis = 2166136261U;
    uint32_t *d = (uint32_t*)k->text;
    uint32_t i, max;
    uint32_t len = strlen(k->text);
    uint32_t hash = offset_basis;

    max = len/4;

    // 32 bit data
    for(i = 0; i < max; ++i)
    {
        hash ^= *d++;
        hash *= FNV_prime;
    }

    // last bits
    for(i *= 4; i < len; ++i)
    {
        hash ^= (uint32_t) k->text[i];
        hash *= FNV_prime;
    }
    return hash;
}

void *gr_ttf_loadFont(const char *filename, int size, int dpi)
{
    int error;
    if(!ft_library)
    {
        error = FT_Init_FreeType(&ft_library);
        if(error)
        {
            fprintf(stderr, "Failed to init libfreetype! %d\n", error);
            return NULL;
        }
    }

    FT_Face face;
    error = FT_New_Face(ft_library, filename, 0, &face);
    if(error)
    {
        fprintf(stderr, "Failed to load truetype face %s: %d\n", filename, error);
        return NULL;
    }

    error = FT_Set_Char_Size(face, 0, size*16, dpi, dpi);
    if(error)
    {
         fprintf(stderr, "Failed to set truetype face size to %d, dpi %d: %d\n", size, dpi, error);
         FT_Done_Face(face);
         return NULL;
    }

    TrueTypeFont *d = malloc(sizeof(TrueTypeFont));
    d->type = FONT_TYPE_TTF;
    d->size = size;
    d->dpi = dpi;
    d->face = face;
    d->max_height = -1;
    d->base = -1;
    d->glyph_cache = hashmapCreate(32, hashmapIntHash, hashmapIntEquals);
    d->string_cache = hashmapCreate(128, gr_ttf_string_cache_hash, gr_ttf_string_cache_equals);
    pthread_mutex_init(&d->mutex, 0);
    return d;
}

static bool gr_ttf_freeFontCache(void *key, void *value, void *context)
{
    TrueTypeCacheEntry *e = value;
    FT_Done_Glyph((FT_Glyph)e->glyph);
    free(e);
    free(key);
    return true;
}

static bool gr_ttf_freeStringCache(void *key, void *value, void *context)
{
    StringCacheKey *k = key;
    free(k->text);
    free(k);

    StringCacheEntry *e = value;
    free(e->surface.data);
    free(e);
    return true;
}

void gr_ttf_freeFont(void *font)
{
    TrueTypeFont *d = font;
    FT_Done_Face(d->face);
    hashmapForEach(d->string_cache, gr_ttf_freeStringCache, NULL);
    hashmapFree(d->string_cache);
    hashmapForEach(d->glyph_cache, gr_ttf_freeFontCache, NULL);
    hashmapFree(d->glyph_cache);
    pthread_mutex_destroy(&d->mutex);
    free(d);
}

static TrueTypeCacheEntry *gr_ttf_glyph_cache_peek(TrueTypeFont *font, int char_index)
{
    return hashmapGet(font->glyph_cache, &char_index);
}

static TrueTypeCacheEntry *gr_ttf_glyph_cache_get(TrueTypeFont *font, int char_index)
{
    TrueTypeCacheEntry *res = hashmapGet(font->glyph_cache, &char_index);
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

        res = malloc(sizeof(TrueTypeCacheEntry));
        res->glyph = glyph;
        FT_Glyph_Get_CBox((FT_Glyph)glyph, FT_GLYPH_BBOX_PIXELS, &res->bbox);

        int *key = malloc(sizeof(int));
        *key = char_index;

        hashmapPut(font->glyph_cache, key, res);
    }

    return res;
}

static int gr_ttf_copy_glyph_to_surface(GGLSurface *dest, FT_BitmapGlyph glyph, int offX, int offY, int base)
{
    int y;
    uint8_t *src_itr = glyph->bitmap.buffer;
    uint8_t *dest_itr = dest->data;

    if(glyph->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
    {
        fprintf(stderr, "Unsupported pixel mode in FT_BitmapGlyph %d\n", glyph->bitmap.pixel_mode);
        return -1;
    }

    dest_itr += (offY + base - glyph->top)*dest->stride + (offX + glyph->left);

    for(y = 0; y < glyph->bitmap.rows; ++y)
    {
        memcpy(dest_itr, src_itr, glyph->bitmap.width);
        src_itr += glyph->bitmap.pitch;
        dest_itr += dest->stride;
    }
    return 0;
}

static int gr_ttf_render_text(TrueTypeFont *font, GGLSurface *surface, const char *text, int max_width)
{
    TrueTypeFont *f = font;
    TrueTypeCacheEntry *ent;
    int max_len = 0, total_w = 0;
    char c;
    int i, x, diff, char_idx, prev_idx = 0;
    int height, base;
    FT_Vector delta;
    uint8_t *data = NULL;
    const char *text_itr = text;

    while((c = *text_itr++))
    {
        char_idx = FT_Get_Char_Index(f->face, c);
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
        ++max_len;
    }

    if(font->max_height == -1)
        gr_ttf_getMaxFontHeight(font);

    if(font->max_height == -1)
        return -1;

    height = font->max_height;

    data = malloc(total_w*height);
    memset(data, 0, total_w*height);
    x = 0;
    prev_idx = 0;

    surface->version = sizeof(*surface);
    surface->width = total_w;
    surface->height = height;
    surface->stride = total_w;
    surface->data = (void*)data;
    surface->format = GGL_PIXEL_FORMAT_A_8;

    for(i = 0; i < max_len; ++i)
    {
        char_idx = FT_Get_Char_Index(f->face, text[i]);
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

    return max_len;
}

static StringCacheEntry *gr_ttf_string_cache_peek(TrueTypeFont *font, const char *text, int max_width)
{
    StringCacheEntry *res;
    StringCacheKey k = {
        .text = (char*)text,
        .max_width = max_width
    };

    return hashmapGet(font->string_cache, &k);
}

static StringCacheEntry *gr_ttf_string_cache_get(TrueTypeFont *font, const char *text, int max_width)
{
    StringCacheEntry *res;
    StringCacheKey k = {
        .text = (char*)text,
        .max_width = max_width
    };

    res = hashmapGet(font->string_cache, &k);
    if(!res)
    {
        res = malloc(sizeof(StringCacheEntry));
        res->rendered_len = gr_ttf_render_text(font, &res->surface, text, max_width);
        if(res->rendered_len < 0)
        {
            free(res);
            return NULL;
        }

        StringCacheKey *new_key = malloc(sizeof(StringCacheKey));
        new_key->max_width = max_width;
        new_key->text = strdup(text);
        hashmapPut(font->string_cache, new_key, res);
    }
    return res;
}

int gr_ttf_measureEx(const char *s, void *font)
{
    TrueTypeFont *f = font;
    int res = -1;

    pthread_mutex_lock(&f->mutex);
    StringCacheEntry *e = gr_ttf_string_cache_get(font, s, -1);
    if(e)
        res = e->surface.width;
    pthread_mutex_unlock(&f->mutex);

    return res;
}

int gr_ttf_maxExW(const char *s, void *font, int max_width)
{
    TrueTypeFont *f = font;
    TrueTypeCacheEntry *ent;
    int max_len = 0, total_w = 0;
    char c;
    int char_idx, prev_idx = 0;
    FT_Vector delta;
    StringCacheEntry *e;

    pthread_mutex_lock(&f->mutex);

    e = gr_ttf_string_cache_peek(font, s, max_width);
    if(e)
    {
        max_len = e->rendered_len;
        pthread_mutex_unlock(&f->mutex);
        return max_len;
    }

    for(; (c = *s++); ++max_len)
    {
        char_idx = FT_Get_Char_Index(f->face, c);
        if(FT_HAS_KERNING(f->face) && prev_idx && char_idx)
        {
            FT_Get_Kerning(f->face, prev_idx, char_idx, FT_KERNING_DEFAULT, &delta);
            total_w += delta.x >> 6;
        }
        prev_idx = char_idx;

        if(total_w > max_width)
            break;

        ent = gr_ttf_glyph_cache_get(f, char_idx);
        if(!ent)
            continue;

        total_w += ent->glyph->root.advance.x >> 16;
    }
    pthread_mutex_unlock(&f->mutex);
    return max_len > 0 ? max_len - 1 : 0;
}

int gr_ttf_textExWH(void *context, int x, int y, const char *s, void *pFont, int max_width, int max_height)
{
    GGLContext *gl = context;
    TrueTypeFont *font = pFont;

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
    int res = e->rendered_len;

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

    pthread_mutex_unlock(&font->mutex);
    return res;
}

int gr_ttf_getMaxFontHeight(void *font)
{
    int res;
    TrueTypeFont *f = font;

    pthread_mutex_lock(&f->mutex);

    if(f->max_height == -1)
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

    res = f->max_height;

    pthread_mutex_unlock(&f->mutex);
    return res;
}
