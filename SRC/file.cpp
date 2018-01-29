#include "stdafx.h"

_NT_BEGIN

#include "task.h"
#include "file.h"

extern "C"{
	PBYTE __fastcall strnstr(SIZE_T n1, const void* str1, SIZE_T n2, const void* str2);
	PBYTE __fastcall wtrnstr(SIZE_T n1, const void* str1, SIZE_T n2, const void* str2);
}

void Upcase(UINT cp, PSTR buf, int cch, PWSTR wz)
{
	if (cch == MultiByteToWideChar(cp, 0, buf, cch, wz, cch))
	{
		LCMapStringW(LOCALE_NEUTRAL, LCMAP_UPPERCASE, wz, cch, wz, cch);

		WideCharToMultiByte(cp, 0, wz, cch, buf, cch, 0, 0);
	}
}

void LogError(Task* pTask, PCWSTR format, NAME_COMPONENT* name, NTSTATUS status)
{
	UNICODE_STRING us;
	ULONG len = name->get_Length();
	name->Print(us.Buffer = (PWSTR)alloca(us.MaximumLength = us.Length = (USHORT)len*sizeof(WCHAR)), len);
	len = 1 + _scwprintf(format, status, &us);
	if (PWSTR sz = (PWSTR)pTask->m_log.Allocate(len*sizeof(WCHAR), __alignof(WCHAR)))
	{
		swprintf(sz, format, status, &us);
		sz[len-1]='\n';
	}
}
//////////////////////////////////////////////////////////////////////////
// FileOfFolder

FileOfFolder::FileOfFolder(Task* pTask) : m_pTask(pTask)
{
	pTask->BeginTask();
	m_Name = 0;
}

FileOfFolder::~FileOfFolder()
{
	if (m_Name)
	{
		m_Name->Release();
	}
	m_pTask->EndTask();
}

