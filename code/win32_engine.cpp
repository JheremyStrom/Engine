/*
 * Author: Jheremy Strom
 */

#include "engine.h"

#include <malloc.h>
#include <windows.h>
#include <xinput.h>
#include <dsound.h>
// C runtime library
#include <stdio.h>

#include "win32_engine.h"

#define KeyMessageWasDownBit (1 << 30)
#define KeyMessageIsDownBit (1 << 31)
#define KeyMessageAltKeyDown (1 << 29)
#define DesiredSchedulerMS 1

// TODO: This is a global for now
global_variable bool32 gRunning;
global_variable bool32 gPause;
global_variable win32_offscreen_buffer gBackBuffer;
global_variable LPDIRECTSOUNDBUFFER gSecondaryBuffer;
global_variable int64 gPerfCountFrequency;
global_variable bool32 DEBUGgShowCursor;
global_variable WINDOWPLACEMENT gWindowPosition = { sizeof(gWindowPosition) };

/* START Utility Functions */

// Concatenate two C-strings
internal void
CatStrings(size_t sourceACount, char* sourceA,
			size_t sourceBCount, char* sourceB,
			size_t destCount, char* dest) {

	// Dest bounds checking
	for (int index = 0; index < sourceACount; ++index) {
		*dest++ = *sourceA++;
	}
	for (int index = 0; index < sourceBCount; ++index) {
		*dest++ = *sourceB++;
	}
	*dest++ = 0;
}

// Return the length of a C-string
internal int
StringLength(char* pString) {
	int count = 0;
	while (*pString++) {
		++count;
	}
	return count;
}

// Get the name of the EXE
internal void
Win32GetEXEFilename(win32_state* state) {
	DWORD sizeOfFilename = GetModuleFileNameA(0, state->EXEFilename, sizeof(state->EXEFilename));
	state->onePastLastEXEFilenameSlash = state->EXEFilename;
	for (char* scan = state->EXEFilename; *scan; ++scan) {
		if (*scan == '\\') {
			state->onePastLastEXEFilenameSlash = scan + 1;
		}
	}
}

internal void
Win32BuildEXEPathFilename(win32_state* state, char* filename, int destSize, char* dest) {
	CatStrings(state->onePastLastEXEFilenameSlash - state->EXEFilename, state->EXEFilename,
		StringLength(filename), filename,
		destSize, dest);
}

/* END Utility Functions*/

/* START File I/O */

// Free memory in a certain thread
DEBUG_PLATFORM_FREE_FILE_MEMORY(DEBUGPlatformFreeFileMemory) {
	if (pBitmapMemory) {
		VirtualFree(pBitmapMemory, 0, MEM_RELEASE);
	}
}

