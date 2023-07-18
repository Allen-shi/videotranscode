#ifndef _FFHEADER_H
#define _FFHEADER_H

#include "deflibcommon.h"
//包含ffmpeg的库
extern "C" {
#include "libavformat/avformat.h"
#include "libavformat/movenc.h"	
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/log.h" //日志
#include "libavutil/error.h"//av_err2str,错误码转换
#include "libavutil/pixfmt.h"
#include "libavutil/avutil.h"
#include "libavutil/time.h"
#include "libavutil/opt.h"
#include "libavutil/mathematics.h"
#include "libavutil/imgutils.h"
#include "libavutil/frame.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/mem.h"
#include "libavutil/rational.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libswresample/swresample.h"
}
#endif
