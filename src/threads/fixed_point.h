#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdbool.h>
#include <stdint.h>

/* Convert n to fixed point n * f  */
inline int32_t int2f(int32_t n);
/* Convert x to integer (rounding toward zero) x / f*/
inline int32_t f2int_r20(int32_t x);
/* Convert x to integer (rounding to nearest)
   (x + f / 2) / f if x >= 0, (x - f / 2) / f if x <= 0.*/
inline int32_t f2int_r2near(int32_t x);
/* Add x and y: x + y*/
inline int32_t f_add_f(int32_t x, int32_t y);
/* Subtract y from x: x - y*/
inline int32_t f_sub_f (int32_t x, int32_t y);
/* Add x and n: x + n * f*/
inline int32_t f_add_int (int32_t x, int32_t n);
/* Subtract n from x: x - n * f */
inline int32_t f_sub_int (int32_t x, int32_t n);
/* Multiply x by y: ((int64_t) x) * y / f */
inline int32_t f_multiply_f (int32_t x, int32_t y);
/* Multiply x by n: x * n */
inline int32_t f_multiply_int (int32_t x, int32_t n);
/* Divide x by y:((int64_t) x) * f / y */
inline int32_t f_divide_f (int32_t x, int32_t y);
/* Divide x by n: x / n */
inline int32_t f_divide_int (int32_t x, int32_t n);


#endif /* threads/fixed_point.h */
