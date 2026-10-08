/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/bch.h>

struct bch_control *fixed_init_bch(int, int, unsigned int);
void fixed_free_bch(struct bch_control *);
void fixed_encode_bch(struct bch_control *, const uint8_t *, unsigned int, uint8_t *);
int fixed_decode_bch(struct bch_control *, const uint8_t *, unsigned int,
                     const uint8_t *, const uint8_t *, const unsigned int *,
                     unsigned int *);
static uint32_t random_state = 0x432b;
static uint32_t next_random(void)
{
 random_state ^= random_state << 13;
 random_state ^= random_state >> 17;
 random_state ^= random_state << 5;
 return random_state;
}
static int compare(const void *a, const void *b)
{
 unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;
 return (x > y) - (x < y);
}
int main(void)
{
 struct bch_control *a = init_bch(13, 8, 0);
 struct bch_control *b = fixed_init_bch(13, 8, 0);
 unsigned pattern, offset, errors, i, cases = 0;
 assert(a && b && a->ecc_bytes == 13 && b->ecc_bytes == 13);
 assert(!fixed_init_bch(13, 4, 0));
 assert(!fixed_init_bch(14, 8, 0));
 for (pattern = 0; pattern < 64; pattern++) {
  for (offset = 0; offset < 4; offset++) {
   uint8_t storage[516], original[512], received[525];
   uint8_t ea[13] = {0}, eb[13] = {0};
   uint8_t *data = storage + offset;
   for (i = 0; i < 512; i++)
    data[i] = pattern == 0 ? 0 : pattern == 1 ? 255 :
              pattern == 2 ? 0x55 : pattern == 3 ? 0xaa : next_random();
   memcpy(original, data, 512);
   encode_bch(a, data, 512, ea);
   fixed_encode_bch(b, data, 512, eb);
   assert(!memcmp(ea, eb, 13));
   /* Include correctable data, parity and mixed errors, plus >t equivalence.
    * Above t, do not assume every corruption is detectable. */
   for (errors = 0; errors <= 10; errors++) {
    unsigned mode;
    for (mode = 0; mode < 3; mode++) {
     unsigned positions[10], la[16], lb[16], j, bit;
     uint8_t ca[13] = {0}, cb[13] = {0};
     int na, nb;
     memcpy(received, original, 512);
     memcpy(received + 512, ea, 13);
     for (i = 0; i < errors; i++) {
      do {
       bit = mode == 0 ? next_random() % 4096 :
             mode == 1 ? 4096 + next_random() % 104 :
                         next_random() % 4200;
       for (j = 0; j < i && positions[j] != bit; j++) {}
      } while (j != i);
      positions[i] = bit;
      received[bit / 8] ^= 1u << (bit % 8);
     }
     encode_bch(a, received, 512, ca);
     fixed_encode_bch(b, received, 512, cb);
     assert(!memcmp(ca, cb, 13));
     na = decode_bch(a, NULL, 512, received + 512, ca, NULL, la);
     nb = fixed_decode_bch(b, NULL, 512, received + 512, cb, NULL, lb);
     assert(na == nb);
     if (errors <= 8) assert(na == (int)errors);
     if (na >= 0) {
      qsort(la, na, sizeof(*la), compare);
      qsort(lb, nb, sizeof(*lb), compare);
      assert(!memcmp(la, lb, na * sizeof(*la)));
      if (errors <= 8) {
       for (i = 0; i < (unsigned)na; i++)
        if (la[i] < 4096) received[la[i]/8] ^= 1u << (la[i]%8);
       assert(!memcmp(received, original, 512));
      }
     }
     cases++;
    }
   }
  }
 }
 free_bch(a);
 fixed_free_bch(b);
 printf("BCH generic/fixed equivalence PASS: %u corruption cases\n", cases);
 return 0;
}
