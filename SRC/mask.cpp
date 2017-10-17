#include "StdAfx.h"

_NT_BEGIN

#include "mask.h"

BOOL IsNameInExpression(PCWSTR Expression, PCWSTR Name, SIZE_T len)
{
__begin:

	switch (WCHAR c = *Expression++)
	{
	default:
		if (!len-- || c != *Name++)
		{
			return FALSE;
		}
		goto __begin;

	case '?':
		if (!len--)
		{
			return FALSE;
		}
		Name++;
		goto __begin;

	case 0:
		return !len;

	case '*':
__loop:
		switch (c = *Expression++)
		{
		case '?':
			if (!len--)
			{
				return FALSE;
			}
			Name++;
		case '*':
			goto __loop;
		case 0:
			return TRUE;
		}

		// c ## Expression where c not in { '*', '?' }

		// c must be in Name
		if (!len)
		{
			return FALSE;
		}

		ULONG k = 0;

		PCWSTR pc = Expression;

		BOOLEAN bExistQuestion = FALSE;

		for (;;)
		{
			switch (++k, *pc++)
			{
			case '?':
				bExistQuestion = TRUE;
				continue;

			case '*':
				// Name = * ## c ## Expression
				do 
				{
					if (c != (--len, *Name++))
					{
						continue;
					}

					if (IsNameInExpression(Expression, Name, len)) return TRUE;

				} while (len);

				return FALSE;

			case 0:
				// no more '*' in Expression
				if (len < k)
				{
					return FALSE;
				}

				Name += len - k, len = k;

				if (*Name++ != c)
				{
					return FALSE;
				}

				if (!--len)
				{
					return TRUE;
				}

				if (!bExistQuestion)
				{
					return !memcmp(Expression, Name, len * sizeof(WCHAR));
				}
				goto __begin;
			}
		}

		break;
	}
}

BOOL MyIsNameInExpression(PCWSTR Expression, PUNICODE_STRING Name)
{
	RtlUpcaseUnicodeString(Name, Name, FALSE);
	return IsNameInExpression(Expression, Name->Buffer, Name->Length / sizeof(WCHAR) );
}

BOOLEAN FILE_MASK::IsNameInExpression(PUNICODE_STRING Name)
{
	RtlUpcaseUnicodeString(Name, Name, FALSE);
	return MyIsNameInExpression(_Expression, Name) ? TRUE : _next ? _next->IsNameInExpression(Name) : FALSE;
}

void FILE_MASK::free(FILE_MASK* next)
{
	if (next)
	{
		free(next->_next);
		delete next;
	}
}

NTSTATUS FILE_MASK::Create(PWSTR psz, FILE_MASK** first)
{
	*first = 0;

	if (!psz)
	{
		return STATUS_SUCCESS;
	}

	FILE_MASK* next = 0;

	// psz = mask:[mask:]
	while (PWSTR c = wcschr(psz, ':'))
	{
		*c = 0;
		if (FILE_MASK* p = new FILE_MASK(psz, next))
		{
			next = p;
			psz = c + 1;
		}
		else
		{
			if (next)
			{
				delete next;
			}
			return STATUS_INSUFFICIENT_RESOURCES;
		}
	}

	*first = next;
	return STATUS_SUCCESS;
}

_NT_END