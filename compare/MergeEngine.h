// MergeEngine.h --- Binary-level merge engine
//////////////////////////////////////////////////////////////////////////////
// Operates directly on RisohEditor core types (DialogRes, MenuRes, StringRes).
// Applies compare results back to the secondary EntrySet at binary level.
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include "CompareTypes.h"
#include "CompareEngine.h"
#include "TopologyMatcher.h"
#include "../src/Res.hpp"
#include "../src/DialogRes.hpp"
#include "../src/MenuRes.hpp"
#include "../src/StringRes.hpp"
#include <vector>
#include <map>

//////////////////////////////////////////////////////////////////////////////
// Merge result tracking

struct SMergeRecord
{
	enum EMergeAction
	{
		MA_INSERT_CONTROL,      // Inserted a new control into dialog
		MA_UPDATE_CONTROL_TEXT,  // Updated control text
		MA_INSERT_STRING,       // Inserted a string table entry
		MA_UPDATE_STRING,       // Updated a string table entry
		MA_INSERT_MENU_ITEM,    // Inserted a menu item
		MA_UPDATE_MENU_TEXT,    // Updated menu item text
		MA_UPDATE_DIALOG_TITLE, // Updated dialog caption
	};

	EMergeAction eAction;
	UINT nResourceID;       // Dialog/Menu/StringTable block ID
	UINT nItemID;           // Control ID / String ID / Menu ID
	std::wstring wsOldText;
	std::wstring wsNewText;
	std::wstring wsDescription;
};

struct SMergeResult
{
	int nInsertedControls;
	int nUpdatedControls;
	int nInsertedStrings;
	int nUpdatedStrings;
	int nInsertedMenuItems;
	int nUpdatedMenuItems;
	int nUpdatedCaptions;
	std::vector<SMergeRecord> vecRecords;
	std::vector<std::wstring> vecWarnings;

	SMergeResult()
		: nInsertedControls(0)
		, nUpdatedControls(0)
		, nInsertedStrings(0)
		, nUpdatedStrings(0)
		, nInsertedMenuItems(0)
		, nUpdatedMenuItems(0)
		, nUpdatedCaptions(0)
	{
	}

	int TotalChanges() const
	{
		return nInsertedControls + nUpdatedControls +
		       nInsertedStrings + nUpdatedStrings +
		       nInsertedMenuItems + nUpdatedMenuItems +
		       nUpdatedCaptions;
	}
};

//////////////////////////////////////////////////////////////////////////////

class MergeEngine
{
public:
	MergeEngine();
	~MergeEngine();

	// Set the engine to operate on
	void SetCompareEngine(CompareEngine* pEngine);

	// Set the secondary EntrySet that will be modified
	void SetSecondary(EntrySet* pRes, WORD lang);

	// Set the primary EntrySet for reading source data
	void SetPrimary(EntrySet* pRes, WORD lang);

	// Direct context access
	const SLanguageContext& GetPrimaryContext() const { return m_primaryCtx; }
	const SLanguageContext& GetSecondaryContext() const { return m_secondaryCtx; }

	// Apply all modified compare items to secondary
	SMergeResult ApplyAll();

	// Apply a single compare item by index
	bool ApplyItem(int nIndex, SMergeResult& result);

private:
	CompareEngine* m_pEngine;
	EntrySet* m_pPrimary;
	EntrySet* m_pSecondary;
	SLanguageContext m_primaryCtx;
	SLanguageContext m_secondaryCtx;

	// Apply changes grouped by resource type
	void ApplyDialogChanges(SMergeResult& result);
	void ApplyStringChanges(SMergeResult& result);
	void ApplyMenuChanges(SMergeResult& result);

	// Single-item apply helpers
	bool ApplyControlItem(const SCompareItem& item, SMergeResult& result);
	bool ApplyDialogCaptionItem(const SCompareItem& item, SMergeResult& result);
	bool ApplyStringItem(const SCompareItem& item, SMergeResult& result);
	bool ApplyMenuItem(const SCompareItem& item, SMergeResult& result);

	// Write modified resource back to EntryBase::m_data
	bool WriteDialogBack(EntryBase* entry, const DialogRes& dlg);
	bool WriteMenuBack(EntryBase* entry, const MenuRes& menu);
	bool WriteStringBack(EntryBase* entry, WORD wName, StringRes& str);
};
