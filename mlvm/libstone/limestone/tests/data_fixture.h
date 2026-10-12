#ifndef LIMESTONE_DATA_FIXTURE_H
#define LIMESTONE_DATA_FIXTURE_H
#include <stdint.h>
struct exl_fixture_pair { int64_t count;double weight; };
struct exl_fixture_small { int8_t tag;uint16_t count; };
struct exl_fixture_hfa { float x,y,z,w; };
struct exl_fixture_nested { uint16_t tag;struct exl_fixture_pair pair;float lanes[3];void *pointer; };
struct exl_fixture_large { int64_t values[8];double weight; };
#endif
