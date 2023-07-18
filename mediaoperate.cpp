#include "./mediaoperate.h"
#include "log/easylogging++.h"
#include "stdarg.h"
#include <algorithm>

//#define TMP "/tmp/leopard/"
extern std::string TMP;
extern rabbitmq::amqpAddress gaddress;
extern worker::Queues queues;
extern std::string workerId;
using namespace Media;

char av_error[AV_ERROR_MAX_STRING_SIZE] = {0};
#define av_err2str(errnum) \
    av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)

cv::Mat AvframeToMat(AVFrame *frame) {
    AVFrame dst;
    cv::Mat m;
    memset(&dst, 0, sizeof(dst));

    int w = frame->width, h = frame->height;
    m = cv::Mat(h, w, CV_8UC3);
    dst.data[0] = (uint8_t *)m.data;
    avpicture_fill((AVPicture *)&dst, dst.data[0], AV_PIX_FMT_BGR24, w, h);

    struct SwsContext *convertCtx = NULL;
    enum AVPixelFormat srcPixFmt = (enum AVPixelFormat)frame->format;
    enum AVPixelFormat dstPixFmt = AV_PIX_FMT_BGR24;

    convertCtx = sws_getContext(w, h, srcPixFmt, w, h, dstPixFmt,
                                SWS_FAST_BILINEAR, NULL, NULL, NULL);
    sws_scale(convertCtx, frame->data, frame->linesize, 0, h, dst.data,
              dst.linesize);
    sws_freeContext(convertCtx);

    return m;
}

AVFrame *MatToAvframe(cv::Mat image, AVFrame *frame) {
    int width = image.cols;
    int height = image.rows;

    int cvLinesizes[1];
    cvLinesizes[0] = image.step1();
    // AVFrame *frame;
    // frame = av_frame_alloc();
    // av_image_alloc(frame->data, frame->linesize, width, height,
    //               AVPixelFormat::AV_PIX_FMT_YUV420P, 1);

    SwsContext *convertCtx = sws_getContext(
        width, height, AVPixelFormat::AV_PIX_FMT_BGR24, width, height,
        AVPixelFormat::AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);
    sws_scale(convertCtx, &image.data, cvLinesizes, 0, height, frame->data,
              frame->linesize);
    sws_freeContext(convertCtx);

    return frame;
}
//#define USEGPU
//硬件格式
static enum AVPixelFormat _hwPixFmt;
void my_logoutput(void *ptr, int level, const char *fmt, va_list vl) {
    int line;
    line = va_arg(vl, int);
    THROW(fmt, line);
}
void initLog() {
    // av_log_set_level(AV_LOG_DEBUG);
    // av_log_set_level(AV_LOG_INFO);
    av_log_set_level(AV_LOG_ERROR);
    // av_log_set_callback(my_logoutput);
}

string substrFileName(string fileName) {
    int index = fileName.find_last_of("/");
    return fileName.substr(index + 1);
}

void checkDir(string fileName) {
    int ret;
    //判断打开是否成功
    fstream file;
    file.open(fileName, ios::in);
    if (!file) {
        mkdir(fileName.c_str(), S_IRWXU | S_IRWXG | S_IXOTH);
    }
}
int checkIdByRedis(string &ip, string &port, string &id) {

    //	if (id.empty()){
    //		av_log(NULL, AV_LOG_ERROR, "The task id is NULL, please
    // set\n",__LINE__);
    //		return -1;
    //	}
    //
    //	std::string uri("http://" + ip + ":" + port + "/progress/" +  id);
    //	string res = HttpOperate::ConnectGet(uri);
    //	if (res.empty()){
    //		//获取失败
    //		//服务器没开或者其他情况
    //		av_log(NULL, AV_LOG_ERROR, "The progress server in not
    // online\n");
    //		return -1;
    //	} else{
    //		//获取成功，来验证id
    //		progress::Status resStatus;
    //		proto::Status status = proto::JsonStringToMessage(res,
    //&resStatus);
    //		if (! status.ok()){
    //			return -1;
    //		}else{
    //			if (resStatus.code() == 200){
    //				//有这个id，不可以使用
    //				av_log(NULL, AV_LOG_ERROR, "The task id already
    // existed,please
    // reset\n");
    //				return -1;
    //			} else if (resStatus.code() == 500){
    //				//没有这个id，可以使用
    //				return 0;
    //			}
    //
    //		}
    //
    //
    //	}
}
int checkIdByFile(string id) {
    int ret;
    AVIODirContext *ctx = NULL;
    AVIODirEntry *entry = NULL;
    if (id.empty()) {
        LOG(ERROR) << "The task id is NULL, please set";
        // av_log(NULL, AV_LOG_ERROR, "The task id is NULL, please set\n",
        //       __LINE__);
        return -1;
    } else {
        //检查id是否重复,读取文件名
        ret = avio_open_dir(&ctx, TMP.c_str(), NULL);
        if (0 > ret) {
            LOG(ERROR) << "Cannot open tmp dir, file: " << TMP
                       << "error: " << av_err2str(ret);
            // av_log(NULL, AV_LOG_ERROR, "Can not open dir,file:%s,error:%s\n",
            //       id.c_str(), av_err2str(ret));
            goto _fail;
        }
        while (true) {
            ret = avio_read_dir(ctx, &entry);
            if (0 > ret) {
                av_log(NULL, AV_LOG_ERROR, "Can not read dir,error:%s\n",
                       av_err2str(ret));
                avio_free_directory_entry(&entry);
                goto _fail;
            }
            if (NULL == entry) break;
            string tmp(entry->name);
            if (id == tmp) {
                LOG(ERROR) << "The task id already existed, please reset";
                // av_log(NULL, AV_LOG_ERROR,
                //       "The task id already existed,please reset\n");
                ret = -1;
                goto _fail;
            }
            avio_free_directory_entry(&entry);
        }
    }
    ret = 0;
_fail:
    avio_close_dir(&ctx);
    return ret;
}

enum AVPixelFormat getNvidiaFormat(AVCodecContext *ctx,
                                   const enum AVPixelFormat *pixFmts) {
    const enum AVPixelFormat *p;
    for (p = pixFmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == _hwPixFmt) return *p;
    }
    av_log(NULL, AV_LOG_ERROR, "Failed to get HW surface formate,%s:%d\n",
           __FILE__, __LINE__);
    return AV_PIX_FMT_NONE;
}
MediaOperate::MediaOperate()
    : _outDuration(0),
      _pts2ms(0),
      _videoPTS(0),
      _audioPTS(0),
      _needAudioTranscoding(0),
      _needVideoTranscoding(0) {
    initLog();
    char *ip = getenv("RPC_SERVER_IP");
    char *port = getenv("RPC_SERVER_PORT");
    if (NULL != ip && NULL != port) {
        _ip = ip;
        _port = port;
    }
    //创建临时文件目录
    DealFile::CheckDir(TMP);
}

MediaOperate::~MediaOperate() {
    //删除进度文件
    if (!_progressId.empty()) {
        string filePath = TMP + _progressId;
        if (DealFile::FileIsExists(filePath)) DealFile::FileDelete(filePath);
    }
}

int MediaOperate::getMediaInfo(GeneralInfo &generalinfo, VideoInfo &videoinfo,
                               AudioInfo &audioinfo) {
    if (!_inputFile.empty() || !_upload._hosts.empty()) {
        FileInfoStruct *input = new FileInfoStruct;
        if (_inputFile.empty()){
            input->chunk->upload = &_upload;
        }else {
            input->path = _inputFile.c_str();
        }
        int ret = getMediaInfo(input, generalinfo, videoinfo, audioinfo);
        //如果是流则需要计算码率
        if (!_upload._hosts.empty()){
            int64_t durationS = videoinfo._duration/10000000; //时间换算为秒
            generalinfo._bitrate = generalinfo._size * 8 / durationS;
            videoinfo._bitrate = generalinfo._bitrate - audioinfo._bitrate;
        }
        delete input;
        input = NULL;
        return ret;
    } else {
        av_log(
            NULL, AV_LOG_ERROR,
            "The inputFile is NULL,you should call MediaOprete(inputFile)\n");
        return -1;
    }
}

int MediaOperate::getMediaInfo(FileInfoStruct *input, GeneralInfo &generalinfo,
                               VideoInfo &videoinfo, AudioInfo &audioinfo) {
    int ret;

    _needAudioTranscoding = 1;
    _needVideoTranscoding = 1;

    ret = openInputFile(input);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR,
               "Failed while call openInputFile in getMediaInfo\n");
        return ret;
    }
    // get General Info
    int64_t size =
        input->formatContext->pb ? avio_size(input->formatContext->pb) : -1;
    if (input->path != NULL)
        generalinfo._name = input->path;
    if (!input->chunk->upload->_key.empty())
        generalinfo._name = input->chunk->upload->_key;
    generalinfo._nb_streams = input->formatContext->nb_streams;
    generalinfo._format = input->formatContext->iformat->name;
    generalinfo._duration = (float)input->duration / 1000;  //单位为s
    if (input->formatContext->iformat->long_name) {
        generalinfo._describe = input->formatContext->iformat->long_name;
    } else {
        generalinfo._describe = "unknown";
    }
    if (0 <= size && (input->path != NULL)) generalinfo._size = size;
    if (!input->chunk->upload->_key.empty()) generalinfo._size = input->chunk->upload->objectLength();
    if (0 < input->formatContext->bit_rate){
        generalinfo._bitrate = input->formatContext->bit_rate;  //单位是b/s
        generalinfo._duration = (generalinfo._size/1000 * 8)/(generalinfo._bitrate/1000); // 通过码率计算时长            
    }
    AVDictionaryEntry *tag = NULL;
    while ((tag = av_dict_get(input->formatContext->metadata, "", tag,
                              AV_DICT_IGNORE_SUFFIX))) {
        generalinfo._extinfo.insert(make_pair(tag->key, tag->value));
    }

    const AVCodecDescriptor *cd;
    const char *profile = NULL;
    const AVCodec *dec;
    char valStr[128];

    // get Video Info
    if (NULL != input->videoStream) {
        videoinfo._index = input->videoStream->index;

        dec = input->videoCodecCtx->codec;
        if (NULL != dec) {
            videoinfo._codecname = dec->name;
        } else if ((cd = avcodec_descriptor_get(
                        input->videoCodecCtx->codec_id))) {
            videoinfo._codecname = cd->name;
            videoinfo._describe = cd->long_name ? cd->long_name : "unknown";
        }

        if (dec &&
            (profile = av_get_profile_name(dec, input->videoCodecCtx->profile)))
            videoinfo._profile = profile;

        if (av_get_media_type_string(input->videoCodecCtx->codec_type))
            videoinfo._type =
                av_get_media_type_string(input->videoCodecCtx->codec_type);

        av_get_codec_tag_string(valStr, sizeof(valStr),
                                input->videoCodecCtx->codec_tag);
        videoinfo._codecid = valStr;

        videoinfo._width = input->width;
        videoinfo._height = input->height;
        videoinfo._gopsize = input->videoCodecCtx->gop_size;

        AVRational sar, dar;
        sar = av_guess_sample_aspect_ratio(input->formatContext,
                                           input->videoStream, NULL);
        if (0 != sar.den) {
            av_reduce(&dar.num, &dar.den, input->videoCodecCtx->width * sar.num,
                      input->videoCodecCtx->height * sar.den, 1024 * 1024);
            videoinfo._aspectradio =
                to_string(dar.num) + ":" + to_string(dar.den);
        }

        if (NULL != (av_get_pix_fmt_name(input->videoCodecCtx->pix_fmt)))
            videoinfo._pixfmt =
                av_get_pix_fmt_name(input->videoCodecCtx->pix_fmt);
        videoinfo._level = input->videoCodecCtx->level;

        videoinfo._duration = (float)input->videoStream->duration *
                              input->videoStream->time_base.num /
                              input->videoStream->time_base.den * 10000000;

        if (0 < (input->videoCodecCtx->bit_rate))
            videoinfo._bitrate = input->videoCodecCtx->bit_rate;

        if (0 != (input->videoStream->avg_frame_rate.den))
            videoinfo._framerate = input->videoStream->avg_frame_rate.num /
                                   input->videoStream->avg_frame_rate.den;
    }

    // get Audio Info
    if (NULL != input->audioStream) {
        audioinfo._index = input->audioStream->index;

        dec = input->audioCodecCtx->codec;
        if (NULL != dec) {
            audioinfo._codecname = dec->name;
            if (NULL != (dec->long_name)) audioinfo._describe = dec->long_name;
        } else if ((cd = avcodec_descriptor_get(
                        input->audioCodecCtx->codec_id))) {
            audioinfo._codecname = cd->name;
            audioinfo._describe = cd->long_name ? cd->long_name : "unknown";
        }

        profile = av_get_profile_name(dec, input->audioCodecCtx->profile);
        if ((NULL != dec) && (NULL != profile)) audioinfo._profile = profile;

        if (NULL !=
            (av_get_media_type_string(input->audioCodecCtx->codec_type)))
            audioinfo._type =
                av_get_media_type_string(input->audioCodecCtx->codec_type);

        av_get_codec_tag_string(valStr, sizeof(valStr),
                                input->audioCodecCtx->codec_tag);
        audioinfo._codecid = valStr;

        if (NULL != (av_get_sample_fmt_name(input->audioCodecCtx->sample_fmt)))
            audioinfo._samplefmt =
                av_get_sample_fmt_name(input->audioCodecCtx->sample_fmt);

        audioinfo._samplerate = input->audioCodecCtx->sample_rate;
        audioinfo._channels = input->audioCodecCtx->channels;
        audioinfo._bitspersample =
            av_get_bits_per_sample(input->audioCodecCtx->codec_id);

        audioinfo._duration =
            av_rescale(input->audioStream->duration, 1000, AV_TIME_BASE);
        if (0 < input->audioCodecCtx->bit_rate){
            audioinfo._bitrate = input->audioCodecCtx->bit_rate;
            int bitrate = audioinfo._bitrate;
            if((bitrate != generalinfo._bitrate) && (generalinfo._bitrate > 0)) bitrate = generalinfo._bitrate;
            audioinfo._duration = (generalinfo._size/1000 * 8)/(bitrate/1000); // 通过码率计算时长            
        }
    }
    return 0;
}

