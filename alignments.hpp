// The class handles reading the bam file

#ifndef _LSONG_RSCAF_ALIGNMENT_HEADER
#define _LSONG_RSCAF_ALIGNMENT_HEADER

#include "samtools-0.1.19/sam.h"
#include <map>
#include <string>
#include <assert.h>
#include <iostream>
#include <stdlib.h>
#include <math.h>

#include "defs.h"

class Alignments
{
private:
	samfile_t *fpSam ;	
	bam1_t *b ;

	char fileName[1024] ;
	bool opened ;	
	std::map<std::string, int> chrNameToId ;
	bool allowSupplementary ;
	bool allowClip ;

	bool atBegin ;
	bool atEnd ;

	static int CompInt( const void *p1, const void *p2 )
	{
		return (*(int *)p1 ) - (*(int *)p2 ) ;
	}

	void Open()
	{
		fpSam = samopen( fileName, "rb", 0 ) ;
		if ( !fpSam->header )
		{
			fprintf( stderr, "Can not open %s.\n", fileName ) ;
			exit( 1 ) ;
		}

		// Collect the chromosome information
		for ( int i = 0 ; i < fpSam->header->n_targets ; ++i )		
		{
			std::string s( fpSam->header->target_name[i] ) ;
			chrNameToId[s] = i ;
		}
		opened = true ;
		atBegin = true ;
		atEnd = false ;
	}
public:
	struct _pair segments[MAX_SEG_COUNT] ;		
	int segCnt ;

	int totalReadCnt ;
	int fragLen, fragStdev ;
	int readLen ;
	bool matePaired ;
	
	Alignments() 
	{ 
		b = NULL ; 
		opened = false ; 
		atBegin = true ;
		atEnd = false ;
		allowSupplementary = false ;
		allowClip = true ;

		totalReadCnt = 0 ;
		fragLen = 0 ;
		fragStdev = 0 ;
		readLen = 0 ;
		matePaired = false ;
	}
	~Alignments() 
	{
		if ( b )
			bam_destroy1( b ) ;
	}

	void Open( char *file )
	{
		strcpy( fileName, file ) ;
		Open() ;
	}

	void Rewind()
	{
		Close() ;
		Open() ;
	}

	void Close()
	{
		samclose( fpSam ) ;
		fpSam = NULL ;
	}

	bool IsOpened()
	{
		return opened ;
	}

	bool IsAtBegin()
	{
		return atBegin ;
	}

	bool IsAtEnd()
	{
		return atEnd ;
	}

	int Next()
	{
		int i ;
		int start = 0, len = 0 ;
		uint32_t *rawCigar ;
		atBegin = false ;
		while ( 1 )
		{
			while ( 1 )
			{
				if ( b )
					bam_destroy1( b ) ;
				b = bam_init1() ;

				if ( samread( fpSam, b ) <= 0 )
				{
					atEnd = true ;
					return 0 ;
				}
				if ( b->core.flag & 0xC )
					continue ;

				//if ( ( b->core.flag & 0x900 ) == 0 )
					break ;
			}
			// Compute the exons segments from the reads
			segCnt = 0 ;
			start = b->core.pos ; //+ 1 ;
			rawCigar = bam1_cigar( b ) ; 
			// Check whether the query length is compatible with the read
			if ( bam_cigar2qlen( &b->core, rawCigar ) != b->core.l_qseq ) 
				continue ;

			len = 0 ;
			bool hasClip = false ;
			for ( i = 0 ; i < b->core.n_cigar ; ++i )
			{
				int op = rawCigar[i] & BAM_CIGAR_MASK ;
				int num = rawCigar[i] >> BAM_CIGAR_SHIFT ;

				switch ( op )
				{
					case BAM_CMATCH:
					case BAM_CDEL:
						len += num ; break ;
					case BAM_CSOFT_CLIP:
					case BAM_CHARD_CLIP:
					case BAM_CPAD:
						hasClip = true ;
					case BAM_CINS:
						num = 0 ; break ;
					case BAM_CREF_SKIP:
						{
							segments[ segCnt ].a = start ;
							segments[ segCnt ].b = start + len - 1 ;
							++segCnt ;
							start = start + len + num ;
							len = 0 ;
						} break ;
					default:
						len += num ; break ;
				}
			}
			if ( hasClip && !allowClip )
				continue ;

			if ( len > 0 )
			{
				segments[ segCnt ].a = start ;
				segments[ segCnt ].b = start + len - 1 ;
				++segCnt ;
			}
			/*if ( !strcmp( bam1_qname( b ), "chr1:109656301-109749401W:ENST00000490758.2:381:1480:1090:1290:X" ) )
			{
				for ( i = 0 ; i < segCnt ; ++i )
					printf( "(%d %d) ", segments[i].a, segments[i].b ) ;
				printf( "\n" ) ;
			}*/
			
			// Check whether the mates are compatible
			//int mChrId = b->core.mtid ;
			int64_t mPos = b->core.mpos ;

			if ( b->core.mtid == b->core.tid )
			{
				for ( i = 0 ; i < segCnt - 1 ; ++i )
				{
					if ( mPos >= segments[i].b && mPos <= segments[i + 1].a )
						break ;
				}
				if ( i < segCnt - 1 )
					continue ;
			}
			
			break ;
		}

		return 1 ;
	}


	int GetChromId()
	{
		return b->core.tid ; 
	}

	char* GetChromName( int tid )
	{
		return fpSam->header->target_name[ tid ] ; 
	}

