#ifndef __MEDIA_AV_INFO_H__
#define __MEDIA_AV_INFO_H__
#include "./ffheader.h"
#include "x2struct.hpp"
using std::string;
using std::map;
using std::vector;
class GeneralInfo {
   public:
    string _name;
    string _format;
    string _describe;
    float _duration;
    int64_t _size;
    int _nb_streams;
    int _bitrate;
    map<string, string> _extinfo;
    XTOSTRUCT(O(_name, _format, _describe, _size, _nb_streams, _bitrate,
                _extinfo));
};

class AudioInfo {
   public:
    int _index;
    string _codecname;
    string _describe;
    string _profile;
    string _type;
    string _codecid;
    string _samplefmt;
    int _samplerate;
    int _channels;
    int _bitspersample;
    int64_t _duration;
    int _bitrate;
    map<string, string> _extinfo;
    XTOSTRUCT(O(_index, _codecname, _describe, _profile, _type, _codecid,
                _samplefmt, _samplerate, _channels, _bitspersample, _duration,
                _bitrate, _extinfo));
};

class VideoInfo {
   public:
    int _index;
    string _codecname;
    string _describe;
    string _profile;
    string _type;
    string _codecid;
    string _pixfmt;
    string _aspectradio;
    int _width;
    int _height;
    int _level;
    int64_t _duration;
    int _bitrate;
    int _gopsize;
    float _framerate;
    map<string, string> _extinfo;
    XTOSTRUCT(O(_index, _codecname, _describe, _profile, _type, _codecid,
                _pixfmt, _aspectradio, _width, _height, _level, _duration,
                _bitrate, _gopsize, _framerate, _extinfo));
};
class Info {
   public:
    GeneralInfo general;
    VideoInfo video;
    AudioInfo audio;
    XTOSTRUCT(O(general, video, audio));
};

typedef struct FilteringContext {
    FilteringContext()
        : buffersrcCtx(NULL), buffersinkCtx(NULL), filterGraph(NULL) {};
    ~FilteringContext() {
        if (NULL != filterGraph) {
            avfilter_graph_free(&filterGraph);
            filterGraph = NULL;
        }
    }
    AVFilterContext *buffersrcCtx;
    AVFilterContext *buffersinkCtx;
    AVFilterGraph *filterGraph;
} FilteringContext;

class MemUpload {
   public:
    MemUpload()
        : _hosts(""),
          _ak(""),
          _sk(""),
          _bucket(""),
          _key(""),
          _uploadId(""),
          _seq(0) {}
    ~MemUpload() { _etags.clear(); }
    //分块上传初始化
    bool multiInit() {
        S3::Client client(_hosts, _ak, _sk);
        try {
            _uploadId = client.MultipartInit(_bucket, _key);
        }
        catch (S3::S3Exception &e) {
            std::cerr << e.str() << std::endl;
            return false;
        }
        return true;
    }
    //分块上传某块内容
    bool multiUpload(int seq, int partLength, char *data) {
        S3::Client client(_hosts, _ak, _sk);
        std::string etag = "";
        try {
            etag = client.MultipartUpload(_bucket, _key, _uploadId, seq,
                                          partLength, data);
        }
        catch (S3::S3Exception &e) {
            std::cerr << e.str() << std::endl;
            return false;
        }
        _etags.push_back(etag);
        _seq = seq;
        return true;
    }
    //分块上传合并
    bool multiComplete() {
        S3::Client client(_hosts, _ak, _sk);
        try {
            client.MultipartComplete(_bucket, _key, _uploadId, _etags);
        }
        catch (S3::S3Exception &e) {
            std::cerr << e.str() << std::endl;
            return false;
        }
        return true;
    }
    //读取流数据
    uint64_t getObject(uint64_t offset, int bufSize, char *buf) {
        S3::Client client(_hosts, _ak, _sk);
        uint64_t num = 0;
        try {
            num = client.GetObject(_bucket, _key, offset, bufSize, buf);
            return num;
        }
        catch (S3::S3Exception &e) {
            std::cerr << e.str() << std::endl;
            return 0;
        }
    }
    //获取对象大小
    uint64_t objectLength(){
        S3::Client client(_hosts, _ak, _sk);
        try{
            S3::Client::Properties info = client.ObjectInfo(_bucket, _key);
            return  info.contentLength;
        }catch(S3::S3Exception &e){
            std::cerr << e.str() << std::endl;
            return 0;
        }
    }

