/*
 * Author: Jheremy Strom
 */

#include "engine.h"
#include "engine_tile.cpp"
#include "engine_random.h"

internal void
GameOutputSound(game_sound_output_buffer* soundBuffer, game_state* gameState, int toneHz) {
	int16 toneVolume = 3000;
	int wavePeriod = soundBuffer->mSamplesPerSecond / toneHz;

	int16* sampleOut = soundBuffer->mSamples;
	for (int sampleIndex = 0; sampleIndex < soundBuffer->mSampleCount; ++sampleIndex) {
		int16 sampleValue = 0;
		*sampleOut++ = sampleValue;
		*sampleOut++ = sampleValue;
	}
}

internal void
DrawBitmap(game_offscreen_buffer* buffer, loaded_bitmap* bitmap, real32 realX, real32 realY, int32 alignX = 0, int32 alignY = 0)) {
	realX -= (real32)alignX;
	realY -= (real32)alignY;

	int32 min = RoundReal32ToInt32(realX);
	int32 minY = RoundReal32ToInt32(realY);
	int32 maxX = RoundReal32ToInt32(realX + (real32)bitmap->mWidth);
	int32 maxY = RoundReal32ToInt32(realY + (real32)bitmap->mHeight);

	int32 sourceOffsetX = 0;
	if (min < 0) {
		sourceOffsetX = -minX;
		min = 0;
	}

	int32 sourceOffsetY = 0;
	if (minY < 0) {
		sourceOffsetY = -minY;
		minY = 0;
	}

	if (maxX > buffer->mWidth) {
		maxX = buffer->mWidth;
	}

	if (maxY > buffer->mHeight) {
		maxY = buffer->mHeight;
	}

	// TODO: SourceRow needs to be based on clipping.
	uint32* sourceRow = bitmap->mPixels + bitmap->mWidth*(bitmap->mHeight - 1);
	sourceRow += -sourceOffsetY * bitmap->mWidth + sourceOffsetX;
	uint8* destRow = ((uint8*)buffer->mMemory +
						minX * buffer->mBytesPerPixel +
						minY * buffer->mPitch);
	for (int Y = minY; Y < maxY; ++Y) {
		uint32* dest = (uint32*)destRow;
		uint32* source = sourceRow;

		for (int X = min; X < maxX; ++X) {
			real32 A = (real32)((*source >> 24) & 0xFF) / 255.0f;
			real32 SR = (real32)((*source >> 16) & 0xFF);
			real32 SG = (real32)((*source >> 8) & 0xFF);
			real32 SB = (real32)((*source >> 0) & 0xFF);

			real32 DR = (real32)((*dest >> 16) & 0xFF);
			real32 DG = (real32)((*dest >> 8) & 0xFF);
			real32 DB = (real32)((*dest >> 0) & 0xFF);

			real32 R = (1.0f - A)*DR + A * SR;
			real32 G = (1.0f - A)*DG + A * SG;
			real32 B = (1.0f - A)*DB + A * SB;

			*dest = (((uint32)(R + 0.5f) << 16) |
					((uint32)(G + 0.5f) << 8) |
					((uint32)(B + 0.5f) << 0));

			++dest;
			++source;
		}

		destRow += buffer->mPitch;
		sourceRow -= bitmap->mWidth;
	}
}

//internal void
//RenderPlayer(game_offscreen_buffer* pBuffer, int playerX, int playerY) {
//	uint8* endOfBuffer = (uint8*)pBuffer->mMemory + pBuffer->mPitch*pBuffer->mHeight;
//	uint32 color = 0xFFFFFFFF;
//	int top = playerY;
//	int bottom = playerY + 10;
//	for (int X = playerX; X < playerX + 10; ++X) {
//		uint8* pixel = (uint8*)pBuffer->mMemory + X * pBuffer->mBytesPerPixel + top * pBuffer->mPitch;
//		for (int y = top; y < bottom; ++y) {
//			if ((pixel >= pBuffer->mMemory) && ((pixel + 4) <= endOfBuffer)) {
//				*(uint32*)pixel = color;
//			}
//			pixel += pBuffer->mPitch;
//		}
//	}
//}