NTSTATUS FileOfFolder::Open(POBJECT_ATTRIBUTES poa, NAME_COMPONENT* parent)
{
	if (m_pTask->Quit())
	{
		return STATUS_CANCELLED;
	}

	if (!(m_Name = new NAME_COMPONENT(parent)))
	{
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	PUNICODE_STRING ObjectName = poa->ObjectName;

	if (!parent)
	{
		UNICODE_STRING us;
		RtlInitUnicodeString(ObjectName = &us, m_pTask->m_DosPath);
		if (*(PWSTR)RtlOffsetToPointer(us.Buffer, us.Length - sizeof(WCHAR)) == OBJ_NAME_PATH_SEPARATOR)
		{
			us.Length -= sizeof(WCHAR);
		}
	}

	NTSTATUS status = m_Name->Init(ObjectName);

	if (0 > status)
	{
		return status;
	}

	HANDLE hFile;
	IO_STATUS_BLOCK iosb;

	if (0 <= (status = NtOpenFile(&hFile, FILE_GENERIC_READ, poa, &iosb, 
		FILE_SHARE_VALID_FLAGS, m_pTask->get_OpenOptions())))
	{
		Assign(hFile);

		if (!(m_pTask->get_OpenOptions() & (FILE_SYNCHRONOUS_IO_NONALERT|FILE_SYNCHRONOUS_IO_ALERT)))
		{
			status = NT_IRP::RtlBindIoCompletion(hFile);			
		}
	}

	return status;
}

//////////////////////////////////////////////////////////////////////////
// File

File::File(Task* pTask) : FileOfFolder(pTask)
{
	m_ByteOffset.QuadPart = 0;
	pTask->NewFile();
}

File* File::CreateObject(Task* pTask, ULONGLONG FileSize)
{
	ULONG SectorSize = pTask->m_SectorSize;// this is really (byte_per_sector - 1)
	ULONG cbBuffer = FileSize > ChunkSize ? ChunkSize : (ULONG)FileSize;
	cbBuffer = (cbBuffer + SectorSize) & ~SectorSize;

	ULONG cbForUpcase = pTask->m_CaseSensetive || pTask->m_UnicodeSearch ? 0 : cbBuffer * sizeof(WCHAR);

	ULONG cbStr = pTask->m_cbStr - (pTask->m_UnicodeSearch ? sizeof(WCHAR) : sizeof(CHAR));
	ULONG_PTR AlignmentRequirement = pTask->m_AlignmentRequirement;

	if (File* p = new(cbStr + AlignmentRequirement + cbBuffer + cbForUpcase) File(pTask))
	{
		PBYTE pb = p->m_buf + cbStr;
		p->m_pbReadBuffer = (PBYTE)(((ULONG_PTR)pb + AlignmentRequirement) & ~AlignmentRequirement);
		p->m_cbChunk = cbBuffer;
		return p;
	}

	return 0;
}

void File::Read()
{
	if (m_pTask->Quit()) return ;

	if (NT_IRP* irp = new NT_IRP(this, 0, 0))
	{
		m_pTask->BeginIO();

		// not use LockHandle()/UnlockHandle() here because call Close() only from destructor - so m_hFile valid here
		irp->CheckNtStatus(NtReadFile(m_hFile, NULL, NULL, irp, 
			irp, m_pbReadBuffer, m_cbChunk, &m_ByteOffset, 0));
	}
}

void File::IOCompletionRoutine(CDataPacket* /*packet*/, DWORD /*Code*/, NTSTATUS status, ULONG_PTR dwNumberOfBytesTransfered, PVOID /*Pointer*/)
{
	Task* pTask = m_pTask;

	pTask->EndIO();

	if (0 > status)
	{
		if (status != STATUS_END_OF_FILE)
		{
			LogError(pTask, L"%x: ReadFile(%wZ)\r", getName(), status);
		}
	}
	else
	{
		OnRead(dwNumberOfBytesTransfered);
	}

	while (pTask->CheckSuspendedTasks()) ;
}

void File::OnRead(ULONG_PTR dwNumberOfBytesTransfered)
{
	Task* pTask = m_pTask;

	pTask->AddSize(dwNumberOfBytesTransfered);

	if (pTask->Quit()) return ;

	BOOLEAN UnicodeSearch = pTask->m_UnicodeSearch, CaseSensetive = pTask->m_CaseSensetive;

	ULONG cbStr = pTask->m_cbStr, cbStr_1 = cbStr - (UnicodeSearch ? sizeof(WCHAR) : sizeof(CHAR));
	const void* pbStr = pTask->m_pbStr;

	PBYTE pbReadBuffer = m_pbReadBuffer, pb = pbReadBuffer, pbEnd = pb + dwNumberOfBytesTransfered;

	NAME_COMPONENT* name = m_Name;
	LONGLONG ByteOffset = m_ByteOffset.QuadPart;

	if (ByteOffset)
	{
		pb -= cbStr_1;
	}

	if (UnicodeSearch)
	{
		if (!CaseSensetive)
		{
			LCMapStringW(LOCALE_NEUTRAL, LCMAP_UPPERCASE, 
				(PWSTR)pbReadBuffer, (ULONG)dwNumberOfBytesTransfered / sizeof(WCHAR),
				(PWSTR)pbReadBuffer, (ULONG)dwNumberOfBytesTransfered / sizeof(WCHAR));
		}

		while (pb = wtrnstr((pbEnd - pb) / sizeof(WCHAR), pb, cbStr / sizeof(WCHAR), pbStr))
		{
			pTask->AddSearchResult(name, ByteOffset + (pb - pbReadBuffer) - cbStr);

			if (pTask->Quit()) return ;
		}
	}
	else
	{
		if (!CaseSensetive)
		{
			Upcase(pTask->m_CodePage, (PSTR)pbReadBuffer, (ULONG)dwNumberOfBytesTransfered, 
				(PWSTR)RtlOffsetToPointer(pbReadBuffer, dwNumberOfBytesTransfered));
		}

		while (pb = strnstr(pbEnd - pb, pb, cbStr, pbStr))
		{
			pTask->AddSearchResult(name, ByteOffset + (pb - pbReadBuffer) - cbStr);

			if (pTask->Quit()) return ;
		}
	}

	if (dwNumberOfBytesTransfered < m_cbChunk)
	{
		// end of file
		return ;
	}

	if (cbStr_1) memcpy(pbReadBuffer - cbStr_1, pbEnd - cbStr_1, cbStr_1);

	m_ByteOffset.QuadPart += m_cbChunk;

	Read();
}

//////////////////////////////////////////////////////////////////////////
// Folder
BLOCK_HEAP Folder::s_bh;

void IoPostInit()
{
	NT_IRP::_init(128);
	Folder::_init(128);
}

void Folder::Query()
{
	if (m_pTask->Quit()) return;

	if (NT_IRP* irp = new NT_IRP(this, 0, 0))
	{
		m_pTask->BeginIO();

		// not use LockHandle()/UnlockHandle() here because call Close() only from destructor - so m_hFile valid here
		irp->CheckNtStatus(NtQueryDirectoryFile(m_hFile, NULL, NULL, irp, irp, 
			m_buf, sizeof(m_buf), FileDirectoryInformation, FALSE, NULL, FALSE));
	}
}

void Folder::ProcessFile(Task* pTask, POBJECT_ATTRIBUTES poa, NAME_COMPONENT* parent, ULONGLONG FileSize)
{
	ULONG cbStr = pTask->get_cb_Str();

	if (FileSize >= cbStr && pTask->IsFileInMask(poa->ObjectName))
	{
		if (cbStr)
		{
			if (File* pFile = File::CreateObject(pTask, FileSize))
			{
				NTSTATUS status = pFile->Open(poa, parent);
				if (0 <= status)
				{
					pFile->Read();
				}
				else
				{
					LogError(pTask, L"%x: Open(%wZ)\r", pFile->getName(), status);
				}
				pFile->Release();
			}
		}
		else
		{
			// we search file by mask only. no scan it content

			if (NAME_COMPONENT* Name = new NAME_COMPONENT(parent))
			{
				if (0 <= Name->Init(poa->ObjectName))
				{
					pTask->AddSearchResult(Name);
					Name->Release();
				}
			}
		}
	}
}

void Folder::ProcessFolder(Task* pTask, POBJECT_ATTRIBUTES poa, NAME_COMPONENT* Name, int nLevel)
{
	if (!pTask->IsLevelOk(nLevel))
	{
		return ;
	}

	pTask->NewFolder();

	NTSTATUS status;

	if (Folder* pFolder = new Folder(pTask, nLevel))
	{
		if (0 <= (status = pFolder->Open(poa, Name)))
		{
			pFolder->Query();
		}
		else
		{
			LogError(pTask, L"%x: Open(%wZ)\r", pFolder->getName(), status);
		}
		pFolder->Release();
	}
}

void Folder::IOCompletionRoutine(CDataPacket* /*packet*/, DWORD /*Code*/, NTSTATUS status, ULONG_PTR dwNumberOfBytesTransfered, PVOID /*Pointer*/)
{
	Task* pTask = m_pTask;
	
	pTask->EndIO();

	if (0 > status)
	{
		switch (status)
		{
		case STATUS_NO_SUCH_FILE:
		case STATUS_NO_MORE_FILES:
			break;
		default:
			LogError(pTask, L"%x: QueryDirectory(%wZ)\r", getName(), status);
		}
	}
	else
	{
		m_pfdi = &m_fdi;
		// optimization : in m_buf enough space for how minimum one file record
		// this only happens if no more files to fill
		// if we call NtQueryDirectoryFile here - it return STATUS_NO_MORE_FILES
		m_bContinue = sizeof(m_buf) - dwNumberOfBytesTransfered < (ULONG)FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName[0xff]);

		Process();
	}

	while (pTask->CheckSuspendedTasks()) ;
}

