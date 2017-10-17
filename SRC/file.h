#pragma once

#include "../asio/io.h"

class Task;
class NAME_COMPONENT;

class __declspec(novtable) FileOfFolder : public IO_OBJECT
{
protected:

	Task* m_pTask;
	NAME_COMPONENT* m_Name;

	FileOfFolder(Task* pTask);

	virtual ~FileOfFolder();

public:

	NTSTATUS Open(POBJECT_ATTRIBUTES poa, NAME_COMPONENT* parent);

	NAME_COMPONENT* getName()
	{
		return m_Name;
	}
};

class File : public FileOfFolder
{
	enum { ChunkSize = 0x10000 };//64kb

	LARGE_INTEGER m_ByteOffset;
	PBYTE m_pbReadBuffer;
	ULONG m_cbChunk;
	UCHAR m_buf[];

	File(Task* pTask);

	void* operator new(size_t cb, size_t cbBuf)
	{
		return IO_OBJECT::operator new(cb + cbBuf);
	}

	void OnRead(ULONG_PTR dwNumberOfBytesTransfered);
public:

	static File* CreateObject(Task* pTask, ULONGLONG FileSize);

	void Read();

	virtual void IOCompletionRoutine(CDataPacket* /*packet*/, DWORD /*Code*/, NTSTATUS status, ULONG_PTR dwNumberOfBytesTransfered, PVOID /*Pointer*/);
};

class Folder : public FileOfFolder, SLIST_ENTRY
{
	friend class Task;

	static BLOCK_HEAP s_bh;

	FILE_DIRECTORY_INFORMATION* m_pfdi;
	ULONG m_nLevel;
	BOOLEAN m_bContinue;
	union {
		FILE_DIRECTORY_INFORMATION m_fdi;
		UCHAR m_buf[0x10000];// must be aligned as FILE_DIRECTORY_INFORMATION
	};

	virtual void IOCompletionRoutine(CDataPacket* packet, DWORD Code, NTSTATUS status, ULONG_PTR dwNumberOfBytesTransfered, PVOID Pointer);

	void Process();

	void Query();

public:
	void operator delete(PVOID p)
	{
		s_bh.IsBlock(p) ? s_bh.free(p) : ::operator delete(p);
	}

	Folder(Task* pTask, ULONG nLevel) : FileOfFolder(pTask), m_nLevel(nLevel)
	{
	}

	void* operator new(size_t size)
	{
		PVOID p = size == sizeof(Folder) ? s_bh.alloc() : 0;
		return p ? p : p = ::operator new(size);
	}

	static BOOL _init(DWORD count)
	{
		return s_bh.Create(sizeof(Folder), count);
	}

	static void ProcessFolder(Task* pTask, POBJECT_ATTRIBUTES poa, NAME_COMPONENT* Name, int nLevel);
	static void ProcessFile(Task* pTask, POBJECT_ATTRIBUTES poa, NAME_COMPONENT* Name, ULONGLONG FileSize);
};