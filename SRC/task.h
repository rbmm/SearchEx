#pragma once

#include "name component.h"
#include "mask.h"
#include "lineheap.h"

class Folder;

class Task
{
public:
	SLIST_HEADER m_head;
	LineHeap m_srh, m_log;
	UNICODE_STRING m_mask;
	LONGLONG m_TotalSize;
	
	PCWSTR m_DosPath;//temp!
	HWND m_hwnd;

	FILE_MASK* m_fm;
	void* m_pbStr;
	ULONG m_cbStr;

	ULONG m_AlignmentRequirement;
	ULONG m_SectorSize;
	ULONG m_maxLevel;
	LONG m_nFolders, m_nFiles;
	ULONG m_OpenOptions;
	ULONG m_time;
	LONG m_nTasks, m_MaxTasks;
	LONG m_MaxIoCount, m_IoCount, m_PeakIoCount;
	LONG m_nResults;
	ULONG m_CodePage;
	BOOLEAN m_bStop, m_UnicodeSearch, m_CaseSensetive;

	NTSTATUS _Start(PCWSTR DosPath, PWSTR mask, const void* pbStr, ULONG cbStr, BOOLEAN CachedIO);

	void BeginIO()
	{
		ULONG IoCount = InterlockedIncrement(&m_IoCount), PeakIoCount = m_PeakIoCount;
		if (PeakIoCount < IoCount)
		{
			InterlockedCompareExchange(&m_PeakIoCount, IoCount, PeakIoCount);
		}
	}

	void EndIO()
	{
		InterlockedDecrement(&m_IoCount);
	}

	BOOL CheckSuspendedTasks()
	{
		if (m_IoCount < m_MaxIoCount)
		{
			return ResumeFolder();
		}
		return FALSE;
	}

	void FreeResults();

	void Reset();

	BOOL PauseFolder(Folder* p);

	BOOL ResumeFolder();

	void BeginTask()
	{
		ULONG Tasks = InterlockedIncrement(&m_nTasks), MaxTasks = m_MaxTasks;

		if (MaxTasks < Tasks)
		{
			InterlockedCompareExchange(&m_MaxTasks, Tasks, MaxTasks);
		}
	}

	void EndTask()
	{
		if (!InterlockedDecrement(&m_nTasks))
		{
			PostMessage(m_hwnd, WM_USER+WM_QUIT, GetTickCount() - m_time, 0);
		}
	}

	void NewFolder()
	{
		InterlockedIncrement(&m_nFolders);
	}

	void NewFile()
	{
		InterlockedIncrement(&m_nFiles);
	}

	void AddSize(ULONGLONG Size)
	{
		InterlockedExchangeAdd64(&m_TotalSize, Size);
	}

	void AddSearchResult(NAME_COMPONENT* name, LONGLONG offset = -1);

	~Task()
	{
		FreeResults();
	}

	Task()
	{
		m_nTasks = 0, m_nResults = 0, m_fm = 0, m_pbStr = 0, m_mask = {}, m_IoCount = 0;
		InitializeSListHead(&m_head);
		Reset();
	}

	BOOL IsLevelOk(ULONG maxLevel)
	{
		return maxLevel < m_maxLevel;
	}

	NTSTATUS Create(HWND hwnd, SIZE_T SearchSize, SIZE_T LogSize);

	BOOLEAN Quit()
	{
		return m_bStop;
	}

	ULONG get_OpenOptions()
	{
		return m_OpenOptions;
	}

	ULONG get_cb_Str()
	{
		return m_cbStr;
	}

	LPCVOID get_Str()
	{
		return m_pbStr;
	}

	ULONG getResults(SEARCH_RESULT** ppsr)
	{
		*ppsr = (SEARCH_RESULT*)m_srh.getBase();
		return m_nResults;
	}

	NTSTATUS Start(PCWSTR DosPath, PWSTR mask, const void* pbStr, ULONG cbStr, 
		ULONG MaxIoCount, ULONG maxLevel, BOOLEAN CachedIO, BOOLEAN UnicodeSearch, BOOLEAN CaseSensetive, ULONG CodePage);

	void Stop()
	{
		m_bStop = TRUE;
	}

	BOOLEAN IsFileInMask(PUNICODE_STRING Name)
	{
		return m_fm ? m_fm->IsNameInExpression(Name) : TRUE;
	}
};
