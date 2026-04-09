// CompareTypes.h --- Comparison-specific types for RC resource comparison
// Only types needed for the comparison/merge workflow live here.
// Resource structures (DialogRes, MenuRes, StringRes, EntrySet) come from
// RisohEditor core (src/).
//////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _INC_WINDOWS
	#include <windows.h>
#endif
#include <vector>
#include <string>
#include <set>

#ifndef BAD_LANG
	#define BAD_LANG 0xFFFF
#endif

//////////////////////////////////////////////////////////////////////////////
// Language resolution context — data-driven language identification
//
// Resolution priority:
//   1. wExplicitLang  (caller override, highest priority)
//   2. wDetectedLang  (auto-detected from EntrySet data)
//   3. BAD_LANG       (unresolved — comparison cannot proceed)

struct SLanguageContext
{
	WORD wExplicitLang;     // Set by caller (BAD_LANG = not set)
	WORD wDetectedLang;     // Auto-detected from resource data
	WORD wEffectiveLang;    // Resolved result (used by engine)

	SLanguageContext()
		: wExplicitLang(BAD_LANG)
		, wDetectedLang(BAD_LANG)
		, wEffectiveLang(BAD_LANG)
	{
	}

	// Resolve effective language from explicit > detected > BAD_LANG
	void Resolve()
	{
		if (wExplicitLang != BAD_LANG)
			wEffectiveLang = wExplicitLang;
		else if (wDetectedLang != BAD_LANG)
			wEffectiveLang = wDetectedLang;
		else
			wEffectiveLang = BAD_LANG;
	}

	bool IsResolved() const { return wEffectiveLang != BAD_LANG; }

	void Reset()
	{
		wExplicitLang = BAD_LANG;
		wDetectedLang = BAD_LANG;
		wEffectiveLang = BAD_LANG;
	}

	void SetExplicit(WORD lang)
	{
		wExplicitLang = lang;
		Resolve();
	}

	void SetDetected(WORD lang)
	{
		wDetectedLang = lang;
		Resolve();
	}
};

//////////////////////////////////////////////////////////////////////////////
// Conflict types — detected when merging primary/secondary resources

enum EConflictType
{
	CT_MACRO_NAME,      // Same macro name defined with different values
	CT_ID_VALUE,        // Same numeric ID assigned to different names
	CT_CONTROL_ID,      // Control ID collision in dialog
};

struct SConflictInfo
{
	EConflictType eType;
	std::wstring wsPrimaryName;     // Macro/resource name in primary
	std::wstring wsSecondaryName;   // Macro/resource name in secondary
	UINT nPrimaryValue;             // Numeric value in primary
	UINT nSecondaryValue;           // Numeric value in secondary
	UINT nDialogID;                 // Owning dialog (for CT_CONTROL_ID)

	SConflictInfo()
		: eType(CT_MACRO_NAME)
		, nPrimaryValue(0)
		, nSecondaryValue(0)
		, nDialogID(0)
	{
	}
};

//////////////////////////////////////////////////////////////////////////////
// Comparison result item — one entry per difference found

enum ECompareItemType
{
	CIT_CONTROL,        // Dialog control
	CIT_STRING,         // String table entry
	CIT_MENU,           // Menu item
	CIT_DIALOG_CAPTION, // Dialog caption text
	CIT_RESOURCE,       // Generic resource (binary comparison by unique ID)
};

enum ECompareItemStatus
{
	CIS_ADDED,          // Exists in primary but not in secondary
	CIS_CHANGED,        // Exists in both but text/position differs
	CIS_MATCHED,        // Identical in both
};

struct SCompareItem
{
	ECompareItemType eType;
	ECompareItemStatus eStatus;

	// Resource identification
	UINT nDialogID;             // Owning dialog ID (controls/caption)
	UINT nControlID;            // Control ID (controls only)
	UINT nStringID;             // String ID (strings only)
	UINT nMenuID;               // Menu item ID (menus only)
	int nControlIndex;          // Control index in dialog (for IDC=-1 STATICs)
	int nMenuItemIndex;         // Menu item index (for separator/popup)

	// Text comparison
	std::wstring wsPrimaryText;     // Text in primary language
	std::wstring wsSecondaryText;   // Text in secondary language (empty if missing)

	// Generic resource identification (for CIT_RESOURCE)
	std::wstring wsResType;         // Resource type display name
	std::wstring wsResName;         // Resource name display string

	// Translation state
	bool bModified;             // User has reviewed/translated this item
	bool bNeedsLayoutAdjust;    // Control position needs adjustment after merge

	SCompareItem()
		: eType(CIT_CONTROL)
		, eStatus(CIS_ADDED)
		, nDialogID(0)
		, nControlID(0)
		, nStringID(0)
		, nMenuID(0)
		, nControlIndex(-1)
		, nMenuItemIndex(-1)
		, bModified(false)
		, bNeedsLayoutAdjust(false)
	{
	}
};

//////////////////////////////////////////////////////////////////////////////
// Conflict resolution strategy (used by CConflictResolveDialog)

enum EConflictResolution
{
	CR_KEEP_SECONDARY,  // Keep secondary language value (ignore conflict)
	CR_USE_PRIMARY,     // Use primary language value
	CR_AUTO_REASSIGN,   // Automatically assign a new ID
	CR_MANUAL,          // User manually specifies value
};

struct SConflictResolutionEntry
{
	SConflictInfo conflict;
	EConflictResolution resolution;
	UINT nManualValue;

	SConflictResolutionEntry()
		: resolution(CR_KEEP_SECONDARY)
		, nManualValue(0)
	{
	}
};

//////////////////////////////////////////////////////////////////////////////
// Inline RECT helper functions (replace MFC CRect methods)

inline LONG RectWidth(const RECT& r) { return r.right - r.left; }
inline LONG RectHeight(const RECT& r) { return r.bottom - r.top; }

inline void SetRect(RECT& r, LONG l, LONG t, LONG right, LONG bottom)
{
	r.left = l; r.top = t; r.right = right; r.bottom = bottom;
}

inline void InflateRect(RECT& r, LONG dx, LONG dy)
{
	r.left -= dx; r.top -= dy; r.right += dx; r.bottom += dy;
}

inline void OffsetRect(RECT& r, LONG dx, LONG dy)
{
	r.left += dx; r.top += dy; r.right += dx; r.bottom += dy;
}

inline BOOL PtInRect(const RECT& r, POINT pt)
{
	return pt.x >= r.left && pt.x < r.right &&
	       pt.y >= r.top && pt.y < r.bottom;
}

inline BOOL IsRectEmpty(const RECT& r)
{
	return r.left >= r.right || r.top >= r.bottom;
}
