
// 고정소수점 변환을 위한 비트 시프트 값
#define FRACTION (1 << 14)         

// 정수를 고정소수점으로 변환
#define I_TO_F(n) ((n) * FRACTION)

// 고정소수점을 정수로 변환
#define F_TO_I(x) (((x) >= 0) ? (((x) + FRACTION / 2) / FRACTION) : (((x) - FRACTION / 2) / FRACTION))

// 고정소수점 & 고정소수점 연산
#define ADD(x, y) ((x) + (y))
#define SUB(x, y) ((x) - (y))
#define MULTIPLY(x, y) (((int64_t)(x)) * (y) / FRACTION)
#define DIVIDE(x, y) (((int64_t)(x)) * FRACTION / (y))

// 정수 & 고정소수점 연산
#define ADD_INT(x, n) ((x) + I_TO_F(n))
#define SUB_INT(x, n) ((x) - I_TO_F(n))
#define MULTIPLY_INT(x, n) ((x) * (n))
#define DIVIDE_INT(x, n) ((x) / (n))