// Read everything into memory
DEBUG_PLATFORM_READ_ENTIRE_FILE(DEBUGPlatformReadEntireFile) {
	debug_read_file_result result;
	ZeroMemory(&result, sizeof(debug_read_file_result));

	HANDLE fileHandle = CreateFileA(pFilename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if (fileHandle != INVALID_HANDLE_VALUE) {
		LARGE_INTEGER fileSize;
		if (GetFileSizeEx(fileHandle, &fileSize)) {
			uint32 fileSize32 = SafeTruncateUInt64(fileSize.QuadPart);
			// Should not use a lot of VAlloc calls because minimum chuck allocation. Use HeapAlloc instead
			result.mContents = VirtualAlloc(0, fileSize32, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
			if (result.mContents) {
				// Cannot read a 64bit file
				DWORD bytesRead;
				if (ReadFile(fileHandle, result.mContents, fileSize32, &bytesRead, 0) &&
					(fileSize32 == bytesRead)) {
					// File read successfully
					result.mContentsSize = fileSize32;
				}
				else {
					DEBUGPlatformFreeFileMemory(thread, result.mContents);
					result.mContents = 0;
				}
			}
			else {
				// TODO: Could not allocate enough memory for the size of the file
			}
		}
		else {
			// TODO: Could not get the size of the file
		}

		CloseHandle(fileHandle);
	}
	else {
		// Could not open the file
	}

	return result;
}

// Write all memory to file
DEBUG_PLATFORM_WRITE_ENTIRE_FILE(DEBUGPlatformWriteEntireFile) {
	bool32 result = false;

	HANDLE fileHandle = CreateFileA(pFilename, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
	if (fileHandle != INVALID_HANDLE_VALUE) {
		// Cannot write to a 64bit file
		DWORD bytesWritten;
		if (WriteFile(fileHandle, pMemory, pMemorySize, &bytesWritten, 0)) {
			// File wrote to successfully
			result = (bytesWritten == pMemorySize);
		}
		else {
			// TODO: Logging
		}

		CloseHandle(fileHandle);
	}
	else {
		// Could not write to the file
	}

	return result;
}

/* END File I/O */

/* START Dynamically linking the platform independent code */

// Get the last time the file was written to
inline FILETIME Win32GetLastWriteTime(char* filename) {
	FILETIME lastWriteTime;
	ZeroMemory(&lastWriteTime, sizeof(lastWriteTime));

	WIN32_FILE_ATTRIBUTE_DATA data;
	ZeroMemory(&data, sizeof(data));
	if (GetFileAttributesEx(filename, GetFileExInfoStandard, &data)) {
		lastWriteTime = data.ftLastWriteTime;
	}

	return lastWriteTime;
}

// Load in the DLL containing the game code
internal win32_game_code
Win32LoadGameCode(char* sourceDLLName, char* tempDLLName, char* lockFilename) {
	win32_game_code result;
	ZeroMemory(&result, sizeof(result));

	WIN32_FILE_ATTRIBUTE_DATA ignored;
	if (!GetFileAttributesEx(lockFilename, GetFileExInfoStandard, &ignored)) {
		result.DLLLastWriteTime = Win32GetLastWriteTime(sourceDLLName);

		CopyFile(sourceDLLName, tempDLLName, FALSE);
		result.gameCodeDLL = LoadLibraryA(tempDLLName);
		// Setup function pointers
		if (result.gameCodeDLL) {
			result.updateAndRender =
				(game_update_and_render*)GetProcAddress(result.gameCodeDLL, "GameUpdateAndRender");
			result.getSoundSamples =
				(game_get_sound_samples*)GetProcAddress(result.gameCodeDLL, "GameGetSoundSamples");

			result.isValid = (result.updateAndRender && result.getSoundSamples);
		}
	}

	if (!result.isValid) {
		result.updateAndRender = 0;
		result.getSoundSamples = 0;
	}

	return result;
}

// Unload  the game code (reset function pointers)
internal void
Win32UnloadGameCode(win32_game_code* gameCode) {
	if (gameCode->gameCodeDLL) {
		FreeLibrary(gameCode->gameCodeDLL);
		gameCode->gameCodeDLL = 0;
	}
	gameCode->isValid = false;
	gameCode->updateAndRender = 0;
	gameCode->getSoundSamples = 0;
}


/* END Dynamically linking the platform independent code */

/* START Controller Support */

// Support for XInputGetState
#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE* pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub) {
	return ERROR_DEVICE_NOT_CONNECTED;
}
global_variable x_input_get_state* XInputGetState_ = XInputGetStateStub;
#define XInputGetState (XInputGetState_)

// Support for XInputSetState
#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub) {
	return ERROR_DEVICE_NOT_CONNECTED;
}
global_variable x_input_set_state* XInputSetState_ = XInputSetStateStub;
#define XInputSetState (XInputSetState_)

// Try to load the latest XInpuLibrary possible
internal void Win32LoadXInput(void) {
	// TODO: Test on Windows 8
	// XInput1_4
	HMODULE XInputLibrary = LoadLibraryA("xinput1_4.dll");

	// XInput 9_1_0
	if (!XInputLibrary) {
		// TODO track which library is loaded
		XInputLibrary = LoadLibraryA("xinput9_1_0.dll");
	}
	// XInput1_3
	if (!XInputLibrary) {
		// TODO track which library is loaded
		XInputLibrary = LoadLibraryA("xinput1_3.dll");
	}

	if (XInputLibrary) {
		XInputGetState = (x_input_get_state *)GetProcAddress(XInputLibrary, "XInputGetState");
		XInputSetState = (x_input_set_state *)GetProcAddress(XInputLibrary, "XInputSetState");
	}
	else {
		// TODO: XInput could not load
	}
}

// Process controller button
internal void
Win32ProcessXInputAnalogButton(game_button_state* pNewState, game_button_state* pOldState,
	DWORD pButtonBit, DWORD XInputButtonState) {
	pNewState->EndedDown = (XInputButtonState & pButtonBit) == pButtonBit;
	pNewState->mHalfTransitionCount =
		(pOldState->EndedDown != pNewState->EndedDown) ? 1 : 0;
}

// Process controller analog stick
internal real32
Win32ProcessXInputStickValue(SHORT pValue, SHORT pDeadZoneThreshold) {
	real32 result = 0;
	if (pValue < -pDeadZoneThreshold) {
		result = ((real32)(pValue + pDeadZoneThreshold) / (32768.0f - pDeadZoneThreshold));
	}
	else if (pValue > pDeadZoneThreshold) {
		result = ((real32)(pValue - pDeadZoneThreshold) / (32767.0f - pDeadZoneThreshold));
	}

	return result;
}

/* END Controller Support */

/* START Debug Loop Support */

/***********************************************************************************

CAN'T USE FUNCTION POINTERS IN CODE BECAUSE SAVING THE MEMORY OUT TO DISK
WILL NOT WORK WHEN LOADED BACK IN. THEREFORE USING C++ CLASSES WILL NOT WORK
BECAUSE OF V TABLES.

*************************************************************/

internal void
Win32GetInputFileLocation(win32_state* state, bool32 inputStream, int slotIndex, int destSize, char* dest) {
	char temp[64];
	wsprintf(temp, "code_loop_%d_%s.ei", slotIndex, inputStream ? "input" : "state");
	Win32BuildEXEPathFilename(state, temp, destSize, dest);
}

internal win32_replay_buffer*
Win32GetReplayBuffer(win32_state* state, unsigned int index) {
	Assert(index > 0);
	Assert(index < ArrayCount(state->replayBuffers));
	win32_replay_buffer* result = &state->replayBuffers[index];
	return result;
}

// Begin recording for debuging loop
internal void
Win32BeginRecordingInput(win32_state* state, int inputRecordingIndex) {
	win32_replay_buffer* replayBuffer = Win32GetReplayBuffer(state, inputRecordingIndex);
	if (replayBuffer->memoryBlock) {
		state->inputRecordingIndex = inputRecordingIndex;

		char filename[WIN32_STATE_FILE_NAME_COUNT];
		Win32GetInputFileLocation(state, true, inputRecordingIndex, sizeof(filename), filename);
		state->recordingHandle = CreateFileA(filename, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);

		/*LARGE_INTEGER filePosition;
		filePosition.QuadPart = state->totalSize;
		SetFilePointerEx(state->recordingHandle, filePosition, 0, FILE_BEGIN);*/

		CopyMemory(replayBuffer->memoryBlock, state->gameMemoryBlock, state->totalSize);
	}
}

internal void
Win32EndRecordingInput(win32_state* state) {
	CloseHandle(state->recordingHandle);
	state->inputRecordingIndex = 0;
}

// Begin playback for a debuging loop
internal void
Win32BeginInputPlayback(win32_state* state, int inputPlayingIndex) {
	win32_replay_buffer* replayBuffer = Win32GetReplayBuffer(state, inputPlayingIndex);
	if (replayBuffer->memoryBlock) {
		state->inputPlayingIndex = inputPlayingIndex;

		char filename[WIN32_STATE_FILE_NAME_COUNT];
		Win32GetInputFileLocation(state, true, inputPlayingIndex, sizeof(filename), filename);
		state-> playbackHandle = CreateFileA(filename, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);

		/*LARGE_INTEGER filePosition;
		filePosition.QuadPart = state->totalSize;
		SetFilePointerEx(state->playbackHandle, filePosition, 0, FILE_BEGIN);*/

		CopyMemory(state->gameMemoryBlock, replayBuffer->memoryBlock, state->totalSize);
	}
}

internal void
Win32EndInputPlayback(win32_state* state) {
	CloseHandle(state->playbackHandle);
	state->inputPlayingIndex = 0;
}

internal void
Win32RecordInput(win32_state* state, game_input* newInput) {
	DWORD bytesWritten;
	WriteFile(state->recordingHandle, newInput, sizeof(*newInput), &bytesWritten, 0);
}

internal void
Win32PlaybackInput(win32_state* state, game_input* newInput) {
	DWORD bytesRead = 0;
	// Read in input
	if (ReadFile(state->playbackHandle, newInput, sizeof(*newInput), &bytesRead, 0)) {
		// Restart at the end of loop
		if (bytesRead == 0) {
			int playingIndex = state->inputPlayingIndex;
			Win32EndInputPlayback(state);
			Win32BeginInputPlayback(state, playingIndex);
			ReadFile(state->playbackHandle, newInput, sizeof(*newInput), &bytesRead, 0);
		}
	}
}

/* END Debug Loop Support */

/* START Keyboard Support */

internal void
Win32ProcessKeyboardMessage(game_button_state* pNewState, bool32 isDown) {
	if (pNewState->EndedDown != isDown) {
		pNewState->EndedDown = isDown;
		++pNewState->mHalfTransitionCount;
	}
}

internal void
Win32ProcessPendingMessages(win32_state* state, game_controller_input* keyboardController) {
	// Event Message Loop
	MSG message;
	while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) {
		switch (message.message) {
			case WM_QUIT: {
				gRunning = false;
			} break;

			case WM_LBUTTONDOWN: {
		
			} break;

			case WM_MBUTTONDOWN: {
		
			} break;

			case WM_RBUTTONDBLCLK: {
		
			}

			case WM_SYSKEYUP:
			case WM_SYSKEYDOWN:
			case WM_KEYDOWN:
			case WM_KEYUP: {
				uint32 VKCode = (uint32)message.wParam;
				bool wasDown = ((message.lParam & KeyMessageWasDownBit) != 0);  // Key Up or Down before the message was sent
				bool isDown = ((message.lParam & KeyMessageIsDownBit) == 0);  // Key is currently down
				if (isDown != wasDown) {
					if (VKCode == 'W') {
						Win32ProcessKeyboardMessage(&keyboardController->mMoveUp, isDown);
					}
					else if (VKCode == 'A') {
						Win32ProcessKeyboardMessage(&keyboardController->mMoveLeft, isDown);
					}
					else if (VKCode == 'S') {
						Win32ProcessKeyboardMessage(&keyboardController->mMoveDown, isDown);
					}
					else if (VKCode == 'D') {
						Win32ProcessKeyboardMessage(&keyboardController->mMoveRight, isDown);
					}
					else if (VKCode == 'Q') {
						Win32ProcessKeyboardMessage(&keyboardController->mLeftShoulder, isDown);
					}
					else if (VKCode == 'E') {
						Win32ProcessKeyboardMessage(&keyboardController->mRightShoulder, isDown);
					}
					else if (VKCode == VK_UP) {
						Win32ProcessKeyboardMessage(&keyboardController->mActionUp, isDown);
					}
					else if (VKCode == VK_LEFT) {
						Win32ProcessKeyboardMessage(&keyboardController->mActionLeft, isDown);
					}
					else if (VKCode == VK_DOWN) {
						Win32ProcessKeyboardMessage(&keyboardController->mActionDown, isDown);
					}
					else if (VKCode == VK_RIGHT) {
						Win32ProcessKeyboardMessage(&keyboardController->mActionRight, isDown);
					}
					else if (VKCode == VK_ESCAPE) {
						Win32ProcessKeyboardMessage(&keyboardController->mStart, isDown);
					}
					else if (VKCode == VK_SPACE) {
						Win32ProcessKeyboardMessage(&keyboardController->mBack, isDown);
					}
	#if ENGINE_INTERNAL
					else if (VKCode == 'P') {
						if (isDown) {
							gPause = !gPause;
						}
					}
					else if (VKCode == 'L') {
						if (isDown) {
							if (state->inputPlayingIndex == 0) {
								if (state->inputRecordingIndex == 0) {
									Win32BeginRecordingInput(state, 1);
								}
								else {
									Win32EndRecordingInput(state);
									Win32BeginInputPlayback(state, 1);
								}
							}
							else {
								Win32EndInputPlayback(state);
							}
						}
					}
	#endif
					if (isDown) {

						bool32 altKeyWasDown = (message.lParam & KeyMessageAltKeyDown);
						if ((VKCode == VK_F4) && altKeyWasDown) {
							gRunning = false;
						}
					}
				}  // END (isDown != wasDown)
			} break;

			default: {
				TranslateMessage(&message);
				DispatchMessageA(&message);
			} break;
		}  // END Switch
	}  // END While
}

