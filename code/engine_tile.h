#if !defined(ENGINE_TILE_H)

/*
 * Author: Jheremy Strom
 */

struct tile_map_difference {
	Vector3 mVector;
};


struct tile_map_location {
	// These are fixed point tile locations
	// The high bits are the tile chunk index
	// The low bits are the tile index in the chunk
	uint32 mAbsTileX;
	uint32 mAbsTileY;
	uint32 mAbsTileZ;

	// Offsets from the tile center
	Vector2 mOffset;
};

struct tile_chunk {
	uint32* mTiles;
};

struct tile_chunk_location {
	uint32 mTileChunkX;
	uint32 mTileChunkY;
	uint32 mTileChunkZ;

	uint32 mRelTileX;
	uint32 mRelTileY;
};

struct tile_map {
	uint32 mChunkShift;
	uint32 mChunkMask;
	uint32 mChunkDim;

	real32 mTileSideInMeters;

	uint32 mTileChunkCountX;
	uint32 mTileChunkCountY;
	uint32 mTileChunkCountZ;

	tile_chunk* mTileChunks;
};

#define ENGINE_TILE_H
#endif