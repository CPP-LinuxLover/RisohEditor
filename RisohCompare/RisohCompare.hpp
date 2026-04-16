// RisohCompare.hpp --- RisohCompare main header
//////////////////////////////////////////////////////////////////////////////
// RisohEditor --- Another free Win32 resource editor
// Copyright (C) 2017-2025 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
// License: GPL-3 or later
//
// This header pulls in only the subset of the RisohEditor headers that is
// required by the RisohCompare tool.  It deliberately does NOT include
// MMainWnd.hpp or RisohEditor.hpp so that the compare tool remains
// independent of the full editor's UI infrastructure.
//////////////////////////////////////////////////////////////////////////////

#pragma once

// Windows and standard headers
#include <initguid.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <dlgs.h>
#include <tchar.h>
#include <mmsystem.h>
#include <uxtheme.h>
#include <strsafe.h>
#include <gdiplus.h>
#include <vfw.h>
#include <wininet.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <clocale>
#include <map>
#include <string>
#include <vector>

// Resource constants shared with the main editor
#include "resource.h"

// Core utility layer
#include "MWindowBase.hpp"
#include "MString.hpp"
#include "MByteStream.hpp"
#include "MByteStreamEx.hpp"
#include "MIdOrString.hpp"
#include "MFile.hpp"

// Settings (full struct required because GUI dialog headers include it)
#include "RisohSettings.hpp"

// Resource database and ID constants
#include "ConstantsDB.hpp"

// Resource model layer
#include "ResHeader.hpp"
#include "PackedDIB.hpp"
#include "Res.hpp"
#include "AccelRes.hpp"
#include "IconRes.hpp"
#include "MenuRes.hpp"
#include "MessageRes.hpp"
#include "StringRes.hpp"
#include "DialogRes.hpp"
#include "VersionRes.hpp"
#include "DlgInitRes.hpp"
#include "DlgInit.h"

// Text conversion layer
#include "ResToText.hpp"

// Bitmap / image support (required by MTestDialog via Res.hpp)
#include "MBitmapDx.hpp"

// UI utility classes
#include "MPointSizeRect.hpp"
#include "MScrollBar.hpp"
#include "MResizable.hpp"
#include "MEditCtrl.hpp"
#include "MComboBox.hpp"
#include "MComboBoxAutoComplete.hpp"
#include "MToolBarCtrl.hpp"
#include "Common.hpp"

// OLE host support for AtlAxWin controls in RAD designer
#include "MOleHost.hpp"

// GUI dialog editors (OnGuiEdit)
#include "MEditAccelDlg.hpp"
#include "MEditMenuDlg.hpp"
#include "MEditToolbarDlg.hpp"
#include "MDlgInitDlg.hpp"
#include "MStringsDlg.hpp"
#include "MMessagesDlg.hpp"

// RAD dialog designer (OnGuiEdit for RT_DIALOG)
#include "MRubberBand.hpp"
#include "MIndexLabels.hpp"
#include "MCtrlDataDlg.hpp"
#include "MStringListDlg.hpp"
#include "MAddCtrlDlg.hpp"
#include "MDlgPropDlg.hpp"
#include "MCtrlPropDlg.hpp"
#include "MRadWindow.hpp"

// Dialog test / preview (OnTest)
#include "MTestDialog.hpp"
#include "MTestParentWnd.hpp"
#include "MTestMenuDlg.hpp"

// Language entry list — must be defined before Utils.h which uses it in
// "extern std::vector<LANG_ENTRY> g_langs;"
struct LANG_ENTRY
{
	WORD     LangID;
	MStringW str;
	bool operator<(const LANG_ENTRY& ent) const { return str < ent.str; }
};

// Utilities shared with the main editor (Utils.h declares helpers used by
// the GUI dialog editors above)
#include "Utils.h"