/* END Keyboard Support */

/* START Sound Support */

#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND* ppDS, LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(direct_sound_create);

// Clear the sound buffer
internal void
Win32ClearSoundBuffer(win32_sound_output* pSoundOutput) {
	VOID* region1;
	DWORD region1Size;
	VOID* region2;
	DWORD region2Size;
	if (SUCCEEDED(gSecondaryBuffer->Lock(0, pSoundOutput->secondaryBufferSize,
		&region1, &region1Size,
		&region2, &region2Size,
		0))) {
		// TODO: Make sure region sizes are valid
		uint8* destSample = (uint8*)region1;
		for (DWORD byteIndex = 0; byteIndex < region1Size; ++byteIndex) {
			*destSample++ = 0;
		}
		destSample = (uint8*)region2;
		for (DWORD byteIndex = 0; byteIndex < region2Size; ++byteIndex) {
			*destSample++ = 0;
		}

		gSecondaryBuffer->Unlock(region1, region1Size, region2, region2Size);
	}
}

// Fill the sound buffer
internal void
Win32FillSoundBuffer(win32_sound_output* soundOutput, DWORD byteToLock, DWORD bytesToWrite,
	game_sound_output_buffer* sourceBuffer) {
	VOID* region1;
	DWORD region1Size;
	VOID* region2;
	DWORD region2Size;

	if (SUCCEEDED(gSecondaryBuffer->Lock(byteToLock, bytesToWrite,
		&region1, &region1Size,
		&region2, &region2Size,
		0))) {
		// TODO: Make sure region sizes are valid
		DWORD region1SampleCount = region1Size / soundOutput->bytesPerSample;
		int16* destSample = (int16*)region1;
		int16* sourceSample = sourceBuffer->mSamples;
		for (DWORD sampleIndex = 0; sampleIndex < region1SampleCount; ++sampleIndex) {
			*destSample++ = *sourceSample++;
			*destSample++ = *sourceSample++;
			++soundOutput->runningSampleIndex;
		}
		destSample = (int16*)region2;
		DWORD region2SampleCount = region2Size / soundOutput->bytesPerSample;
		for (DWORD sampleIndex = 0; sampleIndex < region2SampleCount; ++sampleIndex) {
			*destSample++ = *sourceSample++;
			*destSample++ = *sourceSample++;
			++soundOutput->runningSampleIndex;
		}

		gSecondaryBuffer->Unlock(region1, region1Size, region2, region2Size);
	}
}

// Initialize sound capabilities
internal void Win32InitDSound(HWND pWindow, int32 pSamplesPerSecond, int32 pBufferSize) {
	// Load the library
	HMODULE DSoundLibrary = LoadLibraryA("dsound.dll");

	if (DSoundLibrary) {
		// Get a DirectSound object - cooperative
		direct_sound_create* DirectSoundCreate = (direct_sound_create *)
			GetProcAddress(DSoundLibrary, "DirectSoundCreate");

		// TODO: Check if this works on XP - DirectSound8 or 7?
		LPDIRECTSOUND DirectSound;
		if (DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &DirectSound, 0))) {
			WAVEFORMATEX waveFormat;
			ZeroMemory(&waveFormat, sizeof(WAVEFORMATEX));
			waveFormat.wFormatTag = WAVE_FORMAT_PCM;
			waveFormat.nChannels = 2;
			waveFormat.nSamplesPerSec = pSamplesPerSecond;
			waveFormat.wBitsPerSample = 16;
			waveFormat.nBlockAlign = (waveFormat.nChannels*waveFormat.wBitsPerSample) / 8;
			waveFormat.nAvgBytesPerSec = (waveFormat.nSamplesPerSec*waveFormat.nBlockAlign);
			waveFormat.cbSize = 0;

			if (SUCCEEDED(DirectSound->SetCooperativeLevel(pWindow, DSSCL_PRIORITY))) {
				DSBUFFERDESC bufferDesc;
				ZeroMemory(&bufferDesc, sizeof(DSBUFFERDESC));
				bufferDesc.dwSize = sizeof(bufferDesc);
				bufferDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;

				// "Create" a primary buffer (handle to the sound card)
				LPDIRECTSOUNDBUFFER primaryBuffer;
				ZeroMemory(&primaryBuffer, sizeof(LPDIRECTSOUNDBUFFER));
				if (SUCCEEDED(DirectSound->CreateSoundBuffer(&bufferDesc, &primaryBuffer, 0))) {
					if (SUCCEEDED(primaryBuffer->SetFormat(&waveFormat))) {
						//  Format is set for the primary buffer
					}
					else {
						// TODO: Log
					}
				}
				else {
					// TODO: Log
				}
			}
			else {
				// TODO: Log
			}

			// "Create" a secondary buffer
			DSBUFFERDESC bufferDesc;
			ZeroMemory(&bufferDesc, sizeof(DSBUFFERDESC));
			bufferDesc.dwSize = sizeof(bufferDesc);
			bufferDesc.dwFlags = DSBCAPS_GETCURRENTPOSITION2;
			bufferDesc.dwBufferBytes = pBufferSize;
			bufferDesc.lpwfxFormat = &waveFormat;
			ZeroMemory(&gSecondaryBuffer, sizeof(LPDIRECTSOUNDBUFFER));
			if (SUCCEEDED(DirectSound->CreateSoundBuffer(&bufferDesc, &gSecondaryBuffer, 0))) {
				// TODO: Log
				// Secondary buffer created
			}
		}
		else {
			// TODO: Log
		}
	}
	else {
		// TODO Log
	}
}

