/*
		Copyright 2013 to 2020 TeamWin
		TWRP is free software: you can redistribute it and/or modify
		it under the terms of the GNU General Public License as published by
		the Free Software Foundation, either version 3 of the License, or
		(at your option) any later version.

		TWRP is distributed in the hope that it will be useful,
		but WITHOUT ANY WARRANTY; without even the implied warranty of
		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
		GNU General Public License for more details.

		You should have received a copy of the GNU General Public License
		along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _TWRP_TRUETYPE_HPP
#define _TWRP_TRUETYPE_HPP

#include <map>
#include <string>
#include <ft2build.h>
#include <pthread.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include <pixelflinger/pixelflinger.h>
#include "minui.h"

typedef struct TrueTypeFontKey {
    int size;
    int dpi;
    std::string path;
} TrueTypeFontKey;

inline bool operator<(const TrueTypeFontKey &ttfkLeft, const TrueTypeFontKey &ttfkRight) {
    return std::tie(ttfkLeft.size, ttfkLeft.dpi, ttfkLeft.path) < std::tie(ttfkRight.size, ttfkRight.dpi, ttfkRight.path);
}

typedef struct {
    FT_BBox bbox;
    FT_BitmapGlyph glyph;
} TrueTypeCacheEntry;

typedef struct StringCacheKey {
    int max_width;
    std::string text;
} StringCacheKey;

inline bool operator<(const StringCacheKey &sckLeft, const StringCacheKey &sckRight)  {
    return std::tie(sckLeft.text, sckLeft.max_width) < std::tie(sckRight.text, sckRight.max_width);
}

typedef struct StringCacheEntry {
    GGLSurface surface;
    int rendered_bytes; // number of bytes from C string rendered, not number of UTF8 characters!
    StringCacheKey *key;
} StringCacheEntry;

typedef struct {
    int type;
    int refcount;
    int size;
    int dpi;
    int max_height;
    int base;
    FT_Face face;
    std::map<int, TrueTypeCacheEntry*> glyph_cache;
    std::map<StringCacheKey, StringCacheEntry*> string_cache;
    pthread_mutex_t mutex;
    TrueTypeFontKey *key;
} TrueTypeFont;

typedef struct {
    FT_Library ft_library;
    std::map<TrueTypeFontKey, TrueTypeFont*> fonts;
    pthread_mutex_t mutex;
} FontData;

typedef std::map<StringCacheKey, StringCacheEntry*> StringCacheMap;
typedef std::map<int, TrueTypeCacheEntry*> TrueTypeCacheEntryMap;
typedef std::map<TrueTypeFontKey, TrueTypeFont*> TrueTypeFontMap;

#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))

// 32bit FNV-1a hash algorithm
// http://isthe.com/chongo/tech/comp/fnv/#FNV-1a
static const uint32_t FNV_prime = 16777619U;
static const uint32_t offset_basis = 2166136261U;

#define STRING_CACHE_MAX_ENTRIES 400
#define STRING_CACHE_TRUNCATE_ENTRIES 150

class twrpTruetype {
public:
    twrpTruetype();
    static int utf8_to_unicode(const char* pIn, unsigned int *pOut);
    static void* gr_ttf_loadFont(const char *filename, int size, int dpi);
    static void* gr_ttf_scaleFont(void *font, int max_width, int measured_width);
    static void gr_ttf_freeStringCache(void *key, void *value, void *context __unused);
    static void gr_ttf_freeFont(void *font);
    static TrueTypeCacheEntry* gr_ttf_glyph_cache_peek(TrueTypeFont *font, int char_index);
    static TrueTypeCacheEntry* gr_ttf_glyph_cache_get(TrueTypeFont *font, int char_index);
    static int gr_ttf_copy_glyph_to_surface(GGLSurface *dest, FT_BitmapGlyph glyph, int offX, int offY, int base);
    static void gr_ttf_calcMaxFontHeight(TrueTypeFont *f);
    static int gr_ttf_render_text(TrueTypeFont *font, GGLSurface *surface, const std::string text, int max_width);
    static StringCacheEntry* gr_ttf_string_cache_peek(TrueTypeFont *font, const std::string text, __attribute__((unused)) int max_width);
    static StringCacheEntry* gr_ttf_string_cache_get(TrueTypeFont *font, const std::string text, int max_width);
    static int gr_ttf_measureEx(const char *s, void *font);
    static int gr_ttf_maxExW(const char *s, void *font, int max_width);
    static int gr_ttf_textExWH(void *context, int x, int y,
                    const char *s, void *pFont,
                    int max_width, int max_height,
                    const gr_surface gr_draw_surface);
    static int gr_ttf_getMaxFontHeight(void *font);
    static void gr_ttf_string_cache_truncate(TrueTypeFont *font);
};
#endif // _TWRP_TRUETYPE_HPP