//读取流数据
int fill_iobuffer(void *opaque, uint8_t *buf, int buf_size) {
    MemChunk *chunk = (MemChunk *)opaque;
    uint64_t num;
    num = chunk->upload->getObject(chunk->offset, buf_size, (char*)buf);
    chunk->offset += num;
    return num;
}
int MediaOperate::openInputFile(FileInfoStruct *input) {
    int ret = 0;
    AVCodec *dec = NULL, *enc = NULL;

    if (input->path == NULL){
        //输入是流
        input->formatContext = avformat_alloc_context();
        unsigned char *iobuffer = (unsigned char*)av_malloc(32768);
        AVIOContext *avio = avio_alloc_context(iobuffer, 32768, 0, input->chunk, fill_iobuffer, NULL, NULL);
        input->formatContext->pb = avio;
        ret = avformat_open_input(&input->formatContext, "nothing", NULL, NULL);
    }else{
        //输入是本地文件
        // 1，打开输入文件的头信息
        ret = avformat_open_input(&input->formatContext, input->path, NULL, NULL);
    }
    if (ret < 0) {
        LOG(ERROR) << "Cannot open the input file, file: \"" << input->path
                   << "\" error: " << av_err2str(ret);
        // av_log(NULL, AV_LOG_ERROR,
        //       "Cannot open the input file ,file:'%s',error:%s\n",
        // input->path,
        //       av_err2str(ret), __LINE__);
        return ret;
    }

    // 2,查找输入文件中的流信息
    ret = avformat_find_stream_info(input->formatContext, NULL);
    if (ret < 0) {
        LOG(ERROR) << "Cannot find the stream information: " << av_err2str(ret)
                   << " inputfile: " << input->path;
        // av_log(NULL, AV_LOG_ERROR,
        //       "Cannot find the stream information %s | %s\n",
        // av_err2str(ret),
        //       input->path);
        avformat_close_input(&input->formatContext);
        input->formatContext = NULL;
        return ret;
    }

    //查找音频流信息
    ret = av_find_best_stream(input->formatContext, AVMEDIA_TYPE_AUDIO, -1, -1,
                              &dec, 0);
    if (0 <= ret) {
        input->audioStream = input->formatContext->streams[ret];
        input->audioIndex = ret;
    }

    //查找视频流的信息
    ret = av_find_best_stream(input->formatContext, AVMEDIA_TYPE_VIDEO, -1, -1,
                              &dec, 0);
    if (0 <= ret) {
        input->videoStream = input->formatContext->streams[ret];
        input->videoIndex = ret;
        input->width = input->formatContext->streams[ret]->codecpar->width;
        input->height = input->formatContext->streams[ret]->codecpar->height;
    }

    if (-1 == input->audioIndex && -1 == input->videoIndex) {
        LOG(ERROR) << "Cannot find video stream and audio stream";
        // av_log(NULL, AV_LOG_ERROR,
        //       "Cannot find video stream  and audio stream,%s:%d\n", __FILE__,
        //       __LINE__);
        avformat_close_input(&input->formatContext);
        input->formatContext = NULL;
        return ret;
    }
    //计算当前文件的时间长度,单位微妙 ms
    input->duration =
        av_rescale(input->formatContext->duration, 1000, AV_TIME_BASE);
    if (0 > input->duration) input->duration = 0;
    _outDuration += input->duration;
    // av_dump_format(input -> formatContext, 0, input -> path, 0);

    //音频转码
    if (-1 != input->audioIndex && _needAudioTranscoding == 1) {
        ret = openInputDecoder(input, AVMEDIA_TYPE_AUDIO);
        if (ret < 0) {
            LOG(ERROR) << "Cannot open \"" << input->path << "\" audio decoder";
            // av_log(NULL, AV_LOG_ERROR, "Cannot open %s, audio decoder\n",
            //       input->path);
            return ret;
        }
    }

    //视频转码
    if (-1 != input->videoIndex && _needVideoTranscoding == 1) {
        ret = openInputDecoder(input, AVMEDIA_TYPE_VIDEO);
        if (ret < 0) {
            LOG(ERROR) << "Cannot open \"" << input->path << "\" video decoder";
            // av_log(NULL, AV_LOG_ERROR, "Cannot open %s, video decoder\n",
            //        input->path);
            return ret;
        }
    }

    ret = 0;
    return ret;
}

int MediaOperate::openInputDecoder(FileInfoStruct *input,
                                   enum AVMediaType type) {
    if (input == NULL) {
        LOG(ERROR) << "openInputDecoder with NULl parameter";
        // av_log(NULL, AV_LOG_ERROR, "openInputDecoder with NULl parameter\n");
        return -1;
    }
    int ret;
    AVDictionary *opts = NULL;
    AVCodecContext **decCtx;
    AVStream *stream = NULL;
    AVCodec *dec = NULL;

    if (AVMEDIA_TYPE_VIDEO == type) {
        stream = input->videoStream;
        decCtx = &input->videoCodecCtx;
        //#ifdef USEGPU
        if (input->useGPU) {
            // hw 检查当前机器是否有硬件解码支持
            if (supportHWDecoder(stream->codecpar->codec_id) &&
                supportHWYUV((AVPixelFormat)stream->codecpar->format)) {
                ret = av_hwdevice_ctx_create(
                    &input->hwDeviceCtx, AV_HWDEVICE_TYPE_CUDA, NULL, NULL, 0);
                if (0 > ret) {
                    LOG(INFO) << "Failed to create a CUDA device ,the machine "
                                 "can not support hw";
                    // av_log(NULL, AV_LOG_INFO,
                    //       "Failed to create a CUDA device ,the machine can "
                    //       "not support hw,%s:%d\n",
                    //       __FILE__, __LINE__);
                }
                // hw 后面可以根据input -> hwDeviceCtx
                // 是否为空来判断是否找到了硬件设备
                //如果 input -> hwDeviceCtx == NULL 没有硬件设备
            }
        }
        //#endif
    } else if (AVMEDIA_TYPE_AUDIO == type) {
        stream = input->audioStream;
        decCtx = &input->audioCodecCtx;
    }

    if (NULL != *decCtx) {
        av_log(NULL, AV_LOG_ERROR, "%s file decoder opend already\n",
               input->path);
        return 0;
    }
    if (NULL == stream) {
        av_log(NULL, AV_LOG_ERROR,
               "openInputDecoder cannot find input stream\n");
        return -1;
    }
    dec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (NULL == dec) {
        av_log(NULL, AV_LOG_ERROR, "Failed to find %s, %s codec", input->path,
               av_get_media_type_string(type));
        return AVERROR(EINVAL);
    }
    // hw 如果当前机器支持hw，则查找硬件的配置
    if (NULL != input->hwDeviceCtx) {
        // hw 查找硬件的配置
        for (int i = 0;; i++) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(dec, i);
            if (NULL == config) {
                av_log(NULL, AV_LOG_ERROR,
                       "Decoder %s dose not support device type %s", dec->name,
                       av_hwdevice_get_type_name(AV_HWDEVICE_TYPE_CUDA));
                return -1;
            }
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == AV_HWDEVICE_TYPE_CUDA) {
                _hwPixFmt = config->pix_fmt;
                break;
            }
        }
    }
    // hw end find the hw config
    *decCtx = avcodec_alloc_context3(dec);
    if (input->threadCount != 0) {
        (*decCtx)->thread_count = input->threadCount;
    }
    if (NULL == *decCtx) {
        av_log(NULL, AV_LOG_ERROR,
               "Failed to allocate the %s, %s codec context\n", input->path,
               av_get_media_type_string(type));
        return AVERROR(ENOMEM);
    }
    ret = avcodec_parameters_to_context(*decCtx, stream->codecpar);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR,
               "Faile to copy %s,%s codec parameters to decoder context\n",
               input->path, av_get_media_type_string(type));
        return ret;
    }
    if (AVMEDIA_TYPE_VIDEO == type) {
        //如果不加这个则视频编码中的time_base 为空
        (*decCtx)->framerate =
            av_guess_frame_rate(input->formatContext, stream, NULL);
        if(stream->avg_frame_rate.num != 0)
               (*decCtx)->framerate = stream->avg_frame_rate;  
    }
    // hw 如果机器支持硬件解码，则将硬件信息拷贝一份到解码器上下文中
    if (NULL != input->hwDeviceCtx) {
        (*decCtx)->hw_device_ctx = av_buffer_ref(input->hwDeviceCtx);
        if (NULL == (*decCtx)->hw_device_ctx) {
            av_log(NULL, AV_LOG_ERROR,
                   "A hardware device reference create failed,%s:%d\n",
                   __FILE__, __LINE__);
            return -1;
        }
        // hw 添加函数，get_formate
        // 是一个函数，返回AV_PIX_FMT_CUDA,而不是AV_PIX_FMT_YUV420p
        (*decCtx)->get_format = getNvidiaFormat;
    }
    // hw end copy hw_device_ctx to AVCodecContext
    av_dict_set(&opts, "refcounted_frames", "0", 0);
    ret = avcodec_open2(*decCtx, dec, &opts);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR, "Failed to open %s, %s codec\n", input->path,
               av_get_media_type_string(type));
        return ret;
    }
    //有的音频 查找channel_layout为0，所以先要判断一下
    if (_needAudioTranscoding == 1 && input->audioCodecCtx != NULL &&
        input->audioCodecCtx->channel_layout == 0) {
        input->audioCodecCtx->channel_layout =
            av_get_default_channel_layout(input->audioCodecCtx->channels);
    }
    return 0;
}

int MediaOperate::openOutputFile(FileInfoStruct *outputStruct,
                                 FileInfoStruct *input, AvOpts &avopts) {
    int ret;

    if (NULL != outputStruct->formatContext) {
        //说明已经打开了，不能再打开一遍
        return 0;
    }
    // cout << outputStruct->path << endl;
    avformat_alloc_output_context2(&outputStruct->formatContext, NULL, NULL,
                                   outputStruct->path);
    if (NULL == outputStruct->formatContext) {
        LOG(ERROR) << "Cannot open output,filepath: \"" << outputStruct->path
                   << "\"";
        // av_log(NULL, AV_LOG_ERROR, "Cannot open output %s\n",
        //       outputStruct->path, __LINE__);
        ret = -1;
        return ret;
    }

    //音频流,可以根据输入文件是否缺少流来创建相应的流
    if (NULL != input->audioStream || avopts._isMergeVideo) {
        outputStruct->audioStream =
            avformat_new_stream(outputStruct->formatContext, NULL);
        if (NULL == outputStruct->audioStream) {
            av_log(NULL, AV_LOG_ERROR, "Cannot new audio stream for output!\n");
            ret = -1;
            return ret;
        }
        outputStruct->audioIndex = outputStruct->audioStream->index;

        //如果需要转码，则
        if (1 == _needAudioTranscoding) {
            ret = openOutputEncoder(outputStruct, input, avopts,
                                    AVMEDIA_TYPE_AUDIO);
            if (0 > ret) {
                av_log(NULL, AV_LOG_ERROR, "failed to open audio encoder\n ");
                return ret;
            }
        } else {
            ret = avcodec_parameters_copy(outputStruct->audioStream->codecpar,
                                          input->audioStream->codecpar);
            if (0 > ret) {
                av_log(NULL, AV_LOG_ERROR,
                       "Failed to copy audio codec parameters,%s:%d", __FILE__,
                       __LINE__);
                return ret;
            }
        }
        outputStruct->audioStream->codecpar->codec_tag = 0;
    }

    //视频流
    if ((1 == _needVideoTranscoding)&&(NULL != input->videoStream || avopts._isMergeVideo)) {
        outputStruct->videoStream =
            avformat_new_stream(outputStruct->formatContext, NULL);
        if (NULL == outputStruct->videoStream) {
            av_log(NULL, AV_LOG_ERROR, "Cannot new video stream for output!\n");
            ret = -1;
            return ret;
        }

        outputStruct->videoIndex = outputStruct->videoStream->index;
        //如果需要转码，则
        if (1 == _needVideoTranscoding) {
            ret = openOutputEncoder(outputStruct, input, avopts,
                                    AVMEDIA_TYPE_VIDEO);
            if (0 > ret) {
                av_log(NULL, AV_LOG_ERROR, "Failed to open video encoder\n");
                return ret;
            }
            outputStruct->width = outputStruct->videoCodecCtx->width;
            outputStruct->height = outputStruct->videoCodecCtx->height;
        } else {
            ret = avcodec_parameters_copy(outputStruct->videoStream->codecpar,
                                          input->videoStream->codecpar);
            if (0 > ret) {
                av_log(NULL, AV_LOG_ERROR,
                       "Failed to copy video codec parameters, %s:%d", __FILE__,
                       __LINE__);
                return ret;
            }
        }
        outputStruct->videoStream->codecpar->codec_tag = 0;
    }

    // av_dump_format(outputStruct -> formatContext, 0, outputStruct -> path,
    // 1);
    //写入文件头
    ret = writeOutputFileHeader(outputStruct->formatContext, outputStruct->path,
                                avopts);
    if (0 > ret) return ret;

    return 0;
}

