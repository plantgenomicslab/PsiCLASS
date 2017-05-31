#ifndef _MOURISL_CLASSES_CONSTRAINTS_HEADER
#define _MOURISL_CLASSES_CONSTRAINTS_HEADER

#include <vector> 
#include <map>
#include <algorithm>
#include <string>
#include <string.h>

#include "BitTable.hpp"
#include "alignments.hpp"
#include "SubexonGraph.hpp"

struct _constraint
{
	BitTable vector ; // subexon vector
	double weight ;
	double abund ;
	double normAbund ;
	int support ;
} ;

struct _mateIdx
{
	int i, j ;
	int support ;
	double abund ;
} ;

struct _readIdHeap
{
	char *readId ;
	int pos ;
	int matePos ;
	int idx ;
} ;

//----------------------------------------------------------------------------------
// We assume the access to the data structure is sorted by matePos.
// So we can "pre-cache" the ids with the same matePos.
class MateReadIds
{
private:
	std::vector< struct _readIdHeap > heap ;

	void HeapifyUp( int tag )
	{
		while ( tag > 1 )
		{
			if ( heap[tag / 2].matePos < heap[tag].matePos )
				return ;
			struct _readIdHeap tmp ;
			tmp = heap[tag / 2] ;
			heap[tag / 2] = heap[tag] ;
			heap[tag] = tmp ;

			tag /= 2 ;
		}
	}

	void HeapifyDown( int tag )
	{
		int size = heap.size() ;
		while ( 2 * tag < size )
		{
			int choose = 2 * tag ;
			if ( 2 * tag + 1 < size && 
				heap[ 2 * tag + 1].matePos < heap[2 * tag ].matePos )
			{
				choose = 2 * tag + 1 ;
			}
			
			if ( heap[tag].matePos < heap[choose].matePos )
				return ;

			struct _readIdHeap tmp ; 
			tmp = heap[choose] ;
			heap[choose] = heap[tag] ;
			heap[tag] = tmp ;

			tag = choose ;
		}
	}

	struct _readIdHeap Pop()
	{
		struct _readIdHeap ret ;
		int size = heap.size() ;
		if ( size < 2 )
		{
			ret.readId = NULL ;
			return ret ;
		}

		ret = heap[1] ;
		
		heap[1] = heap[ heap.size() - 1] ;
		heap.pop_back() ;
		HeapifyDown( 1 ) ;

		return ret ;
	}

	int cachedMatePos ;
	std::map<std::string, int> cachedIdx ;
public:
	MateReadIds() 
	{ 
		// Push a dummy element so the vector becomes 1-based.
		struct _readIdHeap nh ;
		nh.readId = NULL ; 
		nh.pos = nh.idx = nh.matePos = -1 ;
		heap.push_back( nh ) ;
		cachedMatePos = -1 ;
	}
	~MateReadIds() 
	{
		int size = heap.size() ;
		for ( int i = 0 ; i < size ; ++i )
			if ( heap[i].readId != NULL )
				free( heap[i].readId ) ;
	}

	void Insert( char *id, int pos, int idx, int matePos )
	{
		struct _readIdHeap nh ;
		nh.readId = strdup( id ) ;
		nh.pos = pos ;
		nh.idx = idx ;
		nh.matePos = matePos ;
		
		heap.push_back( nh ) ;
		HeapifyUp( heap.size() - 1 ) ;
	}
	
	// If the id does not exist, return -1.
	int Query( char *id, int matePos )
	{
		int size ;
		size = heap.size() ;
		if ( matePos > cachedMatePos )
		{
			std::map<std::string, int>().swap( cachedIdx ) ;
			
			while ( size >= 2 && heap[1].matePos < matePos )
			{
				struct _readIdHeap r = Pop() ;
				free( r.readId ) ;
			}

			while ( size >= 2 && heap[1].matePos == matePos )
			{
				struct _readIdHeap r = Pop() ;
				cachedIdx[ std::string( r.readId ) ] = r.idx ;
			}
		}
		std::string s( id ) ;
		if ( cachedIdx.find( s ) != cachedIdx.end() )
		{
			return cachedIdx[s] ;
		}
		return -1 ;	
	}
} ;


//--------------------------------------------------------------------------
class Constraints
{
private:
	std::vector<struct _constraint> constraints ;
	int prevStart, prevEnd ;	
	std::vector<struct _mateIdx> mates ; 

	Alignments &alignments ;
	
	//@return: whether this alignment is compatible with the subexons or not.
	bool ConvertAlignmentToBitTable( struct _pair *segments, int segCnt, struct _subexon *subexons, int seCnt, int seStart, struct _constraint &ct ) ;

	// Sort to increasing order. Since the first subexon occupies the least important digit.
	static bool CompSortConstraints( const struct _constraint &a, const struct _constraint &b )
	{
		//int k 
		int diffPos = a.vector.GetFirstDifference( b.vector ) ;
		if ( diffPos == -1 )
			return false ;

		if ( a.vector.Test( diffPos ))
			return true ;
		else
			return false ;
	}
	void CoalesceSameConstraints() ;
public:
	Constraints( Alignments &a ): alignments( a ) 
	{
	}
	~Constraints() 
	{
	}

	void Clear() 
	{
		//TODO: do I need to release the memory from BitTable?
		constraints.clear() ;
	}

	int BuildConstraints( struct _subexon *subexons, int scnt, int start, int end ) ;
} ;

#endif