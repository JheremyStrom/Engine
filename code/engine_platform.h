#if !defined(ENGINE_PLATFORM_H)

/*
 * Author: Jheremy Strom
 */

 /*
 * ENGINE_INTERNAL:
 * 0 - Build for public release
 * 1 - Build for developer only
 *
 * ENGINE_SLOW:
 * 0 - No slow code allowed
 * 1 - Slow code is allowed (can debug)
 */

#ifdef __cplusplus
extern "C" {
#endif

// Compilers
#if !defined(COMPILER_MSVC)
#define COMPILER_MSVC 0
#endif

#if !defined(COMPILER_LLVM)
#define COMPILER_LLVM 0
#endif

#if !COMPILER_MSVC && !COMPILER_LLVM
#if _MSC_VER
#undef COMPILER_MSVC
#define COMPILER_MSVC 1
#else
#undef COMPILER_LLVM
#define COMPILER_LLVM 1
#endif
#endif

#if COMPILER_MSVC
#include <intrin.h>
#endif

// TODO Implement sin
#include <stddef.h>
#include <stdint.h>

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef int32 bool32;  // Bool is intended with 0 or nonzero value

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef size_t memory_index;

typedef float real32;
typedef double real64;

#define internal static
#define local_persist static
#define global_variable static

#define Pi32 3.14159265359f

#define UInt32Max 0xFFFFFFFF

#if ENGINE_SLOW
#define Assert(Expression) if(!(Expression)) {*(int*)0 = 0;}
#else
#define Assert(Expression)
#endif

// TODO: sawp, min, max, ... macros
#define ArrayCount(Array) (sizeof(Array)/sizeof((Array)[0]))
#define Kilobytes(value) ((value)*1024LL)
#define Megabytes(value) (Kilobytes(value)*1024LL)
#define Gigabytes(value) (Megabytes(value)*1024LL)
#define Terabytes(value) (Gigabytes(value)*1024LL)

#define MSB(integer32) (integer32 >> 31)  // Most significant bit of an int32

 /* START Utility Functions */

inline uint32
SafeTruncateUInt64(uint64 pValue) {
	Assert(pValue <= UInt32Max);
	uint32 result = (uint32)pValue;
	return result;
}

typedef struct thread_context {
	int placeholder;
} thread_context;

/*
 * Services that the platform layer provides to the game
*/
#if ENGINE_INTERNAL
// IMPORTANT: These files are blocking and the write does not protect against lost data
struct debug_read_file_result {
	uint32 mContentsSize;
	void* mContents;
};

#define DEBUG_PLATFORM_READ_ENTIRE_FILE(name) debug_read_file_result name(thread_context* thread, char* pFilename)
typedef DEBUG_PLATFORM_READ_ENTIRE_FILE(debug_platform_read_entire_file);

#define DEBUG_PLATFORM_WRITE_ENTIRE_FILE(name) bool32 name(thread_context* thread, char* pFilename, uint32 pMemorySize, void* pMemory)
typedef DEBUG_PLATFORM_WRITE_ENTIRE_FILE(debug_platform_write_entire_file);

#define DEBUG_PLATFORM_FREE_FILE_MEMORY(name) void name(thread_context* thread, void* pBitmapMemory)
typedef DEBUG_PLATFORM_FREE_FILE_MEMORY(debug_platform_free_file_memory);

#endif

/*
Services that the game provides to the platform layer
*/

// Needs timing, controller/keyboard input, bitmap buffer to use, sound buffer to use
typedef struct game_offscreen_buffer {
	//  Not Needed, pixels are always 32-bit wide, Memory order BB GG RR XX
	void* mMemory;
	int mWidth;
	int mHeight;
	int mPitch;
	int mBytesPerPixel;
} game_offscreen_buffer;

typedef struct game_sound_output_buffer {
	int mSamplesPerSecond;
	int mSampleCount;
	int16* mSamples;
} game_sound_output_buffer;

typedef struct game_button_state {
	int mHalfTransitionCount;
	bool32 EndedDown;
} game_button_state;

typedef struct game_controller_input {
	bool32 IsConnected;
	bool32 IsAnalog;
	real32 mStickAverageX;
	real32 mStickAverageY;

	union {
		game_button_state mButtons[12];
		struct {
			game_button_state mMoveUp;
			game_button_state mMoveDown;
			game_button_state mMoveLeft;
			game_button_state mMoveRight;

			game_button_state mActionUp;
			game_button_state mActionDown;
			game_button_state mActionLeft;
			game_button_state mActionRight;

			game_button_state mLeftShoulder;
			game_button_state mRightShoulder;

			game_button_state mStart;
			game_button_state mBack;

			// All buttons must be added above this line
			game_button_state mTerminator;
		};
	};
} game_controller_input;

typedef struct game_input {
	game_button_state mouseButtons[5];
	int32 mouseX, mouseY, mouseZ;

	real32 deltaTime;

	game_controller_input mControllers[5];
} game_input;

typedef struct game_memory {
	bool32 IsInitialized;

	uint64 mPermanentStorageSize;
	void* mPermanentStorage;  // Must be cleared to zero at startup

	uint64 mTransientStorageSize;
	void* mTransientStorage;  // Must be cleared to zero at startup

	debug_platform_read_entire_file* DEBUGPlatformReadEntireFile;
	debug_platform_write_entire_file* DEBUGPlatformWriteEntireFile;
	debug_platform_free_file_memory* DEBUGPlatformFreeFileMemory;
} game_memory;

#define GAME_UPDATE_AND_RENDER(name) void name(thread_context* thread, game_memory* pMemory, game_input* pInput, game_offscreen_buffer* pScreenBuffer)
typedef GAME_UPDATE_AND_RENDER(game_update_and_render);

// This has to be a fast function, less than 1ms
#define GAME_GET_SOUND_SAMPLES(name) void name(thread_context* thread, game_memory* pMemory, game_sound_output_buffer* pSoundBuffer)
typedef GAME_GET_SOUND_SAMPLES(game_get_sound_samples);

inline game_controller_input* GetController(game_input* pInput, unsigned int pControllerIndex) {
	Assert(pControllerIndex < ArrayCount(pInput->mControllers));
	return &pInput->mControllers[pControllerIndex];
}

#ifdef __cplusplus
}
#endif

#define ENGINE_PLATFORM_H
#endif