// RisohCompareSettings.hpp --- Minimum settings subset for RisohCompare
//////////////////////////////////////////////////////////////////////////////
// RisohEditor --- Another free Win32 resource editor
// Copyright (C) 2017-2025 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
// License: GPL-3 or later
//
// This file documents the minimum subset of RisohSettings fields actually
// required by the GUI dialog editors (OnGuiEdit) and dialog test/preview
// (OnTest) functionality used in RisohCompare.
//
// It is NOT included by the build directly; the full RisohSettings.hpp from
// src/ is used instead.  Future refactoring can promote this to a true
// drop-in replacement once all dependents are audited.
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include "MWindowBase.hpp"
#include "MIdOrString.hpp"
#include <vector>
#include <map>

//////////////////////////////////////////////////////////////////////////////
// Minimum type aliases (same names as RisohSettings.hpp)

typedef std::map<MString, MString>      encoding_map_type;
typedef std::map<MStringA, MStringA>    id_map_type;
typedef std::map<MString, MString>      macro_map_type;
typedef std::vector<MString>            include_dirs_type;
typedef std::vector<MString>            captions_type;

//////////////////////////////////////////////////////////////////////////////
// Minimum RisohSettings fields required by OnGuiEdit, OnTest and the
// GUI dialog editors they invoke.

struct RisohCompareSettings
{
	// ---- Resource dumping (DialogRes, AccelRes, MenuRes, ...) ----
	BOOL	bAlwaysControl;		// always show CONTROL statements?
	BOOL	bUseBeginEnd;		// use BEGIN/END instead of braces?
	BOOL	bUseMSMSGTABLE;		// use Microsoft MESSAGETABLE format?
	BOOL	bWrapManifest;		// wrap manifest with #ifndef MSVC … #endif?

	// ---- ID display (ConstantsDB, OnGuiEdit) ----
	BOOL	bHideID;			// don't show ID macros?
	BOOL	bUseIDC_STATIC;		// use IDC_STATIC?

	// ---- RAD dialog designer window (MRadWindow) ----
	BOOL	bResumeWindowPos;	// resume the last RAD window position?
	BOOL	bShowDotsOnDialog;	// show alignment dots on the dialog?
	INT		nComboHeight;		// combobox dropdown height
	INT		nRadLeft;			// RAD window X coordinate
	INT		nRadTop;			// RAD window Y coordinate
	MString	strAtlAxWin;		// ATL OLE control class name

	// ---- Caption history (MRadWindow, MCtrlPropDlg) ----
	captions_type	captions;

	// ---- Text encoding (GetResTypeEncoding in RisohEditor.cpp) ----
	encoding_map_type	encoding_map;

	// ---- ID / macro resolution (ConstantsDB) ----
	id_map_type			id_map;
	macro_map_type		macros;
	include_dirs_type	includes;

	// ---- Font substitution (DialogRes) ----
	MString strFontReplaceFrom1;
	MString strFontReplaceFrom2;
	MString strFontReplaceFrom3;
	MString strFontReplaceTo1;
	MString strFontReplaceTo2;
	MString strFontReplaceTo3;

	// ---- UI language ----
	DWORD ui_lang;

	RisohCompareSettings()
		: bAlwaysControl(FALSE)
		, bUseBeginEnd(FALSE)
		, bUseMSMSGTABLE(FALSE)
		, bWrapManifest(TRUE)
		, bHideID(FALSE)
		, bUseIDC_STATIC(FALSE)
		, bResumeWindowPos(TRUE)
		, bShowDotsOnDialog(TRUE)
		, nComboHeight(300)
		, nRadLeft(CW_USEDEFAULT)
		, nRadTop(CW_USEDEFAULT)
		, ui_lang(0)
	{
		ResetEncoding();
	}

	// Append a caption to the remembered caption list.
	void AddCaption(LPCTSTR pszCaption)
	{
		if (!pszCaption || !pszCaption[0])
			return;
		for (size_t i = 0; i < captions.size(); ++i)
		{
			if (captions[i] == pszCaption)
			{
				captions.erase(captions.begin() + (INT)i);
				break;
			}
		}
		captions.insert(captions.begin(), pszCaption);
	}

	// Populate default text-encoding entries.
	void ResetEncoding()
	{
		encoding_map.clear();
		encoding_map[L"RISOHTEMPLATE"]	= L"utf8";
		encoding_map[L"REGISTRY"]		= L"ansi";
		encoding_map[L"XML"]			= L"utf8";
		encoding_map[L"XSLT"]			= L"utf8";
		encoding_map[L"SCHEMA"]			= L"utf8";
		encoding_map[L"REGINST"]		= L"ansi";
	}

	// Add IDC_STATIC to the ID map.
	void AddIDC_STATIC()
	{
		id_map["IDC_STATIC"] = "-1";
	}

	// Return true when the ID map contains only the synthetic IDC_STATIC.
	bool IsIDMapEmpty() const
	{
		if (id_map.empty())
			return true;
		if (id_map.size() == 1 && id_map.begin()->first == "IDC_STATIC")
			return true;
		return false;
	}
};
