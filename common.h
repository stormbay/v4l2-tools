#ifndef  __TOOLS_COMMON_H__
#define  __TOOLS_COMMON_H__

#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* End of #ifdef __cplusplus */


#define	DEBUG

#ifdef DEBUG

#define	T_ENTER()						printf( "ENTER %s()\n", __FUNCTION__ )
#define	T_LEAVE()						printf( "LEAVE %s()\n", __FUNCTION__ )

#define	T_PRINT( fmt... )										\
	do	{														\
			printf( "[%s]-%d: ", __FUNCTION__, __LINE__ );	\
			printf( fmt );										\
		} while( 0 )

#define	T_PRINTF( fmt... )				printf( fmt )

#else	// DEBUG

#define	T_ENTER()
#define	T_LEAVE()
#define	T_PRINT( fmt... )
#define	T_PRINTF( fmt... )

#endif	// DEBUG

#define	T_DUMP( fmt... )										\
	do	{														\
			printf( "\t" );										\
			printf( fmt );										\
		} while( 0 )

#define	T_ERROR( fmt... )										\
	do	{														\
			printf( "[%s]-%d: ", __FUNCTION__, __LINE__ );	\
			printf( "**ERROR** " );								\
			printf( fmt );										\
		} while( 0 )


#define	T_HEX_STR_ASSERT( sbuf )		((( sbuf[0] == '0' ) && (( sbuf[1] == 'x' ) || ( sbuf[1] == 'X' ))) ? 1 : 0 )


#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

#endif	// __TOOLS_COMMON_H__
