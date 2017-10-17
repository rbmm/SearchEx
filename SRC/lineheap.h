#pragma once

class LineHeap 
{
	PVOID _BaseAddress;
	SIZE_T m_reserved, m_commit, m_allocated;

	enum { chunkSize = 0x10000 /*64kb*/};
public:

	PVOID getBase()
	{
		return _BaseAddress;
	}

	SIZE_T getSize()
	{
		return m_allocated;
	}

	void Reset();

	LineHeap()
	{
		_BaseAddress = 0, m_commit = 0, m_allocated = 0;
	}

	~LineHeap()
	{
		if (_BaseAddress)
		{
			SIZE_T RegionSize = 0;
			NtFreeVirtualMemory(NtCurrentProcess(), &_BaseAddress, &RegionSize, MEM_RELEASE);
		}
	}

	NTSTATUS Create(SIZE_T reserved)
	{
		return NtAllocateVirtualMemory(NtCurrentProcess(), &_BaseAddress, 0, 
			&(m_reserved = (reserved + (chunkSize-1)) & ~(chunkSize-1)), MEM_RESERVE, PAGE_READWRITE);
	}

	PVOID Allocate(SIZE_T cb, SIZE_T align = __alignof(PVOID));
};