/* This section is used to debug the sound system*/
//internal void
//Win32DebugDrawVertical(win32_offscreen_buffer* backBuffer, int X, int top, int bottom, uint32 color) {
//	if (top <= 0) {
//		top = 0;
//	}
//	if (bottom > backBuffer->mHeight) {
//		bottom = backBuffer->mHeight;
//	}
//
//	if ((X >= 0 && (X < backBuffer->mWidth))) {
//		uint8* pixel = (uint8*)backBuffer->mMemory + X * backBuffer->mBytesPerPixel + top * backBuffer->mPitch;
//		for (int y = top; y < bottom; ++y) {
//			*(uint32*)pixel = color;
//			pixel += backBuffer->mPitch;
//		}
//	}
//}
//
//inline void
//Win32DrawSoundBufferMarker(win32_offscreen_buffer* backBuffer, win32_sound_output* soundOutput, real32 C, int padX, int top, int bottom, DWORD value, uint32 color) {
//	real32 xreal32 = (C*(real32)value);
//	int x = padX + (int)xreal32;
//	Win32DebugDrawVertical(backBuffer, x, top, bottom, color);
//}
//
//internal void
//Win32DebugSyncDisplay(win32_offscreen_buffer* backBuffer, int markerCount, win32_debug_time_marker* markers, int currentMarkerIndex, win32_sound_output* soundOutput, real32 targetSecondsPerFrame) {
//
//	int padX = 16;
//	int padY = 16;
//	int lineHeight = 64;
//
//	real32 C = ((real32)backBuffer->mWidth - 2 * padX) / (real32)soundOutput->secondaryBufferSize;
//	for (int markerIndex = 0; markerIndex < markerCount; ++markerIndex) {
//		win32_debug_time_marker* thisMarker = &markers[markerIndex];
//		Assert(thisMarker->outputPlayCursor < soundOutput->secondaryBufferSize);
//		Assert(thisMarker->outputWriteCursor < soundOutput->secondaryBufferSize);
//		Assert(thisMarker->outputLocation < soundOutput->secondaryBufferSize);
//		Assert(thisMarker->outputByteCount < soundOutput->secondaryBufferSize);
//		Assert(thisMarker->flipPlayCursor < soundOutput->secondaryBufferSize);
//		Assert(thisMarker->flipWriteCursor < soundOutput->secondaryBufferSize);
//
//		int top = padY;
//		int bottom = padY + lineHeight;
//		DWORD playColor = 0xFFFFFFFF;
//		DWORD writeColor = 0xFFFF0000;
//		DWORD expectedFlipColor = 0xFFFFFF00;
//		DWORD playWindowColor = 0xFFFF00FF;
//		if (markerIndex == currentMarkerIndex) {
//			top += (lineHeight + padY);
//			bottom += (lineHeight + padY);
//
//			int firstTop = top;
//
//			Win32DrawSoundBufferMarker(backBuffer, soundOutput, C, padX, top, bottom, thisMarker->outputPlayCursor, playColor);
//			Win32DrawSoundBufferMarker(backBuffer, soundOutput, C, padX, top, bottom, thisMarker->outputWriteCursor, writeColor);
//
//			top += (lineHeight + padY);
//			bottom += (lineHeight + padY);
//
//			Win32DrawSoundBufferMarker(backBuffer, soundOutput, C, padX, top, bottom, thisMarker->outputLocation, playColor);
//			Win32DrawSoundBufferMarker(backBuffer, soundOutput, C, padX, top, bottom, thisMarker->outputLocation + thisMarker->outputByteCount, writeColor);
//
//			top += (lineHeight + padY);
//			bottom += (lineHeight + padY);
//
//			Win32DrawSoundBufferMarker(backBuffer, soundOutput, C, padX, firstTop, bottom, thisMarker->expectedFlipPlayCursor, expectedFlipColor);
//		}
//
//		Win32DrawSoundBufferMarker(backBuffer, soundOutput, C, padX, top, bottom, thisMarker->flipPlayCursor, playColor);
//		Win32DrawSoundBufferMarker(backBuffer, soundOutput, C, padX, top, bottom, thisMarker->flipPlayCursor + 480*soundOutput->bytesPerSample, playWindowColor);
//		Win32DrawSoundBufferMarker(backBuffer, soundOutput, C, padX, top, bottom, thisMarker->flipWriteCursor, writeColor);
//	}
//}

/* END Sound Support */

// Get window dimentions from Windows 
internal win32_window_dimension Win32GetWindowDimension(HWND window) {
	win32_window_dimension result;
	RECT clientRect;
	GetClientRect(window, &clientRect);
	result.mHeight = clientRect.bottom - clientRect.top;
	result.mWidth = clientRect.right - clientRect.left;
	return result;
}