int MediaOperate::openOutputEncoder(FileInfoStruct *output,
                                    FileInfoStruct *input, AvOpts &avopts,
                                    enum AVMediaType type) {
    AVCodec *encoder;
    enum AVCodecID codecid;
    int ret;
    bool nonsupportHWEncoder = true;

    avopts.initAvOpts(input);

    if (output == NULL || NULL == input) {
        av_log(NULL, AV_LOG_ERROR, "openOutputEncoder with NULl parameter\n");
        return -1;
    }

    if (AVMEDIA_TYPE_VIDEO == type) {
        // encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        // encoder = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
        if (AV_CODEC_ID_MJPEG == avopts.encoderCodecId(avopts._videoCodec))
            output->isImage =
                true;  //当输出时图片是，设置这个变量，用于处理句柄泄漏的问题

        // hw
        // 如果当前机器支持硬件编码，则选择硬件可以支持的编码，如不支持，则选用软件编码
        //目前硬件仅仅支持h264，h265的编码
        if (NULL != input->hwDeviceCtx) {
            //如果支持硬件解码，则可能有硬件编码，若不支持硬件解码，则一定不支持硬件编码
            switch (avopts.encoderCodecId(avopts._videoCodec)) {
                case AV_CODEC_ID_H264:
                    encoder = avcodec_find_encoder_by_name("h264_nvenc");
                    nonsupportHWEncoder = false;
                    break;
                case AV_CODEC_ID_HEVC:
                    encoder = avcodec_find_encoder_by_name("hevc_nvenc");
                    nonsupportHWEncoder = false;
                    break;
                default:
                    encoder = avcodec_find_encoder(
                        avopts.encoderCodecId(avopts._videoCodec));
            }
        } else {
            encoder =
                avcodec_find_encoder(avopts.encoderCodecId(avopts._videoCodec));
        }
        // hw end choose the encoder codec
        if (NULL == encoder) {
            av_log(NULL, AV_LOG_ERROR, "Cannot find video %s encoder, %s:%d\n",
                   avopts._videoCodec.c_str(), __FILE__, __LINE__);
            ret = -1;
            return ret;
        }

        output->videoCodecCtx = avcodec_alloc_context3(encoder);
        if (NULL == output->videoCodecCtx) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot alloc video h264 encoder context!");
            ret = -1;
            return ret;
        }

        output->videoCodecCtx->codec_id = encoder->id;
        output->videoCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
        output->videoCodecCtx->me_range = 16;
        output->videoCodecCtx->max_qdiff = 4;
        output->videoCodecCtx->qmin = 10;
        output->videoCodecCtx->qmax = 51;
        output->videoCodecCtx->qcompress = 0.6;
        // output -> videoCodecCtx -> bit_rate = 484000;
        // output -> videoCodecCtx -> height = 480;
        // output -> videoCodecCtx -> width = 640;
        if (0 != avopts._threadCount) {
            output->videoCodecCtx->thread_count = avopts._threadCount;
            // output->videoCodecCtx->thread_type = FF_THREAD_FRAME;
            // output->videoCodecCtx->thread_type = FF_THREAD_SLICE;
        }

        //解决读取视频码率为0的情况
        int64_t sourceBit = 0;
        if (input->videoCodecCtx->bit_rate == 0) {
            if (input->formatContext->bit_rate == 0) {
                sourceBit = avopts._videoBit;
            } else {
                if (input->audioCodecCtx != NULL)
                    sourceBit = input->formatContext->bit_rate -
                                input->audioCodecCtx->bit_rate;
                else
                    sourceBit = input->formatContext->bit_rate;
            }
        } else {
            sourceBit = input->videoCodecCtx->bit_rate;
        }
        //根据码率的控制标志来判断
        if (avopts._videoBit > sourceBit) {
            if ("usesourcebitrate" == avopts._overSourceBit)
                output->videoCodecCtx->bit_rate = sourceBit;
            else if ("usesetbitrate" == avopts._overSourceBit)
                output->videoCodecCtx->bit_rate = avopts._videoBit;
            else if ("unknown" == avopts._overSourceBit)
                return -5;
            else
                output->videoCodecCtx->bit_rate = avopts._videoBit;
        } else {
            if (0 != avopts._videoBit) {
                output->videoCodecCtx->bit_rate = avopts._videoBit;
            }
        }
        output->videoCodecCtx->height = avopts._height;
        output->videoCodecCtx->width = avopts._width;

        if (avopts._videoCodec == "H264" && nonsupportHWEncoder) {
            // correct,保证生成的mp4视频可以在苹果平台上正常播放
            av_opt_set(output->videoCodecCtx->priv_data, "profile", "baseline",
                       0);
            av_opt_set(output->videoCodecCtx->priv_data, "level", "3.0", 0);
            //控制编码的质量
            av_opt_set(output->videoCodecCtx->priv_data, "preset", "slow", 0);
        }
        if (NULL != input->videoCodecCtx)
            output->videoCodecCtx->sample_aspect_ratio =
                input->videoCodecCtx->sample_aspect_ratio;

        if (0 == avopts._frameRateDen && 1 == avopts._frameRateNum) {
            output->videoCodecCtx->time_base = input->videoCodecCtx->time_base;
            output->videoCodecCtx->framerate = input->videoCodecCtx->framerate;
        } else {
            output->videoCodecCtx->time_base =
                AVRational{avopts._frameRateNum, avopts._frameRateDen};
            output->videoCodecCtx->framerate =
                AVRational{avopts._frameRateDen, avopts._frameRateNum};
        }
        //两个非B帧之间允许出现多少个非B帧数
        //设置0表示不使用B帧
        output->videoCodecCtx->max_b_frames = 0;
        // hw 如果当前机器支持硬件编码，则需要手动的设置编码器的像素格式
        if (nonsupportHWEncoder) {
            if (encoder->pix_fmts) {
                output->videoCodecCtx->pix_fmt = encoder->pix_fmts[0];
            } else {
                if (AV_CODEC_ID_RAWVIDEO == encoder->id) {
                    output->videoCodecCtx->pix_fmt = AV_PIX_FMT_YUYV422;
                } else {
                    output->videoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
                }
            }
        } else {
            output->videoCodecCtx->pix_fmt = _hwPixFmt;
        }
        // hw end set encoder AVCodecContent pix_fmt

        if (output->formatContext->oformat->flags & AVFMT_GLOBALHEADER)
            output->videoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        // hw 如果当前机器支持硬件编码，则设置
        if (!nonsupportHWEncoder) {
            ret = setHwFrameCtx(output->videoCodecCtx, input->hwDeviceCtx);
            if (0 > ret) {
                av_log(NULL, AV_LOG_ERROR,
                       "Failed to set hwframe context,%s:%d\n", __FILE__,
                       __LINE__);
                return ret;
            }
        }
        // hw end set hwFrame

        ret = avcodec_open2(output->videoCodecCtx, encoder, NULL);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot open video output codec ,(error '%s'),%s:%d\n",
                   av_err2str(ret), __FILE__, __LINE__);
            avcodec_free_context(&output->videoCodecCtx);
            output->videoCodecCtx = NULL;
            return ret;
        }

        ret = avcodec_parameters_from_context(output->videoStream->codecpar,
                                              output->videoCodecCtx);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot initialize video stream parameters,ret %d: %s", ret,
                   av_err2str(ret));
            return ret;
        }
    } else if (AVMEDIA_TYPE_AUDIO == type) {
        encoder =
            avcodec_find_encoder(avopts.encoderCodecId(avopts._audioCodec));
        if (NULL == encoder) {
            av_log(NULL, AV_LOG_ERROR, "Cannot find audio %s encoder, %s:%d\n",
                   avopts._audioCodec.c_str(), __FILE__, __LINE__);
            ret = -1;
            return ret;
        }

        output->audioCodecCtx = avcodec_alloc_context3(encoder);
        if (NULL == output->audioCodecCtx) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot alloc audio aac encoder context!");
            ret = -1;
            return ret;
        }
        // output -> audioCodecCtx -> bit_rate = 64000;
        // output -> audioCodecCtx -> sample_rate = 44100;
        output->audioCodecCtx->bit_rate = avopts._audioBit;
        output->audioCodecCtx->sample_rate = avopts._samplespersec;
        output->audioCodecCtx->frame_size = avopts._frameSize;
        if(AV_SAMPLE_FMT_U8 == input->audioCodecCtx->sample_fmt){
            std::cout<<"reset audio sample_rate to 44100"<<std::endl;
            output->audioCodecCtx->sample_rate = 44100;
        }

        // output -> audioCodecCtx -> frame_size = 1024;
        // output -> audioCodecCtx -> channels = inputFileInfo[0] ->
        // audioCodecCtx -> channels;
        output->audioCodecCtx->channels = 2;
        output->audioCodecCtx->channel_layout =
            av_get_default_channel_layout(output->audioCodecCtx->channels);
        output->audioCodecCtx->sample_fmt = encoder->sample_fmts[0];
        output->audioCodecCtx->time_base =
            (AVRational) {1, output->audioCodecCtx->sample_rate};

        if (output->formatContext->oformat->flags & AVFMT_GLOBALHEADER)
            output->audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        ret = avcodec_open2(output->audioCodecCtx, encoder, NULL);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot open audio output codec, (error '%s'),%s:%d",
                   av_err2str(ret), __FILE__, __LINE__);
            avcodec_free_context(&output->audioCodecCtx);
            output->audioCodecCtx = NULL;
            return ret;
        }

        ret = avcodec_parameters_from_context(output->audioStream->codecpar,
                                              output->audioCodecCtx);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot initialize audio stream parameters,ret %d: %s", ret,
                   av_err2str(ret));
            return ret;
        }
    }
    ret = 0;
    return ret;
}

int MediaOperate::writeOutputFileHeader(AVFormatContext *ofmtCtx,
                                        const char *filename, AvOpts &avopts) {
    int ret;
    if (!(ofmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmtCtx->pb, filename, AVIO_FLAG_WRITE);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR, "Could not open output file\n");
            return ret;
        }
    } else {
        // ofmtCtx -> oformat = av_guess_format("mjpeg", NULL, NULL);
        ret = avio_open(&ofmtCtx->pb, filename, AVIO_FLAG_READ_WRITE);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR, "Could not open output file mjpeg\n");
            return ret;
        }
    }
    AVDictionary *dict = NULL;

    //移动moov到文件头
    if (avopts._isMoovHeader) {
        av_dict_set(&dict, "movflags", "faststart", 0);
    }
    //对mp4文件进行分片
    if (avopts._isFragment) {
        av_dict_set(&dict, "movflags", "frag_keyframe+empty_moov", 0);  //切片
    }

    
    // MOVMuxContext *mov = (MOVMuxContext *)ofmtCtx->priv_data;
    // mov->fc = ofmtCtx;
    // mov->flags |= 128;

    ret = avformat_write_header(ofmtCtx, &dict);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR,
               "Error when write output file header %d:%s\n", ret,
               av_err2str(ret));
        return ret;
    }
    return 0;
}

int MediaOperate::writeOutputFileTrailer(AVFormatContext *ofmtCtx) {
    if (NULL == ofmtCtx) {
        av_log(NULL, AV_LOG_ERROR,
               "writeOutputFileTrailer with NULL parameters, %s:%d\n", __FILE__,
               __LINE__);
        return -1;
    }
    return av_write_trailer(ofmtCtx);
}

void MediaOperate::initPacket(AVPacket *packet) {
    av_init_packet(packet);
    packet->data = NULL;
    packet->size = 0;
}

int MediaOperate::initInputFrame(AVFrame **frame) {
    if (!(*frame = av_frame_alloc())) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate input frame\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

int MediaOperate::initOutputFrame(AVFrame **frame, AVCodecContext *encCtx,
                                  int frameSize) {
    int error;
    if (!(*frame = av_frame_alloc())) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate output frame\n");
        return AVERROR_EXIT;
    }
    if (encCtx->codec_type == AVMEDIA_TYPE_VIDEO) {
        (*frame)->format = encCtx->pix_fmt;
        (*frame)->width = encCtx->width;
        (*frame)->height = encCtx->height;
    } else if (encCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
        (*frame)->nb_samples = frameSize;
        (*frame)->channel_layout = encCtx->channel_layout;
        (*frame)->format = encCtx->sample_fmt;
        (*frame)->sample_rate = encCtx->sample_rate;
    }

    error = av_frame_get_buffer(*frame, 0);
    if (error < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate output frame samples");
        av_frame_free(frame);
        return error;
    }
    return 0;
}

int MediaOperate::initOutputHWFrame(AVFrame **frame, AVCodecContext *encCtx,
                                    bool isCPUFrame) {
    int error;
    if (!(*frame = av_frame_alloc())) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate output frame, %s:%d\n",
               __FILE__, __LINE__);
        return AVERROR_EXIT;
    }
    if (encCtx->codec_type == AVMEDIA_TYPE_VIDEO) {
        (*frame)->format = AV_PIX_FMT_NV12;
        (*frame)->width = encCtx->width;
        (*frame)->height = encCtx->height;
    }
    if (isCPUFrame) {
        // init Frame where in CPU
        error = av_frame_get_buffer(*frame, 0);
    } else {
        // init Frame where in GPU
        error = av_hwframe_get_buffer(encCtx->hw_frames_ctx, *frame, 0);
    }
    if (error < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Could not allocate output frame samples,%s:%d\n", __FILE__,
               __LINE__);
        av_frame_free(frame);
        return error;
    }
    return 0;
}

int MediaOperate::initRepixel(AVCodecContext *decCtx, AVCodecContext *encCtx,
                              SwsContext **repixelCtx) {
    int error;
    *repixelCtx = sws_getCachedContext(
        NULL, decCtx->width, decCtx->height, decCtx->pix_fmt,  //输入的数据格式
        encCtx->width, encCtx->height, encCtx->pix_fmt,  //输出的数据格式
        SWS_BICUBIC, NULL, NULL, NULL);

    if (!*repixelCtx) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate repixel context\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

int MediaOperate::initHWRepixel(AVCodecContext *decCtx, AVCodecContext *encCtx,
                                SwsContext **repixelCtx) {
    int error;
    *repixelCtx = sws_getCachedContext(
        NULL, decCtx->width, decCtx->height, AV_PIX_FMT_NV12,  //输入的数据格式
        encCtx->width, encCtx->height, AV_PIX_FMT_NV12,  //输出的数据格式
        SWS_BICUBIC, NULL, NULL, NULL);

    if (!*repixelCtx) {
        av_log(NULL, AV_LOG_ERROR,
               "Could not allocate repixel hw context,%s:%d\n", __FILE__,
               __LINE__);
        return AVERROR(ENOMEM);
    }
    return 0;
}
int MediaOperate::videoSwsScale(SwsContext *repixelCtx, AVFrame *inFrame,
                                AVFrame *swsFrame) {
    int len = sws_scale(repixelCtx, inFrame->data, inFrame->linesize, 0,
                        inFrame->height, swsFrame->data, swsFrame->linesize);
    if (0 >= len) {
        av_log(NULL, AV_LOG_ERROR, "Failed while video scale,%s,%s:%d",
               av_err2str(len), __FILE__, __LINE__);
        av_frame_free(&swsFrame);
        av_frame_free(&inFrame);
        return -1;
    }
    swsFrame->pts = inFrame->pts;
    //swsFrame->dts = inFrame->dts;
    //swsFrame->pkt_duration = inFrame->pkt_duration;
    av_frame_free(&inFrame);
    return 0;
}

int MediaOperate::initFilter(FilteringContext *fctx, AVCodecContext *decCtx,
                             AVCodecContext *encCtx, const char *filterSpec) {
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc = NULL;
    const AVFilter *buffersink = NULL;

    AVFilterContext *buffersrcCtx = NULL;
    AVFilterContext *buffersinkCtx = NULL;

    AVFilterInOut *inputs = avfilter_inout_alloc();
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterGraph *filterGraph = avfilter_graph_alloc();

    if (NULL == inputs || NULL == outputs || NULL == filterGraph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (AVMEDIA_TYPE_VIDEO == decCtx->codec_type) {
        buffersrc = avfilter_get_by_name("buffer");
        buffersink = avfilter_get_by_name("buffersink");
        if (NULL == buffersrc || NULL == buffersink) {
            av_log(NULL, AV_LOG_ERROR,
                   "Filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }
        // hw if current machine support nvidia
        if (NULL != encCtx->hw_frames_ctx) {
            snprintf(args, sizeof(args),
                     "video_size=%dx%d:pix_fmt=%d:time_base=%d/"
                     "%d:pixel_aspect=%d/%d",
                     decCtx->width, decCtx->height, AV_PIX_FMT_NV12,
                     decCtx->time_base.num, decCtx->time_base.den,
                     decCtx->sample_aspect_ratio.num,
                     decCtx->sample_aspect_ratio.den);

        } else {
            snprintf(args, sizeof(args),
                     "video_size=%dx%d:pix_fmt=%d:time_base=%d/"
                     "%d:pixel_aspect=%d/%d",
                     decCtx->width, decCtx->height, decCtx->pix_fmt,
                     decCtx->time_base.num, decCtx->time_base.den,
                     decCtx->sample_aspect_ratio.num,
                     decCtx->sample_aspect_ratio.den);
        }
        // hw end
        ret = avfilter_graph_create_filter(&buffersrcCtx, buffersrc, "in", args,
                                           NULL, filterGraph);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersinkCtx, buffersink, "out",
                                           NULL, NULL, filterGraph);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
            goto end;
        }

        if (NULL != encCtx->hw_frames_ctx) {
            enum AVPixelFormat pixelFmts[] = {AV_PIX_FMT_NONE, AV_PIX_FMT_NONE};
            pixelFmts[0] = AV_PIX_FMT_NV12;
            ret = av_opt_set_int_list(buffersinkCtx, "pix_fmts", pixelFmts, -1,
                                      AV_OPT_SEARCH_CHILDREN);
        } else {
            ret = av_opt_set_bin(
                buffersinkCtx, "pix_fmts", (uint8_t *)&encCtx->pix_fmt,
                sizeof(encCtx->pix_fmt), AV_OPT_SEARCH_CHILDREN);
        }
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
            av_log(NULL, AV_LOG_ERROR, "Error (%s)\n", av_err2str(ret));
            goto end;
        }
    } else if (AVMEDIA_TYPE_AUDIO == decCtx->codec_type) {
        buffersrc = avfilter_get_by_name("abuffer");
        buffersink = avfilter_get_by_name("abuffersink");

        if (NULL == buffersrc || NULL == buffersink) {
            av_log(NULL, AV_LOG_ERROR,
                   "Filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }
        if (!decCtx->channel_layout)
            decCtx->channel_layout =
                av_get_default_channel_layout(decCtx->channels);

        snprintf(
            args, sizeof(args),
            "time_base=%d/"
            "%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%" PRIx64,
            decCtx->time_base.num, decCtx->time_base.den, decCtx->sample_rate,
            av_get_sample_fmt_name(decCtx->sample_fmt), decCtx->channel_layout);

        ret = avfilter_graph_create_filter(&buffersrcCtx, buffersrc, "in", args,
                                           NULL, filterGraph);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersinkCtx, buffersink, "in",
                                           NULL, NULL, filterGraph);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(
            buffersinkCtx, "sample_fmts", (uint8_t *)&encCtx->sample_fmt,
            sizeof(encCtx->sample_fmt), AV_OPT_SEARCH_CHILDREN);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersinkCtx, "channel_layouts",
                             (uint8_t *)&encCtx->channel_layout,
                             sizeof(encCtx->channel_layout),
                             AV_OPT_SEARCH_CHILDREN);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
            goto end;
        }

    } else {
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    /*Endpoints for the filter graph*/
    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrcCtx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersinkCtx;
    inputs->pad_idx = 0;
    inputs->next = NULL;

    if (!outputs->name || !inputs->name) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ret = avfilter_graph_parse_ptr(filterGraph, filterSpec, &inputs, &outputs,
                                   NULL);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR,
               "Failed  while call avfilter_graph_parse_ptr\n");
        goto end;
    }

    ret = avfilter_graph_config(filterGraph, NULL);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR, "Failed while call avfilter_graph_config\n");
        goto end;
    }
    /*Fill FilteringContext*/
    fctx->buffersrcCtx = buffersrcCtx;
    fctx->buffersinkCtx = buffersinkCtx;
    fctx->filterGraph = filterGraph;
