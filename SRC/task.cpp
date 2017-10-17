#include "StdAfx.h"

_NT_BEGIN

#include "task.h"
#include "file.h"

extern volatile UCHAR guz;

void Task::FreeResults()
{
	if (QueryDepthSList(&m_head) || m_IoCount)
	{
		__debugbreak();
	}

	if (ULONG n = m_nResults)
	{
		SEARCH_RESULT* psr = (SEARCH_RESULT*)m_srh.getBase();
		do 
		{
			psr++->name->Release();
		} while (--n);

		m_nResults = 0;
	}

	if (m_pbStr)
	{
		delete [] m_pbStr;
		m_pbStr = 0;
	}

	FILE_MASK::free(m_fm), m_fm = 0;

	RtlFreeUnicodeString(&m_mask);

}

void Task::Reset()
{
	FreeResults();

	m_srh.Reset(), m_log.Reset();

	m_bStop = FALSE;
	m_nFiles = 0, m_nFolders = 0;
	m_TotalSize = 0;
	m_MaxTasks = 0, m_PeakIoCount = 0;
	m_AlignmentRequirement = 0, m_SectorSize = 0;
}

NTSTATUS Task::Create(HWND hwnd, SIZE_T SearchSize, SIZE_T LogSize)
{
	m_hwnd = hwnd;

	NTSTATUS status;

	0 <= (status = m_srh.Create(SearchSize)) && 0 <= (status = m_log.Create(LogSize));

	return status;
}

void Task::AddSearchResult(NAME_COMPONENT* name, LONGLONG offset)
{
	if (SEARCH_RESULT* psr = (SEARCH_RESULT*)m_srh.Allocate(sizeof(SEARCH_RESULT), __alignof(SEARCH_RESULT)))
	{
		psr->name = name;
		psr->offset = offset;
		name->AddRef();
		InterlockedIncrement(&m_nResults);
	}
}

BOOL Task::PauseFolder(Folder* p)
{
	if (m_IoCount >= m_MaxIoCount)
	{
		p->AddRef();
		InterlockedPushEntrySList(&m_head, p);

		return TRUE;
	}

	return FALSE;
}

BOOL Task::ResumeFolder()
{
	if (Folder* p = static_cast<Folder*>(InterlockedPopEntrySList(&m_head)))
	{
		p->Process();
		p->Release();
		return TRUE;
	}
	return FALSE;
}

NTSTATUS Task::Start(PCWSTR DosPath, PWSTR mask, const void* pbStr, ULONG cbStr, 
					 ULONG MaxIoCount, ULONG maxLevel, BOOLEAN CachedIO, 
					 BOOLEAN UnicodeSearch, BOOLEAN CaseSensetive, ULONG CodePage)
{
	if ( ((cbStr != 0) ^ (pbStr != 0)) || (UnicodeSearch && (!cbStr || (cbStr & (sizeof(WCHAR) - 1)))) )
	{
		return STATUS_INVALID_PARAMETER_MIX;
	}

	if (InterlockedCompareExchange(&m_nTasks, 1, 0) == 0)
	{
		Reset();

		m_UnicodeSearch = UnicodeSearch, m_CaseSensetive = CaseSensetive, m_CodePage = CodePage;
		m_MaxIoCount = MaxIoCount, m_maxLevel = maxLevel;

		NTSTATUS status = _Start(DosPath, mask, pbStr, cbStr, CachedIO);

		if (0 > status)
		{
			FreeResults();
		}

		EndTask();

		return status;
	}

	return STATUS_INVALID_DEVICE_STATE;
}

NTSTATUS Task::_Start(PCWSTR DosPath, PWSTR mask, const void* pbStr, ULONG cbStr, BOOLEAN CachedIO)
{
	NTSTATUS status;

	if (mask)
	{
		if (!RtlCreateUnicodeString(&m_mask, mask))
		{
			return STATUS_NAME_TOO_LONG;
		}

		RtlUpcaseUnicodeString(&m_mask, &m_mask, FALSE);
		
		if (0 > (status = FILE_MASK::Create(m_mask.Buffer, &m_fm)))
		{
			return status;
		}
	}

	if (pbStr)
	{
		if (m_pbStr = new char[cbStr])
		{
			memcpy(m_pbStr, pbStr, cbStr);
			m_cbStr = cbStr;
		}
		else
		{
			return STATUS_INSUFFICIENT_RESOURCES;
		}
	}
	else
	{
		m_cbStr = 0, m_pbStr = 0;
	}

	UNICODE_STRING ObjectName;
	OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName };

	if (!RtlDosPathNameToNtPathName_U(DosPath, &ObjectName, 0, 0))
	{
		return STATUS_INVALID_PARAMETER_1;
	}

	m_DosPath = DosPath;

	m_time = GetTickCount();

	HANDLE hFile;
	IO_STATUS_BLOCK iosb;

	status = NtOpenFile(&hFile, FILE_GENERIC_READ, &oa, &iosb, FILE_SHARE_VALID_FLAGS, 
		FILE_OPEN_FOR_BACKUP_INTENT|FILE_SEQUENTIAL_ONLY|FILE_OPEN_REPARSE_POINT);

	if (0 <= status)
	{
		union {
			FILE_ALIGNMENT_INFORMATION fai;
			FILE_ATTRIBUTE_TAG_INFORMATION fati;
			FILE_FS_SIZE_INFORMATION ffsi;
		};

		if (!CachedIO)
		{
			if (0 > NtQueryVolumeInformationFile(hFile, &iosb, &ffsi, sizeof(ffsi), FileFsSizeInformation) ||
				(((ffsi.BytesPerSector - 1) & ffsi.BytesPerSector)) ||
				ffsi.BytesPerSector > 0x10000)
			{
				CachedIO = TRUE;
			}
			else
			{
				m_SectorSize = ffsi.BytesPerSector - 1;

				if (0 > NtQueryInformationFile(hFile, &iosb, &fai, sizeof(fai), FileAlignmentInformation) ||
					(fai.AlignmentRequirement & (fai.AlignmentRequirement + 1)) ||
					fai.AlignmentRequirement > FILE_512_BYTE_ALIGNMENT)
				{
					m_SectorSize = 0;
					CachedIO = TRUE;
				}
				else
				{
					m_AlignmentRequirement = fai.AlignmentRequirement;
				}
			}
		}

		status = NtQueryInformationFile(hFile, &iosb, &fati, sizeof(fati), FileAttributeTagInformation);

		NtClose(hFile);

		if (0 <= status)
		{
			status = STATUS_PENDING;

			m_OpenOptions = CachedIO 
				? FILE_OPEN_FOR_BACKUP_INTENT|FILE_SEQUENTIAL_ONLY|FILE_OPEN_REPARSE_POINT
				: FILE_OPEN_FOR_BACKUP_INTENT|FILE_SEQUENTIAL_ONLY|FILE_OPEN_REPARSE_POINT|FILE_NO_INTERMEDIATE_BUFFERING;

			if (fati.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				Folder::ProcessFolder(this, &oa, 0, 0);
			}
			else
			{
				Folder::ProcessFile(this, &oa, 0, 0x10000);
			}
		}
	}

	RtlFreeUnicodeString(&ObjectName);

	return status;
}

_NT_END