    string _hosts;
    string _ak;
    string _sk;
    string _bucket;
    string _key;
    int _seq;  //记录上传了几个分块
    string _uploadId;
    vector<string> _etags;
};
typedef struct MemChunk {
    MemChunk() : buf(NULL), buf_size(0), over_size(0),offset(0), upload(NULL) {};
    ~MemChunk() {
        if (buf != NULL) {
            free(buf);
            buf = NULL;
        }
        if (upload != NULL) {
            delete upload;
            upload = NULL;
        }
        buf_size = 0;
        over_size = 0;
        offset = 0;
    }
    uint8_t *buf;  //内存数据
    int buf_size;  // 当前内存块大小
    int over_size;  //如果当前内存超过最大值，则超过部分的大小；
    int64_t offset; //记录已经读取文件时的偏移量
    MemUpload *upload;  //上传
} MemChunk;

typedef struct FileInfoStruct {
    FileInfoStruct()
        : isInput(true),
          isImage(false),
          path(NULL),
          width(0),
          height(0),
          duration(0),
          formatContext(NULL),
          videoIndex(-1),
          audioIndex(-1),
          videoStream(NULL),
          audioStream(NULL),
          videoCodecCtx(NULL),
          audioCodecCtx(NULL),
          fctx(new FilteringContext),
          videoScalerCtx(NULL),
          audioPTS(0),
          videoPTS(0),
          audioFifo(NULL),
          audioResampleCtx(NULL),
          hwDeviceCtx(NULL),
          threadCount(0),
          useGPU(false),
          chunk(NULL) {
#define BUFSIZE 1024 * 1024 * 5  // 5M
        chunk = new MemChunk();
        chunk->buf = (uint8_t *)calloc(BUFSIZE, sizeof(uint8_t));
        chunk->upload = new MemUpload();
    };
    //释放资源
    ~FileInfoStruct() {
        if (NULL != audioFifo) av_audio_fifo_free(audioFifo);
        if (NULL != audioResampleCtx) swr_free(&audioResampleCtx);
        if (NULL != fctx) {
            delete fctx;
            fctx = NULL;
        }
        if (NULL != videoScalerCtx) {
            sws_freeContext(videoScalerCtx);
        }
        if (NULL != audioCodecCtx) avcodec_free_context(&audioCodecCtx);
        if (NULL != videoCodecCtx) {
            avcodec_free_context(&videoCodecCtx);
        }
        if (NULL != hwDeviceCtx) {
            av_buffer_unref(&hwDeviceCtx);
        }
        // std::cout << "isInput: " << isInput << std::endl;
        if (isInput) {
            avformat_close_input(&formatContext);
        } else {
            if (formatContext->flags != AVFMT_FLAG_CUSTOM_IO) {
                if ((NULL != formatContext) &&
                    !(formatContext->oformat->flags & AVFMT_NOFILE)) {
                    avio_closep(&formatContext->pb);
                }
            }
            if (isImage) {
                //当输出时图片的时候，需要使用这个进行释放，不然会句柄泄漏
                avio_closep(&formatContext->pb);
            }
            if (NULL != formatContext) avformat_free_context(formatContext);
        }
        if (chunk != NULL) {
            delete chunk;
            chunk = NULL;
        }
    };
    bool isInput;
    bool isImage;
    const char *path;
    int width;
    int height;
    int64_t duration;
    AVFormatContext *formatContext;
    int videoIndex;
    int audioIndex;
    AVStream *videoStream;
    AVStream *audioStream;
    AVCodecContext *videoCodecCtx;
    AVCodecContext *audioCodecCtx;
    FilteringContext *fctx;
    SwsContext *videoScalerCtx;
    int64_t audioPTS;
    int64_t videoPTS;
    AVAudioFifo *audioFifo;
    SwrContext *audioResampleCtx;
    //硬件上下文
    AVBufferRef *hwDeviceCtx;
    //是否使用GPU，默认不使用,true 使用，false不使用
    bool useGPU;
    int threadCount;  //开启多线程转码的线程数，默认是0，不开启多线程转码
    MemChunk *chunk;

} FileInfoStruct;

class WaterInfo {
   public:
    WaterInfo()
        : _fontColor("black"),
          _fontSize(18),
          _x(0),
          _y(0),
          _alph(1),
          _width(0),
          _height(0),
          _isImage(false) {};

