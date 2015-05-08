#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include "mjson.h"
#include "common.h"


/********************************** cfg-capture.json Parser ***********************************/

#define MAX_FORMAT_STR			32

#define CONFIG_FILE				"cfg-capture.json"
#define MAX_STREAM_NUM			2

struct stream_cfg_t
{
	char dev[PATH_MAX];
	char fmt[MAX_FORMAT_STR];
	unsigned int width;
	unsigned int height;

	char outfile[PATH_MAX];
	unsigned int frames;
};

struct stream_list_t
{
	int nstreams;
	struct stream_cfg_t list[MAX_STREAM_NUM];
};

static struct stream_list_t g_streamlist;

static int json_streamlist_read( const char *buf )
{
	int ret = 0;

	const struct json_attr_t json_stream_attrs[] =
	{
		{ "device", t_string,  STRUCTOBJECT( struct stream_cfg_t, dev ),     .len = sizeof( g_streamlist.list[0].dev ) },
		{ "format", t_string,  STRUCTOBJECT( struct stream_cfg_t, fmt ),     .len = sizeof( g_streamlist.list[0].fmt ) },
		{ "width",  t_integer, STRUCTOBJECT( struct stream_cfg_t, width )  },
		{ "height", t_integer, STRUCTOBJECT( struct stream_cfg_t, height ) },
		{ "output", t_string,  STRUCTOBJECT( struct stream_cfg_t, outfile ), .len = sizeof( g_streamlist.list[0].outfile ) },
		{ "frames", t_integer, STRUCTOBJECT( struct stream_cfg_t, frames ) },
		{ NULL },
	};
	const struct json_attr_t json_general_attrs[] =
	{
		{ "class",   t_check, .dflt.check = "STREAMS" },
		{ "streams", t_array, STRUCTARRAY( g_streamlist.list, json_stream_attrs, &g_streamlist.nstreams ) },
		{ NULL },
	};

	memset( &g_streamlist, '\0', sizeof( g_streamlist ));

	ret = json_read_object( buf, json_general_attrs, NULL );

	return ret;
}

static int
json_file_parser( const char *filename )
{
	struct stat fstat;
	char *pbuf = NULL;
	FILE *fp = NULL;
	int ret = 0, status = 0, i = 0;
	int rdsize = 0, filesize = 0;

	ret = stat( filename, &fstat );
	if( ret )
	{
		T_ERROR( "Fail to stat file '%s'. (ret = %d)\n", filename, ret );
		ret = -EACCES;
		goto error_exit;
	}
	filesize = fstat.st_size;

	pbuf = (char *)malloc( filesize + 1 );
	if( !pbuf )
	{
		T_ERROR( "Fail to malloc buffer. (size = %d)\n", filesize );
		ret = -ENOMEM;
		goto error_exit;
	}

	fp = fopen( filename, "r" );
	if( fp == NULL )
	{
		T_ERROR( "fail to open input file '%s'.\n", filename );
		ret = -EACCES;
		goto error_exit;
	}

	rdsize = fread( pbuf, 1, filesize, fp );
	if( rdsize != filesize )
	{
		T_ERROR( "fail to read input file '%s'. (%d / %d)\n", filename, rdsize, filesize );
		ret = -EIO;
		goto error_exit;
	}

	pbuf[filesize] = '\0';

	status = json_streamlist_read( pbuf );
	T_PRINT( "%d streams:\n", g_streamlist.nstreams );
	for( i = 0; i < g_streamlist.nstreams; i++ )
	{
		T_PRINT( "\t\"%s\" | \"%s\" | %dx%d | \"%s\" x%d\n", g_streamlist.list[i].dev, g_streamlist.list[i].fmt,			\
															g_streamlist.list[i].width, g_streamlist.list[i].height,		\
															g_streamlist.list[i].outfile, g_streamlist.list[i].frames );
	}

	if( status != 0 )
		puts(json_error_string( status ));

error_exit :
	if( pbuf )
		free( pbuf );
	if( fp )
		fclose( fp );
	return ret;
}

/**********************************************************************************************/

#define	REQUEST_BUFFER_COUNT			4


struct userbuffer
{
	void *start;
	size_t length;
};

struct userbuffer *gp_buffers = NULL;


