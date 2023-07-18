#ifndef __MEDIAOPERATE_H
#define __MEDIAOPERATE_H
#include "./mediainfo.h"

//#define USE_AV_FILTER
namespace Media{

using namespace std;

class MediaOperate{
public:
	MediaOperate();
	MediaOperate(string inputFile):_inputFile(inputFile){};
    MediaOperate(MemUpload& upload):_upload(upload){};
	~MediaOperate();

private:
	//打开输入的文件信息，配置解码的参数
	int openInputFile(FileInfoStruct *input);

	//打开解码的信息
	int openInputDecoder(FileInfoStruct *input, enum AVMediaType type);

	//打开输出的文件信息，配置编码的参数
	int	openOutputFile(FileInfoStruct *outputStruct,FileInfoStruct *input,AvOpts &avopts);

	//打开输出的编码信息
	int openOutputEncoder(FileInfoStruct *output, FileInfoStruct *input,AvOpts &avopts, enum AVMediaType type);

    //添加输出文件的头信息    
	int writeOutputFileHeader(AVFormatContext *ofmtCtx, const char* filename, AvOpts &avopts);

    //添加输出文件的尾部信息    
	int writeOutputFileTrailer(AVFormatContext *ofmtCtx);

    //初始化Packet
	void initPacket(AVPacket *packet);	
	
    //初始化输入Frame
	int initInputFrame(AVFrame **frame);
	
    //初始化输出Frame
	int initOutputFrame(AVFrame **frame ,AVCodecContext *encCtx, int frameSize);
	
    //hw 初始化hwFrame
    int initOutputHWFrame(AVFrame **frame ,AVCodecContext *encCtx, bool isCPUFrame = true);

	//初始化视频像素转换
	int initRepixel(AVCodecContext *decCtx, AVCodecContext *encCtx, SwsContext **repixelCtx);

    //hw 初始化视频像素转换
	int initHWRepixel(AVCodecContext *decCtx, AVCodecContext *encCtx, SwsContext **repixelCtx);
    
    //进行像素的转换
    int videoSwsScale(SwsContext *repixelCtx, AVFrame *inFrame, AVFrame *swsFrame);
	
    //初始化过滤器
	int initFilter(FilteringContext *fctx, AVCodecContext *decCtx, AVCodecContext *encCtx, const char *filterSpec);
	
	//初始化音频重采样
	int initResampler(AVCodecContext *decCtx, AVCodecContext *encCtx, SwrContext **resamplerContext);

	//audio
	int initConvertedSamples(uint8_t ***convertedInputSamples, AVCodecContext *encCtx, int frameSize);

	//audio
	int initFifo(AVAudioFifo **fifo, AVCodecContext *encCtx);

	//audio
	int convertSamples(SwrContext *resamplerContext, uint8_t **convertedData,const int outframeSize,
						const uint8_t **inputData,  const int inframeSize,int *cachenum);
	
	//audio
	int addSamplesToFifo(AVAudioFifo *fifo, uint8_t **convertedInputSamples, const int frameSize);

	int decodeMediaFrame(AVPacket *inputPacket, AVFrame *frame, AVCodecContext *decCtx, int *dataPresent, int *finished);

	//video
	int readDecodeFromFilter(FilteringContext *fctx, AVFrame *input, AVFrame *filtFrame);

	//audio
	int readDecodeConvertAndStore(AVFrame *inputFrame, AVAudioFifo *fifo, AVCodecContext *decCtx,
									AVCodecContext *encCtx, SwrContext *resamplerContext);

	int encodeMediaFrame(AVFrame *frame, AVFormatContext *ofmtCtx, AVCodecContext *encCtx, int *dataPresent, int streamIndex);

    /*
     *hw 采用硬件的编码方式，比encodeMediaFrame这个函数，多个从CPU复制frame到GPU的过程，
     *然后在调用encodeMediaFrame这个函数进行编码
     * */
    int encodeMediaHWFrame(AVFrame *frame, AVFormatContext *ofmtCtx, AVCodecContext *encCtx, int *dataPresent, int streamIndex);
	//audio
	int loadEncodeAndWrite(AVAudioFifo *fifo, AVFormatContext *outputFormatContext, 
							AVCodecContext *encCtx, int streamIndex);