end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return ret;
}
int MediaOperate::initResampler(AVCodecContext *decCtx, AVCodecContext *encCtx,
                                SwrContext **resamplerContext) {
    int error;

    if (NULL == decCtx || NULL == encCtx || NULL == resamplerContext) {
        av_log(NULL, AV_LOG_ERROR, "initResampler with NULL parameters,%s:%d\n",
               __FILE__, __LINE__);
        return -1;
    }

    *resamplerContext =
        swr_alloc_set_opts(NULL, encCtx->channel_layout, encCtx->sample_fmt,
                           encCtx->sample_rate,  //输出格式
                           decCtx->channel_layout, decCtx->sample_fmt,
                           decCtx->sample_rate,  //输入格式
                           0, NULL);
    if (!*resamplerContext) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate resample context\n");
        return AVERROR(ENOMEM);
    }
    /*
    * Perform a sanity check so that the number of converted samples is
    * not greater than the number of samples to be converted.
    * If the sample rates differ, this case has to be handled differently
    */

    /* Open the resampler with the specified parameters. */
    if ((error = swr_init(*resamplerContext)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open resample context\n");
        swr_free(resamplerContext);
        return error;
    }
    return 0;
}

int MediaOperate::initConvertedSamples(uint8_t ***convertedInputSamples,
                                       AVCodecContext *encCtx, int frameSize) {
    int error;

    /* Allocate as many pointers as there are audio channels.
     * Each pointer will later point to the audio samples of the corresponding
     * channels (although it may be NULL for interleaved formats).
     */
    if (!(*convertedInputSamples = (uint8_t **)calloc(
              encCtx->channels, sizeof(**convertedInputSamples)))) {
        av_log(NULL, AV_LOG_ERROR,
               "Could not allocate converted input sample opinters\n");
        return AVERROR(ENOMEM);
    }

    /* Allocate memory for the samples of all channels in one consecutive
     * block for convenience. */
    if ((error =
             av_samples_alloc(*convertedInputSamples, NULL, encCtx->channels,
                              frameSize, encCtx->sample_fmt, 0)) < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Could not allocate converted input samples\n");
        av_freep(&(*convertedInputSamples)[0]);
        free(*convertedInputSamples);
        return error;
    }
    return 0;
}

int MediaOperate::initFifo(AVAudioFifo **fifo, AVCodecContext *encCtx) {
    if (!(*fifo = av_audio_fifo_alloc(encCtx->sample_fmt, encCtx->channels,
                                      encCtx->frame_size))) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate FIFO\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

int MediaOperate::convertSamples(SwrContext *resamplerContext,
                                 uint8_t **convertedData,
                                 const int outframeSize,
                                 const uint8_t **inputData,
                                 const int inframeSize, int *cachenum) {
    int error;

    /* Convert the samples using the resampler. */
    if ((error = swr_convert(resamplerContext, convertedData,
                             outframeSize,  //输出数据
                             inputData, inframeSize)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not convert input samples\n");
        return error;
    }
    *cachenum = swr_get_out_samples(resamplerContext, 0);

    return 0;
}

int MediaOperate::addSamplesToFifo(AVAudioFifo *fifo,
                                   uint8_t **convertedInputSamples,
                                   const int frameSize) {
    int error;

    /* Make the FIFO as large as it needs to be to hold both,
     * the old and the new samples. */
    if ((error = av_audio_fifo_realloc(
             fifo, av_audio_fifo_size(fifo) + frameSize)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not reaallocate FIFO\n");
        return error;
    }

    /* Store the new samples in the FIFO buffer. */
    if (av_audio_fifo_write(fifo, (void **)convertedInputSamples, frameSize) <
        frameSize) {
        av_log(NULL, AV_LOG_ERROR, "Could not write data to FIFO\n");
        return AVERROR_EXIT;
    }
    return 0;
}

int MediaOperate::decodeMediaFrame(AVPacket *inputPacket, AVFrame *frame,
                                   AVCodecContext *decCtx, int *dataPresent,
                                   int *finished) {
    int error;
    if (inputPacket->data == NULL && inputPacket->size == 0) {
        av_log(NULL, AV_LOG_ERROR, "the parameter input packet is NULL\n");
        error = -1;
        goto cleanup;
    }
    /* Send the audio frame stored in the temporary packet to the decoder.
     * The input audio stream decoder is used to do this. */
    if ((error = avcodec_send_packet(decCtx, inputPacket)) < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Could not send packet for decoding %d: %s\n", error,
               av_err2str(error)); // 忽略该错误
        // return error;
    }

    /* Receive one frame from the decoder. */
    error = avcodec_receive_frame(decCtx, frame);
    /* If the decoder asks for more data to be able to decode a frame,
     * return indicating that no data is present. */
    if (error == AVERROR(EAGAIN)) {
        error = 1;  //正确的应该设置为0，然后在程序中处理这个
        goto cleanup;
        /* If the end of the input file is reached, stop decoding. */
    } else if (error == AVERROR_EOF) {
        *finished = 1;
        error = 0;
        goto cleanup;
    } else if (error < 0) {
        error = 1; // asf类视频此错误可以跳过
        av_log(NULL, AV_LOG_ERROR, "Could not decode frame\n");
        goto cleanup;
        /* Default case: Return decoded data. */
    } else {
        *dataPresent = 1;
        // mypts = frame -> best_effort_timestamp;
        goto cleanup;
    }
cleanup:
    av_packet_unref(inputPacket);
    return error;
}

int MediaOperate::readDecodeFromFilter(FilteringContext *fctx, AVFrame *input,
                                       AVFrame *filtFrame) {
    int ret;
    if (NULL == fctx || NULL == fctx->buffersrcCtx ||
        NULL == fctx->buffersinkCtx || NULL == fctx->filterGraph ||
        NULL == input) {
        av_log(NULL, AV_LOG_ERROR,
               "Failed with the input parameters is NULL,%s:%d\n", __FILE__,
               __LINE__);
        return -1;
    }
    //添加inputFrame  to filter
    ret = av_buffersrc_add_frame_flags(fctx->buffersrcCtx, input, 0);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR,
               "Failed while pushing the filtergraph,error:%s,%s:%d\n",
               av_err2str(ret), __FILE__, __LINE__);
        return ret;
    }
    // pull filtFrame from
    // filter,对于视频来说，这个不需要while，音频可能需要，还没有弄太懂
    ret = av_buffersink_get_frame(fctx->buffersinkCtx, filtFrame);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR,
               "Failed while pulling the filtergraph,error:%s,%s:%d\n",
               av_err2str(ret), __FILE__, __LINE__);
        return ret;
    }
    filtFrame->pict_type =
        AV_PICTURE_TYPE_NONE;  //让编码器根据参数自行生成I/B/P帧类型
    filtFrame->pts = input->pts;
    av_frame_free(&input);
    return 0;
}

int MediaOperate::readDecodeConvertAndStore(AVFrame *inputFrame,
                                            AVAudioFifo *fifo,
                                            AVCodecContext *decCtx,
                                            AVCodecContext *encCtx,
                                            SwrContext *resamplerContext) {
    /* Temporary storage for the converted input samples. */
    uint8_t **convertedInputSamples = NULL;
    int ret = AVERROR_EXIT;
    int cachenum = -1;

    /* Initialize the temporary storage for the converted input samples. */
    if (initConvertedSamples(&convertedInputSamples, encCtx,
                             inputFrame->nb_samples) < 0)
        goto cleanup;

    /* Convert the input samples to the desired output sample format.
     * This requires a temporary storage provided by converted_input_samples. */
    if (convertSamples(resamplerContext, convertedInputSamples,
                       inputFrame->nb_samples,
                       (const uint8_t **)inputFrame->extended_data,
                       inputFrame->nb_samples, &cachenum) < 0)
        goto cleanup;

    /* Add the converted input samples to the FIFO buffer for later processing.
     */
    //将重采样后的数据放入队列中,注意在这里len和input->nb_samples的取值，必须使用len的，重采样后的！
    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    if (addSamplesToFifo(fifo, convertedInputSamples, inputFrame->nb_samples) <
        0)
        goto cleanup;

    //在重采样中会有缓存，这个几乎在demo中都没有说明，
    //导致会有一些问题，
    while (cachenum >= inputFrame->nb_samples) {
        if (convertedInputSamples) {
            av_freep(&convertedInputSamples[0]);
            free(convertedInputSamples);
        }
        if (initConvertedSamples(&convertedInputSamples, encCtx,
                                 inputFrame->nb_samples) < 0)
            goto cleanup;

        if (convertSamples(resamplerContext, convertedInputSamples,
                           inputFrame->nb_samples, NULL, 0, &cachenum) < 0)
            goto cleanup;

        if (addSamplesToFifo(fifo, convertedInputSamples,
                             inputFrame->nb_samples) < 0)
            goto cleanup;
    }
    ret = 0;

cleanup:
    if (convertedInputSamples) {
        av_freep(&convertedInputSamples[0]);
        free(convertedInputSamples);
    }
    av_frame_free(&inputFrame);

    return ret;
}

// streamIndex表示输出流的索引
int MediaOperate::encodeMediaFrame(AVFrame *frame, AVFormatContext *ofmtCtx,
                                   AVCodecContext *encCtx, int *dataPresent,
                                   int streamIndex) {
    AVPacket outputPacket;
    int error;
    int64_t pts2ms;

    initPacket(&outputPacket);

    if (frame && (encCtx->codec_type == AVMEDIA_TYPE_AUDIO)) {
        frame->pts = _audioPTS;
        _audioPTS += frame->nb_samples;
    }

    if (frame && (encCtx->codec_type == AVMEDIA_TYPE_VIDEO)) {
        // frame->pts = outputFileInfo -> videoPTS;
        // outputFileInfo -> videoPTS += encCtx->time_base.den /
        // encCtx->framerate.num;
        //frame->pts = _videoPTS;
        //_videoPTS += encCtx->time_base.den / encCtx->framerate.num;
        // outputFileInfo -> videoPTS += 1;
        /*计算公式，计算一帧的实际时间：1/fps,如25帧/s的话一帧的时间是1/25=0.04s
         *在AVFrame中根据 AVFrame -> pts * av_q2d(AVCodecContext -> time_base) =
         *1/fps
         */
    }

    error = avcodec_send_frame(encCtx, frame);
    if (error == AVERROR_EOF) {
        av_log(NULL, AV_LOG_ERROR, "avcodec_send_frame return AVERROR_EOF\n");
        error = 0;
        goto cleanup;
    } else if (error < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not send frame for encodeing\n");// 忽略该错误
        // return error; 
    }

    while (true) {
        //对于每个输入的packet或者frame，codec一般会输出一个frame或者packet，但是也有可能输出0个或者多于1个
        //对于多于1个的情况，我们使用while来解决
        error = avcodec_receive_packet(encCtx, &outputPacket);
        if (error == AVERROR(EAGAIN)) {
            error = 0;
            // cout << "EAGAIN" << endl;
            goto cleanup;
        } else if (error == AVERROR_EOF) {
            error = 0;
            goto cleanup;
        } else if (error < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not encode frame\n");
            goto cleanup;
        } else {
            *dataPresent = 1;
        }

        outputPacket.stream_index = streamIndex;

        //转换pts
        av_packet_rescale_ts(&outputPacket, encCtx->time_base,
                             ofmtCtx->streams[streamIndex]->time_base);

        // if (AVMEDIA_TYPE_AUDIO == encCtx -> codec_type){
        //	_pts2ms = outputPacket.pts *av_q2d(ofmtCtx ->
        // streams[streamIndex] ->
        // time_base) * 1000 ;//单位ms(微妙)
        //}
        //_pts2ms = outputPacket.pts *av_q2d(ofmtCtx -> streams[streamIndex] ->
        // time_base) * 1000 ;//单位ms(微妙)
        pts2ms = outputPacket.pts *
                 av_q2d(ofmtCtx->streams[streamIndex]->time_base) *
                 1000;  //单位ms(微妙)
                        //编码视频和音频的时候，两者的时间是不一样的，为了保证进度一直向前，所以取最大值
        if (pts2ms > _pts2ms) _pts2ms = pts2ms;
        av_log(NULL, AV_LOG_INFO, "progress: %s\n", printProgress().c_str());
        sendProgress();
        if (!_ip.empty()) printProgress(_ip, _port);

        if (*dataPresent &&
            (error = av_interleaved_write_frame(ofmtCtx, &outputPacket)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not write frame,%s,%s:%d\n",
                   av_err2str(error), __FILE__, __LINE__);
            goto cleanup;
        }
    }

cleanup:
    av_packet_unref(&outputPacket);
    return error;
}