static void
usage( void )
{
	printf( "Usage: \n" );
}

static int
arg_parser( int argc, char *argv[] )
{
	int ret = 0;

	ret = json_file_parser( CONFIG_FILE );
	if( ret )
	{
		T_ERROR( "Failed to parse config file %s\n", CONFIG_FILE );
		ret = -EINVAL;
		goto parser_exit;
	}

	return 0;

parser_exit:
	return ret;
}

int
single_stream_process( struct stream_cfg_t *pstream )
{
	FILE *outfp = NULL;
	enum v4l2_buf_type type;
	int ret = 0, devfd = -1, bufidx = 0;

	devfd = open( pstream->dev, ( O_RDWR | O_NONBLOCK ), 0 );
	if( devfd < 0 )
	{
		T_ERROR( "Failed to open video device \"%s\"\n", pstream->dev );
		ret = -ENODEV;
		goto proc_out;
	}

	outfp = fopen( pstream->outfile, "wb" );
	if( outfp == NULL )
	{
		T_ERROR( "Failed to open output file \"%s\". (-%d)\n", pstream->outfile, errno );
		ret = -errno;
		goto proc_out;
	}

{
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
//	struct v4l2_crop crop;
	struct v4l2_fmtdesc fmtdesc;
	struct v4l2_format fmt;
	struct v4l2_streamparm capparm;

	/***** ioctl[VIDIOC_QUERYCAP] *****/
	ret = ioctl( devfd, VIDIOC_QUERYCAP, &cap );
	if( ret < 0 )
	{
		T_ERROR( "ioctl[VIDIOC_QUERYCAP] failed. (-%d)\n", errno );
		ret = -errno;
		goto proc_out;
	}
	T_PRINT( "\"%s\" Capability:\n", pstream->dev );
	T_PRINT( "    driver   : %s\n", cap.driver );
	T_PRINT( "    card     : %s\n", cap.card );
	T_PRINT( "    bus_info : %s\n", cap.bus_info );
	T_PRINT( "    version  : 0x%08x\n", cap.version );
	T_PRINT( "    capabilities : 0x%08x\n", cap.capabilities );
	T_PRINT( "    device_caps  : 0x%08x\n", cap.device_caps );
	if(!( cap.capabilities & V4L2_CAP_VIDEO_CAPTURE ))
	{
		T_ERROR( "\"%s\" does not support video capture\n", pstream->dev );
		ret = -EINVAL;
		goto proc_out;
	}
	if(!( cap.capabilities & V4L2_CAP_STREAMING ))
	{
		T_ERROR( "\"%s\" does not support streaming i/o\n", pstream->dev );
		ret = -EINVAL;
		goto proc_out;
	}

	/***** ioctl[VIDIOC_CROPCAP] / [VIDIOC_S_CROP] *****/
	memset( &cropcap, 0, sizeof(struct v4l2_cropcap));
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl( devfd, VIDIOC_CROPCAP, &cropcap );
	if( ret < 0 )
	{
		T_ERROR( "ioctl[VIDIOC_CROPCAP] failed. (-%d)\n", errno );
		ret = -errno;
		goto proc_out;
	}
	else
	{
		T_PRINT( "\"%s\" CROPCAP:\n", pstream->dev );
		T_PRINT( "    type   : 0x%08x\n", cropcap.type );
		T_PRINT( "    bounds  rect : x[%d] / y[%d] / w[%d] / h[%d]\n",			\
									cropcap.bounds.left,  cropcap.bounds.top,	\
									cropcap.bounds.width, cropcap.bounds.height );
		T_PRINT( "    default rect : x[%d] / y[%d] / w[%d] / h[%d]\n",			\
									cropcap.defrect.left,  cropcap.defrect.top,	\
									cropcap.defrect.width, cropcap.defrect.height );
		T_PRINT( "    pixelaspect  : %d / %d\n", cropcap.pixelaspect.numerator, cropcap.pixelaspect.denominator );

//		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
//		crop.c = cropcap.defrect;
//		ret = ioctl( devfd, VIDIOC_S_CROP, &crop );
//		if( ret < 0 )
//		{
//			T_ERROR( "ioctl[VIDIOC_S_CROP] failed. (-%d)\n", errno );
//			ret = -errno;
//			goto proc_out;
//		}
	}

	/***** ioctl[VIDIOC_ENUM_FMT] *****/
	memset( &fmtdesc, 0, sizeof(struct v4l2_fmtdesc));
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	T_PRINT( "\"%s\" Format Enum:\n", pstream->dev );
	while( 1 )
	{
		ret = ioctl( devfd, VIDIOC_ENUM_FMT, &fmtdesc );
		if( ret )
			break;

		fmtdesc.index++;

		T_PRINT("    <%d> PixelFormat = \"%c%c%c%c\", Description = %s\n",											\
					fmtdesc.index, ( fmtdesc.pixelformat & 0xFF ), (( fmtdesc.pixelformat >> 8 ) & 0xFF ),			\
					(( fmtdesc.pixelformat >> 16 ) & 0xFF ), (( fmtdesc.pixelformat >> 24 ) & 0xFF ), fmtdesc.description );
	}

	/***** ioctl[VIDIOC_G_FMT] *****/
	memset( &fmt, 0, sizeof(struct v4l2_format));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl( devfd, VIDIOC_G_FMT, &fmt );
	if( ret < 0 )
	{
		T_ERROR( "ioctl[VIDIOC_G_FMT] failed. (-%d)\n", errno );
		ret = -errno;
		goto proc_out;
	}
	T_PRINT( "\"%s\" Current Format:\n", pstream->dev );
	T_PRINT( "    Resolution  : %d x %d\n", fmt.fmt.pix.width, fmt.fmt.pix.height );
	T_PRINT( "    PixelFormat : \"%c%c%c%c\"\n",														\
				( fmt.fmt.pix.pixelformat & 0xFF ), (( fmt.fmt.pix.pixelformat >> 8 ) & 0xFF ),			\
				(( fmt.fmt.pix.pixelformat >> 16 ) & 0xFF ), (( fmt.fmt.pix.pixelformat >> 24 ) & 0xFF ));
	T_PRINT( "    bytesperline[%d] / sizeimage[%d]\n", fmt.fmt.pix.bytesperline, fmt.fmt.pix.sizeimage );
	T_PRINT( "    Field[%d] / colorspace[%d]\n", fmt.fmt.pix.field, fmt.fmt.pix.colorspace );
	/***** ioctl[VIDIOC_S_FMT] *****/
//	ret = ioctl( devfd, VIDIOC_S_FMT, &fmt );
//	if( ret < 0 )
//	{
//		T_ERROR( "ioctl[VIDIOC_S_FMT] failed. (-%d)\n", errno );
//		ret = -errno;
//		goto proc_out;
//	}

	/***** ioctl[VIDIOC_G_PARM] *****/
	memset( &capparm, 0, sizeof(struct v4l2_streamparm));
	capparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl( devfd, VIDIOC_G_PARM, &capparm );
	if( ret < 0 )
	{
		T_ERROR( "ioctl[VIDIOC_G_PARM] failed. (-%d)\n", errno );
		ret = -errno;
		goto proc_out;
	}
	T_PRINT( "\"%s\" Stream Parm:\n", pstream->dev );
	T_PRINT( "    capability   : 0x%08x\n", capparm.parm.capture.capability );
	T_PRINT( "    capturemode  : 0x%08x\n", capparm.parm.capture.capturemode );
	T_PRINT( "    timeperframe : %d / %d\n", capparm.parm.capture.timeperframe.numerator, capparm.parm.capture.timeperframe.denominator );
	T_PRINT( "    extendedmode : 0x%08x\n", capparm.parm.capture.extendedmode );
	T_PRINT( "    readbuffers  : 0x%08x\n", capparm.parm.capture.readbuffers );
	/***** ioctl[VIDIOC_S_PARM] *****/
//	ret = ioctl( devfd, VIDIOC_S_PARM, &capparm );
//	if( ret < 0 )
//	{
//		T_ERROR( "ioctl[VIDIOC_S_PARM] failed. (-%d)\n", errno );
//		ret = -errno;
//		goto proc_out;
//	}
}

{
	struct v4l2_buffer buf;
	struct v4l2_requestbuffers reqbuf;

	/***** ioctl[VIDIOC_REQBUFS] *****/
	memset( &reqbuf, 0, sizeof(struct v4l2_requestbuffers));
	reqbuf.count  = REQUEST_BUFFER_COUNT;
	reqbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuf.memory = V4L2_MEMORY_MMAP;
 	ret = ioctl( devfd, VIDIOC_REQBUFS, &reqbuf );
	if( ret < 0 )
	{
		T_ERROR( "ioctl[VIDIOC_REQBUFS] failed. (-%d)\n", errno );
		ret = -errno;
		goto proc_out;
	}
	if( reqbuf.count < REQUEST_BUFFER_COUNT )
	{
		T_ERROR( "insufficient buffer memory (%d)\n", reqbuf.count );
		ret = -ENOMEM;
		goto proc_out;
	}

	gp_buffers = calloc( REQUEST_BUFFER_COUNT, sizeof(struct userbuffer));
	if( !gp_buffers )
	{
		T_ERROR( "out of memory.\n" );
		ret = -ENOMEM;
		goto proc_out;
	}

	T_PRINT( "\"%s\" MMAP Buffers:\n", pstream->dev );
	for( bufidx = 0; bufidx < REQUEST_BUFFER_COUNT; ++bufidx )
	{
		T_PRINT( "    mmap <%d/%d> : ", (bufidx + 1), REQUEST_BUFFER_COUNT );

		/***** ioctl[VIDIOC_QUERYBUF] *****/
		memset( &buf, 0, sizeof(struct v4l2_buffer));
		buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index  = bufidx;
		ret = ioctl( devfd, VIDIOC_QUERYBUF, &buf );
		if( ret < 0 )
		{
			T_ERROR( "ioctl[VIDIOC_QUERYBUF] failed. (-%d)\n", errno );
			ret = -errno;
			goto proc_out;
		}

		gp_buffers[bufidx].length = buf.length;
		gp_buffers[bufidx].start  = mmap( NULL, buf.length, ( PROT_READ | PROT_WRITE ), MAP_SHARED, devfd, buf.m.offset );
		if( gp_buffers[bufidx].start == MAP_FAILED )
		{
			T_ERROR( "mmap failed. (-%d)\n", errno );
			ret = -ENOMEM;
			goto proc_out;
		}

		T_PRINT( "start[%p] / len[%d]\n", gp_buffers[bufidx].start, (unsigned int)gp_buffers[bufidx].length );
		T_PRINT( "    buf<%d>: \n", buf.index );
		T_PRINT( "        type : %d\n", buf.type );
		T_PRINT( "        bytesused : %d\n", buf.bytesused );
		T_PRINT( "        flags : 0x%08x\n", buf.flags );
		T_PRINT( "        field : %d\n", buf.field );
		T_PRINT( "        sequence  : %d\n", buf.sequence );
		T_PRINT( "        memory    : %d\n", buf.memory );
		T_PRINT( "        offset    : 0x%08x\n", buf.m.offset );
		T_PRINT( "        userptr   : 0x%08lx\n", buf.m.userptr );
		T_PRINT( "        length    : %d\n", buf.length );
		T_PRINT( "        timestamp : %d.%06d\n", (unsigned int)buf.timestamp.tv_sec, (unsigned int)buf.timestamp.tv_usec );
		T_PRINT( "        timecode  : type[%d] | flags[0x%x] | frames[%d] | [%02d:%02d:%02d]\n",	\
									buf.timecode.type, buf.timecode.flags, buf.timecode.frames,		\
									buf.timecode.hours, buf.timecode.minutes, buf.timecode.seconds );
	}
}

{
	struct v4l2_buffer buf;

	for( bufidx = 0; bufidx < REQUEST_BUFFER_COUNT; ++bufidx )
	{
		T_PRINT( "\"%s\" QBUF - <%d> : ", pstream->dev, bufidx );
		/***** ioctl[VIDIOC_QBUF] *****/
		memset( &buf, 0, sizeof(struct v4l2_buffer));
		buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index  = bufidx;
		ret = ioctl( devfd, VIDIOC_QBUF, &buf );
		if( ret < 0 )
		{
			T_ERROR( "ioctl[VIDIOC_QBUF] failed. (-%d)\n", errno );
			ret = -errno;
			goto proc_out;
		}
		T_PRINT( "OK\n" );
	}

 		T_PRINT( "\"%s\" Stream  ON : ", pstream->dev );
		/***** ioctl[VIDIOC_STREAMON] *****/
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		ret = ioctl( devfd, VIDIOC_STREAMON, &type );
		if( ret < 0 )
		{
			T_ERROR( "ioctl[VIDIOC_STREAMON] failed. (-%d)\n", errno );
			ret = -errno;
			goto proc_out;
		}
		T_PRINTF( "OK\n" );
}

{
	fd_set fds;
	struct timeval tv;
	size_t wr_size = 0;
	unsigned int frames = 0;
	struct v4l2_buffer buf;

	frames = pstream->frames;

	FD_ZERO( &fds );
	FD_SET( devfd, &fds );

 	T_PRINT( "\"%s\" Runing:\n", pstream->dev );
	while( frames-- )
	{
		T_PRINT( "    Frame.%d : Waiting ... ", frames );

		tv.tv_sec  = 2;
		tv.tv_usec = 0;

		ret = select(( devfd + 1 ), &fds, NULL, NULL, &tv );
		if( ret < 0 )
		{
			T_ERROR( "FAILED (-%d)\n", errno );
			ret = -errno;
			goto stream_off;
		}
		else if( ret == 0 )
		{
			T_ERROR( "TIMEOUT (-%d)\n", errno );
			ret = -errno;
			goto stream_off;
		}

		T_PRINTF( "DQBUF-" );
		/***** ioctl[VIDIOC_DQBUF] *****/
		memset( &buf, 0, sizeof(struct v4l2_buffer));
		buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		ret = ioctl( devfd, VIDIOC_DQBUF, &buf );
		if( ret < 0 )
		{
			T_ERROR( "FAILED (-%d)\n", errno );
			ret = -errno;
			goto stream_off;
		}
		T_PRINTF( "%d ... ", buf.index );

		T_PRINTF( "Saving ... " );
		wr_size = fwrite( gp_buffers[buf.index].start, 1, buf.bytesused, outfp );
		if( wr_size != buf.bytesused )
		{
			T_ERROR( "FAILED (%d/%d)\n", (unsigned int)wr_size, buf.bytesused );
			ret = -EIO;
			goto stream_off;
		}

		T_PRINTF( "QBUF-" );
		/***** ioctl[VIDIOC_QBUF] *****/
		ret = ioctl( devfd, VIDIOC_QBUF, &buf );
		if( ret < 0 )
		{
			T_ERROR( "FAILED (-%d)\n", errno );
			ret = -errno;
			goto stream_off;
		}
		T_PRINTF( "%d ... ", buf.index );

		T_PRINTF( "Done.\n" );
	}
}

stream_off:
	T_PRINT( "\"%s\" Stream OFF : ", pstream->dev );
	/***** ioctl[VIDIOC_STREAMOFF] *****/
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl( devfd, VIDIOC_STREAMOFF, &type );
	if( ret < 0 )
	{
		T_ERROR( "ioctl[VIDIOC_STREAMOFF] failed. (-%d)\n", errno );
		ret = -errno;
		goto proc_out;
	}
	T_PRINTF( "OK\n" );
proc_out:
	for( bufidx = 0; bufidx < REQUEST_BUFFER_COUNT; ++bufidx )
	{
		if( gp_buffers[bufidx].start )
		{
			munmap( gp_buffers[bufidx].start, gp_buffers[bufidx].length );
			gp_buffers[bufidx].start  = NULL;
			gp_buffers[bufidx].length = 0;
		}
	}
	if( gp_buffers )
		free( gp_buffers );
	if( outfp )
		fclose( outfp );
	if( devfd > 0 )
		close( devfd );
	return ret;
}

int
streams_process( void )
{
	int ret = 0;

	ret = single_stream_process(&(g_streamlist.list[0]));

	return ret;
}

int
main( int argc, char *argv[] )
{
	int ret = 0;

	ret = arg_parser( argc, argv );
	if( ret )
	{
		T_ERROR( "Error when parse arguments.\n" );
		usage();
		return ret;
	}

	ret = streams_process();

	return ret;
}
