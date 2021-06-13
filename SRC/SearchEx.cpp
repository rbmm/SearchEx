// SearchEx.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

_NT_BEGIN

#include "resource.h"
#include "../inc/initterm.h"
#include "../inc/rundown.h"
#include "../asio/io.h"
#include "../asio/tp.h"
#include "../winZ/window.h"
#include "../winZ/ctrl.h"
#include "task.h"

HFONT CreateCaptionFont()
{
	ULONG m;
	RtlGetNtVersionNumbers(&m, 0, 0);

	NONCLIENTMETRICS ncm = { m < 6 ? sizeof(NONCLIENTMETRICS) - 4 : sizeof(NONCLIENTMETRICS) };
	if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
	{
		ncm.lfCaptionFont.lfWeight = FW_NORMAL;
		ncm.lfCaptionFont.lfQuality = CLEARTYPE_QUALITY;
		ncm.lfCaptionFont.lfPitchAndFamily = FIXED_PITCH|FF_MODERN;
		wcscpy(ncm.lfCaptionFont.lfFaceName, L"Courier New");

		return CreateFontIndirect(&ncm.lfCaptionFont);
	}

	return 0;
}

class SearchDlg : public ZDlg, CIcons
{
	Task m_task;
	HFONT m_hFont;
	int m_Encoding, m_SearchEncoding;
	ULONG m_cbStr;
	BOOL m_bInSearch;

	enum {
		i_ANSI, i_OEM, i_UTF8, i_UTF16, i_HEX, i_DWORD
	};

	void OnInitDialog(HWND hwndDlg);

	void OnTask(HWND hwndDlg, BOOL begin);

	void OnOk(HWND hwndDlg);

	void ShowLog(HWND hwndDlg);

	void PrintResults(HWND hwndDlg);

	void ShowStatus(HWND hwndDlg, ULONG ticks, BOOL bFinal);

	void OnCaseSensetive(HWND hwndEdit, HWND hwndChk);

	void OnEncodingChanged(HWND hwndEdit, HWND hwndCombo, HWND hwndCheck);

	static PWSTR ReadData(HANDLE hFile, PLARGE_INTEGER ByteOffset, PBYTE pb, ULONG& cb, PWSTR pszText, ULONG& cchTextMax);

	void SetInfoTip(PWSTR pszText, ULONG cchTextMax, SEARCH_RESULT *psr);

