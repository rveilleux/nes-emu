#include "System.h"

#if PLATFORM_WINDOWS

#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#undef ARRAYSIZE // Already defined in a Windows header
#include <Windows.h>
#include <commdlg.h>
#include <conio.h>
#include <cstdio>

// Undef the macro in WinUser.h so we can use this name as our function. We invoke MessageBoxA directly.
#undef MessageBox

namespace System
{
	void Sleep(uint32 ms) { ::Sleep(ms); }

	bool GetKeyPress(char& key)
	{
		if (_kbhit())
		{
			int result = _getch();

			// If user presses fn or arrow, we need two _getch() calls
			if (result == 0 || result == 0xE0)
			{
				_getch();
				result = 0xFF; // Return uncommon result
			}

			key = static_cast<char>(result);
			return true;
		}
		return false;
	}
	
	char WaitForKeyPress()
	{
		char key;
		while (!GetKeyPress(key))
		{
			Sleep(1);
		}
		return key;
	}

	void DebugBreak()
	{
		::DebugBreak();
	}

	void MessageBox(const char* title, const char* message)
	{
		printf(FormattedString<>("%s: %s\n", title, message).Value());
		::MessageBoxA(::GetActiveWindow(), message, title, MB_OK);
	}

	bool OpenFileDialog(std::string& fileSelected, const char* title, const char* filter)
	{
		char file[_MAX_PATH] = "";

		char currDir[_MAX_PATH] = "";
		::GetCurrentDirectoryA(sizeof(currDir), currDir);

		OPENFILENAMEA ofn = {0};
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFile = file;
		ofn.nMaxFile = sizeof(file);
		ofn.lpstrTitle = title;
		ofn.lpstrFilter = filter;
		ofn.nFilterIndex = 0;
		ofn.lpstrInitialDir = currDir;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

		if (::GetOpenFileNameA(&ofn) == TRUE)
		{
			fileSelected = ofn.lpstrFile;
			return true;
		}
		return false;
	}

	static float64 GetPerfCountTicksPerSec()
	{
		LARGE_INTEGER freq;
		::QueryPerformanceFrequency(&freq);
		return static_cast<float64>(freq.QuadPart);
	}

	Ticks GetTicks()
	{
		LARGE_INTEGER li;
		::QueryPerformanceCounter(&li);
		return li.QuadPart;
	}

	float64 TicksToSec(Ticks t1)
	{
		static float64 ticksPerSec = GetPerfCountTicksPerSec();
		return static_cast<float64>(t1)/ ticksPerSec;
	}
}


#else
#error "Implement for current platform
#endif