void Folder::Process()
{
	Task* pTask = m_pTask;

	union {
		PBYTE pb;
		PFILE_DIRECTORY_INFORMATION DirInfo;
	};

	if (DirInfo = m_pfdi)
	{
		ULONG NextEntryOffset = 0;

		UNICODE_STRING ObjectName;
		OBJECT_ATTRIBUTES oa = { sizeof(oa), m_hFile, &ObjectName };

		int level = m_nLevel + 1;

		do 
		{
			if (pTask->Quit()) return;

			pb += NextEntryOffset;

			m_pfdi = DirInfo;

			if (pTask->PauseFolder(this))
			{
				return ;
			}

			ObjectName.Buffer = DirInfo->FileName;

			switch (ObjectName.Length = (USHORT)DirInfo->FileNameLength)
			{
			case 2*sizeof(WCHAR):
				if (ObjectName.Buffer[1] != '.') break;
			case sizeof(WCHAR):
				if (ObjectName.Buffer[0] == '.') continue;
			}

			ObjectName.MaximumLength = ObjectName.Length;

			if (DirInfo->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				ProcessFolder(pTask, &oa, m_Name, level);
			}
			else
			{
				ProcessFile(pTask, &oa, m_Name, DirInfo->EndOfFile.QuadPart);
			}

		} while (NextEntryOffset = DirInfo->NextEntryOffset);

		m_pfdi = 0;
	}

	if (m_bContinue && !pTask->PauseFolder(this))
	{
		Query();
	}
}

_NT_END