	int transcodeFile(FileInfoStruct *input, FileInfoStruct *output, AvOpts &avopts, int *finished);

    /*对于视频，当我们需要增大视频的帧率如25帧/s变成30帧/s，则我们需要复制帧，每1s复制5帧
    *目前的策略是对于后面的几帧进行复制，所以我们需要知道，视频中每帧的序号
    *如视频帧率是25帧/s
    *则视频每帧序列0-24是第一秒（复制20,21,22,23,24则帧率变成30帧/s），25-49是第二秒
    *我们根据计算，判读哪个序号的帧需要copy
    * param:frameCodecId ,视频帧的序号
    * param:srcFrameRate ,源视频的帧率
    * param:dstFrameRate,目标视频的帧率
    * return:（1 or 2） 返回当前帧编码的次数，正常是编码1次，需要copy的frame则编码2次
    */
    int getFrameCopyNum(int frameCodecId, double srcFrameRate, double dstFrameRate);    
	//flush encoder
	int flushEncoder(FileInfoStruct *output);	

    //hw 硬件设置输出视频的AVHWFramesContext
    int setHwFrameCtx(AVCodecContext *ctx, AVBufferRef *hwDeviceCtx);

    //hw 判读NVIDIA是否支持输入视频的解码
    bool supportHWDecoder(enum AVCodecID id);
    //hw 判读NVIDIA是否支持输入视频的采样方式
    bool supportHWYUV(enum AVPixelFormat format);

    //判读是否需要进行像素转换
    bool needScale(FileInfoStruct *input, FileInfoStruct *output);    
	
	int getMediaInfo(FileInfoStruct *input,GeneralInfo &generalinfo, VideoInfo &videoinfos, AudioInfo &audioinfos);
	

	int transcode(FileInfoStruct *input, FileInfoStruct *output, AvOpts &avopts);

    //剪切视频，为了精确时间，采用转码的方式进行剪切，输入视频和输出视频编码一致    
	int cutVideo(FileInfoStruct *input, FileInfoStruct *output, AvOpts &avopts);
   
	int getKeyFrame(FileInfoStruct *input, AvOpts &avopts);

	//计算水印位置
	void getPosition(const string &position, int swidth, int sheight,
                        int fwidth, int fheight, int &x, int &y, int marginx, int marginy);
	
//下面是对外的接口
public:
	//获取音视频的信息
	int getMediaInfo(GeneralInfo &generalinfo, VideoInfo &videoinfos, AudioInfo &audioinfos);
	//转码
	int transcode(AvOpts &avopts);//用户控制GPU
	//剪切视频
	int cutVideo(AvOpts &avopts);//用户控制GPU
	//截取关键帧
	int  getKeyFrame(AvOpts &avopts);//用户控制GPU
	//拼接视频
	int concatVideoWithOutEncode(AvOpts &avopts);

	//视频合并
	int mergeVideo(AvOpts &avopts);//不支持GPU

	//图片合成视频
	int imageToVideo(AvOpts &avopts);

    //Get Audio cover picture
    int getAudioCoverPicture(AvOpts &avopts);    

    int interFrameDifferenceMethod(AvOpts &avopts);
	//将视频转码的进度存储在本地文件
	string printProgress();
    //将视频的进度存入到redis中，在创建任务之前，需要检查当前id是否存在，存在则这个id不能使用
    //这个由checkIdByRedis来实现
	void printProgress(const string& ip, const string& port);
    void sendProgress();

private:
	string _inputFile;
    MemUpload _upload;
	int64_t _outDuration;
	int64_t _pts2ms;
	int64_t _audioPTS;
	int64_t _videoPTS;

	//0:表示不转码，1:表示转码
	int _needAudioTranscoding;
	int _needVideoTranscoding;

	//创建进度文件的名称
	string _progressId;

	//进度服务器的ip，port
	string _ip;
	string _port;
    vector<int> _progressArg;

};

}
#endif // __MEDIAOPERATE_H
