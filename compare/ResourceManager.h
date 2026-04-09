// ResourceManager.h --- Resource file loading and management
//
// Encapsulates PE/RES resource loading, EntrySet ownership, language
// detection, and file export. Extracted from CRcCompareControler to
// enable modular reuse in CRcCompareViewer and other contexts.
//////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _INC_WINDOWS
	#include <windows.h>
#endif
#include <vector>
#include <string>
#include "Res.hpp"
#include "CompareTypes.h"

//////////////////////////////////////////////////////////////////////////////

class ResourceManager
{
public:
	ResourceManager();
	~ResourceManager();

	// Load a PE (DLL/EXE) file's resources
	bool LoadPEFile(LPCWSTR pszPath);

	// Clear all loaded resources
	void Clear();

	// Check if resources are loaded
	bool IsLoaded() const { return m_bLoaded; }

	// Access the EntrySet
	EntrySet& GetEntries() { return m_entries; }
	const EntrySet& GetEntries() const { return m_entries; }
	EntrySet* GetEntrySetPtr() { return &m_entries; }

	// Find a specific entry
	EntryBase* FindEntry(const MIdOrString& type, const MIdOrString& name, WORD lang);

	// Detect all languages present in the loaded resources
	std::vector<LANGID> GetLanguages() const;

	// Get the file path that was loaded
	const std::wstring& GetFilePath() const { return m_wsFilePath; }

	// Save/export the EntrySet back to a PE file
	bool SavePEFile(LPCWSTR pszPath);

private:
	// PE resource enumeration (static helpers)
	static bool LoadPEResources(LPCWSTR pszPath, EntrySet& res);

	EntrySet m_entries;
	std::wstring m_wsFilePath;
	bool m_bLoaded;
};