internal void
DrawRectangle(game_offscreen_buffer* pBuffer,
	Vector2 min, Vector2 max,
	real32 R, real32 G, real32 B) {

	int32 minX = RoundReal32ToInt32(min.x);
	int32 minY = RoundReal32ToInt32(min.y);
	int32 maxX = RoundReal32ToInt32(max.x);
	int32 maxY = RoundReal32ToInt32(max.y);

	if (minX < 0) {
		minX = 0;
	}
	if (minY < 0) {
		minY = 0;
	}
	if (maxX > pBuffer->mWidth) {
		maxX = pBuffer->mWidth;
	}
	if (maxY > pBuffer->mHeight) {
		maxY = pBuffer->mHeight;
	}

	// Bit Pattern: 0x AA RR GG BB
	uint32 color = ((RoundReal32ToUInt32(R*255.0f) << 16) |
					(RoundReal32ToUInt32(G*255.0f) << 8) |
					(RoundReal32ToUInt32(B*255.0f) << 0));

	uint8* row = ((uint8*)pBuffer->mMemory + minX * pBuffer->mBytesPerPixel + minY * pBuffer->mPitch);
	for (int y = minY; y < maxY; ++y) {
		uint32* pixel = (uint32*)row;
		for (int X = minX; X < maxX; ++X) {
			*pixel++ = color;
		}
		row += pBuffer->mPitch;
	}
}

#pragma pack(push, 1)
struct bitmap_header {
	uint16 mFileType;
	uint32 mFileSize;
	uint16 mReserved1;
	uint16 mReserved2;
	uint32 mBitmapOffset;
	uint32 mSize;
	int32 mWidth;
	int32 mHeight;
	uint16 mPlanes;
	uint16 mBitsPerPixel;
	uint32 mCompression;
	uint32 mSizeOfBitmap;
	int32 mHorzResolution;
	int32 mVertResolution;
	uint32 mColorsUsed;
	uint32 mColorsImportant;

	uint32 mRedMask;
	uint32 mGreenMask;
	uint32 mBlueMask;
};
#pragma pack(pop)

// TODO: Create a robust BMP loader
internal loaded_bitmap
DEBUGLoadBMP(thread_context* thread, debug_platform_read_entire_file* readEntireFile, char* fileName) {
	loaded_bitmap result;
	//MemoryZeroClear(result, sizeof(loaded_bitmap));

	// Byte order in memory is AA BB GG RR, bottom up.
	// In little endian -> 0xRRGGBBAA

	debug_read_file_result readResult = readEntireFile(thread, fileName);
	if (readResult.mContentsSize != 0) {
		bitmap_header* header = (bitmap_header*)readResult.mContents;
		uint32* pixels = (uint32*)((uint8*)readResult.mContents + header->mBitmapOffset);
		result.mPixels = pixels;
		result.mWidth = header->mWidth;
		result.mHeight = header->mHeight;

		// Remeber that BMP files can go in either direction and the height will be negative
		// for top down. There is also compression
		uint32 redMask = header->mRedMask;
		uint32 greenMask = header->mGreenMask;
		uint32 blueMask = header->mBlueMask;
		uint32 alphaMask = ~(redMask | greenMask | blueMask);

		bit_scan_result redScan = FindLeastSignificantSetBit(redMask);
		bit_scan_result greenScan = FindLeastSignificantSetBit(greenMask);
		bit_scan_result blueScan = FindLeastSignificantSetBit(blueMask);
		bit_scan_result alphaScan = FindLeastSignificantSetBit(alphaMask);

		int32 redShift = 16 - (int32)redScan.mIndex;
		int32 greenShift = 8 - (int32)greenScan.mIndex;
		int32 blueShift = 0 - (int32)blueScan.mIndex;
		int32 alphaShift = 24 - (int32)alphaScan.mIndex;

		uint32 *sourceDest = pixels;
		for (int32 Y = 0; Y < header->mHeight; ++Y) {
			for (int32 X = 0; X < header->mWidth; ++X) {
				uint32 z = *sourceDest;

				*sourceDest++ = (RotateLeft(z & redMask, redShift) |
								RotateLeft(z & greenMask, greenShift) |
								RotateLeft(z & blueMask, blueShift) |
								RotateLeft(z & alphaMask, alphaShift));
			}
		}
	}

	return result;
}

