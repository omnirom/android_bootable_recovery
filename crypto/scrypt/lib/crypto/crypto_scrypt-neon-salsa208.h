/*
 * version 20110505
 * D. J. Bernstein
 * Public domain.
 *
 * Based on crypto_core/salsa208/armneon/core.c from SUPERCOP 20130419
 */

#define ROUNDS 8
static void
salsa20_8_intrinsic(void * input)
{
  int i;

  const uint32x4_t abab = {-1,0,-1,0};

  /*
   * This is modified since we only have one argument. Usually you'd rearrange
   * the constant, key, and input bytes, but we just have one linear array to
   * rearrange which is a bit easier.
   */

  /*
   * Change the input to be diagonals as if it's a 4x4 matrix of 32-bit values.
   */
  uint32x4_t x0x5x10x15;
  uint32x4_t x12x1x6x11;
  uint32x4_t x8x13x2x7;
  uint32x4_t x4x9x14x3;

  uint32x4_t x0x1x10x11;
  uint32x4_t x12x13x6x7;
  uint32x4_t x8x9x2x3;
  uint32x4_t x4x5x14x15;

  uint32x4_t x0x1x2x3;
  uint32x4_t x4x5x6x7;
  uint32x4_t x8x9x10x11;
  uint32x4_t x12x13x14x15;

  x0x1x2x3 = vld1q_u8((uint8_t *) input);
  x4x5x6x7 = vld1q_u8(16 + (uint8_t *) input);
  x8x9x10x11 = vld1q_u8(32 + (uint8_t *) input);
  x12x13x14x15 = vld1q_u8(48 + (uint8_t *) input);

  x0x1x10x11 = vcombine_u32(vget_low_u32(x0x1x2x3), vget_high_u32(x8x9x10x11));
  x4x5x14x15 = vcombine_u32(vget_low_u32(x4x5x6x7), vget_high_u32(x12x13x14x15));
  x8x9x2x3 = vcombine_u32(vget_low_u32(x8x9x10x11), vget_high_u32(x0x1x2x3));
  x12x13x6x7 = vcombine_u32(vget_low_u32(x12x13x14x15), vget_high_u32(x4x5x6x7));

  x0x5x10x15 = vbslq_u32(abab,x0x1x10x11,x4x5x14x15);
  x8x13x2x7 = vbslq_u32(abab,x8x9x2x3,x12x13x6x7);
  x4x9x14x3 = vbslq_u32(abab,x4x5x14x15,x8x9x2x3);
  x12x1x6x11 = vbslq_u32(abab,x12x13x6x7,x0x1x10x11);

  uint32x4_t start0 = x0x5x10x15;
  uint32x4_t start1 = x12x1x6x11;
  uint32x4_t start3 = x4x9x14x3;
  uint32x4_t start2 = x8x13x2x7;

  /* From here on this should be the same as the SUPERCOP version. */

  uint32x4_t diag0 = start0;
  uint32x4_t diag1 = start1;
  uint32x4_t diag2 = start2;
  uint32x4_t diag3 = start3;

  uint32x4_t a0;
  uint32x4_t a1;
  uint32x4_t a2;
  uint32x4_t a3;

  for (i = ROUNDS;i > 0;i -= 2) {
    a0 = diag1 + diag0;
    diag3 ^= vsriq_n_u32(vshlq_n_u32(a0,7),a0,25);
    a1 = diag0 + diag3;
    diag2 ^= vsriq_n_u32(vshlq_n_u32(a1,9),a1,23);
    a2 = diag3 + diag2;
    diag1 ^= vsriq_n_u32(vshlq_n_u32(a2,13),a2,19);
    a3 = diag2 + diag1;
    diag0 ^= vsriq_n_u32(vshlq_n_u32(a3,18),a3,14);

    diag3 = vextq_u32(diag3,diag3,3);
    diag2 = vextq_u32(diag2,diag2,2);
    diag1 = vextq_u32(diag1,diag1,1);

    a0 = diag3 + diag0;
    diag1 ^= vsriq_n_u32(vshlq_n_u32(a0,7),a0,25);
    a1 = diag0 + diag1;
    diag2 ^= vsriq_n_u32(vshlq_n_u32(a1,9),a1,23);
    a2 = diag1 + diag2;
    diag3 ^= vsriq_n_u32(vshlq_n_u32(a2,13),a2,19);
    a3 = diag2 + diag3;
    diag0 ^= vsriq_n_u32(vshlq_n_u32(a3,18),a3,14);

    diag1 = vextq_u32(diag1,diag1,3);
    diag2 = vextq_u32(diag2,diag2,2);
    diag3 = vextq_u32(diag3,diag3,1);
  }

  x0x5x10x15 = diag0 + start0;
  x12x1x6x11 = diag1 + start1;
  x8x13x2x7 = diag2 + start2;
  x4x9x14x3 = diag3 + start3;

  x0x1x10x11 = vbslq_u32(abab,x0x5x10x15,x12x1x6x11);
  x12x13x6x7 = vbslq_u32(abab,x12x1x6x11,x8x13x2x7);
  x8x9x2x3 = vbslq_u32(abab,x8x13x2x7,x4x9x14x3);
  x4x5x14x15 = vbslq_u32(abab,x4x9x14x3,x0x5x10x15);

  x0x1x2x3 = vcombine_u32(vget_low_u32(x0x1x10x11),vget_high_u32(x8x9x2x3));
  x4x5x6x7 = vcombine_u32(vget_low_u32(x4x5x14x15),vget_high_u32(x12x13x6x7));
  x8x9x10x11 = vcombine_u32(vget_low_u32(x8x9x2x3),vget_high_u32(x0x1x10x11));
  x12x13x14x15 = vcombine_u32(vget_low_u32(x12x13x6x7),vget_high_u32(x4x5x14x15));

  vst1q_u8((uint8_t *) input,(uint8x16_t) x0x1x2x3);
  vst1q_u8(16 + (uint8_t *) input,(uint8x16_t) x4x5x6x7);
  vst1q_u8(32 + (uint8_t *) input,(uint8x16_t) x8x9x10x11);
  vst1q_u8(48 + (uint8_t *) input,(uint8x16_t) x12x13x14x15);
}
