// MergeEngine.cpp --- Binary-level merge engine implementation
//////////////////////////////////////////////////////////////////////////////

#include "MergeEngine.h"
#include "MByteStreamEx.hpp"
#include <algorithm>
#include <map>
#include <set>

//////////////////////////////////////////////////////////////////////////////

MergeEngine::MergeEngine()
	: m_pEngine(NULL)
	, m_pPrimary(NULL)
	, m_pSecondary(NULL)
{
}

MergeEngine::~MergeEngine()
{
}

void MergeEngine::SetCompareEngine(CompareEngine* pEngine)
{
	m_pEngine = pEngine;
}

void MergeEngine::SetSecondary(EntrySet* pRes, WORD lang)
{
	m_pSecondary = pRes;
	m_secondaryCtx.SetExplicit(lang);
}

void MergeEngine::SetPrimary(EntrySet* pRes, WORD lang)
{
	m_pPrimary = pRes;
	m_primaryCtx.SetExplicit(lang);
}

//////////////////////////////////////////////////////////////////////////////
// Apply all modified items

SMergeResult MergeEngine::ApplyAll()
{
	SMergeResult result;

	if (!m_pEngine || !m_pPrimary || !m_pSecondary)
		return result;

	const std::vector<SCompareItem>& items = m_pEngine->GetResults();

	for (size_t i = 0; i < items.size(); ++i)
	{
		if (!items[i].bModified)
			continue;
		ApplyItem((int)i, result);
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////////
// Apply a single item

bool MergeEngine::ApplyItem(int nIndex, SMergeResult& result)
{
	if (!m_pEngine)
		return false;

	const std::vector<SCompareItem>& items = m_pEngine->GetResults();
	if (nIndex < 0 || nIndex >= (int)items.size())
		return false;

	const SCompareItem& item = items[nIndex];

	switch (item.eType)
	{
	case CIT_CONTROL:
		return ApplyControlItem(item, result);
	case CIT_DIALOG_CAPTION:
		return ApplyDialogCaptionItem(item, result);
	case CIT_STRING:
		return ApplyStringItem(item, result);
	case CIT_MENU:
		return ApplyMenuItem(item, result);
	default:
		return false;
	}
}

//////////////////////////////////////////////////////////////////////////////
// Dialog control: insert missing or update text

bool MergeEngine::ApplyControlItem(const SCompareItem& item, SMergeResult& result)
{
	MIdOrString dlgName(item.nDialogID);
	EntryBase* secEntry = m_pSecondary->find(
		ET_LANG, RT_DIALOG, dlgName, m_secondaryCtx.wEffectiveLang);

	if (item.eStatus == CIS_ADDED)
	{
		// Control needs to be inserted 鈥?get from primary
		EntryBase* priEntry = m_pPrimary->find(
			ET_LANG, RT_DIALOG, dlgName, m_primaryCtx.wEffectiveLang);
		if (!priEntry)
			return false;

		DialogRes priDlg;
		MByteStreamEx priStream(priEntry->m_data);
		if (!priDlg.LoadFromStream(priStream))
			return false;

		if (item.nControlIndex < 0 || item.nControlIndex >= (int)priDlg.size())
			return false;

		const DialogItem& srcItem = priDlg[item.nControlIndex];

		if (!secEntry)
		{
			// Secondary dialog doesn't exist at all 鈥?create from primary
			DialogRes newDlg = priDlg;
			// Update title text if user edited it
			for (size_t i = 0; i < newDlg.size(); ++i)
			{
				if ((int)i == item.nControlIndex && !item.wsSecondaryText.empty())
				{
					newDlg[i].m_title = MIdOrString(item.wsSecondaryText.c_str());
				}
			}
			EntryBase* newEntry = m_pSecondary->add_lang_entry(
				RT_DIALOG, dlgName, m_secondaryCtx.wEffectiveLang);
			if (!newEntry)
				return false;
			WriteDialogBack(newEntry, newDlg);
		}
		else
		{
			// Insert control into existing secondary dialog
			DialogRes secDlg;
			MByteStreamEx secStream(secEntry->m_data);
			if (!secDlg.LoadFromStream(secStream))
				return false;

			DialogItem newItem = srcItem;
			if (!item.wsSecondaryText.empty())
				newItem.m_title = MIdOrString(item.wsSecondaryText.c_str());

			secDlg.m_items.push_back(newItem);
			WriteDialogBack(secEntry, secDlg);
		}

		SMergeRecord rec;
		rec.eAction = SMergeRecord::MA_INSERT_CONTROL;
		rec.nResourceID = item.nDialogID;
		rec.nItemID = item.nControlID;
		rec.wsNewText = item.wsSecondaryText.empty() ?
			item.wsPrimaryText : item.wsSecondaryText;
		result.vecRecords.push_back(rec);
		result.nInsertedControls++;
		return true;
	}
	else if (item.eStatus == CIS_CHANGED)
	{
		if (!secEntry)
			return false;

		DialogRes secDlg;
		MByteStreamEx secStream(secEntry->m_data);
		if (!secDlg.LoadFromStream(secStream))
			return false;

		// Find the matching control in secondary by ID
		bool found = false;
		for (size_t i = 0; i < secDlg.size(); ++i)
		{
			if (secDlg[i].m_id == item.nControlID)
			{
				SMergeRecord rec;
				rec.eAction = SMergeRecord::MA_UPDATE_CONTROL_TEXT;
				rec.nResourceID = item.nDialogID;
				rec.nItemID = item.nControlID;
				rec.wsOldText = secDlg[i].m_title.m_str;
				rec.wsNewText = item.wsSecondaryText;

				secDlg[i].m_title = MIdOrString(item.wsSecondaryText.c_str());
				found = true;

				result.vecRecords.push_back(rec);
				break;
			}
		}

		if (found)
		{
			WriteDialogBack(secEntry, secDlg);
			result.nUpdatedControls++;
		}
		return found;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////
// Dialog caption

bool MergeEngine::ApplyDialogCaptionItem(const SCompareItem& item, SMergeResult& result)
{
	MIdOrString dlgName(item.nDialogID);
	EntryBase* secEntry = m_pSecondary->find(
		ET_LANG, RT_DIALOG, dlgName, m_secondaryCtx.wEffectiveLang);

	if (item.eStatus == CIS_ADDED && !secEntry)
	{
		// Need to create the entire dialog from primary
		EntryBase* priEntry = m_pPrimary->find(
			ET_LANG, RT_DIALOG, dlgName, m_primaryCtx.wEffectiveLang);
		if (!priEntry)
			return false;

		DialogRes priDlg;
		MByteStreamEx priStream(priEntry->m_data);
		if (!priDlg.LoadFromStream(priStream))
			return false;

		if (!item.wsSecondaryText.empty())
			priDlg.m_title = MIdOrString(item.wsSecondaryText.c_str());

		EntryBase* newEntry = m_pSecondary->add_lang_entry(
			RT_DIALOG, dlgName, m_secondaryCtx.wEffectiveLang);
		if (!newEntry)
			return false;

		WriteDialogBack(newEntry, priDlg);

		SMergeRecord rec;
		rec.eAction = SMergeRecord::MA_UPDATE_DIALOG_TITLE;
		rec.nResourceID = item.nDialogID;
		rec.wsNewText = item.wsSecondaryText.empty() ?
			item.wsPrimaryText : item.wsSecondaryText;
		result.vecRecords.push_back(rec);
		result.nUpdatedCaptions++;
		return true;
	}
	else if (secEntry)
	{
		DialogRes secDlg;
		MByteStreamEx secStream(secEntry->m_data);
		if (!secDlg.LoadFromStream(secStream))
			return false;

		SMergeRecord rec;
		rec.eAction = SMergeRecord::MA_UPDATE_DIALOG_TITLE;
		rec.nResourceID = item.nDialogID;
		rec.wsOldText = secDlg.m_title.m_str;
		rec.wsNewText = item.wsSecondaryText;

		secDlg.m_title = MIdOrString(item.wsSecondaryText.c_str());
		WriteDialogBack(secEntry, secDlg);

		result.vecRecords.push_back(rec);
		result.nUpdatedCaptions++;
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////
// String table entry

bool MergeEngine::ApplyStringItem(const SCompareItem& item, SMergeResult& result)
{
	// Compute the string table block name from string ID
	WORD wName = (item.nStringID / 16) + 1;
	MIdOrString blockName(wName);

	EntryBase* secEntry = m_pSecondary->find(
		ET_LANG, RT_STRING, blockName, m_secondaryCtx.wEffectiveLang);

	if (item.eStatus == CIS_ADDED && !secEntry)
	{
		// Create a new string table block
		StringRes str;
		str.m_map[item.nStringID] = MStringW(item.wsSecondaryText.empty() ?
			item.wsPrimaryText.c_str() : item.wsSecondaryText.c_str());

		EntryBase* newEntry = m_pSecondary->add_lang_entry(
			RT_STRING, blockName, m_secondaryCtx.wEffectiveLang);
		if (!newEntry)
			return false;

		WriteStringBack(newEntry, wName, str);

		SMergeRecord rec;
		rec.eAction = SMergeRecord::MA_INSERT_STRING;
		rec.nItemID = item.nStringID;
		rec.wsNewText = item.wsSecondaryText.empty() ?
			item.wsPrimaryText : item.wsSecondaryText;
		result.vecRecords.push_back(rec);
		result.nInsertedStrings++;
		return true;
	}
	else if (secEntry)
	{
		StringRes str;
		MByteStreamEx secStream(secEntry->m_data);
		if (!str.LoadFromStream(secStream, wName))
			return false;

		SMergeRecord rec;
		auto it = str.m_map.find(item.nStringID);
		if (it != str.m_map.end())
		{
			rec.eAction = SMergeRecord::MA_UPDATE_STRING;
			rec.wsOldText = it->second;
		}
		else
		{
			rec.eAction = SMergeRecord::MA_INSERT_STRING;
		}
		rec.nItemID = item.nStringID;
		rec.wsNewText = item.wsSecondaryText;

		str.m_map[item.nStringID] = MStringW(item.wsSecondaryText.c_str());
		WriteStringBack(secEntry, wName, str);

		result.vecRecords.push_back(rec);
		if (rec.eAction == SMergeRecord::MA_INSERT_STRING)
			result.nInsertedStrings++;
		else
			result.nUpdatedStrings++;
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////
// Menu item

bool MergeEngine::ApplyMenuItem(const SCompareItem& item, SMergeResult& result)
{
	// Find the menu entry in secondary 鈥?we need to figure out which menu resource
	// We search all menus and match by menu item ID
	for (auto it = m_pSecondary->begin(); it != m_pSecondary->end(); ++it)
	{
		EntryBase* secEntry = *it;
		if (secEntry->m_et != ET_LANG)
			continue;
		if (secEntry->m_type != RT_MENU)
			continue;
		if (secEntry->m_lang != m_secondaryCtx.wEffectiveLang)
			continue;

		MenuRes secMenu;
		MByteStreamEx secStream(secEntry->m_data);

		bool isEx = false;
		{
			// Peek to determine if extended
			MenuRes probe;
			if (secStream.size() >= 4)
			{
				WORD ver = 0;
				secStream.ReadWord(ver);
				secStream.ReadDwordAlignment();
				secStream = MByteStreamEx(secEntry->m_data);
				isEx = (ver == 1);
			}
		}

		if (isEx)
		{
			if (!secMenu.LoadFromStreamEx(secStream))
				continue;
		}
		else
		{
			if (!secMenu.LoadFromStream(secStream))
				continue;
		}

		if (isEx)
		{
			// Search for item by ID
			bool found = false;
			for (size_t i = 0; i < secMenu.exitems().size(); ++i)
			{
				if (secMenu.exitems()[i].menuId == item.nMenuID)
				{
					if (item.eStatus == CIS_CHANGED)
					{
						SMergeRecord rec;
						rec.eAction = SMergeRecord::MA_UPDATE_MENU_TEXT;
						rec.nItemID = item.nMenuID;
						rec.wsOldText = secMenu.exitems()[i].text;
						rec.wsNewText = item.wsSecondaryText;

						secMenu.exitems()[i].text = MStringW(item.wsSecondaryText.c_str());
						WriteMenuBack(secEntry, secMenu);

						result.vecRecords.push_back(rec);
						result.nUpdatedMenuItems++;
						return true;
					}
					found = true;
					break;
				}
			}

			if (!found && item.eStatus == CIS_ADDED)
			{
				// Get the item from primary
				EntryBase* priEntry = m_pPrimary->find(
					ET_LANG, RT_MENU, secEntry->m_name, m_primaryCtx.wEffectiveLang);
				if (!priEntry)
					continue;

				MenuRes priMenu;
				MByteStreamEx priStream(priEntry->m_data);
				if (!priMenu.LoadFromStreamEx(priStream))
					continue;

				// Find the item in primary
				for (size_t j = 0; j < priMenu.exitems().size(); ++j)
				{
					if (priMenu.exitems()[j].menuId == item.nMenuID)
					{
						MenuRes::ExMenuItem newItem = priMenu.exitems()[j];
						if (!item.wsSecondaryText.empty())
							newItem.text = MStringW(item.wsSecondaryText.c_str());

						secMenu.exitems().push_back(newItem);
						WriteMenuBack(secEntry, secMenu);

						SMergeRecord rec;
						rec.eAction = SMergeRecord::MA_INSERT_MENU_ITEM;
						rec.nItemID = item.nMenuID;
						rec.wsNewText = newItem.text;
						result.vecRecords.push_back(rec);
						result.nInsertedMenuItems++;
						return true;
					}
				}
			}
		}
		else
		{
			bool found = false;
			for (size_t i = 0; i < secMenu.items().size(); ++i)
			{
				if (secMenu.items()[i].wMenuID == (WORD)item.nMenuID)
				{
					if (item.eStatus == CIS_CHANGED)
					{
						SMergeRecord rec;
						rec.eAction = SMergeRecord::MA_UPDATE_MENU_TEXT;
						rec.nItemID = item.nMenuID;
						rec.wsOldText = secMenu.items()[i].text;
						rec.wsNewText = item.wsSecondaryText;

						secMenu.items()[i].text = MStringW(item.wsSecondaryText.c_str());
						WriteMenuBack(secEntry, secMenu);

						result.vecRecords.push_back(rec);
						result.nUpdatedMenuItems++;
						return true;
					}
					found = true;
					break;
				}
			}

			if (!found && item.eStatus == CIS_ADDED)
			{
				EntryBase* priEntry = m_pPrimary->find(
					ET_LANG, RT_MENU, secEntry->m_name, m_primaryCtx.wEffectiveLang);
				if (!priEntry)
					continue;

				MenuRes priMenu;
				MByteStreamEx priStream(priEntry->m_data);
				if (!priMenu.LoadFromStream(priStream))
					continue;

				for (size_t j = 0; j < priMenu.items().size(); ++j)
				{
					if (priMenu.items()[j].wMenuID == (WORD)item.nMenuID)
					{
						MenuRes::MenuItem newItem = priMenu.items()[j];
						if (!item.wsSecondaryText.empty())
							newItem.text = MStringW(item.wsSecondaryText.c_str());

						secMenu.items().push_back(newItem);
						WriteMenuBack(secEntry, secMenu);

						SMergeRecord rec;
						rec.eAction = SMergeRecord::MA_INSERT_MENU_ITEM;
						rec.nItemID = item.nMenuID;
						rec.wsNewText = newItem.text;
						result.vecRecords.push_back(rec);
						result.nInsertedMenuItems++;
						return true;
					}
				}
			}
		}
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////
// Write-back helpers

bool MergeEngine::WriteDialogBack(EntryBase* entry, const DialogRes& dlg)
{
	MByteStreamEx stream;
	if (!dlg.SaveToStream(stream))
		return false;
	entry->m_data = stream.data();
	return true;
}

bool MergeEngine::WriteMenuBack(EntryBase* entry, const MenuRes& menu)
{
	MByteStreamEx stream;
	if (menu.IsExtended())
	{
		if (!menu.SaveToStreamEx(stream))
			return false;
	}
	else
	{
		if (!menu.SaveToStream(stream))
			return false;
	}
	entry->m_data = stream.data();
	return true;
}

bool MergeEngine::WriteStringBack(EntryBase* entry, WORD wName, StringRes& str)
{
	MByteStreamEx stream;
	if (!str.SaveToStream(stream, wName))
		return false;
	entry->m_data = stream.data();
	return true;
}
