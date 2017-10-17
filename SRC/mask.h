#pragma once

class FILE_MASK
{
	FILE_MASK* _next;
	PCWSTR _Expression;

	FILE_MASK(PWSTR Expression, FILE_MASK* next)
	{
		_Expression = Expression, _next = next;
	}

	~FILE_MASK()
	{
	}

public:

	BOOLEAN IsNameInExpression(PUNICODE_STRING Name);

	static NTSTATUS Create(PWSTR psz, FILE_MASK** first);

	static void free(FILE_MASK* next);
};
