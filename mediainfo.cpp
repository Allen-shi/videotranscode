#include "./mediainfo.h"
extern bool openGPU;

//解析json字符串，初始化转码参数
//{"taskId":"222",taskDetail:{"srcpath":"wei:video-H263-AC3.avi","srchost":"192.168.50.186:7480","srcaksk":"791f66218f5a090391b38ef501811ffc,18c7cf6f142b9f0e194183dac6d15926","destpath":"wei:interce.avi","desthost":"192.168.50.186:7480","destaksk":"791f66218f5a090391b38ef501811ffc,18c7cf6f142b9f0e194183dac6d15926","start":"30","end":"50"}}
int AvOpts::jsonStringToAvOpts(const string& jsonString,
                               CmdApplication::IOParam& input,
                               CmdApplication::IOParam& output) {
    int ret = 0;
    ;
    string taskDetail("taskDetail");
    Json::Reader reader;
    Json::Value value;
    if (reader.parse(jsonString, value)) {
        if (!value["taskId"].isNull()) {
            _id = value["taskId"].asString();
        }
        if (value[taskDetail].isObject()) {
            //解析输入
            if (!value[taskDetail]["srcpath"].isNull()) {
                input._file = value[taskDetail]["srcpath"].asString();
            }
            //解析s3的输入
            if (!value[taskDetail]["srchost"].isNull()) {
                // s3的输入
                input._type = CmdApplication::IO_S3RGW;
                input._hosts = value[taskDetail]["srchost"].asString();
                if (!value[taskDetail]["srcaksk"].isNull()) {
                    input._aksk = value[taskDetail]["srcaksk"].asString();
                }
            }
            // 解析图片水印参数
            if (!value[taskDetail]["waterpath"].isNull()) {
                _waterInfo._isImage = true;
                input._waterInfo._path =
                    value[taskDetail]["waterpath"].asString();
            }
            if (!value[taskDetail]["waterhost"].isNull()) {
                input._waterInfo._s3rgw._hosts =
                    value[taskDetail]["waterhost"].asString();
                input._waterInfo._type = CmdApplication::IO_S3RGW;
            }
            if (!value[taskDetail]["wateraksk"].isNull()) {
                // ak,sk,解析
                vector<string> aksk;
                vector<string>::iterator iter;
                aksk = DealString::SplitString(
                    ",", value[taskDetail]["wateraksk"].asString());
                for (iter = aksk.begin(); iter != aksk.end(); iter++) {
                    //去除两端的空格
                    DealString::TrimString(*iter);
                }
                if (aksk.size() == 2) {
                    input._waterInfo._s3rgw._accessKey = aksk[0];
                    input._waterInfo._s3rgw._secretKey = aksk[1];
                }
            }
            // end image s3
            // 解析filehub的类型
            if (!value[taskDetail]["filehub"].isNull()) {
                string backed = value[taskDetail]["filehub"].asString();
                Json::Reader reader;
                Json::Value tmp;
                if (reader.parse(backed, tmp)) {
                    input._backendStorage = tmp["backendStorage"].asString();
                    output._backendStorage = tmp["backendStorage"].asString();
                }
            }

            LOG(DEBUG) << "****begin to input initIOParam****";
            ret = input.initIOParam();
            LOG(DEBUG) << "****end to input initIOParam****";
            if (0 > ret) {
                return ret;
                std::cerr << "input initIOParam Failed" << std::endl;
            } else {
                if (1 == input._files.size()) {
                    _inputFile = input._files[0];
                } else {
                    _concatFiles = input._files;
                }
            }
            //解析输出
            if (!value[taskDetail]["destpath"].isNull()) {
                output._file = value[taskDetail]["destpath"].asString();
            }
            //解析s3的输出
            if (!value[taskDetail]["desthost"].isNull()) {
                // s3的输出
                output._isInput = false;
                output._type = CmdApplication::IO_S3RGW;
                output._hosts = value[taskDetail]["desthost"].asString();
                if (!value[taskDetail]["destaksk"].isNull()) {
                    output._aksk = value[taskDetail]["destaksk"].asString();
                }
            }
            // start image s3
            if (CmdApplication::IO_S3RGW == input._waterInfo._type) {
                _waterInfo._content = input._waterInfo._tmpFile;
            } else {
                _waterInfo._content = input._waterInfo._path;
            }

            // end image s3
            LOG(DEBUG) << "****begin to output initIOParam****";
            ret = output.initIOParam();
            LOG(DEBUG) << "****end to output initIOParam****";
            if (0 > ret) {
                return ret;
                std::cerr << "output initIOParam Failed\n";
            } else {
                if (0 < output._file.size()) {
                    _outputFile = output._files[0];
                }
            }
            // end s3
            // 解析transcode的参数
            if (!value[taskDetail]["videobit"].isNull()) {
                _videoBit = std::stoi(value[taskDetail]["videobit"].asString());
            }
            if (!value[taskDetail]["width"].isNull()) {
                _width = std::stoi(value[taskDetail]["width"].asString());
            }
            if (!value[taskDetail]["height"].isNull()) {
                _height = std::stoi(value[taskDetail]["height"].asString());
            }
            if (!value[taskDetail]["samplespersec"].isNull()) {
                _samplespersec =
                    std::stoi(value[taskDetail]["samplespersec"].asString());
            }
            if (!value[taskDetail]["audiobit"].isNull()) {
                _audioBit = std::stoi(value[taskDetail]["audiobit"].asString());
            }
            if (!value[taskDetail]["oversourcebitrate"].isNull()) {
                _overSourceBit =
                    value[taskDetail]["oversourcebitrate"].asString();
            }
            if (!value[taskDetail]["videocodec"].isNull()) {
                _videoCodec = value[taskDetail]["videocodec"].asString();
            }
            if (!value[taskDetail]["audiocodec"].isNull()) {
                _audioCodec = value[taskDetail]["audiocodec"].asString();
            }
            if (!value[taskDetail]["framerateden"].isNull()) {
                _frameRateDen =
                    std::stoi(value[taskDetail]["framerateden"].asString());
            }
            if (!value[taskDetail]["frameratenum"].isNull()) {
                _frameRateNum =
                    std::stoi(value[taskDetail]["frameratenum"].asString());
            }
            if (!value[taskDetail]["optimize"].isNull()) {
                if (value[taskDetail]["optimize"].asString() == "moveheader") {
                    _isMoovHeader = true;
                } else if (value[taskDetail]["optimize"].asString() ==
                           "fragment") {
                    _isFragment = true;
                }
            }
            if (openGPU) _useGPU = true;
            // end transcode

            // start intercept
            if (!value[taskDetail]["start"].isNull()) {
                _startTime = std::stoi(value[taskDetail]["start"].asString());
            }
            if (!value[taskDetail]["end"].isNull()) {
                _endTime = std::stoi(value[taskDetail]["end"].asString());
            }
            // end intercept

            // start snapshot
            if (!value[taskDetail]["duration"].isNull()) {
                _length = std::stoi(value[taskDetail]["duration"].asString());
            }
            if (!value[taskDetail]["isFile"].isNull()) {
                if (std::stoi(value[taskDetail]["isFile"].asString()))
                    _isFile = true;
                else
                    _isFile = false;
            }
            // end snapshot
            //
            // start font watermark
            if (!value[taskDetail]["fontcontent"].isNull()) {
                _waterInfo._content =
                    value[taskDetail]["fontcontent"].asString();
            }
            if (!value[taskDetail]["fontpath"].isNull()) {
                _waterInfo._fontFile = value[taskDetail]["fontpath"].asString();
            }
            if (!value[taskDetail]["fontcolor"].isNull()) {
                //解析rgb
                vector<string> color;
                vector<string>::iterator iter;
                color = DealString::SplitString(
                    ",", value[taskDetail]["fontcolor"].asString());
                for (iter = color.begin(); iter != color.end(); iter++) {
                    DealString::TrimString(*iter);
                }
                int red = 0;
                int green = 0;
                int blue = 0;
                if (color.size() == 3) {
                    red = atoi(color[0].c_str());
                    green = atoi(color[1].c_str());
                    blue = atoi(color[2].c_str());
                }
                _waterInfo._fontColor = DealString::RgbTo16(red, green, blue);
            }
            if (!value[taskDetail]["fontsize"].isNull()) {
                _waterInfo._fontSize =
                    std::stoi(value[taskDetail]["fontsize"].asString());
            }
            if (!value[taskDetail]["fontx"].isNull()) {
                _waterInfo._x =
                    std::stoi(value[taskDetail]["fontx"].asString());
            }
            if (!value[taskDetail]["fonty"].isNull()) {
                _waterInfo._y =
                    std::stoi(value[taskDetail]["fonty"].asString());
            }
            if (!value[taskDetail]["fontalph"].isNull()) {
                _waterInfo._alph =
                    std::stod(value[taskDetail]["fontalph"].asString());
            }
            // end font watermark
            //
            // start image watermark
            //
            if (!value[taskDetail]["waterwidth"].isNull()) {
                _waterInfo._width =
                    std::stoi(value[taskDetail]["waterwidth"].asString());
            }
            if (!value[taskDetail]["waterheight"].isNull()) {
                _waterInfo._height =
                    std::stoi(value[taskDetail]["waterheight"].asString());
            }
            if (!value[taskDetail]["waterx"].isNull()) {
                _waterInfo._x =
                    std::stoi(value[taskDetail]["waterx"].asString());
            }
            if (!value[taskDetail]["watermarginx"].isNull()) {
                _waterInfo._marginX =
                    std::stoi(value[taskDetail]["watermarginx"].asString());
            }
            if (!value[taskDetail]["watermarginy"].isNull()) {
                _waterInfo._marginY =
                    std::stoi(value[taskDetail]["watermarginy"].asString());
            }
            if (!value[taskDetail]["waterposition"].isNull()) {
                _waterInfo._position =
                    value[taskDetail]["waterposition"].asString();
            }
            if (!value[taskDetail]["watery"].isNull()) {
                _waterInfo._y =
                    std::stoi(value[taskDetail]["watery"].asString());
            }
            // end image watermark
            //
            // start sceneframe
            if (!value[taskDetail]["threshold"].isNull()) {
                _threshold =
                    std::stoi(value[taskDetail]["threshold"].asString());
            }
            // end sceneframe
        }
    }
    return 0;
}
void AvOpts::initAvOpts(FileInfoStruct* input) {
    if (0 == _videoBit) {
        if (NULL != input->videoCodecCtx)
            _videoBit = input->videoCodecCtx->bit_rate;
    }
    if (0 == _width && 0 == _height) {
        _width = input->width;
        _height = input->height;
    } else if (0 == _width && 0 != _height) {
        if(input->height != 0) _width = _height * input->width / input->height;
        if (0 != _width % 2) _width = _width - 1;  //保证分辨率是2的倍数
    }
    if (0 == _samplespersec) {
        if (NULL != input->audioCodecCtx)
            _samplespersec =
                input->audioCodecCtx->sample_rate;  // aac的采样必须是44100,size
                                                    // = 1024
    }
    if (0 == _audioBit) {
        if (NULL != input->audioCodecCtx)
            _audioBit = input->audioCodecCtx->bit_rate;
    }
};

