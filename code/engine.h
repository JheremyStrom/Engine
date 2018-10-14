#if !defined(ENGINE_H)

/*
 * Author: Jheremy Strom
 */

#include "engine_platform.h"


#define Minimum(A, B) ((A < B) ? (A) : (B))
#define Maximum(A, B) ((A > B) ? (A) : (B))

//internal void
//GameOutputSound(game_sound_output_buffer* soundBuffer);

struct memory_areana {
	memory_index mSize;
	uint8* mBase;
	memory_index mUsed;
};

internal void
InitializeArena(memory_areana* arena, memory_index size, uint8* base) {
	arena->mSize = size;
	arena->mBase = base;
	arena->mUsed = 0;
}

#define PushStruct(arena, type) (type*)PushSize_(arena, sizeof(type))
#define PushArray(arena, count, type) (type*)PushSize_(arena, (count)*sizeof(type))
void*
PushSize_(memory_areana* arena, memory_index size) {

	Assert((arena->mUsed + size) <= arena->mSize);
	void* result = arena->mBase + arena->mUsed;
	arena->mUsed += size;
	return result;
}

#define MemoryClear(memory, size, value) (void)MemoryClear_((void*)memory, size, value)
#define MemoryZeroClear(memory, size) (void)MemoryClear_((void*)memory, size, 0)
void
MemoryClear_(void* memory, memory_index size, int32 value) {
	// TODO: memset(memory, value, size);
}

#include "engine_intrinsics.h"
#include "engine_math.h"
#include "engine_tile.h"

struct world {
	tile_map* mTileMap;
};

struct loaded_bitmap
{
	int32 mWidth;
	int32 mHeight;
	uint32* mPixels;
};

struct entity {
	bool32 mExists;
	tile_map_location mTilePos;
	Vector2 mPos;
	uint32 mDir;
	real32 mWidth, mHeight;
};

struct game_state {
	memory_areana mWorldArena;
	world* mWorld;

	uint32 mCameraEntityIndex;
	tile_map_location cameraP;

	uint32 mPlayerIndexForController[ArrayCount(((game_input *)0)->Controllers)];
	uint32 mEntityCount;
	entity mEntities[256];

	loaded_bitmap mBackdrop;
};

#define ENGINE_H
#endif