	virtual INT_PTR DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

void SearchDlg::OnInitDialog(HWND hwndDlg)
{
	SetIcons(hwndDlg, (HINSTANCE)&__ImageBase, MAKEINTRESOURCE(1));
	m_bInSearch = FALSE;

	m_hFont = CreateCaptionFont();

	if (0 > m_task.Create(hwndDlg, 0x1000000, 0x1000000))
	{
		EndDialog(hwndDlg, -1);
		return ;
	}

	CheckDlgButton(hwndDlg, IDC_CHECK2, BST_CHECKED);
	SetDlgItemInt(hwndDlg, IDC_EDIT4, 32, FALSE);
	SetDlgItemInt(hwndDlg, IDC_EDIT5, 0, FALSE);

	HWND hwnd = GetDlgItem(hwndDlg, IDC_COMBO1);

	ComboBox_AddString(hwnd, L"ANSI");
	ComboBox_AddString(hwnd, L"OEM");
	ComboBox_AddString(hwnd, L"UTF-8");
	ComboBox_AddString(hwnd, L"UTF-16");
	ComboBox_AddString(hwnd, L"HEX");
	ComboBox_AddString(hwnd, L"DWORD");
	ComboBox_SetCurSel(hwnd, i_ANSI);
	m_Encoding = i_ANSI;

	HWND hwndLV = GetDlgItem(hwndDlg, IDC_LIST1);

	SetWindowTheme(hwndLV, L"Explorer", 0);

	RECT rc;
	GetWindowRect(hwndLV, &rc);

	ListView_SetExtendedListViewStyle(hwndLV, LVS_EX_BORDERSELECT|LVS_EX_INFOTIP|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);

	SIZE size = { 8, 16 };
	if (HDC hdc = GetDC(hwndLV))
	{
		HGDIOBJ o = SelectObject(hdc, (HGDIOBJ)SendMessage(hwndLV, WM_GETFONT, 0, 0));
		GetTextExtentPoint32(hdc, L"W", 1, &size);
		SelectObject(hdc, o);
		ReleaseDC(hwndLV, hdc);
	}

	LVCOLUMN lvc = { LVCF_FMT|LVCF_WIDTH|LVCF_TEXT|LVCF_SUBITEM, LVCFMT_LEFT };

	static PCWSTR headers[] = { L" Offset ", L" Name "};
	DWORD lens[] = { 16, 80};

	do 
	{
		lvc.pszText = (PWSTR)headers[lvc.iSubItem], lvc.cx = lens[lvc.iSubItem] * size.cx;
		ListView_InsertColumn(hwndLV, lvc.iSubItem, &lvc);
	} while (++lvc.iSubItem < RTL_NUMBER_OF(headers));

	SendMessage(ListView_GetToolTips(hwndLV), TTM_SETDELAYTIME, TTDT_AUTOPOP, MAKELONG(MAXSHORT, 0));
}

void SearchDlg::OnTask(HWND hwndDlg, BOOL begin)
{
	m_bInSearch = begin;
	EnableWindow(GetDlgItem(hwndDlg, IDOK), !begin);
	EnableWindow(GetDlgItem(hwndDlg, IDCANCEL), begin);

	if (begin)
	{
		EnableWindow(GetDlgItem(hwndDlg, IDC_BUTTON1), FALSE);
		ListView_SetItemCountEx(GetDlgItem(hwndDlg, IDC_LIST1), 0, 0);
		SetWindowTextW(hwndDlg, L"Search");
		SetTimer(hwndDlg, 0, 1000, 0);
	}
	else
	{
		KillTimer(hwndDlg, 0);
	}
}

void SearchDlg::OnOk(HWND hwndDlg)
{
	BOOL Translated;
	UINT maxIoCount = GetDlgItemInt(hwndDlg, IDC_EDIT4, &Translated, FALSE);

	if (maxIoCount - 1 > 98 || !Translated)
	{
		SetFocus(GetDlgItem(hwndDlg, IDC_EDIT4));
		MessageBoxW(hwndDlg, L"Maximum simultaneous I/O requests\r\ncount must be in range [1, 100)", 0, MB_ICONWARNING|MB_OK);
		return;
	}

	UINT maxLevel = GetDlgItemInt(hwndDlg, IDC_EDIT5, &Translated, FALSE);

	if (!Translated)
	{
		SetFocus(GetDlgItem(hwndDlg, IDC_EDIT5));
		MessageBoxW(hwndDlg, L"0: infinite\r\n1: current folder only\r\nN: and so on", L"You need set deep search level", MB_ICONWARNING|MB_OK);
		return;
	}

	if (!maxLevel)
	{
		maxLevel = MAXDWORD;
	}

	HWND hwnd;
	ULONG len;
	PWSTR mask = 0;

	if (len = GetWindowTextLength(hwnd = GetDlgItem(hwndDlg, IDC_EDIT2)))
	{
		mask = (PWSTR)alloca((len+2)*sizeof(WCHAR));
		GetWindowText(hwnd, mask, len + 1);
		if (len == 1 && *mask == '*')
		{
			mask = 0;
		}
		else
		{
			mask[len] = ':', mask[len + 1] = 0;

			PWSTR c = mask;

			while (c = wcschr(c, '*'))
			{
				if (*++c == '*')
				{
					SetFocus(hwnd);
					MessageBoxW(hwndDlg, L"no sense have ** in mask", 0, MB_ICONWARNING|MB_OK);
					return;
				}
			}
		}
	}

	PVOID pvStr = 0;
	ULONG cbStr = 0;
	UINT CodePage = 0;

	if (len = GetWindowTextLength(hwnd = GetDlgItem(hwndDlg, IDC_EDIT3)))
	{
		if (len > 256)
		{
			SetFocus(hwnd);
			MessageBoxW(hwndDlg, L"string too long (> 256) !", 0, MB_ICONWARNING|MB_OK);
			return ;
		}
		PWSTR str = (PWSTR)alloca((len + 1)* sizeof(WCHAR));
		GetWindowText(hwnd, str, len + 1);

		BOOL UsedDefaultChar = FALSE, *lpUsedDefaultChar = &UsedDefaultChar;

		switch (m_Encoding)
		{
		case i_ANSI:
			CodePage = CP_ACP;
			break;

		case i_OEM:
			CodePage = CP_OEMCP;
			break;

		case i_UTF8:
			CodePage = CP_UTF8;
			lpUsedDefaultChar = 0;
			break;

		case i_UTF16:
			pvStr = str, cbStr = len * sizeof(WCHAR);
			goto __ok;

		case i_HEX:
			while (CryptStringToBinaryW(str, len, CRYPT_STRING_HEX, (PBYTE)pvStr, &cbStr, 0, 0))
			{
				if (pvStr)
				{
					goto __ok;
				}

				pvStr = alloca(cbStr);
			}
			SetFocus(hwnd);
			MessageBoxW(hwndDlg, L"Invalid Hex string", 0, MB_ICONWARNING|MB_OK);
			return;

		case i_DWORD:
			if (len <= 8)
			{
				if (ULONG n =  wcstoul(str, &str, 16))
				{
					if (!*str)
					{
						cbStr = sizeof(n);
						pvStr = &n;
						goto __ok;
					}
				}
			}
			SetFocus(hwnd);
			MessageBoxW(hwndDlg, L"Invalid DWORD", 0, MB_ICONWARNING|MB_OK);
			return;
		default:
			return;
		}

		while ((cbStr = WideCharToMultiByte(CodePage, 0, str, len, 0, 0, 0, lpUsedDefaultChar)) && !UsedDefaultChar)
		{
			if (pvStr)
			{
				goto __ok;
			}

			pvStr = alloca(cbStr);
		}

		SetFocus(hwnd);
		MessageBoxW(hwndDlg, L"string can not be converted !", 0, MB_ICONWARNING|MB_OK);
		return ;
	}

__ok:
	if (!(len = GetWindowTextLength(hwnd = GetDlgItem(hwndDlg, IDC_EDIT1))))
	{
		SetFocus(hwnd);
		MessageBoxW(hwndDlg, L"Root Folder or File Path can not be empty", 0, MB_ICONWARNING|MB_OK);
		return;
	}

	PWSTR Name = (PWSTR)alloca((len + 1) * sizeof(WCHAR));
	GetWindowText(hwnd, Name, len + 1);

	m_cbStr = cbStr, m_SearchEncoding = m_Encoding;

	NTSTATUS status = m_task.Start(Name, mask, pvStr, cbStr, maxIoCount, maxLevel,
		IsDlgButtonChecked(hwndDlg, IDC_CHECK1) == BST_CHECKED, m_Encoding == i_UTF16,
		m_Encoding >= i_HEX || IsDlgButtonChecked(hwndDlg, IDC_CHECK2) == BST_CHECKED, CodePage);

	if (0 <= status)
	{
		SetDlgItemText(hwndDlg, IDC_STATIC1, L"");
		OnTask(hwndDlg, TRUE);
	}
}

void SearchDlg::ShowLog(HWND hwndDlg)
{
	if (SIZE_T size = m_task.m_log.getSize())
	{
		PWSTR sz = (PWSTR)m_task.m_log.getBase();
		*(PWSTR)RtlOffsetToPointer(sz, size - sizeof(WCHAR))=0;
		if (HWND hwnd = CreateWindowExW(0, WC_EDIT, L"Fail files", 
			WS_OVERLAPPEDWINDOW|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE|ES_AUTOHSCROLL|ES_AUTOVSCROLL, 
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndDlg, 0, 0, 0))
		{
			if (m_hFont)
			{
				SendMessage(hwnd, WM_SETFONT, (WPARAM)m_hFont, 0);
			}

			SetWindowTextW(hwnd, sz);
			ShowWindow(hwnd, SW_SHOW);
		}
	}
}

void SearchDlg::PrintResults(HWND hwndDlg)
{
	if (ULONG n = m_task.m_nResults)
	{
		SEARCH_RESULT* psr = (SEARCH_RESULT*)m_task.m_srh.getBase(), *_psr = psr;

		BOOL bOffset = m_task.m_cbStr;
		ULONG cbex = bOffset ? 19 : 2;
		ULONG i = n, len = 1;

		do 
		{
			len += cbex + _psr++->name->get_Length();
		} while (--i);

		if (PWSTR buf = new WCHAR[len])
		{
			PWSTR sz = buf;
			do 
			{
				if (bOffset)
				{
					sz += swprintf(sz, L"%16I64x ", psr->offset);
				}

				len -= cbex;

				sz = psr++->name->Print(sz, len);

				*sz++ = '\r', *sz++ = '\n';

			} while (--n);

			*sz = 0;

			if (HWND hwnd = CreateWindowExW(0, WC_EDIT, L"Results", 
				WS_OVERLAPPEDWINDOW|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE|ES_AUTOHSCROLL|ES_AUTOVSCROLL, 
				CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndDlg, 0, 0, 0))
			{
				if (m_hFont)
				{
					SendMessage(hwnd, WM_SETFONT, (WPARAM)m_hFont, 0);
				}

				SetWindowTextW(hwnd, buf);
				ShowWindow(hwnd, SW_SHOW);
			}

			delete [] buf;
		}
	}
}

void SearchDlg::ShowStatus(HWND hwndDlg, ULONG ticks, BOOL bFinal)
{
	WCHAR sz[128];
	swprintf(sz, L"%u: %u/%u %I64u (%u)(%u)\n", ticks, m_task.m_nFolders, m_task.m_nFiles, 
		m_task.m_TotalSize, m_task.m_MaxTasks, m_task.m_PeakIoCount);
	SetDlgItemText(hwndDlg, IDC_STATIC1, sz);
	SEARCH_RESULT *psr;
	ULONG n = m_task.getResults(&psr);
	swprintf(sz, L"Search - %u found%s", n, m_task.Quit() ? L" [canceled]" : L"");
	SetWindowTextW(hwndDlg, sz);

	if (bFinal && n)
	{
		EnableWindow(GetDlgItem(hwndDlg, IDC_BUTTON1), TRUE);

		qsort(psr, n, sizeof(SEARCH_RESULT), QSORTFN(SEARCH_RESULT::compare));
	}

	if (n)
	{
		ListView_SetItemCountEx(GetDlgItem(hwndDlg, IDC_LIST1), n, bFinal ? 0 : LVSICF_NOINVALIDATEALL);
	}
}

void SearchDlg::OnCaseSensetive(HWND hwndEdit, HWND hwndChk)
{
	ULONG NewStyle, Style = GetWindowLongW(hwndEdit, GWL_STYLE);

	switch (SendMessage(hwndChk, BM_GETCHECK, 0, 0))
	{
	case BST_CHECKED:
		NewStyle = Style & ~ES_UPPERCASE;
		break;
	case BST_UNCHECKED:
		NewStyle = Style | ES_UPPERCASE;
		break;
	default: return ;
	}

	if (NewStyle != Style)
	{
		SetWindowLongW(hwndEdit, GWL_STYLE, NewStyle);

		if (NewStyle & ES_UPPERCASE)
		{
			if (ULONG len = GetWindowTextLength(hwndEdit))
			{
				PWSTR sz = (PWSTR)alloca( (len + 1) * sizeof(WCHAR));
				GetWindowTextW(hwndEdit, sz, len + 1);
				SetWindowTextW(hwndEdit, sz);
			}
		}
	}
}

void SearchDlg::OnEncodingChanged(HWND hwndEdit, HWND hwndCombo, HWND hwndCheck)
{
	int Encoding = ComboBox_GetCurSel(hwndCombo), _Encoding = m_Encoding;

	if (_Encoding == Encoding)
	{
		return ;
	}

	m_Encoding = Encoding;

	if (Encoding == i_UTF8)
	{
		// for utf-8 only case sensetive search
		SendMessage(hwndCheck, BM_SETCHECK, BST_CHECKED, 0);
		EnableWindow(hwndCheck, FALSE);
		ULONG Style = GetWindowLongW(hwndEdit, GWL_STYLE);
		if (Style & ES_UPPERCASE)
		{
			SetWindowLongW(hwndEdit, GWL_STYLE, Style & ~ES_UPPERCASE);
		}
	}
	else
	{
		EnableWindow(hwndCheck, TRUE);
	}

	// for hex/dword search case no sense
	ShowWindow(hwndCheck, Encoding >= i_HEX ? SW_HIDE : SW_SHOW);

	ULONG len = GetWindowTextLengthW(hwndEdit);

	if (!len)
	{
		return ;
	}

	PWSTR sz = (PWSTR)alloca((len + 1)* sizeof(WCHAR)), wz = 0;
	GetWindowTextW(hwndEdit, sz, len + 1);

	PVOID pv = 0;
	ULONG cb = 0;
	UINT CodePage = 0;
	BOOL UsedDefaultChar = FALSE, *lpUsedDefaultChar = &UsedDefaultChar;

	if ((_Encoding == i_DWORD && Encoding != i_HEX) || 
		(Encoding == i_DWORD && _Encoding != i_HEX))
	{
		goto __set;
	}

	if (Encoding == i_HEX)
	{
		switch (_Encoding)
		{
		case i_ANSI:
			CodePage = CP_ACP;
			break;
		case i_OEM:
			CodePage = CP_OEMCP;
			break;
		case i_UTF8:
			CodePage = CP_UTF8;
			lpUsedDefaultChar = 0;
			break;
		case i_UTF16:
			pv = sz, cb = len * sizeof(WCHAR);
			goto __0;
		case i_DWORD:
			if (ULONG n = wcstoul(sz, &sz, 16))
			{
				if (!*sz)
				{
					pv = &n, cb = sizeof(n);
					goto __0;
				}
			}
			[[fallthrough]];
		default:
			return;
		}

		while ((cb = WideCharToMultiByte(CodePage, 0, sz, len, (PSTR)pv, cb, 0, lpUsedDefaultChar)) && !UsedDefaultChar)
		{
			if (pv)
			{
__0:
				sz = 0, len = 0;
				while (CryptBinaryToStringW((PBYTE)pv, cb, CRYPT_STRING_HEX|CRYPT_STRING_NOCRLF, sz, &len))
				{
					if (sz)
					{
						wz = sz;
						goto __set;
					}

					sz = (PWSTR)alloca(len* sizeof(WCHAR));
				}
			}

			pv = alloca(cb);
		}
	}
	else if (_Encoding == i_HEX)
	{
		while (CryptStringToBinaryW(sz, len, CRYPT_STRING_HEX, (PBYTE)pv, &cb, 0, 0))
		{
			if (pv)
			{
				sz = 0;

				switch (Encoding)
				{
				case i_ANSI:
					CodePage = CP_ACP;
					break;
				case i_OEM:
					CodePage = CP_OEMCP;
					break;
				case i_UTF8:
					CodePage = CP_UTF8;
					break;
				case i_UTF16:
					if (!(cb & (sizeof(WCHAR) - 1)))
					{
						len = cb / sizeof(WCHAR);
						sz = (PWSTR)pv;
						goto __1;
					}
					goto __set;
				case i_DWORD:
					if (cb == sizeof(ULONG))
					{
						wz = (PWSTR)alloca(9 * sizeof(WCHAR));
						_itow(*(ULONG*)pv, wz, 16);
					}
					goto __set;
				default: return;
				}

				while (len = MultiByteToWideChar(CodePage, MB_ERR_INVALID_CHARS, (PSTR)pv, cb, sz, len))
				{
					if (sz)
					{
__1:
						sz[len] = 0;
						wz = sz;
						goto __set;
					}

					sz = (PWSTR)alloca((len + 1) * sizeof(WCHAR));
				}

				break;
			}

			pv = alloca(cb + sizeof(WCHAR));
		}
	}
	else
	{
		return;
	}
__set:
	SetWindowTextW(hwndEdit, wz);
}

PWSTR SearchDlg::ReadData(HANDLE hFile, PLARGE_INTEGER ByteOffset, PBYTE pb, ULONG& cb, PWSTR pszText, ULONG& cchTextMax)
{
	IO_STATUS_BLOCK iosb;
	if (0 <= NtReadFile(hFile, 0, 0, 0, &iosb, pb, cb, ByteOffset, 0))
	{
		if (cb = (ULONG)iosb.Information)
		{
			ULONG cch = cchTextMax;
			if (CryptBinaryToStringW(pb, cb, CRYPT_STRING_HEX|CRYPT_STRING_NOCRLF, pszText, &cch))
			{
				cchTextMax -= cch;
				pszText += cch;
			}
			else
			{
				*pszText = 0;
			}
		}
	}

	return pszText;
}

void SearchDlg::SetInfoTip(PWSTR pszText, ULONG cchTextMax, SEARCH_RESULT *psr)
{
	LARGE_INTEGER ByteOffset;
	ByteOffset.QuadPart = psr->offset;

	NAME_COMPONENT* name = psr->name;

	if (ByteOffset.QuadPart < 0)
	{
		// this is folder
		_snwprintf(pszText, cchTextMax, L"%wZ", name->getCUS());
		return ;
	}

	ULONG cch = name->get_Length();
	PWSTR FilePath = (PWSTR)alloca((cch + 1) * sizeof(WCHAR));
	*name->Print(FilePath, cch) = 0;

	UNICODE_STRING ObjectName;

	if (RtlDosPathNameToNtPathName_U(FilePath, &ObjectName, 0, 0))
	{
		OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName };
		HANDLE hFile;
		IO_STATUS_BLOCK iosb;

		NTSTATUS status = NtOpenFile(&hFile, FILE_READ_DATA|SYNCHRONIZE, &oa, &iosb,
			FILE_SHARE_VALID_FLAGS, FILE_SYNCHRONOUS_IO_ALERT|FILE_OPEN_FOR_BACKUP_INTENT);

		RtlFreeUnicodeString(&ObjectName);

		if (0 <= status)
		{
			BYTE buf1[8], buf2[8];
			ULONG cb1 = sizeof(buf1), cb2 = 0;
			if (ByteOffset.QuadPart < sizeof(buf1))
			{
				cb1 = ByteOffset.LowPart;
				ByteOffset.LowPart = 0;
			}
			else
			{
				ByteOffset.QuadPart -= sizeof(buf1);
			}

			if (cb1)
			{
				pszText = ReadData(hFile, &ByteOffset, buf1, cb1, pszText, cchTextMax);
			}

			if (3 < cchTextMax)
			{
				cb2 = sizeof(buf2);

				*pszText++ = ' ', *pszText++ = '*', *pszText++ = ' ', cchTextMax -= 3;

				ByteOffset.QuadPart = psr->offset + m_cbStr;

				pszText = ReadData(hFile, &ByteOffset, buf2, cb2, pszText, cchTextMax);
			}

			NtClose(hFile);

			if (RTL_NUMBER_OF(buf1) + RTL_NUMBER_OF(buf2) + 8 < cchTextMax)
			{
				*pszText++ = ' ', *pszText++ = '|', *pszText++ = ' ', cchTextMax -= 3;

				PWSTR psz = pszText;
				ULONG len = 0;
				ULONG cp = 0;

				switch (m_SearchEncoding)
				{
				case i_HEX:
					pszText[-3] = 0;
					return;

				case i_UTF16:
					memcpy(pszText, buf1, cb1);
					pszText += cb1 / sizeof(WCHAR);
					*pszText++ = ' ', *pszText++ = '*', *pszText++ = ' ';
					memcpy(pszText, buf2, cb2);
					len = cb1 / sizeof(WCHAR) + 3 + cb2 / sizeof(WCHAR);
					break;

				case i_ANSI:
					cp = CP_ACP;
					break;
				case i_UTF8:
					cp = CP_UTF8;
					break;
				case i_OEM:
					cp = CP_OEMCP;
					break;
				}

				if (!len)//m_SearchEncoding != i_UTF16
				{
					cch = MultiByteToWideChar(cp, 0, (PCCH)buf1, cb1, pszText, cchTextMax);
					pszText += cch, len += cch;

					if ((cchTextMax -= cch) > 3)
					{
						*pszText++ = ' ', *pszText++ = '*', *pszText++ = ' ', cchTextMax -= 3, len += 3;
						cch = MultiByteToWideChar(cp, 0, (PCCH)buf2, cb2, pszText, cchTextMax);
						pszText += cch, len += cch;
						cchTextMax -= cch;
					}
				}

				psz[len] = 0;

				if (len)
				{
					do 
					{
						if (*psz < ' ')
						{
							*psz = '.';
						}
					} while (psz++, --len);
				}
			}

		}
	}
}

