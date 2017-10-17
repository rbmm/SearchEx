#pragma once

class NAME_COMPONENT : UNICODE_STRING 
{
	NAME_COMPONENT* _parent;
	LONG _dwRef;
	ULONG _Level;

	~NAME_COMPONENT();

public:

	NAME_COMPONENT(NAME_COMPONENT* parent);

	NTSTATUS Init(PCUNICODE_STRING Str)
	{
		return RtlDuplicateUnicodeString(0, Str, this);
	}

	void Release()
	{
		if (!InterlockedDecrement(&_dwRef))
		{
			delete this;
		}
	}

	void AddRef()
	{
		InterlockedIncrement(&_dwRef);
	}

	PWSTR Print(PWSTR buf, ULONG& cb);

	ULONG get_Length();

	PCUNICODE_STRING getCUS()
	{
		return this;
	}

	static int compare(NAME_COMPONENT* p, NAME_COMPONENT* q);
};

struct SEARCH_RESULT 
{
	LONGLONG offset;//-1 if we search files by mask only
	NAME_COMPONENT* name;

	static int __cdecl compare(SEARCH_RESULT* p, SEARCH_RESULT* q);
};
