/* DomSymAs pure-C helpers: magic probe, CRC32, header peek.
 *
 * Compiles as C11 with no C++ dependency so minimal readers (bootloaders,
 * build scripts) can validate a `.symas` container before linking the full
 * C++ databank library.
 */
#include "sandstone.h"

#include <string.h>

int
domsymas_probe_magic (const uint8_t *data, size_t len)
{
  if (!data || len < 3)
    return 0;
  return data[0] == DOMSYMAS_MAGIC_0 && data[1] == DOMSYMAS_MAGIC_1
         && data[2] == DOMSYMAS_MAGIC_2;
}

uint32_t
domsymas_crc32 (const uint8_t *data, size_t len)
{
  static uint32_t tab[256];
  static int init = 0;
  uint32_t c = 0xFFFFFFFFu;
  size_t i;
  int k;
  if (!init)
    {
      for (i = 0; i < 256; i++)
        {
          uint32_t v = (uint32_t)i;
          for (k = 0; k < 8; k++)
            v = (v & 1u) ? (0xEDB88320u ^ (v >> 1)) : (v >> 1);
          tab[i] = v;
        }
      init = 1;
    }
  if (data)
    {
      for (i = 0; i < len; i++)
        c = tab[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    }
  return c ^ 0xFFFFFFFFu;
}