INT_PTR SearchDlg::DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_USER+WM_QUIT:
		OnTask(hwndDlg, FALSE);
		ShowStatus(hwndDlg, (ULONG)wParam, TRUE);
		ShowLog(hwndDlg);
		break;

	case WM_DESTROY:
		if (m_hFont) DeleteObject(m_hFont);
		break;

	case WM_TIMER:
		if (m_bInSearch)
		{
			ShowStatus(hwndDlg, GetTickCount() - m_task.m_time, FALSE);
		}
		break;

	case WM_INITDIALOG:
		OnInitDialog(hwndDlg);
		break;

	case WM_COMMAND:
		switch (wParam)
		{
		case IDCANCEL:
			if (m_bInSearch)
			{
				m_task.Stop();
			}
			break;

		case IDOK:
			OnOk(hwndDlg);
			break;

		case MAKEWPARAM(IDC_BUTTON1, BN_CLICKED):
			PrintResults(hwndDlg);
			break;

		case MAKEWPARAM(IDC_COMBO1, CBN_SELCHANGE):
			OnEncodingChanged(GetDlgItem(hwndDlg, IDC_EDIT3), (HWND)lParam, GetDlgItem(hwndDlg, IDC_CHECK2));
			break;

		case MAKEWPARAM(IDC_CHECK2, BN_CLICKED):
			OnCaseSensetive(GetDlgItem(hwndDlg, IDC_EDIT3), (HWND)lParam);
			break;
		}
		break;

	case WM_CLOSE:
		if (m_bInSearch)
		{
			m_task.Stop();
		}
		else
		{
			EndDialog(hwndDlg, 1);
		}
		return TRUE;

	case WM_NOTIFY:

		if (((LPNMHDR)lParam)->idFrom == IDC_LIST1)
		{
			PWSTR pszText;
			SEARCH_RESULT *psr;
			ULONG cchTextMax, iItem, nItems = m_task.getResults(&psr);

			switch (((LPNMHDR)lParam)->code)
			{
			case LVN_GETINFOTIP:
				if (
					(cchTextMax = ((LPNMLVGETINFOTIP)lParam)->cchTextMax)
					&&
					(pszText = ((LPNMLVGETINFOTIP)lParam)->pszText)
					&&
					(iItem = ((LPNMLVGETINFOTIP)lParam)->iItem) < nItems
					)
				{
					SetInfoTip(pszText, cchTextMax, psr + iItem);
				}
				break;

			case LVN_GETDISPINFO:

				if (
					(((NMLVDISPINFO*)lParam)->item.mask & LVIF_TEXT) 
					&&
					(cchTextMax = ((NMLVDISPINFO*)lParam)->item.cchTextMax)
					&&
					(iItem = ((NMLVDISPINFO*)lParam)->item.iItem) < nItems
					)
				{			
					pszText = ((NMLVDISPINFO*)lParam)->item.pszText;

					pszText[0] = 0;

					psr += iItem;

					switch (((NMLVDISPINFO*)lParam)->item.iSubItem)
					{
					case 0:
						if (0 <= psr->offset)
						{
							_snwprintf(pszText, cchTextMax, L"%I64x", psr->offset);
						}
						break;
					case 1:
						*(pszText = psr->name->Print(pszText, --cchTextMax)) = 0;
						break;
					}
				}
			}
		}
		break;
	}
	return ZDlg::DialogProc(hwndDlg, uMsg, wParam, lParam);
}