int MediaOperate::encodeMediaHWFrame(AVFrame *frame, AVFormatContext *ofmtCtx,
                                     AVCodecContext *encCtx, int *dataPresent,
                                     int streamIndex) {
    int ret;
    // first copy frame from cpu to gpu
    AVFrame *gpuFrame;
    if (0 > initOutputHWFrame(&gpuFrame, encCtx, false)) {
        av_log(NULL, AV_LOG_ERROR,
               "Failed while initOutputHWFrame video ,%s:%d\n", __FILE__,
               __LINE__);
        return -1;
    }
    ret = av_hwframe_transfer_data(gpuFrame, frame, 0);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR,
               "Failed while copy frame from cpu to gpu,%s,%s:%d\n",
               av_err2str(ret), __FILE__, __LINE__);
        return ret;
    }
    av_log(NULL, AV_LOG_INFO, "video encode with nvidia................\n");

    ret = encodeMediaFrame(gpuFrame, ofmtCtx, encCtx, dataPresent, streamIndex);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR, "Failed while encode hw frame video,%s:%d\n",
               __FILE__, __LINE__);
        av_frame_free(&gpuFrame);
        av_frame_free(&frame);
        return ret;
    }
    av_frame_free(&gpuFrame);
    return 0;
}

int MediaOperate::loadEncodeAndWrite(AVAudioFifo *fifo,
                                     AVFormatContext *outputFormatContext,
                                     AVCodecContext *encCtx, int streamIndex) {
    /* Temporary storage of the output samples of the frame written to the file.
     */
    AVFrame *outputFrame;
    /* Use the maximum number of possible samples per frame.
     * If there is less than the maximum possible frame size in the FIFO
     * buffer use this number. Otherwise, use the maximum possible frame size.
     */
    const int frameSize = FFMIN(av_audio_fifo_size(fifo), encCtx->frame_size);
    int dataWritten;

    /* Initialize temporary storage for one output frame. */
    if (initOutputFrame(&outputFrame, encCtx, frameSize) < 0)
        return AVERROR_EXIT;

    /* Read as many samples from the FIFO buffer as required to fill the frame.
     * The samples are stored in the frame temporarily. */
    if (av_audio_fifo_read(fifo, (void **)outputFrame->data, frameSize) <
        frameSize) {
        av_log(NULL, AV_LOG_ERROR, "Could not read data from FIFO\n");
        av_frame_free(&outputFrame);
        return AVERROR_EXIT;
    }

    /* Encode one frame worth of audio samples. */
    if (encodeMediaFrame(outputFrame, outputFormatContext, encCtx, &dataWritten,
                         streamIndex) < 0) {
        av_frame_free(&outputFrame);
        return AVERROR_EXIT;
    }
    av_frame_free(&outputFrame);
    return 0;
}
int MediaOperate::getFrameCopyNum(int frameCodecId, double srcFrameRate,
                                  double dstFrameRate) {
    int frameCopyNum = 1;
    int needFrameNum = round(dstFrameRate - srcFrameRate);
    int frameStep = round(srcFrameRate - needFrameNum);
    int sourceFrameRate = round(srcFrameRate);
    if ((frameCodecId - frameStep) % sourceFrameRate >= 0 &&
        (frameCodecId - frameStep) % sourceFrameRate < needFrameNum &&
        (dstFrameRate - sourceFrameRate) != 0)
        frameCopyNum = 2;
    return frameCopyNum;
}
int MediaOperate::flushEncoder(FileInfoStruct *output) {
    if (NULL == output) {
        av_log(NULL, AV_LOG_ERROR, "flushEncoder with NULL parameters, %s:%d\n",
               __FILE__, __LINE__);
        return -1;
    }

    int dataWritten;
    int ret;
    if (NULL != output->videoStream) {
        do {
            dataWritten = 0;
            ret = encodeMediaFrame(NULL, output->formatContext,
                                   output->videoCodecCtx, &dataWritten,
                                   output->videoStream->index);
            if (0 > ret) {
                av_log(NULL, AV_LOG_ERROR,
                       "Failed whileflush video encode, %s:%d\n", __FILE__,
                       __LINE__);
                return ret;
            }
            av_log(NULL, AV_LOG_INFO, "flush video encoder data\n");
        } while (dataWritten);
    }

    if (NULL != output->audioStream) {
        do {
            dataWritten = 0;
            ret = encodeMediaFrame(NULL, output->formatContext,
                                   output->audioCodecCtx, &dataWritten,
                                   output->audioStream->index);
            if (0 > ret) {
                av_log(NULL, AV_LOG_ERROR,
                       "Failed while flush audio encode, %s:%d\n", __FILE__,
                       __LINE__);
                return ret;
            }
            av_log(NULL, AV_LOG_INFO, "flush audio encoder data\n");
        } while (dataWritten);
    }
    return 0;
}
int MediaOperate::setHwFrameCtx(AVCodecContext *ctx, AVBufferRef *hwDeviceCtx) {
    AVBufferRef *hwFramesRef = NULL;
    AVHWFramesContext *hwFramesCtx = NULL;
    int err = 0;

    hwFramesRef = av_hwframe_ctx_alloc(hwDeviceCtx);
    if (NULL == hwFramesRef) {
        av_log(NULL, AV_LOG_ERROR, "Failed during av_hwframe_ctx_alloc,%s:%d\n",
               __FILE__, __LINE__);
        return -1;
    }
    hwFramesCtx = (AVHWFramesContext *)(hwFramesRef->data);
    hwFramesCtx->format = AV_PIX_FMT_CUDA;
    hwFramesCtx->sw_format = AV_PIX_FMT_NV12;
    hwFramesCtx->width = ctx->width;
    hwFramesCtx->height = ctx->height;
    hwFramesCtx->initial_pool_size = 20;
    err = av_hwframe_ctx_init(hwFramesRef);
    if (0 > err) {
        av_log(NULL, AV_LOG_ERROR,
               "Failed while initialize CUDA frame context,%s:%d\n", __FILE__,
               __LINE__);
        av_buffer_unref(&hwFramesRef);
        return err;
    }
    ctx->hw_frames_ctx = av_buffer_ref(hwFramesRef);
    if (NULL == ctx->hw_frames_ctx) {
        err = AVERROR(ENOMEM);
    }
    av_buffer_unref(&hwFramesRef);
    return err;
}

bool MediaOperate::supportHWDecoder(enum AVCodecID id) {
    bool support = true;
    switch (id) {
        case AV_CODEC_ID_MPEG1VIDEO:
            ;
        case AV_CODEC_ID_MPEG2VIDEO:
            ;
        case AV_CODEC_ID_VC1:
            ;
        case AV_CODEC_ID_VP8:
            ;
        case AV_CODEC_ID_VP9:
            ;
        case AV_CODEC_ID_H264:
            ;
        case AV_CODEC_ID_HEVC:
            ;
            break;
        default:
            support = false;
    }
    return support;
}

bool MediaOperate::supportHWYUV(enum AVPixelFormat format) {
    bool support = true;
    switch (format) {
        case AV_PIX_FMT_YUV420P:
            ;
        case AV_PIX_FMT_YUV444P:
            ;
            break;
        default:
            support = false;
    }
    return support;
}

bool MediaOperate::needScale(FileInfoStruct *input, FileInfoStruct *output) {
    if (input->width == output->width && output->height == input->height &&
        input->videoCodecCtx->pix_fmt == output->videoCodecCtx->pix_fmt)
        return false;
    else
        return true;
}