//返回编码id
AVCodecID AvOpts::encoderCodecId(string encodeCodec) {
    enum AVCodecID codecid;
    if (0 == encodeCodec.compare("H264")) {
        codecid = AV_CODEC_ID_H264;
    } else if (0 == encodeCodec.compare("H263")) {
        codecid = AV_CODEC_ID_H263;
    } else if (0 == encodeCodec.compare("H265")) {
        codecid = AV_CODEC_ID_HEVC;
    } else if (0 == encodeCodec.compare("H261")) {
        codecid = AV_CODEC_ID_H261;
    } else if (0 == encodeCodec.compare("WMV1")) {
        codecid = AV_CODEC_ID_WMV1;
    } else if (0 == encodeCodec.compare("WMV2")) {
        codecid = AV_CODEC_ID_WMV2;
        //	}else if (0 == encodeCodec.compare("WMV3")){
        //		codecid = AV_CODEC_ID_WMV3;
    } else if (0 == encodeCodec.compare("MPEG1VIDEO")) {
        codecid = AV_CODEC_ID_MPEG1VIDEO;
    } else if (0 == encodeCodec.compare("MPEG2VIDEO")) {
        codecid = AV_CODEC_ID_MPEG2VIDEO;
    } else if (0 == encodeCodec.compare("MPEG4")) {
        codecid = AV_CODEC_ID_MPEG4;
    } else if (0 == encodeCodec.compare("FLV1")) {
        codecid = AV_CODEC_ID_FLV1;
    } else if (0 == encodeCodec.compare("FFV1")) {
        codecid = AV_CODEC_ID_FFV1;
    } else if (0 == encodeCodec.compare("MSMPEG4V3")) {
        codecid = AV_CODEC_ID_MSMPEG4V3;
    } else if (0 == encodeCodec.compare("MSMPEG4V2")) {
        codecid = AV_CODEC_ID_MSMPEG4V2;
    } else if (0 == encodeCodec.compare("DVVIDEO")) {
        codecid = AV_CODEC_ID_DVVIDEO;
    } else if (0 == encodeCodec.compare("RV20")) {
        codecid = AV_CODEC_ID_RV20;
    } else if (0 == encodeCodec.compare("RV40")) {
        codecid = AV_CODEC_ID_RV40;
    } else if (0 == encodeCodec.compare("RAWVIDEO")) {
        codecid = AV_CODEC_ID_RAWVIDEO;
    } else if (0 == encodeCodec.compare("JPG")) {
        codecid = AV_CODEC_ID_MJPEG;  //图片编码jpg
    } else if (0 == encodeCodec.compare("MP3")) {
        codecid = AV_CODEC_ID_MP3;
        _frameSize = 1152;
    } else if (0 == encodeCodec.compare("MP2")) {
        codecid = AV_CODEC_ID_MP2;
        _frameSize = 1152;
    } else if (0 == encodeCodec.compare("AC3")) {
        codecid = AV_CODEC_ID_AC3;
        _frameSize = 0;
    } else if (0 == encodeCodec.compare("WMAV1")) {
        codecid = AV_CODEC_ID_WMAV1;
        _frameSize = 0;
    } else if (0 == encodeCodec.compare("WMAV2")) {
        codecid = AV_CODEC_ID_WMAV2;
        _frameSize = 0;
    } else if (0 == encodeCodec.compare("AAC")) {
        codecid = AV_CODEC_ID_AAC;  //音频编码
        _frameSize = 1024;
    } else if (0 == encodeCodec.compare("VORBIS")) {
        codecid = AV_CODEC_ID_VORBIS;  //音频编码，ogg封装格式中的常用编码
        _frameSize = 0;
    } else if (0 == encodeCodec.compare("PCM_S16LE")) {
        codecid = AV_CODEC_ID_PCM_S16LE;  //音频编码，这个编码有问题，需要修改
        _frameSize = 0;
    } else if (0 == encodeCodec.compare("AMR_NB")) {
        codecid = AV_CODEC_ID_AMR_NB;  //音频编码
        _frameSize = 0;
    } else if (0 == encodeCodec.compare("WAVPACK")) {
        codecid = AV_CODEC_ID_WAVPACK;  // wv
        _frameSize = 0;
    }
    return codecid;
};
string AvOpts::encoderCodecName(AVCodecID id, bool isVideo) {
    string codecName;
    switch (id) {
        case AV_CODEC_ID_H264:
            codecName = "H264";
            break;
        case AV_CODEC_ID_H263:
            codecName = "H263";
            break;
        case AV_CODEC_ID_HEVC:
            codecName = "H265";
            break;
        case AV_CODEC_ID_H261:
            codecName = "H261";
            break;
        case AV_CODEC_ID_WMV1:
            codecName = "WMV1";
            break;
        case AV_CODEC_ID_WMV2:
            codecName = "WMV2";
            break;
        // case AV_CODEC_ID_WMV3 : codecName = "WMV3";  break;
        case AV_CODEC_ID_MPEG1VIDEO:
            codecName = "MPEG1VIDEO";
            break;
        case AV_CODEC_ID_MPEG2VIDEO:
            codecName = "MPEG2VIDEO";
            break;
        case AV_CODEC_ID_MPEG4:
            codecName = "MPEG4";
            break;
        case AV_CODEC_ID_FLV1:
            codecName = "FLV1";
            break;
        case AV_CODEC_ID_FFV1:
            codecName = "FFV1";
            break;
        case AV_CODEC_ID_MSMPEG4V2:
            codecName = "MSMPEG4V2";
            break;
        case AV_CODEC_ID_MSMPEG4V3:
            codecName = "MSMPEG4V3";
            break;
        case AV_CODEC_ID_DVVIDEO:
            codecName = "DVVIDEO";
            break;
        // case AV_CODEC_ID_RV20 : codecName = "RV20"; break;
        // case AV_CODEC_ID_RV40 : codecName = "RV40"; break;
        case AV_CODEC_ID_RAWVIDEO:
            codecName = "RAWVIDEO";
            break;

        case AV_CODEC_ID_MP3:
            codecName = "MP3";
            break;
        case AV_CODEC_ID_MP2:
            codecName = "MP2";
            break;
        case AV_CODEC_ID_AC3:
            codecName = "AC3";
            break;
        case AV_CODEC_ID_WMAV1:
            codecName = "WMAV1";
            break;
        case AV_CODEC_ID_WMAV2:
            codecName = "WMAV2";
            break;
        case AV_CODEC_ID_AAC:
            codecName = "AAC";
            break;
        case AV_CODEC_ID_VORBIS:
            codecName = "VORBIS";
            break;
        case AV_CODEC_ID_WAVPACK:
            codecName = "WAVPACK";
            break;
        // case AV_CODEC_ID_AMR_NB: codecName = "AMR_NB"; break;
        default:
            codecName = "UNKNOWN";
    }
    if (isVideo && 0 == codecName.compare("UNKNOWN")) {
        codecName = "H264";
    } else if ((!isVideo) && 0 == codecName.compare("UNKNOWN")) {
        codecName = "AAC";
    }
    return codecName;
};