void IoPostInit();

extern "C"
{
	extern PVOID __imp_CryptBinaryToStringW;

	BOOL ( WINAPI * orig_CryptBinaryToStringW)(
		__in       const BYTE* pbBinary,
		__in       DWORD cbBinary,
		__in       DWORD dwFlags,
		__out_opt  PWSTR pszString,
		__inout    DWORD* pcchString
		);
}

BOOL WINAPI XpCryptBinaryToStringW(
								__in       const BYTE* pbBinary,
								__in       DWORD cbBinary,
								__in       DWORD dwFlags,
								__out_opt  PWSTR pszString,
								__inout    DWORD* pcchString
								)
{
	if (dwFlags != (CRYPT_STRING_HEX|CRYPT_STRING_NOCRLF))
	{
		return orig_CryptBinaryToStringW(pbBinary, cbBinary, dwFlags, pszString, pcchString);
	}

	DWORD cchString = cbBinary ? cbBinary * 3 : 1;


	if (!pszString)
	{
		*pcchString = cchString;
		return TRUE;
	}

	if (*pcchString < cchString)
	{
		*pcchString = cchString;
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}

	if (cbBinary)
	{
		do 
		{
			pszString += swprintf(pszString, L"%02x", *pbBinary++);
			*pszString++ = ' ';
		} while (--cbBinary);
		--pszString;
	}

	*pszString = 0;
	*pcchString = cchString - 1;

	return TRUE;
}