void MediaOperate::getPosition(const string &position, int width, int height,
                        int fwidth, int fheight, int &x, int &y, int marginx, int marginy) {
    if (position == "TOP_LEFT") {
        x = 0 + marginx;
        y = 0 + marginx;
    } else if (position == "LEFT_MIDDLE") {
        x = width / 2 - fwidth / 2;
        y = 0 + marginy;
    } else if (position == "TOP_RIGHT") {
        x = width - fwidth + marginx;
        y = 0 + marginy;
    } else if (position == "TOP_MIDDLE") {
        x = 0 + marginx;
        y = height / 2 - fheight / 2;
    } else if (position == "CENTER") {
        x = width / 2 - fwidth / 2;
        y = height / 2 - fheight / 2;
    } else if (position == "RIGHT_MIDDLE") {
        x = width - fwidth - marginx;
        y = height / 2 - fheight / 2 - marginy;
    } else if (position == "BOTTOM_LEFT") {
        x = 0 + marginx;
        y = height - fheight - marginy;
    } else if (position == "BOTTOM_MIDDLE") {
        x = width / 2 - fwidth / 2;
        y = height - fheight - marginy;
    } else if (position == "BOTTOM_RIGHT") {
        x = width - fwidth - marginx;
        y = height - fheight - marginy;
    }
}
int MediaOperate::transcodeFile(FileInfoStruct *input, FileInfoStruct *output,
                                AvOpts &avopts, int *finished) {
    int ret;

    if (NULL == input || NULL == output) {
        av_log(NULL, AV_LOG_ERROR, "Failed with parameters is NULL,%s:%d\n",
               __FILE__, __LINE__);
        return -1;
    }

    if(1 == _needVideoTranscoding){
        if (!avopts._waterInfo._content.empty()) {
            //初始化过滤器
            if (NULL != input->videoStream) {
                int positionX = avopts._waterInfo._x;
                int positionY = avopts._waterInfo._y;
                if(avopts._waterInfo._position != ""){
                    getPosition(avopts._waterInfo._position, 
                            output->videoCodecCtx->width, output->videoCodecCtx->height,
                            avopts._waterInfo._width, avopts._waterInfo._height,
                            positionX, positionY,
                            avopts._waterInfo._marginX, avopts._waterInfo._marginY);
                }                
                char filterSpec[1024];
                if (!avopts._waterInfo._isImage) {
                    snprintf(
                        filterSpec, sizeof(filterSpec),
                        "scale=%d:%d,drawtext=fontfile=%s:fontsize=%d:text=%s:x=%d:"
                        "y=%d:fontcolor=%s@%f",
                        output->videoCodecCtx->width, output->videoCodecCtx->height,
                        avopts._waterInfo._fontFile.c_str(),
                        avopts._waterInfo._fontSize,
                        avopts._waterInfo._content.c_str(), positionX,
                        positionY, avopts._waterInfo._fontColor.c_str(),
                        avopts._waterInfo._alph);
                } else {
                    snprintf(filterSpec, sizeof(filterSpec),
                             "movie=%s[wm];[in][wm]overlay=%d:%d,scale=%d:%d,lut=a=val*0.95[out]",
                             avopts._waterInfo._content.c_str(),
                             positionX, positionY,
                             output->videoCodecCtx->width,
                             output->videoCodecCtx->height);
                }
                ret = initFilter(input->fctx, input->videoCodecCtx,
                                 output->videoCodecCtx, filterSpec);
                if (0 > ret) {
                    av_log(NULL, AV_LOG_ERROR, "Failed while call initFilter\n");
                    return ret;
                }
            }
        } else {
            //初始化视频像素转换
            if (NULL != input->videoStream) {
                // hw if current machine support the Nvidia device
                if (NULL != input->hwDeviceCtx) {
                    ret = initHWRepixel(input->videoCodecCtx, output->videoCodecCtx,
                                        &input->videoScalerCtx);
                } else {
                    ret = initRepixel(input->videoCodecCtx, output->videoCodecCtx,
                                      &input->videoScalerCtx);
                }
                // hw end
                if (0 > ret) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Failed  while call initRepixel,%s:%d\n", __FILE__,
                           __LINE__);
                    return ret;
                }
            }
        }
    }
    //初始化音频重采样
    if (NULL != input->audioStream) {
        ret = initResampler(input->audioCodecCtx, output->audioCodecCtx,
                            &input->audioResampleCtx);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed occur while call initResampler,%s:%d\n", __FILE__,
                   __LINE__);
            return ret;
        }
        //初始化音频队列
        ret = initFifo(&input->audioFifo, output->audioCodecCtx);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed occur while call initFifo,%s:%d\n", __FILE__,
                   __LINE__);
            return ret;
        }
    }

    bool audioFinished = false;
    bool videoFinished = false;
    bool videoPacketContinue = false;
    int dataPresent = 0;
    bool packetContinue = false;
    double lastTimeDuration = 0;
    while (true) {
        AVPacket packet;
        initPacket(&packet);

        ret = av_read_frame(input->formatContext, &packet);
        if (0 > ret) {
            if (AVERROR_EOF == ret) {
                *finished = 1;
                av_log(NULL, AV_LOG_ERROR, "read frame AVERROR_EOF\n");
                break;
            } else {
                av_log(NULL, AV_LOG_ERROR, "Cannot to read frame\n");
                //这里最开始没有处理，值得考虑，为什么
                return ret;
            }
        }

       double defaultpts = 28.5;
       double pts = packet.pts;
        double currTime =
            av_q2d(
                input->formatContext->streams[packet.stream_index]->time_base) * pts;

        if (packet.stream_index == input->audioIndex) {  // input -> audioIndex

            av_log(NULL, AV_LOG_INFO, "audio decode......\n");
            AVFrame *inputFrame = NULL;
            if (initInputFrame(&inputFrame) < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "Failed  while call initInputFrame audio,%s:%d\n",
                       __FILE__, __LINE__);
                return -1;
            }

            ret = decodeMediaFrame(&packet, inputFrame, input->audioCodecCtx,
                                   &dataPresent, finished);
            if (1 == ret) {
                av_frame_free(&inputFrame);
                continue;
            } else if (0 > ret) {
                av_log(NULL, AV_LOG_ERROR,
                       "Failed while call decodeMediaFrame video,%s:%d \n",
                       __FILE__, __LINE__);
                return ret;
            }

            if(pts < 0){
                    pts = inputFrame->pts;
                    if(pts < 0){
                        pts = av_frame_get_best_effort_timestamp(inputFrame);
                    }
                    if(pts < 0) pts = defaultpts;
                    currTime = av_q2d(input->formatContext->streams[packet.stream_index]->time_base) * pts;
            }

            if (1 == dataPresent) {
                if (currTime >= avopts._startTime) {
                    //对于currTime小于0的情况，如果在有currTIme大于startTIme后，则需要保留
                    packetContinue = true;
                } else {
                    if (!packetContinue) {
                        av_frame_free(&inputFrame);
                        continue;
                    }
                }
                if (currTime <= avopts._endTime) {
                } else {
                    if(audioFinished && videoFinished && (currTime > avopts._endTime)) {
                        *finished = 1;
                        av_frame_free(&inputFrame);
                        break;
                    }
                    if (-1 != avopts._endTime) {
                        audioFinished = true;
                        av_frame_free(&inputFrame);
                        continue;
                    }
                }
                //音频解码成功，再进行音频的重采集
                ret = readDecodeConvertAndStore(
                    inputFrame, input->audioFifo, input->audioCodecCtx,
                    output->audioCodecCtx, input->audioResampleCtx);
                if (0 > ret) {
                    return ret;
                }
                const int outputFrameSize = output->audioCodecCtx->frame_size;
                while (
                    av_audio_fifo_size(input->audioFifo) >= outputFrameSize ||
                    ((*finished) && av_audio_fifo_size(input->audioFifo) > 0)) {
                    //编码
                    ret = loadEncodeAndWrite(
                        input->audioFifo, output->formatContext,
                        output->audioCodecCtx, output->audioStream->index);
                    if (0 > ret) {
                        av_log(NULL, AV_LOG_ERROR,
                               "Failed  while loadEncodeAndWrite,%s:%d\n",
                               __FILE__, __LINE__);
                        return ret;
                    }
                }
            }
        } else if ((1 == _needVideoTranscoding) && (packet.stream_index == 
                   input->videoIndex)) {  // input -> videoIndex
            av_log(NULL, AV_LOG_INFO, "video decode......\n");
            AVFrame *inputFrame = NULL;
            if (initInputFrame(&inputFrame) < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "Failed  while call initInputFrame,%s:%d\n", __FILE__,
                       __LINE__);
                return -1;
            }
		av_packet_rescale_ts(&packet, input->videoStream->time_base, output->videoCodecCtx->time_base);

            ret = decodeMediaFrame(&packet, inputFrame, input->videoCodecCtx,
                                   &dataPresent, finished);
            if (1 == ret) {
                av_frame_free(&inputFrame);
                continue;
            } else if (0 > ret) {
                av_log(NULL, AV_LOG_ERROR,
                       "Failed  while call decodeMediaFrame video,%s:%d \n",
                       __FILE__, __LINE__);
                return ret;
            }
            if(pts < 0){
                    pts = inputFrame->pts;
                    if(pts < 0){
                        pts = av_frame_get_best_effort_timestamp(inputFrame);
                    }
                    if(pts < 0) pts = defaultpts;
                    currTime = av_q2d(input->formatContext->streams[packet.stream_index]->time_base) * pts;
            }
            pts = inputFrame->pts;
            if (1 == dataPresent) {

                if (currTime >= avopts._startTime) {
                    //对于currTime小于0的情况，如果在有currTIme大于startTIme后，则需要保留
                    videoPacketContinue = true;
                } else {
                    if (!videoPacketContinue) {
                        av_frame_free(&inputFrame);
                        continue;
                    }
                }

                if (currTime <= avopts._endTime) {
                } else {
                    if(audioFinished && videoFinished && (currTime > avopts._endTime)){
                        *finished = 1;
                        av_frame_free(&inputFrame);
                        break;
                    }
                    if (-1 != avopts._endTime) {
                        videoFinished = true;
                        av_frame_free(&inputFrame);
                        continue;
                    }
                }
                //获取每个帧的序号，用于控制帧率时使用
                int coded_picture_number = inputFrame->coded_picture_number;
                //计算当前帧的时间
                // cout << inputFrame ->pts* av_q2d(input -> videoStream ->
                // time_base) << endl;
                //解码成功，先判读是软件解码还是硬件解码
                // hw if the current machine support
                // nvidia,则将解码的frame从GPU中copy到CPU进行像素转换
                AVFrame *swsFrame = NULL;
                if (NULL != output->videoCodecCtx->hw_frames_ctx) {
                    if (needScale(input, output) ||
                        !avopts._waterInfo._content.empty()) {
                        AVFrame *outFrameFromGPU;
                        outFrameFromGPU = av_frame_alloc();
                        ret = av_hwframe_transfer_data(outFrameFromGPU,
                                                       inputFrame, 0);
                        if (0 > ret) {
                            av_log(NULL, AV_LOG_ERROR, "%s,%s:%d\n",
                                   av_err2str(ret), __FILE__, __LINE__);
                            return ret;
                        }
                        outFrameFromGPU->pts = inputFrame->pts;
                        av_frame_free(&inputFrame);

                        if (0 > initOutputHWFrame(&swsFrame,
                                                  output->videoCodecCtx)) {
                            av_log(
                                NULL, AV_LOG_ERROR,
                                "Failed while initOutputHWFrame video,%s:%d\n",
                                __FILE__, __LINE__);
                            return -1;
                        }
                        inputFrame = outFrameFromGPU;
                    }
                } else {
                    // hw 考虑硬件解码，软件编码的方式
                    if (NULL != input->hwDeviceCtx) {
                        AVFrame *outFrameFromGPU;
                        outFrameFromGPU = av_frame_alloc();
                        ret = av_hwframe_transfer_data(outFrameFromGPU,
                                                       inputFrame, 0);
                        if (0 > ret) {
                            av_log(NULL, AV_LOG_ERROR, "%s,%s:%d\n",
                                   av_err2str(ret), __FILE__, __LINE__);
                            return ret;
                        }
                        outFrameFromGPU->pts = inputFrame->pts;
                        av_frame_free(&inputFrame);
                        inputFrame = outFrameFromGPU;
                    }
                    if (0 >
                        initOutputFrame(&swsFrame, output->videoCodecCtx, 0)) {
                        av_log(NULL, AV_LOG_ERROR,
                               "Failed  while initOutputFrame video,%s:%d\n",
                               __FILE__, __LINE__);
                        return -1;
                    }
                }
                // hw end

                //判读是否需要水印
                if (!avopts._waterInfo._content.empty()) {
                    //需要水印
                    ret =
                        readDecodeFromFilter(input->fctx, inputFrame, swsFrame);
                } else {
                    //不需要水印，判读是否需要进行像素转换
                    if (needScale(input, output)) {
                        //需要像素转换
                        ret = videoSwsScale(input->videoScalerCtx, inputFrame,
                                            swsFrame);
                        if (0 > ret) {
                            av_log(NULL, AV_LOG_ERROR,
                                   "Failed while videoSwsScale, %s:%d\n",
                                   __FILE__, __LINE__);
                            return ret;
                        }
                    } else {
                        //不需要像素转换
                        if (NULL != swsFrame) av_frame_free(&swsFrame);
                        swsFrame = inputFrame;
                    }
                }
                swsFrame->pts = pts;
                //进行编码
                int dataWritten;
                int frameCopyNum =
                    getFrameCopyNum(coded_picture_number,
                                    av_q2d(input->videoStream->avg_frame_rate),
                                    av_q2d(output->videoCodecCtx->framerate));
                lastTimeDuration = currTime;
                for (int i = 0; i < frameCopyNum; i++) {
                    // hw
                    if (NULL == output->videoCodecCtx->hw_frames_ctx) {
                        ret = encodeMediaFrame(swsFrame, output->formatContext,
                                               output->videoCodecCtx,
                                               &dataWritten,
                                               output->videoStream->index);
                        if (0 > ret) {
                            av_log(NULL, AV_LOG_ERROR,
                                   "Failed occur while encodeMediaFrame "
                                   "video,%s:%d\n",
                                   __FILE__, __LINE__);
                            av_frame_free(&swsFrame);
                            return ret;
                        }
                    } else {
                        // hw if current machine support nvidia
                        if (needScale(input, output) ||
                            !avopts._waterInfo._content.empty()) {
                            ret = encodeMediaHWFrame(
                                swsFrame, output->formatContext,
                                output->videoCodecCtx, &dataWritten,
                                output->videoStream->index);
                        } else {
                            av_log(
                                NULL, AV_LOG_INFO,
                                "video encode with nvidia................\n");
                            ret = encodeMediaFrame(
                                swsFrame, output->formatContext,
                                output->videoCodecCtx, &dataWritten,
                                output->videoStream->index);
                        }
                        if (0 > ret) {
                            return ret;
                        }
                    }
                    // hw end
                }
                av_frame_free(&swsFrame);
            }
        } else {
            av_log(NULL, AV_LOG_ERROR, "Cannot find video or audio packet\n");
        }
    }

    //	if (1 == finished){
    //		//flush
    //		ret = flushEncoder(output);
    //		if (0 > ret){
    //			av_log(NULL, AV_LOG_ERROR, "FlushEncoder
    // Failed,%s:%d\n",
    //__FILE__,
    //__LINE__);
    //			return ret;
    //		}
    //	}

    //写入文件尾部信息
    // ret = writeOutputFileTrailer(output -> formatContext);

    return 0;
}

int MediaOperate::transcode(FileInfoStruct *input, FileInfoStruct *output,
                            AvOpts &avopts) {
    int ret;
    int finished = 0;

    //打开输入文件
    ret = openInputFile(input);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR, "Failed  while call openInputFile,%s:%d\n",
               __FILE__, __LINE__);
        return ret;
    }

    // cout << input -> audioCodecCtx -> frame_size << endl;
    // getchar();
    //打开输出文件
    ret = openOutputFile(output, input, avopts);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR, "Failed  while call openOutputFile,%s:%d\n",
               __FILE__, __LINE__);
        return ret;
    }

    ret = transcodeFile(input, output, avopts, &finished);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR, "Failed while call transcodeFile, %s:%d\n",
               __FILE__, __LINE__);
        return ret;
    }
    if (1 == finished) {
        flushEncoder(output);
    }
    ret = writeOutputFileTrailer(output->formatContext);
    return ret;
}

int MediaOperate::cutVideo(FileInfoStruct *input, FileInfoStruct *output,
                           AvOpts &avopts) {
    int ret;

    if (NULL == input || NULL == output) {
        av_log(NULL, AV_LOG_ERROR, "Failed with parameters is NULL, %s:%d\n",
               __FILE__, __LINE__);
        return -1;
    }

    ret = openInputFile(input);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR, "Failed while call openInputFile, %s:%d",
               __FILE__, __LINE__);
        return ret;
    }

    if (NULL != input->videoCodecCtx)
        avopts._videoCodec =
            avopts.encoderCodecName(input->videoCodecCtx->codec_id, true);
    if (NULL != input->audioCodecCtx)
        avopts._audioCodec =
            avopts.encoderCodecName(input->audioCodecCtx->codec_id, false);

    ret = openOutputFile(output, input, avopts);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR, "Failed while call openOutputFile, %s:%d",
               __FILE__, __LINE__);
        return ret;
    }

    if (0 > avopts._startTime || _outDuration < avopts._startTime * 1000)
        avopts._startTime = 0;

    if (0 >= avopts._endTime || _outDuration < avopts._endTime * 1000) {
        avopts._endTime = _outDuration / 1000.0;
    }
    _outDuration = (avopts._endTime - avopts._startTime) * 1000;

    int finished = 0;
    ret = transcodeFile(input, output, avopts, &finished);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR, "Failed while call transcodeFile, %s:%d\n",
               __FILE__, __LINE__);
        return ret;
    }
    if (1 == finished) {
        flushEncoder(output);
    }
    //写入文件尾信息
    ret = writeOutputFileTrailer(output->formatContext);

    return ret;
}

