/* The fixed_point number math operation layer*/

#include <stdio.h>
#include "threads/fixed_point.h"

/*pow(2,14)*/
#define FIXED_POINT_F 16384

/* Convert n to fixed point n * f  */
inline int32_t int2f(int32_t n){
	return n*FIXED_POINT_F;
}

/* Convert x to integer (rounding toward zero) x / f*/
inline int32_t f2int_r20(int32_t x){
	return x/FIXED_POINT_F;
}


/* Convert x to integer (rounding to nearest)
   (x + f / 2) / f if x >= 0, (x - f / 2) / f if x <= 0.*/
inline int32_t f2int_r2near(int32_t x){
	if(x>0){
		return (x+FIXED_POINT_F/2)/FIXED_POINT_F;
	}
	else{
		return (x-FIXED_POINT_F/2)/FIXED_POINT_F;
	}
}

/* Add x and y: x + y*/
inline int32_t f_add_f(int32_t x, int32_t y){
	return x+y;
}


/* Subtract y from x: x - y*/
inline int32_t f_sub_f (int32_t x, int32_t y) {
	return x - y;
}


/* Add x and n: x + n * f*/
inline int32_t f_add_int (int32_t x, int32_t n) {
	return x + n * FIXED_POINT_F;
}

/* Subtract n from x: x - n * f */
inline int32_t f_sub_int (int32_t x, int32_t n) {
	return x - n * FIXED_POINT_F;
}
/* Multiply x by y: ((int64_t) x) * y / f */

inline int32_t f_multiply_f (int32_t x, int32_t y) {
	return ((int64_t) x) * y / FIXED_POINT_F;
}
/* Multiply x by n: x * n */
inline int32_t f_multiply_int (int32_t x, int32_t n) {
	return x * n;
}
/* Divide x by y:((int64_t) x) * f / y */
inline int32_t f_divide_f (int32_t x, int32_t y) {
	ASSERT (y != 0);

	return ((int64_t) x) * FIXED_POINT_F / y;
}
/* Divide x by n: x / n */
inline int32_t f_divide_int (int32_t x, int32_t n) {
	ASSERT (n != 0);
	return x / n;
}
