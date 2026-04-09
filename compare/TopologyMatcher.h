// TopologyMatcher.h --- Control topology matching for dialog comparison
// Uses board-tiling model to robustly match controls between primary and
// secondary language dialogs — handles GroupBox nesting, Y-clustering,
// and IDC=-1 STATIC pairing.
//////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _INC_WINDOWS
	#include <windows.h>
#endif
#include <vector>
#include <map>
#include "DialogRes.hpp"

//////////////////////////////////////////////////////////////////////////////
// Topology structures

struct SBoardTile
{
	int nItemIndex;     // Index into DialogRes items
	WORD wID;           // Control ID (m_id)
	POINT pt;           // Position (m_pt)
	SIZE siz;           // Size (m_siz)
	int nRow;           // Assigned row in topology grid
	int nCol;           // Assigned column in topology grid
	bool bIsGroupBox;   // Is this control a GroupBox?
	bool bIsStatic;     // Is this a STATIC control with IDC=-1?

	SBoardTile()
		: nItemIndex(-1), wID(0), nRow(-1), nCol(-1)
		, bIsGroupBox(false), bIsStatic(false)
	{
		pt.x = pt.y = 0;
		siz.cx = siz.cy = 0;
	}
};

struct STopoRowInfo
{
	int nRowIndex;
	LONG nYCenter;                  // Y center of this row
	std::vector<int> vecTileIndices; // Indices into tile array, sorted by X

	STopoRowInfo() : nRowIndex(-1), nYCenter(0) {}
};

struct STileGroup
{
	int nGroupBoxIndex;             // Index of owning GroupBox tile (-1 for top-level)
	std::vector<SBoardTile> vecTiles;
	std::vector<STopoRowInfo> vecRows;

	STileGroup() : nGroupBoxIndex(-1) {}
};

struct SDialogBoard
{
	STileGroup topLevel;            // Controls not inside any GroupBox
	std::vector<STileGroup> vecGroups; // Controls grouped by owning GroupBox
};

//////////////////////////////////////////////////////////////////////////////
// Control match result

struct SControlMatch
{
	int nPrimaryIndex;      // Index in primary DialogRes
	int nSecondaryIndex;    // Index in secondary DialogRes (-1 if missing)
	bool bTextDiffers;      // True if control text differs
	bool bPositionDiffers;  // True if position/size differs

	SControlMatch()
		: nPrimaryIndex(-1), nSecondaryIndex(-1)
		, bTextDiffers(false), bPositionDiffers(false)
	{
	}
};

//////////////////////////////////////////////////////////////////////////////
// TopologyMatcher class

class TopologyMatcher
{
public:
	TopologyMatcher();
	~TopologyMatcher();

	// Build topology board from a dialog resource
	void BuildBoard(const DialogRes& dlg, SDialogBoard& board);

	// Match controls between primary and secondary dialogs.
	// Returns a vector of matches.  If a primary control has no match
	// in secondary, nSecondaryIndex == -1 (i.e., it is missing/added).
	std::vector<SControlMatch> MatchControls(
		const DialogRes& primary,
		const DialogRes& secondary);

	// Tolerance in DLU for position matching
	void SetPositionTolerance(int nDLU) { m_nPosTolerance = nDLU; }

private:
	int m_nPosTolerance;    // Default: 2 DLU

	// Internal: build topology rows by Y-clustering
	void BuildRows(STileGroup& group);

	// Internal: fast match by control ID
	int FindByID(const DialogRes& dlg, WORD wID) const;

	// Internal: topology-based match for STATIC controls (IDC=-1)
	int FindStaticByTopology(
		const SBoardTile& primaryTile,
		const STileGroup& primaryGroup,
		const DialogRes& secondary,
		const SDialogBoard& secBoard,
		const std::vector<bool>& vecUsed) const;

	// Internal: check if a control is inside a GroupBox rect
	static bool IsInsideGroupBox(const DialogItem& item, const DialogItem& groupBox);

	// Internal: check position proximity
	bool IsPositionClose(POINT a, POINT b) const;
};