	int GetChromIdFromName( const char *s )
	{
		std::string ss( s ) ;
		if ( chrNameToId.find( ss ) == chrNameToId.end() )
		{
			printf( "Unknown genome name: %s\n", s ) ;
			exit( 1 ) ;
		}
		return chrNameToId[ss] ;
	}

	int GetChromLength( int tid )
	{
		return fpSam->header->target_len[ tid ] ;
	}

	void GetMatePosition( int &chrId, int64_t &pos )
	{
		if ( b->core.flag & 0x8 )
		{
			chrId = -1 ;
			pos = -1 ;
		}
		else
		{
			chrId = b->core.mtid ;
			pos = b->core.mpos ; //+ 1 ;
		}
	}

	int GetRepeatPosition( int &chrId, int64_t &pos )
	{
		// Look at the CC field.
		if ( !bam_aux_get( b, "CC" ) || !bam_aux_get( b, "CP" ) )
		{
			chrId = -1 ;
			pos = -1 ;
			return 0 ;
		}
		
		std::string s( bam_aux2Z( bam_aux_get(b, "CC" ) ) ) ;
		chrId = chrNameToId[ s ] ;
		pos = bam_aux2i( bam_aux_get( b, "CP" ) ) ;// Possible error for 64bit	
		return 1 ;
	}

	int GetReadLength()
	{
		return b->core.l_qseq ;
	}

	bool IsReverse()
	{
		if ( b->core.flag & 0x10 )	
			return true ;
		return false ;
	}

	bool IsMateReverse()
	{
		if ( b->core.flag & 0x20 )
			return true ;
		return false ;
	}

	char *GetReadId()
	{
		return bam1_qname( b ) ;
	}

	bool IsUnique()
	{
		if ( bam_aux_get( b, "NH" ) )
		{
			if ( bam_aux2i( bam_aux_get( b, "NH" ) ) > 1 )
				return false ;
		}	
		if ( IsSupplementary() && GetFieldZ( (char *)"XZ" ) != NULL )
			return false ;
		return true ;
	}
	
	// -1:minus, 0: unknown, 1:plus
	int GetStrand()
	{
		if ( segCnt == 1 )
			return 0 ;
		if ( bam_aux_get( b, "XS" ) )
		{
			if ( bam_aux2A( bam_aux_get( b, "XS" ) ) == '-' )
				return -1 ;	
			else
				return 1 ;
		}
		else
			return 0 ;
	}

	int GetFieldI( char *f )
	{
		if ( bam_aux_get( b, f ) )
		{
			return bam_aux2i( bam_aux_get( b, f ) ) ;
		}
		return -1 ;
	}

	char *GetFieldZ( char *f )
	{
		if ( bam_aux_get( b, f ) )
		{
			return bam_aux2Z( bam_aux_get( b, f ) ) ;
		}
		return NULL ;
	}
	
	bool IsSupplementary()
	{
		if ( ( b->core.flag & 0x800 ) == 0 )
			return false ;
		else
			return true ;
	}

	void SetAllowSupplementary( bool in )
	{ 
		allowSupplementary = in ;
	}

	void SetAllowClip( bool in )
	{
		allowClip = in ;
	}

	void GetGeneralInfo( bool stopEarly = false )
	{
		int i, k ;

		const int sampleMax = 1000000 ;
		int *lens = new int[sampleMax] ;
		int *mateDiff = new int[sampleMax] ;
		int lensCnt = 0 ;
		int mateDiffCnt = 0 ;
		bool end = false ;

		while ( 1 )
		{
			while ( 1 )
			{
				if ( b )
					bam_destroy1( b ) ;
				b = bam_init1() ;

				if ( samread( fpSam, b ) <= 0 )
				{
					end = true ;
					break ;
				}
				if ( b->core.flag & 0xC )
					continue ;

				if ( ( b->core.flag & 0x900 ) == 0 )
					break ;
			}
			if ( end )
				break ;
			
			if ( lensCnt < sampleMax )
			{
				lens[ lensCnt ] = b->core.l_qseq ; 
				++lensCnt ;
			}

			if ( mateDiffCnt < sampleMax && b->core.tid == b->core.mtid 
				&& b->core.pos < b->core.mpos )
			{
				mateDiff[ mateDiffCnt ] = b->core.mpos - b->core.pos ;
				++mateDiffCnt ;
			}
			++totalReadCnt ; 
			if ( totalReadCnt >= sampleMax && stopEarly )
				break ;
		}

		// Get the read length info and fragment length info
		qsort( lens, lensCnt, sizeof( int ), CompInt ) ;
		readLen = lens[ lensCnt - 1 ] ;
		
		if ( mateDiffCnt > 0 )
		{
			matePaired = true ;

			qsort( mateDiff, mateDiffCnt, sizeof( int ), CompInt ) ;
			long long int sum = 0 ;
			long long int sumsq = 0 ;
			
			for ( i = 0 ; i < mateDiffCnt * 0.7 ; ++i )
			{
				sum += ( mateDiff[i] + readLen );
				sumsq += ( mateDiff[i] + readLen ) * ( mateDiff[i] + readLen ) ;
			}
			k = i ;
			fragLen = (int)( sum / k ) ;
			fragStdev = (int)sqrt( sumsq / k - fragLen * fragLen ) ;
		}
		else
		{
			fragLen = readLen ;
			fragStdev = 0 ;
		}
		//printf( "readLen = %d\nfragLen = %d, fragStdev = %d\n", readLen, fragLen, fragStdev ) ;		
		delete[] lens ;
		delete[] mateDiff ;
	}
} ;
#endif
