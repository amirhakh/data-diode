#ifndef UTIL_H
#define UTIL_H

#include <stdlib.h>

#define MALLOC(type) ((type*)calloc(1, sizeof(type)))

/* bitmap manipulation */
#define BITS_PER_ITEM(map) (sizeof(map[0])*8)
#define MASK(pos,map) (1 << ((pos) % (BITS_PER_ITEM(map))))
#define POS(pos,map)  ((pos) / BITS_PER_ITEM(map))
#define SET_BIT(x, map) (map[POS(x,map)] |= MASK(x,map))
#define CLR_BIT(x, map) (map[POS(x,map)] &= ~MASK(x,map))
#define BIT_ISSET(x, map) (map[POS(x,map)] & MASK(x,map))


#endif
