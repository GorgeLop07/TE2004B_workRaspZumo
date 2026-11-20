#ifndef ENCODERS_H
#define ENCODERS_H

#include <stdbool.h>
#include <stdint.h>

/* Counts are sign-normalized so forward wheel motion is positive. */
bool encoders_init(void);
bool encoders_read(int32_t *left_count, int32_t *right_count);

#endif
