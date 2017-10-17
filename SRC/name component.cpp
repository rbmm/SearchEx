#include "StdAfx.h"

_NT_BEGIN

#include "name component.h"

NAME_COMPONENT::NAME_COMPONENT(NAME_COMPONENT* parent) : _parent(parent)
{
	RtlZeroMemory(static_cast<PUNICODE_STRING>(this), sizeof(UNICODE_STRING));
	_dwRef = 1;
	_Level = 0;
	if (parent)
	{
		parent->AddRef();
		_Level = parent->_Level + 1;
	}
}

NAME_COMPONENT::~NAME_COMPONENT()
{
	RtlFreeUnicodeString(this);
	if (_parent)
	{
		_parent->Release();
	}
}

PWSTR NAME_COMPONENT::Print(PWSTR buf, ULONG& cb)
{
	if (_parent)
	{
		buf = _parent->Print(buf, cb);

		if (cb)
		{
			*buf++ = '\\', cb--;
		}
	}

	ULONG cch = Length / sizeof(WCHAR);

	if (cb >= cch)
	{
		memcpy(buf, Buffer, Length), buf += cch, cb -= cch;
	}
	else
	{
		cb = 0;
	}

	return buf;
}

ULONG NAME_COMPONENT::get_Length()
{
	ULONG cb = Length / sizeof(WCHAR);

	if (_parent)
	{
		cb += 1 + _parent->get_Length();
	}

	return cb;
}

int NAME_COMPONENT::compare(NAME_COMPONENT* p, NAME_COMPONENT* q)
{
	if (p == q)
	{
		return 0;
	}

	int i = q->_Level - p->_Level;

	if (i < 0)
	{
		do 
		{
			p = p->_parent;
		} while (++i);
	}
	else if (i > 0)
	{
		do 
		{
			q = q->_parent;
		} while (--i);
	}

	while (p->_parent != q->_parent)
	{
		p = p->_parent, q = q->_parent;
	}

	if (int j = RtlCompareUnicodeString(p, q, TRUE))
	{
		return j;
	}

	return i;
}

int __cdecl SEARCH_RESULT::compare(SEARCH_RESULT* p, SEARCH_RESULT* q)
{
	if (int i = NAME_COMPONENT::compare(p->name, q->name))
	{
		return i;
	}

	if (p->offset < q->offset) return -1;
	if (p->offset > q->offset) return +1;
	return 0;
}

_NT_END