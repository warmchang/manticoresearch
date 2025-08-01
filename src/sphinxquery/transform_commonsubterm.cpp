//
// Copyright (c) 2017-2025, Manticore Software LTD (https://manticoresearch.com)
// Copyright (c) 2001-2016, Andrew Aksyonoff
// Copyright (c) 2008-2016, Sphinx Technologies Inc
// All rights reserved
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License. You should have
// received a copy of the GPL license along with this program; if you
// did not, you can find it at http://www.gnu.org
//

#include "transformation.h"

// common subterm
// ((A (AA | X)) | (B (BB | X))) -> ((A AA) | (B BB) | ((A|B) X)) [ if cost(X) > cost(A) + cost(B) ]
//  NOT:
//        ________OR (gGOr)
//		/           |
//	......	  AND (grandAnd)
//                /     |
//      relatedNode    OR (parentOr)
//        	            /   |
//                   pNode  ...
//
bool CSphTransformation::CheckCommonSubTerm ( const XQNode_t * pNode ) noexcept
{
	return Grand2Node::Valid ( pNode )
		&& (pNode->GetOp() != SPH_QUERY_PHRASE || pNode->dChildren().IsEmpty())
		&& ParentNode::From(pNode)->GetOp()==SPH_QUERY_OR
		&& GrandNode::From(pNode)->GetOp()==SPH_QUERY_AND
		&& Grand2Node::From(pNode)->GetOp()==SPH_QUERY_OR;
}

//int iTurns = 0;

bool CSphTransformation::TransformCommonSubTerm () noexcept
{
	int iActiveDeep = 0;
	ResetCostsHash();
	for ( auto & [_, hSimGroup]: m_hSimilar )
		for ( auto & [_, dX] : hSimGroup.tHash )
		{
			if ( iActiveDeep && iActiveDeep < hSimGroup.iDeep )
				continue;

			// Nodes with the same iFuzzyHash
			if ( dX.GetLength()<2
				|| HasSameParent ( dX )
				|| HasSameGrand ( dX )
				)
				continue;

			if ( !CollectRelatedNodes < ParentNode, GrandNode> ( dX ) )
				continue;

			// Load cost of the first node from the group
			// of the common nodes. The cost of nodes from
			// TransformableNodes are the same.
			SetCosts ( dX[0], m_dRelatedNodes );
			const int iCostCommonSubTermNode = dX[0]->m_iUser;
			const int iCostRelatedNodes = m_dRelatedNodes.sum_of<int> ( [] ( auto & tNode ) { return tNode->m_iUser; } );

			// Check that optimization will be useful.
			if ( iCostCommonSubTermNode <= iCostRelatedNodes )
				continue;

			MakeTransformCommonSubTerm ( dX );
			ResetCostsHash();
//			StringBuilder_c sHead;
//			sHead << "\nTransformCommonSubTerm #" << ++iTurns << " ((A (X | AA)) | (B (X | BB))) -> (((A|B) X) | (A AA) | (B BB)) [ if cost(X) > cost(A) + cost(B) ]";
//			Dump ( *m_ppRoot, sHead.cstr() );
			// Don't make transformation for other nodes from the same OR-node,
			// because query tree was changed and further transformations
			// might be invalid.
			iActiveDeep = hSimGroup.iDeep;
			break;
		}

	return iActiveDeep;
}

// Pick the weakest node from the equal
// PROXIMITY and PHRASE nodes with same keywords have an equal magic hash
// so they are considered as equal nodes.
void CSphTransformation::MakeTransformCommonSubTerm ( const CSphVector<XQNode_t *> & dX ) const
{
	int iWeakestIndex = GetWeakestIndex ( dX );
	XQNode_t * pX = dX[iWeakestIndex];

//	Dump (pX, "weakest");

	// common parents of X and AA / BB need to be excluded
	const CSphVector<XQNode_t *> dExcluded ( dX.GetLength() );

	// Factor out and delete/unlink similar nodes ( except weakest )
	ARRAY_FOREACH ( i, dX )
	{
		XQNode_t * pExcl = dX[i]->m_pParent;
//		Dump(pExcl, "dX[i]->m_pParent" );
		Verify ( pExcl->RemoveChild ( dX[i] ) );
		if ( i!=iWeakestIndex )
			SafeDelete ( dX[i] );

//		Dump(pExcl, "dExcluded[i]" );
		dExcluded[i] = pExcl;
		pExcl->m_pParent->RemoveChild ( pExcl );
	}

	CSphVector<XQNode_t *> dRelatedParents;
	for ( const XQNode_t * pRelated: m_dRelatedNodes )
	{
		XQNode_t * pParent = pRelated->m_pParent;
		if ( !dRelatedParents.Contains ( pParent ) )
			dRelatedParents.Add ( pParent );
	}

	for ( XQNode_t *& pRelated : dRelatedParents )
	{
//		Dump ( pRelated, "Cloning pRelated" );
		pRelated = pRelated->Clone();
		CompositeFixup ( pRelated, &pRelated );
	}

	// push excluded children back
	dExcluded.for_each ([](XQNode_t * pExcl) { pExcl->m_pParent->WithChildren ( [pExcl] ( auto & dChildren ) { dChildren.Add ( pExcl ); } ); });

	auto * pNewOr = new XQNode_t ( XQLimitSpec_t() );
	pNewOr->SetOp ( SPH_QUERY_OR, dRelatedParents );

	// Create yet another AND node
	// with related nodes and one common dSimilar node
	auto * pNewAnd = new XQNode_t ( XQLimitSpec_t() );
	XQNode_t * pCommonOr = Grand2Node::From ( pX );
	pNewAnd->SetOp ( SPH_QUERY_AND, pNewOr, pX );
	pCommonOr->AddNewChild ( pNewAnd );

//	Dump ( *m_ppRoot, "root before cleanup" );
	dExcluded.for_each ( [this] ( XQNode_t * pExcl ) { CleanupSubtree ( pExcl, m_ppRoot ); } );
//	Dump ( *m_ppRoot, "root after cleanup" );
}