inline entity*
GetEntity(game_state* gameState, uint32 index) {
	entity* entity = 0;

	if ((Index > 0) && (index < ArrayCount(gameState->mEntities))) {
		entity = &gameState->mEntities[index];
	}

	return entity;
}

internal uint32
AddEntity(game_state* gameState) {
	uint32 entityIndex = gameState->mEntityCount++;

	Assert(gameState->mEntityCount < ArrayCount(gameState->mEntities));
	entity* ent = &gameState->mEntities[entityIndex];
	*ent = NULL;

	return entityIndex;
}

internal void
InitializePlayer(game_state* gameState, uint32 entityIndex) {
	entity* ent = GetEntity(gameState, entityIndex);

	ent->mExists = true;
	ent->mTilePos.mAbsTileX = 1;
	ent->mTilePos.mAbsTileY = 3;
	ent->mTilePos.mOffset.x = 0;
	ent->mTilePos.mOffset.t = 0;
	ent->mHeight = 1.0f;
	ent->mWidth = 1.0f;

	if (!GetEntity(gameState, gameState->mCameraEntityIndex)) {
		gameState->mCameraEntityIndex = entityIndex;
	}
}

internal bool32
TestWall(real32 wallX, real32 relX, real32 relY, real32 playerDeltaX, real32 playerDeltaY,
	real32* tMin, real32 minY, real32 maxY) {
	bool32 hit = false;

	// TODO: Test if the player hit the wall
	return hit;
}

internal void
MovePlayer(game_state* gameState, entity* entity, real32 deltaTime, Vector2 ddP) {
	// TODO: Move the player
}

extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender) {
	Assert((&pInput->mControllers[0].mTerminator - &pInput->mControllers[0].mButtons[0]) == (ArrayCount(pInput->mControllers[0].mButtons)));
	Assert(sizeof(game_state) <= pMemory->mPermanentStorageSize);

	real32 playerHeight = 1.4f;
	real32 playerWidth = 0.75f*playerHeight;

	// Initialize the game state
	game_state* gameState = (game_state*)pMemory->mPermanentStorage;
	if (!pMemory->IsInitialized) {
		AddEntity(gameState);

		/*gameState->mBackdrop =
			DEBUGLoadBMP(thread, pMemory->DEBUGPlatformReadEntireFile, "test/test_background.bmp");*/

		gameState->cameraP.mAbsTileX = 17 / 2;
		gameState->cameraP.mAbsTileY = 9 / 2;

		InitializeArena(&gameState->mWorldArena, pMemory->mPermanentStorageSize - sizeof(game_state),
			(uint8*)pMemory->mPermanentStorage + sizeof(game_state));

		gameState->mWorld = PushStruct(&gameState->mWorldArena, world);
		world* world = gameState->mWorld;
		world->mTileMap = PushStruct(&gameState->mWorldArena, tile_map);

		tile_map* tileMap = world->mTileMap;

		// 256x256 tile chunks
		tileMap->mChunkShift = 4;
		tileMap->mChunkMask = (1 << tileMap->mChunkShift) - 1;
		tileMap->mChunkDim = (1 << tileMap->mChunkShift);

		tileMap->mTileChunkCountX = 128;
		tileMap->mTileChunkCountY = 128;
		tileMap->mTileChunkCountZ = 2;
		tileMap->mTileChunks = PushArray(&gameState->mWorldArena,
			tileMap->mTileChunkCountX*
			tileMap->mTileChunkCountY*
			tileMap->mTileChunkCountZ,
			tile_chunk);

		tileMap->mTileSideInMeters = 1.4f;

		uint32 randomNumberIndex = 0;
		uint32 tilesPerWidth = 17;
		uint32 tilesPerHeight = 9;
		uint32 screenX = 0;
		uint32 screenY = 0;
		uint32 absTileZ = 0;

		bool32 doorLeft = false;
		bool32 doorRight = false;
		bool32 doorTop = false;
		bool32 doorBottom = false;
		bool32 doorUp = false;
		bool32 doorDown = false;
		for (uint32 screenIndex = 0; screenIndex < 100; ++screenIndex) {
			Assert(randomNumberIndex < ArrayCount(randomNumberTable));

			uint32 randomChoice;
			if (doorUp || doorDown) {
				randomChoice = randomNumberTable[randomNumberIndex++] % 2;
			}
			else {
				randomChoice = randomNumberTable[randomNumberIndex++] % 3;
			}

			bool32 createdZDoor = false;
			if (randomChoice == 2) {
				createdZDoor = true;
				if (absTileZ == 0) {
					doorUp = true;
				}
				else {
					doorDown = true;
				}
			}
			else if (randomChoice == 1) {
				doorRight = true;
			}
			else {
				doorTop = true;
			}

			for (uint32 tileY = 0; tileY < tilesPerHeight; ++tileY) {
				for (uint32 tileX = 0; tileX < tilesPerWidth; ++tileX) {
					uint32 absTileX = screenX * tilesPerWidth + tileX;
					uint32 absTileY = screenY * tilesPerHeight + tileY;

					uint32 tileValue = 1;
					if ((tileX == 0) && (!doorLeft || (tileY != (tilesPerHeight / 2)))) {
						tileValue = 2;
					}

					if ((tileX == (tilesPerWidth - 1)) && (!doorRight || (tileY != (tilesPerHeight / 2)))) {
						tileValue = 2;
					}

					if ((tileY == 0) && (!doorBottom || (tileX != (tilesPerWidth / 2)))) {
						tileValue = 2;
					}

					if ((tileY == (tilesPerHeight - 1)) && (!doorTop || (tileX != (tilesPerWidth / 2)))) {
						tileValue = 2;
					}

					if ((tileX == 10) && (tileY == 6)) {
						if (doorUp) {
							tileValue = 3;
						}

						if (doorDown) {
							tileValue = 4;
						}
					}

					SetTileValue(&gameState->mWorldArena, world->mTileMap, absTileX, absTileY, absTileZ,
						tileValue);
				}
			}

			doorLeft = doorRight;
			doorBottom = doorTop;

			if (createdZDoor) {
				doorDown = !doorDown;
				doorUp = !doorUp;
			}
			else {
				doorUp = false;
				doorDown = false;
			}

			doorRight = false;
			doorTop = false;

			if (randomChoice == 2) {
				if (absTileZ == 0) {
					absTileZ = 1;
				}
				else {
					absTileZ = 0;
				}
			}
			else if (randomChoice == 1) {
				screenX += 1;
			}
			else {
				screenY += 1;
			}
		}

		pMemory->IsInitialized = true;
	}

	world* world = gameState->mWorld;
	tile_map* tileMap = world->mTileMap;

	int32 tileSideInPixels = 60;
	real32 metersToPixels = (real32)tileSideInPixels / (real32)tileMap->mTileSideInMeters;

	real32 lowerLeftX = -(real32)tileSideInPixels / 2;
	real32 lowerLeftY = (real32)pScreenBuffer->mHeight;

	for (int controllerIndex = 0; controllerIndex < ArrayCount(pInput->mControllers); ++controllerIndex) {
		game_controller_input* controller = GetController(pInput, controllerIndex);
		entity* controllingEntity = GetEntity(gameState,
			gameState->mPlayerIndexForController[controllerIndex]);
		if (controllingEntity) {
			Vector2 accel;
			if (controller->IsAnalog) {
				// Use analog movement
				accel = Vector2(controller->mStickAverageX, controller->mStickAverageY);
			}
			else {
				// Use digital movement

				if (controller->mMoveUp.EndedDown) {
					dPlayerY = 1.0f;
				}
				if (controller->mMoveDown.EndedDown) {
					dPlayerY = -1.0f;
				}
				if (controller->mMoveLeft.EndedDown) {
					dPlayerX = -1.0f;
				}
				if (controller->mMoveRight.EndedDown) {
					dPlayerX = 1.0f;
				}

				MovePlayer(gameState, controllingEntity, pInput->dtForFrame, accel);
			}
		}
		else {
			if (controller->mStart.EndedDown) {
				uint32 entityIndex = AddEntity(gameState);
				InitializePlayer(gameState, entityIndex);
				gameState->mPlayerIndexForController[controllerIndex] = entityIndex;
			}
		}
	}

	entity* cameraFollowingEntity = GetEntity(gameState, gameState->mCameraFollowingEntityIndex);
	if (cameraFollowingEntity) {
		gameState->cameraP.AbsTileZ = cameraFollowingEntity->mPos.AbsTileZ;

		tile_map_difference diff = Subtract(tileMap, &cameraFollowingEntity->mPos, &gameState->cameraP);
		if (diff.mVector.x > (9.0f*tileMap->mTileSideInMeters)) {
			gameState->cameraP.mAbsTileX += 17;
		}
		if (diff.mVector.x < -(9.0f*tileMap->mTileSideInMeters)) {
			gameState->cameraP.mAbsTileX -= 17;
		}
		if (diff.mVector.y > (5.0f*tileMap->mTileSideInMeters)) {
			gameState->cameraP.mAbsTileY += 9;
		}
		if (diff.mVector.y < -(5.0f*tileMap->mTileSideInMeters)) {
			gameState->cameraP.mAbsTileY -= 9;
		}
	}

	// Render
	DrawBitmap(pScreenBuffer, &gameState->mBackdrop, 0, 0);

	real32 screenCenterX = 0.5f*(real32)pScreenBuffer->mWidth;
	real32 screenCenterY = 0.5f*(real32)pScreenBuffer->mHeight;

	for (int32 relRow = -10; relRow < 10; ++relRow) {
		for (int32 relColumn = -20; relColumn < 20; ++relColumn) {
			uint32 column = relColumn + gameState->cameraP.mAbsTileX;
			uint32 row = relRow + gameState->cameraP.mAbsTileY;
			uint32 tileID = GetTileValue(tileMap, column, row, gameState->cameraP.mAbsTileZ);
			if (tileID > 1) {
				real32 gray = 0.5f;
				if (tileID == 2) {
					gray = 1.0f;
				}
				if (tileID > 2) {
					gray = 0.25f;
				}
				if ((column == gameState->cameraP.mAbsTileX) &&
					(row == gameState->cameraP.mAbsTileY)) {
					gray = 0.0f;
				}

				Vector2 tileSide(0.5f*TileSideInPixels, 0.5f*TileSideInPixels);
				Vector2 cen(screenCenterX - MetersToPixels * gameState->cameraP.mOffset.x + ((real32)RelColumn)*TileSideInPixels,
						  screenCenterY + MetersToPixels * gameState->cameraP.mOffset.y - ((real32)RelRow)*TileSideInPixels);
				Vector2 Min = cen - 0.9f*tileSide;
				Vector2 Max = cen + 0.9f*tileSide;
				DrawRectangle(pScreenBuffer, Min, Max, gray, gray, gray);
			}
		}
	}

	entity* entity = gameState->Entities;
	for (uint32 entityIndex = 0; entityIndex < gameState->mEntityCount; ++entityIndex, ++entity) {
		if (entity->Exists) {
			tile_map_difference diff = Subtract(tileMap, &entity->mPos, &gameState->cameraP);

			real32 PlayerR = 1.0f;
			real32 PlayerG = 1.0f;
			real32 PlayerB = 0.0f;
			real32 PlayerGroundPointX = screenCenterX + MetersToPixels * diff.mVector.x;
			real32 PlayerGroundPointY = screenCenterY - MetersToPixels * diff.mVector.y;
			Vector2 PlayerLeftTop(PlayerGroundPointX - 0.5f*MetersToPixels*entity->mWidth,
								PlayerGroundPointY - 0.5f*MetersToPixels*entity->mHeight);
			Vector2 entityWidthHeight(entity->mWidth, entity->mHeight);
			DrawRectangle(pScreenBuffer,
				PlayerLeftTop,
				PlayerLeftTop + MetersToPixels * entityWidthHeight,
				PlayerR, PlayerG, PlayerB);
		}
	}
}

#define TONEHZ 400
extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples) {
	game_state* gameState = (game_state*)pMemory->mPermanentStorage;
	GameOutputSound(pSoundBuffer, gameState, TONEHZ);
}