// TopologyMatcher.cpp --- Control topology matching implementation
//////////////////////////////////////////////////////////////////////////////

#include "TopologyMatcher.h"
#include <algorithm>
#include <cmath>
#include <cstring>

//////////////////////////////////////////////////////////////////////////////

TopologyMatcher::TopologyMatcher()
	: m_nPosTolerance(2)
{
}

TopologyMatcher::~TopologyMatcher()
{
}

//////////////////////////////////////////////////////////////////////////////
// Build topology board from DialogRes

void TopologyMatcher::BuildBoard(const DialogRes& dlg, SDialogBoard& board)
{
	board.topLevel.vecTiles.clear();
	board.topLevel.vecRows.clear();
	board.topLevel.nGroupBoxIndex = -1;
	board.vecGroups.clear();

	// First pass: identify GroupBoxes
	std::vector<int> vecGroupBoxIndices;
	for (size_t i = 0; i < dlg.size(); ++i)
	{
		const DialogItem& item = dlg[i];
		if (item.IsGroupBox())
		{
			vecGroupBoxIndices.push_back((int)i);
		}
	}

	// Create a group for each GroupBox
	for (size_t g = 0; g < vecGroupBoxIndices.size(); ++g)
	{
		STileGroup grp;
		grp.nGroupBoxIndex = vecGroupBoxIndices[g];
		board.vecGroups.push_back(grp);
	}

	// Second pass: assign each control to a group or top-level
	for (size_t i = 0; i < dlg.size(); ++i)
	{
		const DialogItem& item = dlg[i];

		SBoardTile tile;
		tile.nItemIndex = (int)i;
		tile.wID = item.m_id;
		tile.pt = item.m_pt;
		tile.siz = item.m_siz;
		tile.bIsGroupBox = item.IsGroupBox();
		tile.bIsStatic = (item.m_id == 0xFFFF || item.m_id == (WORD)-1) &&
		                 (item.m_class.m_id == 0x0082 ||
		                  lstrcmpiW(item.m_class.m_str.c_str(), L"STATIC") == 0);

		if (tile.bIsGroupBox)
		{
			// GroupBoxes go to top-level
			board.topLevel.vecTiles.push_back(tile);
			continue;
		}

		// Check if this control is inside any GroupBox
		bool bAssigned = false;
		for (size_t g = 0; g < board.vecGroups.size(); ++g)
		{
			int gbIdx = board.vecGroups[g].nGroupBoxIndex;
			if (IsInsideGroupBox(item, dlg[gbIdx]))
			{
				board.vecGroups[g].vecTiles.push_back(tile);
				bAssigned = true;
				break;
			}
		}

		if (!bAssigned)
		{
			board.topLevel.vecTiles.push_back(tile);
		}
	}

	// Build rows for each group
	BuildRows(board.topLevel);
	for (size_t g = 0; g < board.vecGroups.size(); ++g)
	{
		BuildRows(board.vecGroups[g]);
	}
}

//////////////////////////////////////////////////////////////////////////////
// Build Y-clustered rows within a tile group