//获取关键帧保存为图片的函数
int MediaOperate::getKeyFrame(FileInfoStruct *input, AvOpts &avopts) {

    int ret;

    if (NULL == input) {
        av_log(NULL, AV_LOG_ERROR, "Failed with parameters is NULL, %s:%d\n",
               __FILE__, __LINE__);
        return -1;
    }
    //打开输入的文件
    if (NULL == input->formatContext) {
        ret = openInputFile(input);
        if (0 > ret) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed occur while call openInputFile ,%s:%d\n", __FILE__,
                   __LINE__);
            return ret;
        }
        //输入文件打开后，可能会是硬件支持的格式，通过硬件方式来进行解码
        //为了编码
    }
    //判断输出目录是否存在,如无则创建
    checkDir(avopts._outputFile);

    int dataPresent = 0;
    int finished = 0;
    char buf[1024];

    //截取某个时间点的图片
    // if (0 <= avopts._startTime){
    // if 	ret = av_seek_frame(input -> formatContext, -1,
    // (int64_t)avopts._startTime*AV_TIME_BASE, AVSEEK_FLAG_BACKWARD);
    // if 	if (0 > ret){
    // if 		av_log(NULL, AV_LOG_ERROR, "Failed to call
    // av_seek_frame,%s,%s:%d",av_err2str(ret), __FILE__, __LINE__);
    // if 		return ret;
    // if 	}
    // if }

    while (true) {
        AVPacket packet;
        initPacket(&packet);
        ret = av_read_frame(input->formatContext, &packet);
        if (0 > ret) {
            if (AVERROR_EOF == ret) {
                finished = 1;
                av_log(NULL, AV_LOG_ERROR,
                       "read frame return AVERROR_EOF, %s:%d\n", __FILE__,
                       __LINE__);
                break;
            } else {
                av_log(NULL, AV_LOG_ERROR, "Cannot to read frame ,%s,%s:%d\n",
                       av_err2str(ret), __FILE__, __LINE__);
            }
        }
        double currTime =
            av_q2d(
                input->formatContext->streams[packet.stream_index]->time_base) *
            packet.pts;

        //判断是否是关键帧，如果不是，则不处理
        if (packet.stream_index == input->videoStream->index) {
            if (packet.flags & AV_PKT_FLAG_KEY) {
                //是关键帧；
            } else {
                //不是关键帧；
                if (0 > avopts._startTime) {
                    av_packet_unref(&packet);
                    continue;
                }
            }
            //如果是视频则进行保存图片,先进行重采样,在进行编码,只是输出的文件变成了多个
            // 1,初始化输出的文件
            //先解码
            AVFrame *inputFrame = NULL;
            if (0 > (initInputFrame(&inputFrame))) {
                av_log(NULL, AV_LOG_INFO,
                       "Failed call initInputFrame decode,%s:%d\n", __FILE__,
                       __LINE__);
                return ret;
            }
            ret = decodeMediaFrame(&packet, inputFrame, input->videoCodecCtx,
                                   &dataPresent, &finished);
            if (1 == ret) {
                av_frame_free(&inputFrame);
                continue;
            } else if (0 > ret) {
                av_log(NULL, AV_LOG_ERROR,
                       "Failed call decodeMediaFrame,%s:%d\n", __FILE__,
                       __LINE__);
                return ret;
            }

            if (1 == dataPresent) {
                //解码成功，计算当前的时间，按时间间隔获取图片
                if (currTime >= avopts._startTime) {
                } else {
                    av_frame_free(&inputFrame);
                    continue;
                }

                snprintf(buf, sizeof(buf), "%s/%s_%d.jpg",
                         avopts._outputFile.c_str(),
                         substrFileName(avopts._inputFile).c_str(),
                         (int)currTime);
                avopts._videoCodec =
                    "JPG";  //硬件不支持JPG的编码，所以后面是软件编码
                FileInfoStruct *tmpout = new FileInfoStruct;
                tmpout->path = buf;
                tmpout->isInput = false;
                ret = openOutputFile(tmpout, input, avopts);
                if (0 > ret) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Failed while openOutputFile  ,%s:%d\n", __FILE__,
                           __LINE__);
                    delete tmpout;
                    return ret;
                }
                //初始化，重采样
                // hw
                if (NULL != input->hwDeviceCtx) {
                    ret = initHWRepixel(input->videoCodecCtx,
                                        tmpout->videoCodecCtx,
                                        &tmpout->videoScalerCtx);
                } else {
                    ret =
                        initRepixel(input->videoCodecCtx, tmpout->videoCodecCtx,
                                    &tmpout->videoScalerCtx);
                }
                // hw end
                if (0 > ret) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Failed while call initRepixel, %s:%d\n", __FILE__,
                           __LINE__);
                    delete tmpout;
                    return ret;
                }
                AVFrame *outFrame;
                // hw
                if (NULL != input->hwDeviceCtx) {
                    // support nvidia decoder,first copy from GPU to GPU
                    AVFrame *outFrameFromGPU;
                    outFrameFromGPU = av_frame_alloc();
                    ret = av_hwframe_transfer_data(outFrameFromGPU, inputFrame,
                                                   0);
                    if (0 > ret) {
                        av_log(NULL, AV_LOG_ERROR, "%s,%s:%d\n",
                               av_err2str(ret), __FILE__, __LINE__);
                        delete tmpout;
                        return ret;
                    }
                    av_frame_free(&inputFrame);
                    inputFrame = outFrameFromGPU;
                }
                if (0 > initOutputFrame(&outFrame, tmpout->videoCodecCtx, 0)) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Failed call initOutputFrame, %s:%d\n", __FILE__,
                           __LINE__);
                    delete tmpout;
                    return ret;
                }
                ret =
                    videoSwsScale(tmpout->videoScalerCtx, inputFrame, outFrame);
                if (0 > ret) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Failed while videoSwsScale, %s:%d\n", __FILE__,
                           __LINE__);
                    delete tmpout;
                    return ret;
                }
                // hw end

                //进行编码
                int dataWritten;
                ret = encodeMediaFrame(outFrame, tmpout->formatContext,
                                       tmpout->videoCodecCtx, &dataWritten,
                                       tmpout->videoStream->index);
                if (0 > ret) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Failed while encodeMediaFrame , %s:%d\n", __FILE__,
                           __LINE__);
                    av_frame_free(&outFrame);
                    delete tmpout;
                    return ret;
                }
                av_frame_free(&outFrame);
                av_write_trailer(tmpout->formatContext);
                delete tmpout;

                if (0 <= avopts._startTime && 0 < avopts._length) {
                    //时间间隔，截取多个，不break
                    avopts._startTime = avopts._startTime + avopts._length;
                } else {
                    if (0 <= avopts._startTime) {
                        break;
                    }
                }
            }
        }
        av_packet_unref(&packet);
    }
    return 0;
}

int MediaOperate::transcode(AvOpts &avopts) {
    int ret;
    // for(int i = 1; i < 6; i++){
    //    avopts._startTime = avopts._startTime + i;
    //}
    // return 0;
    _needVideoTranscoding = 1;
    _needAudioTranscoding = 1;
    _outDuration = 0;
    _pts2ms = 0;
    _audioPTS = 0;
    _videoPTS = 0;
    _progressArg.clear();
    if (_ip.empty() || _port.empty()) {
        _ip = avopts._ip;
        _port = avopts._port;
    }

    //检测id是否存在，如果没有ip则使用本地检测方法
    if (_ip.empty()) {
        //本地检测
        if (0 > checkIdByFile(avopts._id)) return -1;
    } else {
        // redis检测
        // if (0 > checkIdByRedis(_ip, _port, avopts._id)) return -1;
    }
    _progressId = avopts._id;

    FileInfoStruct *input = new FileInfoStruct;
    input->path = avopts._inputFile.c_str();
    input->threadCount = avopts._threadCount;

    FileInfoStruct *output = new FileInfoStruct;
    output->path = avopts._outputFile.c_str();
    output->isInput = false;

    // 如果转成mp3文件不处理视频
    if("mp3" == DealFile::GetExtension(avopts._outputFile)){
        _needVideoTranscoding = 0;
    }

    if (avopts._useGPU) {
        input->useGPU = true;
    }
    //如果有图片水印，先将图片进行处理一下
    if (avopts._waterInfo._isImage) {
/*        AvOpts imageopts;
        imageopts._inputFile = avopts._waterInfo._content;
        imageopts._outputFile =
            TMP + substrFileName(imageopts._inputFile) + ".jpg";
        imageopts._videoCodec = "JPG";
        imageopts._width = avopts._waterInfo._width;
        imageopts._height = avopts._waterInfo._height;
        imageopts._id = "imagetranscode";
        /*
/*
        // 使用ffmpeg做图片转码报错，改为用imagemagick做图片缩放和转码
        char cmd[512];
        sprintf(cmd, "convert -resize \"%dx%d\" %s %s", imageopts._width, imageopts._height, imageopts._inputFile.c_str(), imageopts._outputFile.c_str());
        int ret = system(cmd);
*/
/*                
        FileInfoStruct *input = new FileInfoStruct;
        input->path = imageopts._inputFile.c_str();

        FileInfoStruct *output = new FileInfoStruct();
        output->path = imageopts._outputFile.c_str();
        // cout << output->path << endl;
        output->isInput = false;
        ret = transcode(input, output, imageopts);

        delete input;
        input = NULL;
        delete output;
        output = NULL;

        if (0 != ret) {
            //删除临时文件
            remove(imageopts._outputFile.c_str());
            av_log(NULL, AV_LOG_ERROR, "Failed while transcode Image,%s:%d\n",
                   __FILE__, __LINE__);
            return ret;
        } else {
            avopts._waterInfo._content =
                imageopts
                    ._outputFile;  //使用完之后，临时文件应该删除？？？？？？
        }*/
    }

    ret = transcode(input, output, avopts);

    delete input;
    input = NULL;
    delete output;
    output = NULL;
    //删除图片转码的临时文件
    remove(avopts._waterInfo._content.c_str());

    if (0 > ret) return ret;
    _pts2ms = _outDuration;
    printProgress();
    if (!_ip.empty()) printProgress(_ip, _port);
    sendProgress();

    return 0;
}

int MediaOperate::cutVideo(AvOpts &avopts) {

    int ret;

    _needAudioTranscoding = 1;
    _needVideoTranscoding = 1;
    _progressArg.clear();

    if (_ip.empty() || _port.empty()) {
        _ip = avopts._ip;
        _port = avopts._port;
    }

    //检测id是否存在，如果没有ip则使用本地检测方法
    if (_ip.empty()) {
        //本地检测
        if (0 > checkIdByFile(avopts._id)) return -1;
    } else {
        // redis检测
        // if (0 > checkIdByRedis(_ip, _port, avopts._id)) return -1;
    }
    _progressId = avopts._id;

    FileInfoStruct *input = new FileInfoStruct;
    input->path = avopts._inputFile.c_str();

    // 如果转成mp3文件不处理视频
    if("mp3" == DealFile::GetExtension(avopts._outputFile)){
        _needVideoTranscoding = 0;
    }

    FileInfoStruct *output = new FileInfoStruct;
    output->path = avopts._outputFile.c_str();
    output->isInput = false;

    if (avopts._useGPU) input->useGPU = true;
    ret = cutVideo(input, output, avopts);

    delete input;
    input = NULL;
    delete output;
    output = NULL;

    if (0 > ret) return -1;

    _pts2ms = _outDuration;
    printProgress();
    if (!_ip.empty()) printProgress(_ip, _port);
    sendProgress();

    return 0;
}

int MediaOperate::getKeyFrame(AvOpts &avopts) {
    int ret;

    _needAudioTranscoding = 0;
    _needVideoTranscoding = 1;

    FileInfoStruct *input = new FileInfoStruct;
    input->path = avopts._inputFile.c_str();

    ret = getKeyFrame(input, avopts);
    if (0 > ret) goto _fail;

_fail:
    delete input;
    input = NULL;

    if (0 > ret) return -1;

    return 0;
}

int MediaOperate::mergeVideo(AvOpts &avopts) {

    int ret;

    _needVideoTranscoding = 1;
    _needAudioTranscoding = 1;

    _outDuration = 0;
    _pts2ms = 0;
    _audioPTS = 0;
    _videoPTS = 0;
    _progressArg.clear();
    if (_ip.empty() || _port.empty()) {
        _ip = avopts._ip;
        _port = avopts._port;
    }

    //检测id是否存在，如果没有ip则使用本地检测方法
    if (_ip.empty()) {
        //本地检测
        if (0 > checkIdByFile(avopts._id)) return -1;
    } else {
        // redis检测
        // if (0 > checkIdByRedis(_ip, _port, avopts._id)) return -1;
    }
    _progressId = avopts._id;

    vector<FileInfoStruct *> inputFiles;
    int fileNum = avopts._concatFiles.size();
    if (0 == fileNum) {
        av_log(NULL, AV_LOG_ERROR, "Failed with NULL inputFiles,%s:%d\n",
               __FILE__, __LINE__);
        return -1;
    }
    for (int i = 0; i < fileNum; i++) {
        inputFiles.push_back(new FileInfoStruct);
        inputFiles[i]->path = avopts._concatFiles[i].c_str();
    }

    FileInfoStruct *output = new FileInfoStruct;
    output->path = avopts._outputFile.c_str();
    output->isInput = false;

    //如果有图片水印，先将图片进行处理一下
    // if (avopts._waterInfo._isImage) {
    //     AvOpts imageopts;
    //     imageopts._inputFile = avopts._waterInfo._content;
    //     imageopts._outputFile =
    //         TMP + substrFileName(imageopts._inputFile) + ".jpg";
    //     imageopts._videoCodec = "JPG";
    //     imageopts._width = avopts._waterInfo._width;
    //     imageopts._height = avopts._waterInfo._height;
    //     imageopts._id = "imagetranscode";
    //     FileInfoStruct *input = new FileInfoStruct;
    //     input->path = imageopts._inputFile.c_str();

    //     FileInfoStruct *output = new FileInfoStruct();
    //     output->path = imageopts._outputFile.c_str();
    //     // cout << output->path << endl;
    //     output->isInput = false;
    //     ret = transcode(input, output, imageopts);

    //     delete input;
    //     input = NULL;
    //     delete output;
    //     output = NULL;
    //     if (0 > ret) {
    //         remove(imageopts._outputFile.c_str());
    //         av_log(NULL, AV_LOG_ERROR, "Failed while transcode Image,%s:%d\n",
    //                __FILE__, __LINE__);
    //         return ret;
    //     } else {
    //         avopts._waterInfo._content =
    //             imageopts
    //                 ._outputFile;  //使用完之后，临时文件应该删除？？？？？？
    //     }
    // }

    double frameRate = 0;
    int sampleSperSec = 0;
    //打开输入文件
    for (int i = 0; i < fileNum; i++) {
        ret = openInputFile(inputFiles[i]);
        if (0 > ret) return ret;
        //单路流的处理
        if (NULL != inputFiles[i]->videoCodecCtx) {
            if (av_q2d(inputFiles[i]->videoCodecCtx->framerate) > frameRate) {
                frameRate = av_q2d(inputFiles[i]->videoCodecCtx->framerate);
                avopts._frameRateDen =
                    inputFiles[i]->videoCodecCtx->framerate.num;
                avopts._frameRateNum =
                    inputFiles[i]->videoCodecCtx->framerate.den;
            }
            // avopts._width,avopt._height是为了单路音频和视频合并时，第一个文件为音频
            // 无法获取视频的编码信息，所在自己手动的设置
            if (0 == avopts._width || 0 == avopts._height) {
                avopts._width = inputFiles[i]->videoCodecCtx->width;
                avopts._height = inputFiles[i]->videoCodecCtx->height;
            }
        }
        if (NULL != inputFiles[i]->audioCodecCtx) {
            if (inputFiles[i]->audioCodecCtx->sample_rate > sampleSperSec) {
                sampleSperSec = inputFiles[i]->audioCodecCtx->sample_rate;
                avopts._samplespersec = sampleSperSec;
            }
        }
    }
    if (0 == frameRate) {
        //说明合并的文件均是单路流的音频
        avopts._frameRateDen = 25;
        avopts._width = 640;
        avopts._height = 480;
    }
    if (0 == sampleSperSec) {
        //说明合并的文件均是单路流的视频
        avopts._samplespersec = 44100;
    }
    //视频需要创建两路流，为了应对单路视频和单路音频的存在
    avopts._isMergeVideo = true;
    //打开输出文件
    ret = openOutputFile(output, inputFiles[0], avopts);
    if (0 > ret) return ret;

    int finished;
    for (int i = 0; i < fileNum; i++) {
        ret = transcodeFile(inputFiles[i], output, avopts, &finished);
        if (0 > ret) goto _fail;
        if ((fileNum - 1) != i) finished = 0;

        //		cout << videoPTS << endl;
        //		cout << audioPTS << endl;
        //		if (0 == videoPTS){
        //			//说明上一个文件是单路音频流,则我们手动计算视频的pts
        //			cout << inputFiles[i] -> duration << endl;
        //			int pts = inputFiles[i] -> duration /
        //(av_q2d(output
        //-> videoStream -> time_base) * 1000);
        //			//时间戳的转换
        //			cout << pts << endl;
        //			pts = av_rescale_q_rnd(pts, output ->
        // videoStream
        //->
        // time_base, output -> videoCodecCtx -> time_base,
        //					(AVRounding)(AV_ROUND_NEAR_INF |
        // AV_ROUND_PASS_MINMAX) );
        //			cout << pts << endl;
        //			videoPTS = pts;
        //
        //		}
        //		getchar();
    }
    if (1 == finished) {
        ret = flushEncoder(output);
        if (0 > ret) goto _fail;
    }

    ret = writeOutputFileTrailer(output->formatContext);

_fail:
    //删除临时文件
    remove(avopts._waterInfo._content.c_str());
    for (int i = 0; i < fileNum; i++) {
        delete inputFiles[i];
        inputFiles[i] = NULL;
    }
    delete output;
    output = NULL;
    if (0 > ret) return -1;

    _pts2ms = _outDuration;
    printProgress();
    if (!_ip.empty()) printProgress(_ip, _port);
    sendProgress();

    return 0;
}

