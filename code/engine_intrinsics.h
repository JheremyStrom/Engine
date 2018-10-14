#if !defined(ENGINE_INTRINSICS_H)

/*
 * Author: Jheremy Strom
 */

#include "math.h"

// Make all functions platform-efficient versions and do not use math.h

inline uint32
RotateLeft(uint32 value, int32 shift) {
	return _rotl(value, shift);
}

inline uint32
RotateRight(uint32 value, int32 shift) {
	return _rotr(value, shift);
}

inline uint32
RoundReal32ToUInt32(real32 Real32) {
	// TODO: Find intrinsic
	uint32 result = (uint32)roundf(Real32);
	return result;
}

inline int32
RoundReal32ToInt32(real32 Real32) {
	// TODO: Find intrinsic
	int32 result = (int32)roundf(Real32);
	return result;
}

inline int32
FloorReal32ToInt32(real32 Real32) {
	// TODO: Find intrinsic
	int32 result = (int32)floorf(Real32);
	return result;
}

inline int32
TruncateReal32ToInt32(real32 Real32) {
	// TODO: Find intrinsic
	int32 result = (int32)(Real32);
	return result;
}

inline real32
Sin(real32 angle) {
	real32 result = sinf(angle);
	return result;
}

inline real32
Cos(real32 angle) {
	real32 result = cosf(angle);
	return result;
}

inline real32
ATan2(real32 y, real32 x) {
	real32 result = atan2f(y, x);
	return result;
}

inline int32
SignOf(int32 value) {
	return MSB(value) ? 1 : -1;
}

struct bit_scan {
	bool32 mFound;
	uint32 mIndex;
};

// Find the east significant bit that is set, manually or through instrinsics
inline bit_scan
FindLeastSignificantSetBit() {
	bit_scan result;
	ZeroMemory(result, sizeof(bit_scan));

#if COMPILER_MSVC
	result.mFound = _BitScanForward((unsigned long *)&result.mIndex, value);
#else
	for (uint32 i = 0; i < 32; ++i) {
		if (value & (1 << i)) {
			result.mIndex = i;
			result.mFound = true;
			break;
		}
	}
}

#define ENGINE_INTRINSICS_H
#endif