void TopologyMatcher::BuildRows(STileGroup& group)
{
	group.vecRows.clear();
	if (group.vecTiles.empty())
		return;

	// Sort tiles by Y, then by X
	std::vector<int> sortedIndices;
	for (size_t i = 0; i < group.vecTiles.size(); ++i)
		sortedIndices.push_back((int)i);

	std::sort(sortedIndices.begin(), sortedIndices.end(),
		[&group](int a, int b) -> bool
		{
			const SBoardTile& ta = group.vecTiles[a];
			const SBoardTile& tb = group.vecTiles[b];
			LONG ya = ta.pt.y + ta.siz.cy / 2;
			LONG yb = tb.pt.y + tb.siz.cy / 2;
			if (ya != yb) return ya < yb;
			return ta.pt.x < tb.pt.x;
		});

	// Cluster by Y center with tolerance
	const int Y_TOLERANCE = 3; // DLU
	int rowIndex = 0;
	STopoRowInfo currentRow;
	currentRow.nRowIndex = rowIndex;
	currentRow.nYCenter = group.vecTiles[sortedIndices[0]].pt.y +
	                      group.vecTiles[sortedIndices[0]].siz.cy / 2;
	currentRow.vecTileIndices.push_back(sortedIndices[0]);

	for (size_t i = 1; i < sortedIndices.size(); ++i)
	{
		int idx = sortedIndices[i];
		LONG yc = group.vecTiles[idx].pt.y + group.vecTiles[idx].siz.cy / 2;

		if (std::abs(yc - currentRow.nYCenter) <= Y_TOLERANCE)
		{
			currentRow.vecTileIndices.push_back(idx);
		}
		else
		{
			// Finalize current row
			group.vecRows.push_back(currentRow);

			// Start new row
			rowIndex++;
			currentRow = STopoRowInfo();
			currentRow.nRowIndex = rowIndex;
			currentRow.nYCenter = yc;
			currentRow.vecTileIndices.push_back(idx);
		}
	}
	group.vecRows.push_back(currentRow);

	// Assign row/col to tiles
	for (size_t r = 0; r < group.vecRows.size(); ++r)
	{
		const STopoRowInfo& row = group.vecRows[r];

		// Sort tiles in this row by X
		std::vector<int> rowTiles = row.vecTileIndices;
		std::sort(rowTiles.begin(), rowTiles.end(),
			[&group](int a, int b) -> bool
			{
				return group.vecTiles[a].pt.x < group.vecTiles[b].pt.x;
			});

		for (size_t c = 0; c < rowTiles.size(); ++c)
		{
			group.vecTiles[rowTiles[c]].nRow = (int)r;
			group.vecTiles[rowTiles[c]].nCol = (int)c;
		}
	}
}

//////////////////////////////////////////////////////////////////////////////
// Match controls between two dialogs

std::vector<SControlMatch> TopologyMatcher::MatchControls(
	const DialogRes& primary,
	const DialogRes& secondary)
{
	std::vector<SControlMatch> results;

	// Build topology boards
	SDialogBoard priBoard, secBoard;
	BuildBoard(primary, priBoard);
	BuildBoard(secondary, secBoard);

	// Track which secondary controls have been used
	std::vector<bool> vecUsed(secondary.size(), false);

	// For each primary control, try to find a match in secondary
	for (size_t i = 0; i < primary.size(); ++i)
	{
		const DialogItem& priItem = primary[i];
		SControlMatch match;
		match.nPrimaryIndex = (int)i;

		// Strategy 1: Match by control ID (fast path)
		if (priItem.m_id != 0xFFFF && priItem.m_id != (WORD)-1)
		{
			int secIdx = FindByID(secondary, priItem.m_id);
			if (secIdx >= 0 && !vecUsed[secIdx])
			{
				match.nSecondaryIndex = secIdx;
				vecUsed[secIdx] = true;

				// Check text difference
				const DialogItem& secItem = secondary[secIdx];
				if (priItem.m_title.m_str != secItem.m_title.m_str)
					match.bTextDiffers = true;

				// Check position difference
				if (!IsPositionClose(priItem.m_pt, secItem.m_pt) ||
				    priItem.m_siz.cx != secItem.m_siz.cx ||
				    priItem.m_siz.cy != secItem.m_siz.cy)
				{
					match.bPositionDiffers = true;
				}

				results.push_back(match);
				continue;
			}
		}

		// Strategy 2: Topology match for STATIC/IDC=-1 controls
		// Find what tile group this control belongs to in primary board
		bool bFound = false;

		// Search in top-level
		for (size_t t = 0; t < priBoard.topLevel.vecTiles.size(); ++t)
		{
			if (priBoard.topLevel.vecTiles[t].nItemIndex == (int)i)
			{
				int secIdx = FindStaticByTopology(
					priBoard.topLevel.vecTiles[t],
					priBoard.topLevel,
					secondary, secBoard, vecUsed);
				if (secIdx >= 0)
				{
					match.nSecondaryIndex = secIdx;
					vecUsed[secIdx] = true;
					match.bTextDiffers = true; // STATIC text likely differs across languages
					bFound = true;
				}
				break;
			}
		}

		// Search in groups
		if (!bFound)
		{
			for (size_t g = 0; g < priBoard.vecGroups.size(); ++g)
			{
				for (size_t t = 0; t < priBoard.vecGroups[g].vecTiles.size(); ++t)
				{
					if (priBoard.vecGroups[g].vecTiles[t].nItemIndex == (int)i)
					{
						int secIdx = FindStaticByTopology(
							priBoard.vecGroups[g].vecTiles[t],
							priBoard.vecGroups[g],
							secondary, secBoard, vecUsed);
						if (secIdx >= 0)
						{
							match.nSecondaryIndex = secIdx;
							vecUsed[secIdx] = true;
							match.bTextDiffers = true;
							bFound = true;
						}
						break;
					}
				}
				if (bFound) break;
			}
		}

		results.push_back(match);
	}

	return results;
}

