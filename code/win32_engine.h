#if !defined(WIN32_ENGINE_H)

/*
 * Author: Jheremy Strom
 */

struct win32_offscreen_buffer {
	//  Not Needed, pixels are always 32-bit wide, Memory order BB GG RR XX
	BITMAPINFO mInfo;
	void* mMemory;
	int mWidth;
	int mHeight;
	int mPitch;
	int mBytesPerPixel;
};

struct win32_window_dimension {
	int mWidth;
	int mHeight;
};

struct win32_sound_output {
	int samplesPerSecond;
	uint32 runningSampleIndex;
	int bytesPerSample;
	DWORD secondaryBufferSize;
	DWORD safetyBytes;
};
	
struct win32_debug_time_marker {
	DWORD outputPlayCursor;
	DWORD outputWriteCursor;
	DWORD outputLocation;
	DWORD outputByteCount;
	DWORD expectedFlipPlayCursor;

	DWORD flipPlayCursor;
	DWORD flipWriteCursor;
};

struct win32_game_code {
	HMODULE gameCodeDLL;
	FILETIME DLLLastWriteTime;

	// Both callbacks can be NULL, check before calling
	game_update_and_render* updateAndRender;
	game_get_sound_samples* getSoundSamples;

	bool32 isValid;
};

#define WIN32_STATE_FILE_NAME_COUNT MAX_PATH
struct win32_replay_buffer {
	HANDLE filehandle;
	HANDLE memoryMap;
	char filename[WIN32_STATE_FILE_NAME_COUNT];
	void* memoryBlock;
};

struct win32_state {
	uint64 totalSize;
	void* gameMemoryBlock;
	win32_replay_buffer replayBuffers[4];

	HANDLE recordingHandle;
	int inputRecordingIndex;

	HANDLE playbackHandle;
	int inputPlayingIndex;

	char EXEFilename[WIN32_STATE_FILE_NAME_COUNT];
	char* onePastLastEXEFilenameSlash;
};

#define WIN32_ENGINE_H
#endif