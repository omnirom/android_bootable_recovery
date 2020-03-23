/*
		Copyright 2012 to 2020 TeamWin
		This file is part of TWRP/TeamWin Recovery Project.

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

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <errno.h>
#include <stdio.h>

#include <pthread.h>
#include <algorithm>
#include <string>
#include "truetype.hpp"

static FontData font_data = {
	.ft_library = NULL,
	.mutex = PTHREAD_MUTEX_INITIALIZER
};

twrpTruetype::twrpTruetype(void) {

}

int twrpTruetype::utf8_to_unicode(const char* pIn, unsigned int *pOut) {
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

void* twrpTruetype::gr_ttf_loadFont(const char *filename, int size, int dpi) {
	int error;
	TrueTypeFont* res = nullptr;
	TrueTypeFontKey* key;

	pthread_mutex_lock(&font_data.mutex);

	TrueTypeFontKey k = {
		.size = size,
		.dpi = dpi,
		.path = (char*)filename
	};

	TrueTypeFontMap::iterator ttfIter = font_data.fonts.find(k);

	if (ttfIter != font_data.fonts.end())
	{
		res = ttfIter->second;
		++res->refcount;
		goto exit;
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

	res = new TrueTypeFont;
	res->type = FONT_TYPE_TTF;
	res->size = size;
	res->dpi = dpi;
	res->face = face;
	res->max_height = -1;
	res->base = -1;
	res->refcount = 1;

	pthread_mutex_init(&res->mutex, 0);

	key = new TrueTypeFontKey;
	key->path = strdup(filename);
	key->size = size;
	key->dpi = dpi;

	res->key = key;
	font_data.fonts[*key] = res;

exit:
	pthread_mutex_unlock(&font_data.mutex);
	return res;
}

void* twrpTruetype::gr_ttf_scaleFont(void *font, int max_width, int measured_width) {
	if (!font)
		return nullptr;

	TrueTypeFont *f = (TrueTypeFont *)font;
	float scale_value = (float)(max_width) / (float)(measured_width);
	int new_size = ((int)((float)f->size * scale_value)) - 1;
	if (new_size < 1)
		new_size = 1;
	const char* file = f->key->path;
	int dpi = f->dpi;
	return gr_ttf_loadFont(file, new_size, dpi);
}

static bool gr_ttf_freeFontCache(void *value, void *context __unused)
{
	TrueTypeCacheEntry *e = (TrueTypeCacheEntry *)value;
	FT_Done_Glyph((FT_Glyph)e->glyph);
	free(e);
	return true;
}

bool twrpTruetype::gr_ttf_freeStringCache(void *key, void *value, void *context __unused) {
	StringCacheKey *k = (StringCacheKey *)key;
	delete k->text;
	delete k;

	StringCacheEntry *e = (StringCacheEntry *)value;
	delete e->surface.data;
	delete e;
	return true;
}

void twrpTruetype::gr_ttf_freeFont(void *font) {
	pthread_mutex_lock(&font_data.mutex);

	TrueTypeFont *d = (TrueTypeFont *)font;
	if(--d->refcount == 0)
	{
		delete d->key->path;
		delete d->key;

		FT_Done_Face(d->face);

		StringCacheMap::iterator stringCacheEntryIt = d->string_cache.begin();
		while (stringCacheEntryIt != d->string_cache.end()) {
			gr_ttf_freeStringCache(stringCacheEntryIt->second->key, stringCacheEntryIt->second, NULL);
			stringCacheEntryIt = d->string_cache.erase(stringCacheEntryIt);
		}

		TrueTypeCacheEntryMap::iterator ttcIt = d->glyph_cache.begin();
		while(ttcIt != d->glyph_cache.end()) {
			gr_ttf_freeFontCache(ttcIt->second, NULL);
			ttcIt = d->glyph_cache.erase(ttcIt);
		}

		pthread_mutex_destroy(&d->mutex);

		TrueTypeFontMap::iterator trueTypeFontIt = font_data.fonts.find(*(d->key));
		delete d;
		font_data.fonts.erase(trueTypeFontIt);

	}

	pthread_mutex_unlock(&font_data.mutex);
}

TrueTypeCacheEntry* twrpTruetype::gr_ttf_glyph_cache_peek(TrueTypeFont *font, int char_index) {
	TrueTypeCacheEntryMap::iterator glyphCacheItr = font->glyph_cache.find(char_index);

	if(glyphCacheItr != font->glyph_cache.end()) {
		return font->glyph_cache[char_index];
	}
	return nullptr;
}

TrueTypeCacheEntry* twrpTruetype::gr_ttf_glyph_cache_get(TrueTypeFont *font, int char_index) {
	TrueTypeCacheEntryMap::iterator glyphCacheItr = font->glyph_cache.find(char_index);
	TrueTypeCacheEntry* res = nullptr;
	if(glyphCacheItr == font->glyph_cache.end())
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

		res = new TrueTypeCacheEntry;
		res->glyph = glyph;
		FT_Glyph_Get_CBox((FT_Glyph)glyph, FT_GLYPH_BBOX_PIXELS, &res->bbox);
		font->glyph_cache[char_index] = res;
	}
	else {
		res = glyphCacheItr->second;
	}

	return res;
}

int twrpTruetype::gr_ttf_copy_glyph_to_surface(GGLSurface *dest, FT_BitmapGlyph glyph, int offX, int offY, int base) {
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

void twrpTruetype::gr_ttf_calcMaxFontHeight(TrueTypeFont *f) {
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
int twrpTruetype::gr_ttf_render_text(TrueTypeFont *font, GGLSurface *surface, const char *text, int max_width) {
	TrueTypeFont *f = font;
	TrueTypeCacheEntry *ent;
	int bytes_rendered = 0, total_w = 0;
	int utf_bytes = 0;
	unsigned int unicode = 0;
	int i, x, diff, char_idx, prev_idx = 0;
	int height;
	FT_Vector delta;
	uint8_t *data = NULL;
	const char *text_itr = text;
	int *char_idxs;
	int char_idxs_len = 0;

	char_idxs = new int[strlen(text)];

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
		delete [] char_idxs;
		return -1;
	}

	height = font->max_height;

	data = new uint8_t[total_w * height];
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

	delete [] char_idxs;
	return bytes_rendered;
}

StringCacheEntry* twrpTruetype::gr_ttf_string_cache_peek(TrueTypeFont *font, 
	const char *text, 
	__attribute__((unused)) int max_width) {
		StringCacheKey k = {
			.text = (char*)text,
			.max_width = max_width
		};
		StringCacheMap::iterator stringCacheItr = font->string_cache.find(k);
		if (stringCacheItr != font->string_cache.end()) {
			return stringCacheItr->second;
		}
		else {
			return nullptr;
		}
}

StringCacheEntry* twrpTruetype::gr_ttf_string_cache_get(TrueTypeFont *font, const char *text, int max_width) {
	StringCacheEntry *res = nullptr;

	StringCacheKey k = {
		.text = (char*)text,
		.max_width = max_width
	};

	StringCacheMap::iterator stringCacheItr = font->string_cache.find(k);
	if (stringCacheItr == font->string_cache.end())
	{
		res = new StringCacheEntry;
		res->rendered_bytes = gr_ttf_render_text(font, &res->surface, text, max_width);
		if(res->rendered_bytes < 0)
		{
			delete res;
			return nullptr;
		}

		StringCacheKey *new_key = new StringCacheKey;
		new_key->max_width = max_width;
		new_key->text = strdup(text);
		res->key = new_key;
		font->string_cache[k] = res; 
	}
	else
	{
		res = stringCacheItr->second;

		// truncate old entries
		if (font->string_cache.size() >= STRING_CACHE_MAX_ENTRIES)
		{
			StringCacheEntry *ent;
			for(int i = 0; i < STRING_CACHE_TRUNCATE_ENTRIES; ++i)
			{
				StringCacheMap::iterator stringTruncCacheItr = font->string_cache.find(k);

				if (stringTruncCacheItr != font->string_cache.end()) {
					ent = stringTruncCacheItr->second;
					gr_ttf_freeStringCache(ent->key, ent, nullptr);
					font->string_cache.erase(stringTruncCacheItr);
				}
			}
		}
	}
	return res;
}

int twrpTruetype::gr_ttf_measureEx(const char *s, void *font) {
	TrueTypeFont *f = (TrueTypeFont *)font;
	int res = -1;

	pthread_mutex_lock(&f->mutex);
	StringCacheEntry *e = gr_ttf_string_cache_get(f, s, -1);
	if(e)
		res = e->surface.width;
	pthread_mutex_unlock(&f->mutex);

	return res;
}

int twrpTruetype::gr_ttf_maxExW(const char *s, void *font, int max_width) {
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

int twrpTruetype::gr_ttf_textExWH(void *context, int x, int y,
					const char *s, void *pFont,
					int max_width, int max_height,
					const gr_surface gr_draw_surface) {
	GGLContext *gl = (GGLContext *)context;
	TrueTypeFont *font = (TrueTypeFont *)pFont;
	const GRSurface *gr_draw = (const GRSurface*) gr_draw_surface;

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

#if TW_ROTATION != 0
	// Do not perform relatively expensive operation if not needed
	GGLSurface string_surface_rotated;
	string_surface_rotated.version = sizeof(string_surface_rotated);
	// Skip the **(TW_ROTATION == 0)** || (TW_ROTATION == 180) check
	// because we are under a TW_ROTATION != 0 conditional compilation statement
	string_surface_rotated.width   = (TW_ROTATION == 180) ? e->surface.width  : e->surface.height;
	string_surface_rotated.height  = (TW_ROTATION == 180) ? e->surface.height : e->surface.width;
	string_surface_rotated.stride  = string_surface_rotated.width;
	string_surface_rotated.format  = e->surface.format;
	// e->surface.format is GGL_PIXEL_FORMAT_A_8 (grayscale)
	string_surface_rotated.data = new GGLubyte[string_surface_rotated.stride * string_surface_rotated.height * 1];
	surface_ROTATION_transform((gr_surface) &string_surface_rotated, (const gr_surface) &e->surface, 1);
#endif

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

	// Figuring out display coordinates works for TW_ROTATION == 0 too,
	// and isn't as expensive as allocating and rotating another surface,
	// so we do this anyway.
	int x0_disp, y0_disp, x1_disp, y1_disp;
	int l_disp, r_disp, t_disp, b_disp;

	x0_disp = ROTATION_X_DISP(x, y, gr_draw);
	y0_disp = ROTATION_Y_DISP(x, y, gr_draw);
	x1_disp = ROTATION_X_DISP(x + e->surface.width, y_bottom, gr_draw);
	y1_disp = ROTATION_Y_DISP(x + e->surface.width, y_bottom, gr_draw);
	l_disp = std::min(x0_disp, x1_disp);
	r_disp = std::max(x0_disp, x1_disp);
	t_disp = std::min(y0_disp, y1_disp);
	b_disp = std::max(y0_disp, y1_disp);

#if TW_ROTATION != 0
	gl->bindTexture(gl, &string_surface_rotated);
#else
	gl->bindTexture(gl, &e->surface);
#endif
	gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
	gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
	gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);

	gl->enable(gl, GGL_TEXTURE_2D);
	gl->texCoord2i(gl, -l_disp, -t_disp);
	gl->recti(gl, l_disp, t_disp, r_disp, b_disp);
	gl->disable(gl, GGL_TEXTURE_2D);

#if TW_ROTATION != 0
	delete [] string_surface_rotated.data;
#endif

	pthread_mutex_unlock(&font->mutex);
	return res;
}

int twrpTruetype::gr_ttf_getMaxFontHeight(void *font) {
	int res;
	TrueTypeFont *f = (TrueTypeFont *)font;

	pthread_mutex_lock(&f->mutex);

	if(f->max_height == -1)
		gr_ttf_calcMaxFontHeight(f);
	res = f->max_height;

	pthread_mutex_unlock(&f->mutex);
	return res;
}