// Setup the DIB section
internal void Win32ResizeDIBSection(win32_offscreen_buffer* pBuffer, int width, int height) {
	// TODO: Free after, then free first if that fails.
	if (pBuffer->mMemory) {
		VirtualFree(pBuffer->mMemory, 0, MEM_RELEASE);
	}

	const int bytesPerPixel = 4;

	pBuffer->mWidth = width;
	pBuffer->mHeight = height;
	pBuffer->mBytesPerPixel = bytesPerPixel;

	// biHeight is negative for Windows, so treat the bitmap as top-down, not bottom-up.
	// Meaning the first three bytes of the image are the color for the top left pixel
	ZeroMemory(&pBuffer->mInfo, sizeof(pBuffer->mInfo));
	pBuffer->mInfo.bmiHeader.biSize = sizeof(pBuffer->mInfo.bmiHeader);
	pBuffer->mInfo.bmiHeader.biWidth = pBuffer->mWidth;
	pBuffer->mInfo.bmiHeader.biHeight = -pBuffer->mHeight;
	pBuffer->mInfo.bmiHeader.biPlanes = 1;
	pBuffer->mInfo.bmiHeader.biBitCount = 32;
	pBuffer->mInfo.bmiHeader.biCompression = BI_RGB;

	// TODO: Clear to black
	ZeroMemory(&pBuffer->mMemory, sizeof(pBuffer->mMemory));
	int bitmapMemorySize = (pBuffer->mWidth*pBuffer->mHeight)*bytesPerPixel;
	pBuffer->mMemory = VirtualAlloc(0, bitmapMemorySize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	pBuffer->mPitch = pBuffer->mWidth * bytesPerPixel;
}

// Display the buffer to the screen
internal void Win32DisplayBufferInWindow(win32_offscreen_buffer* pBuffer, HDC deviceContext, 
										 int windowWidth, int windowHeight) {
	int offsetX = 10;
	int offsetY = 10;

	PatBlt(deviceContext, 0, 0, windowWidth, offsetY, BLACKNESS);
	PatBlt(deviceContext, 0, offsetY + pBuffer->mHeight, windowWidth, windowHeight, BLACKNESS);
	PatBlt(deviceContext, 0, 0, offsetX, windowHeight, BLACKNESS);
	PatBlt(deviceContext, offsetX + pBuffer->mWidth, 0, windowWidth, windowHeight, BLACKNESS);

	// For prototying, blit 1-to-1 pixels
	StretchDIBits(
		deviceContext,
		// 0, 0, windowWidth, windowHeight,
		offsetX, offsetY, pBuffer->mWidth, pBuffer->mHeight,
		0, 0, pBuffer->mWidth, pBuffer->mHeight,
		pBuffer->mMemory,
		&pBuffer->mInfo,
		DIB_RGB_COLORS,
		SRCCOPY);
}

// Toggles fullscreen
internal void
ToggleFullscreen(HWND pWindow) {
	// http://blogs.msdn.com/b/oldnewthing/archive/2010/04/12/9994016.aspx

	DWORD style = GetWindowLong(pWindow, GWL_STYLE);
	if (style & WS_OVERLAPPEDWINDOW) {
		MONITORINFO monitorInfo = { sizeof(monitorInfo) };
		if (GetWindowPlacement(pWindow, &gWindowPosition) &&
			GetMonitorInfo(MonitorFromWindow(pWindow, MONITOR_DEFAULTTOPRIMARY), &monitorInfo))
		{
			SetWindowLong(pWindow, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
			SetWindowPos(pWindow, HWND_TOP,
				monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
				monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
				monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		}
	}
	else {
		SetWindowLong(pWindow, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
		SetWindowPlacement(pWindow, &gWindowPosition);
		SetWindowPos(pWindow, 0, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
}

LRESULT CALLBACK Win32MainWindowCallBack(
	HWND window, UINT message, WPARAM WParam, LPARAM LParam
) {
	LRESULT result = 0;

	switch (message) {
		case WM_SIZE:
		{
		} break;

		case WM_DESTROY:
		{
			// TODO: Handle error
			// PostQuitMessage(0);
			gRunning = false;
			OutputDebugStringA("WM_DESTROY\n");
		} break;

		case WM_CLOSE:
		{
			gRunning = false;
			OutputDebugStringA("WM_CLOSE\n");
		} break;

		case WM_ACTIVATEAPP:
		{

			OutputDebugStringA("WM_ACTIVATEAPP\n");
		} break;

		case WM_SYSKEYUP:
		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
		case WM_KEYUP:
		{
			Assert("Wrong keyboard input");
		} break;

		case WM_PAINT:
		{
			PAINTSTRUCT paint;
			HDC deviceContext = BeginPaint(window, &paint);
			win32_window_dimension dimension = Win32GetWindowDimension(window);
			Win32DisplayBufferInWindow(&gBackBuffer, deviceContext, dimension.mWidth, dimension.mHeight);
			EndPaint(window, &paint);
		} break;

		default:
		{
			result = DefWindowProcA(window, message, WParam, LParam);
		} break;
	}  // END of switch(message)

	return result;
}

inline LARGE_INTEGER
Win32GetWallClock(void) {
	LARGE_INTEGER result;
	QueryPerformanceCounter(&result);
	return result;
}

inline real32
Win32GetSecondsElapsed(LARGE_INTEGER start, LARGE_INTEGER end) {
	real32 result = ((real32)(end.QuadPart - start.QuadPart) / (real32)gPerfCountFrequency);
	return result;
}

int CALLBACK WinMain(
	HINSTANCE instance,
	HINSTANCE prevInstance,
	LPSTR commandLine,
	int showCode
) {
	win32_state state;
	ZeroMemory(&state, sizeof(state));
	Win32GetEXEFilename(&state);

	// Set up DLL paths
	char sourceGameCodeDLLFullPath[WIN32_STATE_FILE_NAME_COUNT];
	Win32BuildEXEPathFilename(&state, "engine.dll",
		sizeof(sourceGameCodeDLLFullPath), sourceGameCodeDLLFullPath);

	char tempGameCodeDLLFullPath[WIN32_STATE_FILE_NAME_COUNT];
	Win32BuildEXEPathFilename(&state, "engine_temp.dll",
		sizeof(tempGameCodeDLLFullPath), tempGameCodeDLLFullPath);

	char gameCodeLockFullPath[WIN32_STATE_FILE_NAME_COUNT];
	Win32BuildEXEPathFilename(&state, "lock.tmp",
		sizeof(gameCodeLockFullPath), gameCodeLockFullPath);

	LARGE_INTEGER perfCountFrequencyResult;
	QueryPerformanceFrequency(&perfCountFrequencyResult);
	gPerfCountFrequency = perfCountFrequencyResult.QuadPart;

	bool32 sleepIsGranular = (timeBeginPeriod(DesiredSchedulerMS) == TIMERR_NOERROR);

	Win32LoadXInput();

#if ENGINE_INTERNAL
	DEBUGGlobalShowCursor = true;
#endif
	WNDCLASSA windowClass;
	ZeroMemory(&windowClass, sizeof(WNDCLASS));

	// TODO: Switch to GPU rendering to support 1080p display mode (1920x1080)
	// This is for CPU rendering (half resolution)
	Win32ResizeDIBSection(&gBackBuffer, 960, 540);

	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = Win32MainWindowCallBack;
	windowClass.hInstance = instance;
	WindowClass.hCursor = LoadCursor(0, IDC_ARROW);
	windowClass.lpszClassName = "EngineWindowClass";

	if (RegisterClassA(&windowClass)) {
		HWND window =
			CreateWindowExA(0,//WS_EX_TOPMOST|WS_EX_LAYERED,
				windowClass.lpszClassName, "Engine",
				WS_OVERLAPPEDWINDOW | WS_VISIBLE,
				CW_USEDEFAULT, CW_USEDEFAULT,
				CW_USEDEFAULT, CW_USEDEFAULT,
				0, 0, instance, 0);
		if (window) {

			// NULL Since we specified CS_OWNDC, only one device context is needed for the duration of the program
			// HDC deviceContext = GetDC(window);

			int monitorRefreshHz = 60;
			HDC refreshDC = GetDC(window);
			int win32RefreshRate = GetDeviceCaps(refreshDC, VREFRESH);
			ReleaseDC(window, refreshDC);
			if (win32RefreshRate > 1) {
				monitorRefreshHz = win32RefreshRate;
			}
			real32 gameUpdateHz = (monitorRefreshHz / 2.0f);
			real32 targetSecondsPerFrame = 1.0f / (real32)gameUpdateHz;
			
			// TODO: Make this so long (60 seconds) that the playcursor will always be known
			win32_sound_output soundOutput;
			ZeroMemory(&soundOutput, sizeof(win32_sound_output));
			soundOutput.samplesPerSecond = 48000;
			soundOutput.bytesPerSample = sizeof(int16) * 2;
			soundOutput.secondaryBufferSize = soundOutput.samplesPerSecond * soundOutput.bytesPerSample;
			soundOutput.safetyBytes = (int)(((real32)soundOutput.samplesPerSecond*(real32)soundOutput.bytesPerSample / gameUpdateHz) / 3.0f);

			// Initialize the sound system
			Win32InitDSound(window, soundOutput.samplesPerSecond, soundOutput.secondaryBufferSize);
			Win32ClearSoundBuffer(&soundOutput);
			gSecondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);

			/* Tests the flipPlayCursor/flipWriteCursor update frequency
			while (1) {
				DWORD flipPlayCursor;
				DWORD flipWriteCursor;
				gSecondaryBuffer->GetCurrentPosition(&flipPlayCursor, &flipWriteCursor);

				char textBuffer[256];
				snprintf(textBuffer, sizeof(textBuffer), "PC:%u, WC:%u\n", flipPlayCursor, flipWriteCursor);
				OutputDebugStringA(textBuffer);
			}*/

			int16* samples = (int16*)VirtualAlloc(0, soundOutput.secondaryBufferSize,
				MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#if ENGINE_INTERNAL
			// First 8 terabytes of address space are reserved for the application
			LPVOID baseAddress = (LPVOID)Terabytes(2);
#else
			LPVOID baseAddress = 0;
#endif
			// Game Memory
			game_memory gameMemory;
			ZeroMemory(&gameMemory, sizeof(gameMemory));
			gameMemory.mPermanentStorageSize = Megabytes(64);
			gameMemory.mTransientStorageSize = Gigabytes(1);
			gameMemory.DEBUGPlatformReadEntireFile = DEBUGPlatformReadEntireFile;
			gameMemory.DEBUGPlatformWriteEntireFile = DEBUGPlatformWriteEntireFile;
			gameMemory.DEBUGPlatformFreeFileMemory = DEBUGPlatformFreeFileMemory;

			state.totalSize = gameMemory.mPermanentStorageSize + gameMemory.mTransientStorageSize;
			// TODO: Use MEM_LARGE_PAGES and call adjust token privileges when not on Windows XP
			state.gameMemoryBlock =
				VirtualAlloc(baseAddress, (size_t)state.totalSize,
					MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
			// ZeroMemory(state.gameMemoryBlock, state.totalSize);
			gameMemory.mPermanentStorage = state.gameMemoryBlock;
			gameMemory.mTransientStorage = ((uint8*)gameMemory.mPermanentStorage +
				gameMemory.mPermanentStorageSize);

			for (int replayIndex = 1; replayIndex < ArrayCount(state.replayBuffers); ++replayIndex) {
				win32_replay_buffer* replayBuffer = &state.replayBuffers[replayIndex];

				// TODO: Recording system takes too long on record start. Speed it up or find out what Windows is doing
				Win32GetInputFileLocation(&state, false, replayIndex, sizeof(replayBuffer->filename), replayBuffer->filename);

				replayBuffer->filehandle = CreateFileA(replayBuffer->filename, GENERIC_WRITE|GENERIC_READ, 0, 0, CREATE_ALWAYS, 0, 0);

				LARGE_INTEGER maxSize;
				maxSize.QuadPart = state.totalSize;
				replayBuffer->memoryMap = CreateFileMapping(
					replayBuffer->filehandle, 0, PAGE_READWRITE,
					maxSize.HighPart, maxSize.LowPart, 0);
				replayBuffer->memoryBlock = MapViewOfFile(replayBuffer->memoryMap, FILE_MAP_ALL_ACCESS, 0, 0, state.totalSize);

				/*replayBuffer->memoryBlock = VirtualAlloc(0, (size_t)state.totalSize,
					MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);*/
				
				if (replayBuffer->memoryBlock) {
				}
				else {
					// TODO: Change this to a log message for people who don't have enough memory
				}
			}

			if (samples && gameMemory.mPermanentStorage && gameMemory.mTransientStorage) {

				game_input input[2];
				ZeroMemory(input, sizeof(input));
				game_input* newInput = &input[0];
				game_input* oldInput = &input[1];

				LARGE_INTEGER lastCounter = Win32GetWallClock();
				LARGE_INTEGER flipWallClock = Win32GetWallClock();

				int debugTimeMarkerIndex = 0;
				win32_debug_time_marker debugTimeMarkers[30];
				ZeroMemory(&debugTimeMarkers, sizeof(debugTimeMarkers));

				DWORD audioLatencyBytes = 0;
				real32 audioLatencySeconds = 0.0f;
				bool32 soundIsValid = false;

				// Load in the platform independent code
				win32_game_code game = Win32LoadGameCode(sourceGameCodeDLLFullPath, tempGameCodeDLLFullPath, gameCodeLockFullPath);

				// Main window loop
				gRunning = true;
				uint64 loadCounter = 0;
				uint64 lastCycleCount = __rdtsc();
				while (gRunning) {
					newInput->deltaTime = targetSecondsPerFrame;
					FILETIME newDLLWriteTime = Win32GetLastWriteTime(sourceGameCodeDLLFullPath);
					if (CompareFileTime(&newDLLWriteTime, &game.DLLLastWriteTime) != 0) {
						Win32UnloadGameCode(&game);
						game = Win32LoadGameCode(sourceGameCodeDLLFullPath, tempGameCodeDLLFullPath, gameCodeLockFullPath);
						loadCounter = 0;
					}

					// Process Windows messages and keyboard input
					game_controller_input* oldKeyboardController = GetController(oldInput, 0);
					game_controller_input* newKeyboardController = GetController(newInput, 0);
					ZeroMemory(*newKeyboardController, sizeof(game_controller_input));
					newKeyboardController->IsConnected = true;
					for (int buttonIndex = 0; buttonIndex < ArrayCount(newKeyboardController->mButtons); ++buttonIndex) {
						newKeyboardController->mButtons[buttonIndex].EndedDown =
							oldKeyboardController->mButtons[buttonIndex].EndedDown;
					}

					Win32ProcessPendingMessages(&state, newKeyboardController);

					if (!gPause) {
						POINT mouseLocation;
						GetCursorPos(&mouseLocation);
						ScreenToClient(window, &mouseLocation);
						newInput->mouseX = mouseLocation.x;
						newInput->mouseY = mouseLocation.y;
						newInput->mouseZ = 0;  // TODO: Support mouse wheel
						// TODO: Change this to use Windows event processing in Win32ProcessPendingMessages()
						Win32ProcessKeyboardMessage(&newInput->mouseButtons[0], GetKeyState(VK_LBUTTON) & (1 << 15));
						Win32ProcessKeyboardMessage(&newInput->mouseButtons[1], GetKeyState(VK_MBUTTON) & (1 << 15));
						Win32ProcessKeyboardMessage(&newInput->mouseButtons[2], GetKeyState(VK_RBUTTON) & (1 << 15));
						Win32ProcessKeyboardMessage(&newInput->mouseButtons[3], GetKeyState(VK_XBUTTON1) & (1 << 15));
						Win32ProcessKeyboardMessage(&newInput->mouseButtons[4], GetKeyState(VK_XBUTTON2) & (1 << 15));

						// TODO: Should we poll this more frequently
						// TODO: When a controller is not plugged in XInput stalls for a few milliseconds
						// Controller support
						DWORD maxControllerCount = XUSER_MAX_COUNT;
						if (maxControllerCount > (ArrayCount(input->mControllers) - 1)) {
							maxControllerCount = (ArrayCount(input->mControllers) - 1);
						}
						for (DWORD controllerIndex = 0; controllerIndex < maxControllerCount; ++controllerIndex) {
							DWORD ourControllerIndex = controllerIndex + 1;
							game_controller_input* oldController = GetController(oldInput, ourControllerIndex);
							game_controller_input* newController = GetController(newInput, ourControllerIndex);


							XINPUT_STATE controllerState;
							if (XInputGetState(controllerIndex, &controllerState) == ERROR_SUCCESS) {
								newController->IsConnected = true;
								newController->IsAnalog = oldController->IsAnalog;

								// The controller is plugged in
								// TODO: See if controllerState.dwPacketNumber increments too rapidly
								XINPUT_GAMEPAD* pad = &controllerState.Gamepad;
								// TODO: This is a square deadzone, change to circular deadzone
								newController->mStickAverageX = Win32ProcessXInputStickValue(pad->sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
								newController->mStickAverageY = Win32ProcessXInputStickValue(pad->sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
								if ((newController->mStickAverageX != 0.0f) ||
									(newController->mStickAverageY != 0.0f)) {
									newController->IsAnalog = true;
								}

								if (pad->wButtons & XINPUT_GAMEPAD_DPAD_UP) {
									newController->mStickAverageY = 1.0f;
									newController->IsAnalog = false;
								}
								if (pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN) {
									newController->mStickAverageY = -1.0f;
									newController->IsAnalog = false;
								}
								if (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) {
									newController->mStickAverageX = -1.0f;
									newController->IsAnalog = false;
								}
								if (pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) {
									newController->mStickAverageX = 1.0f;
									newController->IsAnalog = false;
								}

								// Move left analog stick
								real32 threshold = 0.5;
								Win32ProcessXInputAnalogButton(&newController->mMoveLeft, &oldController->mMoveLeft,
									1, (newController->mStickAverageX < -threshold) ? 1 : 0);
								Win32ProcessXInputAnalogButton(&newController->mMoveRight, &oldController->mMoveRight,
									1, (newController->mStickAverageX > threshold) ? 1 : 0);
								Win32ProcessXInputAnalogButton(&newController->mMoveDown, &oldController->mMoveDown,
									1, (newController->mStickAverageY < -threshold) ? 1 : 0);
								Win32ProcessXInputAnalogButton(&newController->mMoveUp, &oldController->mMoveUp,
									1, (newController->mStickAverageY > threshold) ? 1 : 0);

								// A
								Win32ProcessXInputAnalogButton(&newController->mActionDown, &oldController->mActionDown,
									XINPUT_GAMEPAD_A, pad->wButtons);
								// B
								Win32ProcessXInputAnalogButton(&newController->mActionRight, &oldController->mActionRight,
									XINPUT_GAMEPAD_B, pad->wButtons);
								// X
								Win32ProcessXInputAnalogButton(&newController->mActionLeft, &oldController->mActionLeft,
									XINPUT_GAMEPAD_X, pad->wButtons);
								// Y
								Win32ProcessXInputAnalogButton(&newController->mActionUp, &oldController->mActionUp,
									XINPUT_GAMEPAD_Y, pad->wButtons);
								// Left_Shoulder
								Win32ProcessXInputAnalogButton(&newController->mLeftShoulder, &oldController->mLeftShoulder,
									XINPUT_GAMEPAD_LEFT_SHOULDER, pad->wButtons);
								// Right_Shoulder
								Win32ProcessXInputAnalogButton(&newController->mRightShoulder, &oldController->mRightShoulder,
									XINPUT_GAMEPAD_RIGHT_SHOULDER, pad->wButtons);
								// Start
								Win32ProcessXInputAnalogButton(&newController->mStart, &oldController->mStart,
									XINPUT_GAMEPAD_START, pad->wButtons);
								// Back
								Win32ProcessXInputAnalogButton(&newController->mBack, &oldController->mBack,
									XINPUT_GAMEPAD_BACK, pad->wButtons);
							}
							else {
								// The controller is not available
								newController->IsConnected = false;
							}
						}

						/*
						 * Platform independent code
						 */

						thread_context thread;
						ZeroMemory(&thread, sizeof(thread));

						 // Screen
						game_offscreen_buffer screenBuffer;
						ZeroMemory(&screenBuffer, sizeof(game_offscreen_buffer));
						screenBuffer.mMemory = gBackBuffer.mMemory;
						screenBuffer.mWidth = gBackBuffer.mWidth;
						screenBuffer.mHeight = gBackBuffer.mHeight;
						screenBuffer.mPitch = gBackBuffer.mPitch;
						screenBuffer.mBytesPerPixel = gBackBuffer.mBytesPerPixel;

						// win32_state looping
						if (state.inputRecordingIndex) {
							Win32RecordInput(&state, newInput);
						}
						if (state.inputPlayingIndex) {
							Win32PlaybackInput(&state, newInput);
						}

						// Pass everything off to the game 
						if (game.updateAndRender) {
							game.updateAndRender(&thread, &gameMemory, newInput, &screenBuffer);
						}

						LARGE_INTEGER audioWallClock = Win32GetWallClock();
						real32 fromBeginningToAudioSeconds = Win32GetSecondsElapsed(flipWallClock, audioWallClock);


						DWORD flipPlayCursor;
						DWORD flipWriteCursor;
						if (gSecondaryBuffer->GetCurrentPosition(&flipPlayCursor, &flipWriteCursor) == DS_OK) {
							/*
								How the sound output computation works:

								Define a safety value that is the number of samples
								that the game update loop may vary by (2ms).

								For the wake up to write audio, look and see
								what position the play cursor is in and guess where
								the play cursor will be on the next frame boundary.

								Then look to see if the write cursor is at least before the safe value.
								If it is, the target fill position is that frame boundary plus one frame.
								This gives great audio sync in the case of a card that has low enough latency.

								If the write cursor is "after" that safety margin, then
								assume to never sync the audio perfectly, so
								write one frame's worth of audio plus safety value in guard samples.
							*/

							if (!soundIsValid) {
								soundOutput.runningSampleIndex = flipWriteCursor / soundOutput.bytesPerSample;
								soundIsValid = true;
							}

							DWORD byteToLock = (soundOutput.runningSampleIndex * soundOutput.bytesPerSample) %
								soundOutput.secondaryBufferSize;

							DWORD expectedSoundBytesPerFrame = (int)((real32)soundOutput.samplesPerSecond*(real32)soundOutput.bytesPerSample / gameUpdateHz);
							DWORD secondsLeftUntilFlip = (DWORD)(targetSecondsPerFrame - fromBeginningToAudioSeconds);
							DWORD expectedBytesUntilFlip = (DWORD)((secondsLeftUntilFlip / targetSecondsPerFrame) *
								(real32)expectedSoundBytesPerFrame);

							DWORD expectedFrameBoundaryByte = flipPlayCursor + expectedBytesUntilFlip;

							DWORD safeWriteCursor = flipWriteCursor;
							if (safeWriteCursor < flipPlayCursor) {
								safeWriteCursor += soundOutput.secondaryBufferSize;
							}
							Assert(safeWriteCursor >= flipPlayCursor);
							safeWriteCursor += soundOutput.safetyBytes;

							// Figure out if the audio card has low latency
							bool32 audioCardIsLowLatentency = (safeWriteCursor < expectedFrameBoundaryByte);

							// Calculate targetCursor
							DWORD targetCursor = 0;
							if (audioCardIsLowLatentency) {
								targetCursor = (expectedFrameBoundaryByte + expectedSoundBytesPerFrame);
							}
							else {
								targetCursor = (flipWriteCursor + expectedSoundBytesPerFrame + soundOutput.safetyBytes);
							}
							targetCursor %= soundOutput.secondaryBufferSize;

							// Calculate how many bytes to write
							DWORD bytesToWrite = 0;
							if (byteToLock > targetCursor) {
								bytesToWrite = (soundOutput.secondaryBufferSize - byteToLock);
								bytesToWrite += targetCursor;
							}
							else {
								bytesToWrite = targetCursor - byteToLock;
							}

							game_sound_output_buffer soundBuffer;
							ZeroMemory(&soundBuffer, sizeof(game_sound_output_buffer));
							soundBuffer.mSamplesPerSecond = soundOutput.samplesPerSecond;
							soundBuffer.mSampleCount = bytesToWrite / soundOutput.bytesPerSample;
							soundBuffer.mSamples = samples;
							if (game.getSoundSamples) {
								game.getSoundSamples(&thread, &gameMemory, &soundBuffer);
							}

							// Sound testing
#if ENGINE_INTERNAL
							win32_debug_time_marker* marker = &debugTimeMarkers[debugTimeMarkerIndex];
							marker->outputPlayCursor = flipPlayCursor;
							marker->outputWriteCursor = flipWriteCursor;
							marker->outputLocation = byteToLock;
							marker->outputByteCount = bytesToWrite;
							marker->expectedFlipPlayCursor = expectedFrameBoundaryByte;

							DWORD unwrappedWriteCursor = flipWriteCursor;
							if (flipWriteCursor < flipPlayCursor) {
								unwrappedWriteCursor += soundOutput.secondaryBufferSize;
							}
							audioLatencyBytes = unwrappedWriteCursor - flipPlayCursor;
							audioLatencySeconds = (((real32)audioLatencyBytes / (real32)soundOutput.bytesPerSample) /
								(real32)soundOutput.samplesPerSecond);

							/*char textBuffer[256];
							snprintf(textBuffer, sizeof(textBuffer), "BTL:%u, TC:%u, BTW:%u - PC:%u WC:%u DELTA:%u (%fs)\n", byteToLock, targetCursor, bytesToWrite,
								flipPlayCursor, flipWriteCursor, audioLatencyBytes, audioLatencySeconds);
							OutputDebugStringA(textBuffer);*/
#endif
							Win32FillSoundBuffer(&soundOutput, byteToLock, bytesToWrite, &soundBuffer);
						}
						else {
							soundIsValid = false;
						}

						LARGE_INTEGER workCounter = Win32GetWallClock();
						real32 workSecondsElapsed = Win32GetSecondsElapsed(lastCounter, workCounter);

						// Wait for target frames
						real32 secondsElapsedForFrame = workSecondsElapsed;
						if (secondsElapsedForFrame < targetSecondsPerFrame) {
							if (sleepIsGranular) {
								DWORD sleepMS = (DWORD)(1000.0f*(targetSecondsPerFrame - secondsElapsedForFrame));
								if (sleepMS > 0) {
									Sleep(sleepMS);
								}
							}

							real32 testSecondsElapsedForFrame = Win32GetSecondsElapsed(lastCounter, Win32GetWallClock());
							if (testSecondsElapsedForFrame < targetSecondsPerFrame) {
								// TODO: Log sleep miss
							}

							while (secondsElapsedForFrame < targetSecondsPerFrame) {
								secondsElapsedForFrame = Win32GetSecondsElapsed(lastCounter, Win32GetWallClock());
							}
						}
						else {
							// TODO: Missed frame rate
						}
			
						real64 MSPerFrame = 1000.0f*Win32GetSecondsElapsed(lastCounter, Win32GetWallClock());  // MegaCycles
						lastCounter = Win32GetWallClock();

						win32_window_dimension dimension = Win32GetWindowDimension(window);
#if ENGINE_INTERNAL
						// Current is wrong on the 0th index
						/*Win32DebugSyncDisplay(&gBackBuffer, ArrayCount(debugTimeMarkers), debugTimeMarkers, debugTimeMarkerIndex - 1, &soundOutput, targetSecondsPerFrame);*/
#endif
						HDC deviceContext = GetDC(window);
						Win32DisplayBufferInWindow(&gBackBuffer, deviceContext,
							dimension.mWidth, dimension.mHeight);
						ReleaseDC(window, deviceContext);

						flipWallClock = Win32GetWallClock();
#if ENGINE_INTERNAL
						{
							DWORD playCursorD;
							DWORD writeCursorD;
							if (gSecondaryBuffer->GetCurrentPosition(&playCursorD, &writeCursorD) == DS_OK) {
								Assert(debugTimeMarkerIndex < ArrayCount(debugTimeMarkers));
								win32_debug_time_marker* marker = &debugTimeMarkers[debugTimeMarkerIndex];
								marker->flipPlayCursor = playCursorD;
								marker->flipWriteCursor = writeCursorD;
							}
						}
#endif

						// Swap the digital controllers
						game_input* temp = newInput;
						newInput = oldInput;
						oldInput = temp;

						//// Clock window
						//uint64 endCycleCount = __rdtsc();
						//uint64 cyclesElapsed = endCycleCount - lastCycleCount;
						//lastCycleCount = endCycleCount;

						//real64 FPS = 0.0f;  // (real64)gPerfCountFrequency / (real64)counterElapsed;
						//real64 MCPerFrame = ((real64)cyclesElapsed / (1000.0f * 1000.0f));

						//char buffer[256];
						//snprintf(buffer, sizeof(buffer), "%.02fms/f,	%.02ff/s,	%.02fmc/f\n\n", MSPerFrame, FPS, MCPerFrame);
						//OutputDebugStringA(buffer);
#if ENGINE_INTERNAL
						++debugTimeMarkerIndex;
						if (debugTimeMarkerIndex == ArrayCount(debugTimeMarkers)) {
							debugTimeMarkerIndex = 0;
						}
#endif
					}
				}  // END while(gRunning)
			}
			else {
				// TODO: Logging, not enough memory
			}
		}
		else {
			// TODO: Logging
		}
	}
	else {
		// TODO: Logging
	}

	return 0;
}