int MediaOperate::imageToVideo(AvOpts &avopts) {

    int ret;

    _needAudioTranscoding = 0;
    _needVideoTranscoding = 1;

    //将图片文件全部放在不同的FileInfoStruct中
    vector<FileInfoStruct *> inputFiles;
    int fileNum = avopts._concatFiles.size();
    if (0 == fileNum) {
        av_log(NULL, AV_LOG_ERROR, "Failed with NUll inputFiles, %s:%d\n",
               __FILE__, __LINE__);
        return -1;
    }
    for (int i = 0; i < fileNum; i++) {
        inputFiles.push_back(new FileInfoStruct);
        inputFiles[i]->path = avopts._concatFiles[i].c_str();
    }

    //输出文件
    FileInfoStruct *output = new FileInfoStruct;
    output->path = avopts._outputFile.c_str();
    output->isInput = false;
    _outDuration = fileNum * (100 / avopts._frameRateDen) * 1000;

    //打开输入文件
    for (int i = 0; i < fileNum; i++) {
        ret = openInputFile(inputFiles[i]);
        if (0 > ret) return ret;
        //打开输出文件
        ret = openOutputFile(output, inputFiles[0], avopts);
        if (0 > ret) return ret;

        //初始化水印，或者像素转换
        if (0 == avopts._waterInfos.size()) {
            //不需要文字水印,像素转换
            //初始化视频像素转换
            if (NULL != inputFiles[i]->videoStream) {
                ret = initRepixel(inputFiles[i]->videoCodecCtx,
                                  output->videoCodecCtx,
                                  &inputFiles[i]->videoScalerCtx);
                if (0 > ret) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Failed while call initRepixel, %s:%d\n", __FILE__,
                           __LINE__);
                    return ret;
                }
            }

        } else {
            if (i < avopts._waterInfos.size()) {
                if (!avopts._waterInfos[i]._content.empty()) {
                    //初始化过滤器
                    if (NULL != inputFiles[i]->videoStream) {
                        char filterSpec[1024];
                        if (!avopts._waterInfos[i]._isImage) {
                            //文字水印
                            snprintf(filterSpec, sizeof(filterSpec),
                                     "scale=%d:%d,drawtext=fontfile=%s:"
                                     "fontsize=%d:text=%s:x=%d:y=%d:fontcolor=%"
                                     "s@%f",
                                     output->videoCodecCtx->width,
                                     output->videoCodecCtx->height,
                                     avopts._waterInfos[i]._fontFile.c_str(),
                                     avopts._waterInfos[i]._fontSize,
                                     avopts._waterInfos[i]._content.c_str(),
                                     avopts._waterInfos[i]._x,
                                     avopts._waterInfos[i]._y,
                                     avopts._waterInfos[i]._fontColor.c_str(),
                                     avopts._waterInfos[i]._alph);
                        }
                        ret = initFilter(inputFiles[i]->fctx,
                                         inputFiles[i]->videoCodecCtx,
                                         output->videoCodecCtx, filterSpec);
                        if (0 > ret) {
                            av_log(NULL, AV_LOG_ERROR,
                                   "Failed while call initFilter\n");
                            return ret;
                        }
                    }

                } else {
                    //初始化视频像素转换
                    if (NULL != inputFiles[i]->videoStream) {
                        ret = initRepixel(inputFiles[i]->videoCodecCtx,
                                          output->videoCodecCtx,
                                          &inputFiles[i]->videoScalerCtx);
                        if (0 > ret) {
                            av_log(NULL, AV_LOG_ERROR,
                                   "Failed while call initRepixel, %s:%d\n",
                                   __FILE__, __LINE__);
                            return ret;
                        }
                    }
                }
            } else {
                //初始化视频像素转换
                if (NULL != inputFiles[i]->videoStream) {
                    ret = initRepixel(inputFiles[i]->videoCodecCtx,
                                      output->videoCodecCtx,
                                      &inputFiles[i]->videoScalerCtx);
                    if (0 > ret) {
                        av_log(NULL, AV_LOG_ERROR,
                               "Failed while call initRepixel, %s:%d\n",
                               __FILE__, __LINE__);
                        return ret;
                    }
                }
            }
        }  // else
    }      // for

    //开始转码
    int finished = 0;
    int dataPresent = 0;
    for (int i = 0; i < fileNum; i++) {
        while (true) {
            //读取packet数据
            AVPacket packet;
            ret = av_read_frame(inputFiles[i]->formatContext, &packet);
            if (0 > ret) {
                if (AVERROR_EOF == ret) {
                    finished = 1;
                    av_log(NULL, AV_LOG_ERROR, "read frame AVERROR_EOF\n");
                    break;
                } else {
                    av_log(NULL, AV_LOG_ERROR, "Cannot to read frame\n");
                    return ret;
                }
            }
            if (packet.stream_index == inputFiles[i]->videoIndex) {
                //解码图片
                AVFrame *inputFrame = NULL;
                if (initInputFrame(&inputFrame) < 0) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Failed while call initInputFrame, %s:%d\n",
                           __FILE__, __LINE__);
                    return -1;
                }
                ret = decodeMediaFrame(&packet, inputFrame,
                                       inputFiles[i]->videoCodecCtx,
                                       &dataPresent, &finished);
                if (1 == ret) {
                    av_frame_free(&inputFrame);
                    continue;
                } else if (0 > ret) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Failed while call decodeMediaFrame, %s:%d\n",
                           __FILE__, __LINE__);
                    return -1;
                }
                if (1 == dataPresent) {
                    //解码成功，先进行像素转换，再开始编码
                    AVFrame *outFrame;
                    if (0 >
                        initOutputFrame(&outFrame, output->videoCodecCtx, 0)) {
                        av_log(NULL, AV_LOG_ERROR,
                               "Failed while initOutputFrame video,%s:%d\n",
                               __FILE__, __LINE__);
                        return -1;
                    }
                    if (0 == avopts._waterInfos.size()) {
                        int len = sws_scale(
                            inputFiles[i]->videoScalerCtx, inputFrame->data,
                            inputFrame->linesize, 0, inputFiles[i]->height,
                            outFrame->data, outFrame->linesize);
                        if (0 >= len) {
                            av_log(NULL, AV_LOG_ERROR,
                                   "Failed when video repixel, %s:%d\n",
                                   __FILE__, __LINE__);
                            av_frame_free(&inputFrame);
                            av_frame_free(&outFrame);
                            return len;
                        }
                    } else {
                        if (i < avopts._waterInfos.size()) {
                            if (!avopts._waterInfos[i]._content.empty()) {
                                ret = readDecodeFromFilter(
                                    inputFiles[i]->fctx, inputFrame, outFrame);
                            } else {
                                int len = sws_scale(
                                    inputFiles[i]->videoScalerCtx,
                                    inputFrame->data, inputFrame->linesize, 0,
                                    inputFiles[i]->height, outFrame->data,
                                    outFrame->linesize);
                                if (0 >= len) {
                                    av_log(NULL, AV_LOG_ERROR,
                                           "Failed when video repixel, %s:%d\n",
                                           __FILE__, __LINE__);
                                    av_frame_free(&inputFrame);
                                    av_frame_free(&outFrame);
                                    return len;
                                }
                            }

                        } else {
                            int len = sws_scale(
                                inputFiles[i]->videoScalerCtx, inputFrame->data,
                                inputFrame->linesize, 0, inputFiles[i]->height,
                                outFrame->data, outFrame->linesize);
                            if (0 >= len) {
                                av_log(NULL, AV_LOG_ERROR,
                                       "Failed when video repixel, %s:%d\n",
                                       __FILE__, __LINE__);
                                av_frame_free(&inputFrame);
                                av_frame_free(&outFrame);
                                return len;
                            }
                        }
                    }

                    //进行编码
                    av_frame_free(&inputFrame);
                    for (int m = 0; m < 100; m++) {
                        int dataWritten = 0;
                        // while的目的是，因为图片的数据太少，对于编码来说，avcodec_receive_packet()返回AVERROR（EAGAIN）意味
                        //着需要新的数据才能返回新的输出，在编码开始时，编解码器可能会接收多个输入帧而不返回帧，直到其内部
                        //缓存区被填充为止
                        while (true) {
                            ret = encodeMediaFrame(
                                outFrame, output->formatContext,
                                output->videoCodecCtx, &dataWritten,
                                output->videoStream->index);
                            if (0 > ret) {
                                av_log(NULL, AV_LOG_ERROR,
                                       "Failed occur while encodeMediaFrame "
                                       "video, %s:%d\n",
                                       __FILE__, __LINE__);
                                av_frame_free(&outFrame);
                                return ret;
                            }
                            if (1 == dataWritten) {
                                //编码成功，写入成功
                                break;
                            }
                        }
                    }
                    av_frame_free(&outFrame);
                }
            }
        }
    }

    flushEncoder(output);
    ret = writeOutputFileTrailer(output->formatContext);

    return 0;
}
int MediaOperate::getAudioCoverPicture(AvOpts &avopts) {

    int ret;
    FileInfoStruct *input = new FileInfoStruct;
    input->path = avopts._inputFile.c_str();

    //打開輸入文件
    ret = openInputFile(input);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR, "Failed occur while openInputFile ,%s:%d\n",
               __FILE__, __LINE__);
        return ret;
    }
    //读取文件头信息
    ret = input->formatContext->iformat->read_header(input->formatContext);
    if (0 > ret) {
        av_log(NULL, AV_LOG_ERROR, "The audio don't have header info,%s:%d\n",
               __FILE__, __LINE__);
    }

    //读取音频封面信息
    if (input->videoIndex != -1 &&
        (input->formatContext->streams[input->videoIndex]->disposition &
         AV_DISPOSITION_ATTACHED_PIC)) {
        AVPacket pkt =
            input->formatContext->streams[input->videoIndex]->attached_pic;
        FILE *album = fopen(avopts._outputFile.c_str(), "wb");
        ret = fwrite(pkt.data, pkt.size, 1, album);
        fclose(album);
        av_packet_unref(&pkt);
    } else {
        av_log(NULL, AV_LOG_ERROR, "The audio don't have picture");
        return -1;
    }

    // TODO释放资源有 问题
    // delete input;
    // input = NULL;
    if (0 > ret) return -1;
    return 0;
}

int MediaOperate::interFrameDifferenceMethod(AvOpts &avopts) {
    int ret;
    cv::VideoCapture capture(avopts._inputFile);
    if (!capture.isOpened()) {
        av_log(NULL, AV_LOG_ERROR, "The video can not open,%s:%d", __FILE__,
               __LINE__);
        return -1;
    }

    cv::Mat previousImage, resultImage;
    for (;;) {
        cv::Mat currentImage;
        if (!capture.read(currentImage)) break;
        if (previousImage.empty()) {
            previousImage = currentImage;
            continue;
        }
        //计算当前帧与前帧不同
        cv::Mat previousImageGray, currentImageGray;
        cv::cvtColor(previousImage, previousImageGray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(currentImage, currentImageGray, cv::COLOR_BGR2GRAY);
        //帧差法，相减
        cv::absdiff(currentImageGray, previousImageGray, resultImage);
        //二值化，像素
        cv::threshold(resultImage, resultImage, 20, 255.0, cv::THRESH_BINARY);
        //去除图像噪声，先膨胀再腐蚀
        cv::dilate(resultImage, resultImage, cv::Mat());
        cv::erode(resultImage, resultImage, cv::Mat());
        //统计两帧相减后的图像数
        float counter = 0;
        float num = 0;
        for (int y = 0; y < resultImage.rows; y++) {
            uchar *data = resultImage.ptr<uchar>(y);
            for (int x = 0; x < resultImage.cols; x++) {
                num = num + 1;
                if (data[x] == 255) counter++;
            }
        }
        float p = counter / num;
        if (p >
            avopts._threshold)  //达到阈值的像素数目达到一定的数量则保存改图像
        {
            //获取当前帧的时间
            double frameTime = capture.get(cv::CAP_PROP_POS_MSEC) / 1000;
            //判断输出目录是否存在，如无则创建
            checkDir(avopts._outputFile);
            string imgName = avopts._outputFile + "/" +
                             substrFileName(avopts._inputFile) + "_" +
                             to_string((int)frameTime) + ".jpg";
            av_log(NULL, AV_LOG_ERROR, imgName.c_str());
            av_log(NULL, AV_LOG_ERROR, "\n");
            imwrite(imgName, currentImage);
        }
        previousImage = currentImage;
    }
    capture.release();
    return 0;
}

string MediaOperate::printProgress() {
    //将进度写入文件
    ofstream myfile;
    myfile.open(TMP + _progressId, ios::ate);
    double progress = (double)_pts2ms / _outDuration;
    progress *= 100;  //乘以100转换为百分数
    string percent = to_string(progress).substr(0, 5) + "%";
    myfile << percent << endl;
    //将其写入一个文件中
    return percent;
}

void MediaOperate::sendProgress() {
    //计算进度
    double progress = (double)_pts2ms / _outDuration;
    progress *= 100;  //乘以100转换为百分数
    int progressInt = progress;
    if (progressInt % 5 == 0) {
        //查找
        vector<int>::iterator ret;
        ret = std::find(_progressArg.begin(), _progressArg.end(), progressInt);
        if (ret == _progressArg.end()) {
            // not found,
            _progressArg.push_back(progressInt);
            string percent = to_string(progress).substr(0, 5) + "%";
            LOG(INFO) << "progress: " << percent << std::endl;
            char str[512];
            snprintf(str, sizeof(str), "{\"taskId\":\"%s\",\"process\":\"%s\"}",
                     _progressId.c_str(), percent.c_str());
            string aa = str;
            //发送进度
            try {
                rabbitmq::RabbitmqClient client;
                int ret =
                    client.connect(gaddress._ip, gaddress._port, gaddress._user,
                                   gaddress._password, gaddress._vhost);
                // if (ret < 0) return ret;
                client.publish(aa, queues._taskProcess, "");
                client.disConnect();
            }
            catch (...) {
                LOG(ERROR)
                    << "send progress failed: the rabbitmq connnect failed"
                    << std::endl;
            }
        }
    }
}
void MediaOperate::printProgress(const string &ip, const string &port) {
    //计算进度
    double progress = (double)_pts2ms / _outDuration;
    progress *= 100;  //乘以100转换为百分数
    string percent = to_string(progress).substr(0, 5) + "%";
    //将进度发送到进度服务器上
    string host = ip + ":" + port;
    // MediaClient client(
    //    grpc::CreateChannel(host, grpc::InsecureChannelCredentials()));
    // client.setVideoProgress(_progressId, percent);
}