//////////////////////////////////////////////////////////////////////////////
// Find control by ID in secondary dialog

int TopologyMatcher::FindByID(const DialogRes& dlg, WORD wID) const
{
	for (size_t i = 0; i < dlg.size(); ++i)
	{
		if (dlg[i].m_id == wID)
			return (int)i;
	}
	return -1;
}

//////////////////////////////////////////////////////////////////////////////
// Topology-based STATIC matching

int TopologyMatcher::FindStaticByTopology(
	const SBoardTile& primaryTile,
	const STileGroup& primaryGroup,
	const DialogRes& secondary,
	const SDialogBoard& secBoard,
	const std::vector<bool>& vecUsed) const
{
	if (!primaryTile.bIsStatic)
		return -1;

	// Find the corresponding group in secondary by matching GroupBox ID
	const STileGroup* pSecGroup = &secBoard.topLevel;

	if (primaryGroup.nGroupBoxIndex >= 0)
	{
		// This primary control was inside a GroupBox
		// Match by row/col position within the same-ID GroupBox in secondary

		// For now, check all groups in secondary
		for (size_t g = 0; g < secBoard.vecGroups.size(); ++g)
		{
			pSecGroup = &secBoard.vecGroups[g];
			// Try to find a matching STATIC at same row/col
			for (size_t t = 0; t < pSecGroup->vecTiles.size(); ++t)
			{
				const SBoardTile& secTile = pSecGroup->vecTiles[t];
				if (!secTile.bIsStatic)
					continue;
				if (vecUsed[secTile.nItemIndex])
					continue;
				if (secTile.nRow == primaryTile.nRow && secTile.nCol == primaryTile.nCol)
					return secTile.nItemIndex;
			}
		}
	}

	// Fall back: match by position proximity
	for (size_t t = 0; t < pSecGroup->vecTiles.size(); ++t)
	{
		const SBoardTile& secTile = pSecGroup->vecTiles[t];
		if (!secTile.bIsStatic)
			continue;
		if (vecUsed[secTile.nItemIndex])
			continue;
		if (IsPositionClose(primaryTile.pt, secTile.pt))
			return secTile.nItemIndex;
	}

	return -1;
}

//////////////////////////////////////////////////////////////////////////////
// Helpers

bool TopologyMatcher::IsInsideGroupBox(const DialogItem& item, const DialogItem& groupBox)
{
	// Check if item's center point is within groupBox's rect
	LONG cx = item.m_pt.x + item.m_siz.cx / 2;
	LONG cy = item.m_pt.y + item.m_siz.cy / 2;

	return cx > groupBox.m_pt.x &&
	       cx < groupBox.m_pt.x + groupBox.m_siz.cx &&
	       cy > groupBox.m_pt.y &&
	       cy < groupBox.m_pt.y + groupBox.m_siz.cy;
}

bool TopologyMatcher::IsPositionClose(POINT a, POINT b) const
{
	return std::abs(a.x - b.x) <= m_nPosTolerance &&
	       std::abs(a.y - b.y) <= m_nPosTolerance;
}