#ifdef _X86_
#pragma comment(linker, "/alternatename:___imp_CryptBinaryToStringW=__imp__CryptBinaryToStringW@20")
#endif

void RedirectCryptBinaryToString()
{
	ULONG op;
	if (VirtualProtect(&__imp_CryptBinaryToStringW, sizeof(__imp_CryptBinaryToStringW), PAGE_READWRITE, &op))
	{
		*(void**)&orig_CryptBinaryToStringW = __imp_CryptBinaryToStringW;

		__imp_CryptBinaryToStringW = XpCryptBinaryToStringW;

		if (op != PAGE_READWRITE)
		{
			VirtualProtect(&__imp_CryptBinaryToStringW, sizeof(__imp_CryptBinaryToStringW), op, &op);
		}
	}
}

BOOL MyIsNameInExpression(PCWSTR Expression, PUNICODE_STRING Name);

void IO_RUNDOWN::RundownCompleted()
{
	CThreadPool::Stop();
	destroyterm();
	ExitProcess(0);
}

namespace CThreadPool {
	extern HANDLE m_hiocp;
};

void WINAPI ep(void*)
{
	ULONG M;
	RtlGetNtVersionNumbers(&M, 0, 0);
	if (M < 6)
	{
		RedirectCryptBinaryToString();
	}

	initterm();
	IoPostInit();
	CThreadPool::Start();

	BOOLEAN b;
	if (0 > RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, TRUE, FALSE, &b))
	{
		MessageBoxW(0, L"Without this some files can be not opened.", L"Process have not Backup Privilege", MB_ICONWARNING|MB_OK);
	}

	if (SearchDlg* p = new SearchDlg)
	{
		p->DoModal((HINSTANCE)&__ImageBase, MAKEINTRESOURCE(IDD_DIALOG3), HWND_DESKTOP, 0);
		p->Release();
	}

	IO_RUNDOWN::g_IoRundown.BeginRundown();
}

_NT_END