    string _content;   //文字水印的内容,或者图片水印的路径
    string _fontFile;  //文字水印对应的字体
    string _fontColor;  //文字水印的颜色,英文单词如red,或16进制数据如0xA52A2A
    string _position;
    int _marginX;
    int _marginY;    
    int _fontSize;  //文字水印的大小
    int _x;         //水印在视频中的横坐标
    int _y;         //水印在视频中的纵坐标
    double _alph;   //水印的透明度
    int _width;     //图片水印的宽度
    int _height;    //图片水印的高度
    bool _isImage;  //判断是否是图片水印
    void clear() {
        _content.clear();
        _fontFile.clear();
        _fontColor.clear();
        _fontSize = 18;
        _x = 0;
        _y = 0;
        _alph = 1;
        _width = 0;
        _height = 0;
        _isImage = false;
    }
};

//参数0表示以视频原有的参数进行转码
class AvOpts {
   public:
    AvOpts()
        : _frameSize(0),
          _videoBit(0),
          _width(0),
          _height(0),
          _samplespersec(0),
          _audioBit(0),
          _frameRateDen(0),
          _frameRateNum(1),
          _startTime(-1),
          _endTime(-1),
          _length(0),
          _isMergeVideo(false),
          _isMoovHeader(false),
          _isFragment(false),
          _threshold(0.65),
          _threadCount(0),
          _isFile(false),
          _useGPU(false) {};

   public:
    int jsonStringToAvOpts(const string &jsonString,
                           CmdApplication::IOParam &input,
                           CmdApplication::IOParam &output);
    void initAvOpts(FileInfoStruct *input);

    AVCodecID encoderCodecId(string encodeCodec);

    string encoderCodecName(AVCodecID id, bool isVideo);

   public:
    string _inputFile;            //输入文件
    vector<string> _concatFiles;  //拼接视频的路径
    string _outputFile;           //输出文件

    int _videoBit;  //视频的码率，单位b/s
    //用于判读设置的视频码率如果超过了原始码率应该怎么处理
    // usesourcebitrate   使用原视频的码率
    // usesetbitrate      使用用户设置的码率
    // unknown            码率设置错误
    string _overSourceBit;
    int _width;
    int _height;
    int _samplespersec;  //音频采样率
    int _frameSize;  //音频编码的frame_size,不同编码器一帧的大小不一样。
    int _audioBit;
    string _videoCodec;
    string _audioCodec;
    //这两个参数用来处理帧率，考虑到小数的情况
    int _frameRateDen;              //分母
    int _frameRateNum;              //分子
    WaterInfo _waterInfo;           //水印的信息
    vector<WaterInfo> _waterInfos;  //图片合成视频的文字水印

    //剪切视频
    double _startTime;  //开始时间，单位s,如17.5s
    double _endTime;    //结束时间，单位s,如30.5s//
    int _length;        //时间间隔，以秒为单位

    // 合并视频
    // 考虑到合并视频时会出现单路音频和单路视频的合并，所有输出的流必须有两路流
    // 不能根据输入的流进行判断来创建流，特在这里设置参数来进行判断
    // true 表示合并视频，默认为false
    bool _isMergeVideo;

    //考虑查询进度，设置一个id
    string _id;

    //是否将MP4文件头移到文件头
    bool _isMoovHeader;
    //是否对mp4文件分片
    bool _isFragment;

    //是否使用GPU，默认不使用,true 使用，false不使用
    bool _useGPU;

    //帧差分的阈值
    float _threshold;  //默认为0.65
    //设置多线程转码的线程数，逻辑核数*6时效果比较好
    //默认为0，不开启多线程转码
    int _threadCount;
    //进度查询的ip，port，不知道定义在这里好不好
    string _ip;
    string _port;
    //判断是截取帧还是封面,0:是截帧，1：封面
    bool _isFile;
    void clear() {
        _inputFile.clear();
        _concatFiles.clear();
        _outputFile.clear();
        _videoBit = 0;
        _overSourceBit.clear();
        _width = 0;
        _height = 0;
        _samplespersec = 0;
        _frameSize = 0;
        _audioBit = 0;
        _videoCodec.clear();
        _audioCodec.clear();
        _frameRateDen = 0;
        _frameRateNum = 0;
        _waterInfo.clear();
        _waterInfos.clear();
        _startTime = 0;
        _endTime = 0;
        _length = 0;
        _isMergeVideo = false;
        _id.clear();
        _isMoovHeader = false;
        _isFragment = false;
        _useGPU = false;
        _threshold = 0.6;
        _ip.clear();
        _port.clear();
        _isFile = false;
    }
};

#endif  // __MEDIA__AV_INFO_H__
