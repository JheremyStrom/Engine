/*
 * Author: Jheremy Strom
 */

inline tile_chunk*
GetTileChunk(tile_map* tileMap, uint32 tileChunkX, uint32 tileChunkY, uint32 tileChunkZ) {
	tile_chunk* tileChunk = 0;
	if ((tileChunkX >= 0) && (tileChunkX < tileMap->mTileChunkCountX) &&
		(tileChunkY >= 0) && (tileChunkY < tileMap->mTileChunkCountY) &&
		(tileChunkZ >= 0) && (tileChunkZ < tileMap->mTileChunkCountZ))
	{
		tileChunk = &tileMap->mTileChunks[
			tileChunkZ*tileMap->mTileChunkCountY*tileMap->mTileChunkCountX +
				tileChunkY * tileMap->mTileChunkCountX +
				tileChunkX];
	}
	return tileChunk;
}

inline uint32
GetTileValueUnchecked(tile_map* tileMap, tile_chunk* tileChunk, uint32 tileX, uint32 tileY) {
	Assert(tileChunk);
	Assert(tileX < tileMap->mChunkDim);
	Assert(tileY < tileMap->mChunkDim);

	uint32 tileChunkValue = tileChunk->mTiles[tileY*tileMap->mChunkDim + tileX];
	return tileChunkValue;
}

inline void
SetTileValueUnchecked(tile_map* tileMap, tile_chunk* tileChunk, uint32 tileX, uint32 tileY, uint32 tileValue) {
	Assert(tileChunk);
	Assert(tileX < tileMap->mChunkDim);
	Assert(tileY < tileMap->mChunkDim);

	tileChunk->mTiles[tileY*tileMap->mChunkDim + tileX] = tileValue;
}

inline uint32
GetTileValue(tile_map* tileMap, tile_chunk* tileChunk, uint32 testTileX, uint32 testTileY) {
	uint32 tileChunkValue = 0;

	if (tileChunk && tileChunk->mTiles) {
		tileChunkValue = GetTileValueUnchecked(tileMap, tileChunk, testTileX, testTileY);
	}

	return tileChunkValue;
}

inline void
SetTileValue(tile_map* tileMap, tile_chunk* tileChunk, uint32 testTileX, uint32 testTileY, uint32 tileValue) {
	if (tileChunk && tileChunk->mTiles) {
		SetTileValueUnchecked(tileMap, tileChunk, testTileX, testTileY, tileValue);
	}
}

inline tile_chunk_location
GetChunkLocationFor(tile_map* tileMap, uint32 absTileX, uint32 absTileY, uint32 absTileZ) {
	tile_chunk_location result;

	result.mTileChunkX = absTileX >> tileMap->mChunkShift;
	result.mTileChunkY = absTileY >> tileMap->mChunkShift;
	result.mTileChunkZ = absTileZ;
	result.mRelTileX = absTileX & tileMap->mChunkMask;
	result.mRelTileY = absTileY & tileMap->mChunkMask;

	return result;
}

internal uint32
GetTileValue(tile_map* tileMap, uint32 absTileX, uint32 absTileY, uint32 absTileZ) {
	tile_chunk_location chunkLoc = GetChunkLocationFor(tileMap, absTileX, absTileY, absTileZ);
	tile_chunk* tileChunk = GetTileChunk(tileMap, chunkLoc.mTileChunkX, chunkLoc.mTileChunkY, chunkLoc.mTileChunkZ);
	uint32 tileChunkValue = GetTileValue(tileMap, tileChunk, chunkLoc.mRelTileX, chunkLoc.mRelTileY);

	return tileChunkValue;
}

internal uint32
GetTileValue(tile_map* tileMap, tile_map_location loc)
{
	uint32 tileChunkValue = GetTileValue(tileMap, loc.mAbsTileX, loc.mAbsTileY, loc.mAbsTileZ);

	return tileChunkValue;
}

internal bool32
IsTileMapPointEmpty(tile_map* tileMap, tile_map_location canLoc) {
	uint32 tileChunkValue = GetTileValue(tileMap, canLoc);
	bool32 isEmpty = ((tileChunkValue == 1) ||
						(tileChunkValue == 3) ||
						(tileChunkValue == 4));

	return isEmpty;
}

internal void
SetTileValue(memory_areana* arena, tile_map* tileMap, uint32 absTileX, uint32 absTileY, uint32 absTileZ, uint32 tileValue) {
	tile_chunk_location chunkLoc = GetChunkLocationFor(tileMap, absTileX, absTileY, absTileZ);
	tile_chunk* tileChunk = GetTileChunk(tileMap, chunkLoc.mTileChunkX, chunkLoc.mTileChunkY, chunkLoc.mTileChunkZ);

	Assert(tileChunk);
	if (!tileChunk->mTiles) {
		uint32 tileCount = tileMap->mChunkDim*tileMap->mChunkDim;
		tileChunk->mTiles = PushArray(arena, tileCount, uint32);

		for (uint32 tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
			tileChunk->mTiles[tileIndex] = 1;
		}
	}

	SetTileValue(tileMap, tileChunk, chunkLoc.mRelTileX, chunkLoc.mRelTileY, tileValue);
}

inline void
RecanonicalizeCoord(tile_map* tileMap, uint32* tile, real32* tileRelative) {
	// Toroidal topology
	int32 offset = RoundReal32ToInt32(*tileRelative / tileMap->mTileSideInMeters);
	*tile += offset;
	*tileRelative -= offset * tileMap->mTileSideInMeters;

	// TODO: Fix floating point math to make < ?
	Assert(*tileRelative >= -0.5f*tileMap->mTileSideInMeters);
	Assert(*tileRelative <= 0.5f*tileMap->mTileSideInMeters);
}

inline tile_map_location
RecanonicalizeLocation(tile_map* tileMap, tile_map_location loc) {
	tile_map_location result = loc;

	RecanonicalizeCoord(tileMap, &result.mAbsTileX, &result.mOffsetX);
	RecanonicalizeCoord(tileMap, &result.mAbsTileY, &result.mOffsetY);

	return result;
}

inline bool32
AreOnSameTile(tile_map_location* x, tile_map_location* y) {
	bool32 result = ((x->mAbsTileX == y->mAbsTileX) &&
					(x->mAbsTileY == y->mAbsTileY) &&
					(x->mAbsTileZ == y->mAbsTileZ));

	return result;
}

inline tile_map_difference
Subtract(tile_map* TileMap, tile_map_location* x, tile_map_location* y) {
	tile_map_difference result;

	Vector2 dTileXY = { (real32)x->mAbsTileX - (real32)y->mAbsTileX,
				   (real32)x->mAbsTileY - (real32)y->mAbsTileY };

	real32 dTileZ = (real32)x->mAbsTileZ - (real32)y->mAbsTileZ;

	Vector2 temp = TileMap->TileSideInMeters*dTileXY + (x->mOffset - y->mOffset);

	result.mVector.x = temp.x;
	result.mVector.y = temp.y;
	result.mVector.z = tileMap->mTileSideInMeters*dTileZ;

	return result;
}

inline tile_map_location
CenteredTilePoint(uint32 absTileX, uint32 absTileY, uint32 absTileZ) {
	tile_map_location result = {};

	result.mAbsTileX = absTileX;
	result.mAbsTileY = absTileY;
	result.mAbsTileZ = absTileZ;

	return result;
}

inline tile_map_location
Offset(tile_map* tileMap, tile_map_location p, Vector2 offset) {
	p.mOffset += offset;
	p = RecanonicalizePosition(tileMap, p);

	return p;
}