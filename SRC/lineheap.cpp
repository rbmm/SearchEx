#include "StdAfx.h"

_NT_BEGIN

#include "lineheap.h"

void LineHeap::Reset()
{
	if (m_commit > chunkSize)
	{
		PVOID BaseAddress = (PBYTE)_BaseAddress + chunkSize;
		SIZE_T RegionSize = m_commit - chunkSize;
		if (0 > NtFreeVirtualMemory(NtCurrentProcess(), &BaseAddress, &RegionSize, MEM_DECOMMIT))
		{
			__debugbreak();
		}
		m_commit = chunkSize;
	}
	m_allocated = 0;
}

PVOID LineHeap::Allocate(SIZE_T cb, SIZE_T align)
{
	if (align & (align - 1))
	{
		return 0;
	}

	--align;

	SIZE_T allocated = m_allocated, new_allocated, newValue, ofs, commit;

	for (;;)
	{
		ofs = (allocated + align) & ~align;

		new_allocated = ofs + cb;

		if (new_allocated  > m_reserved)
		{
			return 0;
		}

		newValue = (SIZE_T)InterlockedCompareExchangePointer((void**)&m_allocated, (void*)new_allocated, (void*)allocated);

		if (newValue == allocated)
		{
			break;
		}

		allocated = newValue;
	}

	if (new_allocated > (commit = m_commit))
	{
		PVOID BaseAddress = (PBYTE)_BaseAddress + commit;

		new_allocated = (new_allocated + (chunkSize-1)) & ~(chunkSize-1);

		newValue = new_allocated - commit;

		if (0 > NtAllocateVirtualMemory(NtCurrentProcess(), &BaseAddress, 0, &newValue, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
		{
			return 0;
		}

		do 
		{
			newValue = (SIZE_T)InterlockedCompareExchangePointer((void**)&m_commit, (void*)new_allocated, (void*)commit);

		} while (newValue != commit && new_allocated > (commit = newValue));
	}

	return (PBYTE)_BaseAddress + ofs;
}

_NT_END