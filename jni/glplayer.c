//改用OpenGL ES来显示视频
#include <stdio.h>
#include <GLES/gl.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <string.h>
#include "com_fuchao_ffmpegandroidplayer_GlVideoView.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"


//定义日志打印
#define  LOG_TAG    "GlPlayer"
#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

//定义全局的文件地址变量
char *path = NULL;
AVFormatContext *pFormatCtx = NULL; //ffmpeg的上下文变量
AVCodecContext *pCodecCtx = NULL; //编解码的上下文环境
AVCodec *pCodec = NULL; //视频编解码器
AVFrame *pFrame = NULL;//保存原始数据Frame
AVFrame *pFrameRGB = NULL;//保存转换后的Frame
uint8_t *buffer = NULL;//保存转换后数据的缓冲区
struct SwsContext *sws_ctx = NULL;//转换用的上下文
int frameFinished;
AVPacket packet;
int videoStream = -1;//视频流的顺序
int count = 0;

//opengl
int textureFormat = PIX_FMT_RGBA; //PIX_FMT_RGB24;
int textureWidth = 256;
int textureHeight = 256;
int nTextureHeight = -256;
int textureL = 0, textureR = 0, textureW = 0;
GLuint texturesConverted[2] = { 0, 1 };
static int len = 0;


/*
 * Class:     com_fuchao_ffmpegandroidplayer_GlVideoView
 * Method:    onNdkSurfaceCreated
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_fuchao_ffmpegandroidplayer_GlVideoView_onNdkSurfaceCreated(JNIEnv *env, jobject obj)
{

}

/*
 * Class:     com_fuchao_ffmpegandroidplayer_GlVideoView
 * Method:    onNdkSurfaceChanged
 * Signature: (II)V
 */
JNIEXPORT void JNICALL Java_com_fuchao_ffmpegandroidplayer_GlVideoView_onNdkSurfaceChanged(JNIEnv *env, jobject obj, jint width, jint height)
{
		LOGE("Hello world");
		//取出文件路径
		jclass cls = (*env)->GetObjectClass(env, obj);
		jfieldID fid = (*env)->GetFieldID(env, cls, "filePath", "Ljava/lang/String;");
		jstring file_path = (jstring)(*env)->GetObjectField(env, obj, fid);
		path = (*env)->GetStringUTFChars(env, file_path, NULL);
		if (path == NULL)
		{
			LOGE("获取java中的path失败");
			return -1;
		} else
		{
			//输出要播放的文件的地址
			LOGE("%s", path);
		}
		//注册所有类型的格式
		av_register_all();
		//获取FFmpeg的上下文变量
		pFormatCtx = avformat_alloc_context();
		//打开文件
		if (avformat_open_input(&pFormatCtx, path, NULL, NULL) != 0)
		{
			LOGE("Could not open file: %s\n", path);
			return -1;
		}
		//获取流信息
		if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
		{
			LOGE("Could not find stream infomation");
			return -1;
		}
		//找出流中的第一个视频流
		int i;
		for (i = 0; i < pFormatCtx->nb_streams; i++)
		{
			if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0)
			{
				videoStream = i;
			}
		}
		if (videoStream == -1)
		{
			LOGE("Didn't find a video stream");
			return -1;
		}
		//获取视频流的编码上下文环境
		pCodecCtx = pFormatCtx->streams[videoStream]->codec;
		//获取视频的编解码器
		pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
		if (pCodec == NULL)
		{
			LOGE("Codec not found.");
			return -1;
		}
		//打开解码器
		if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
		{
			LOGE("Could not open codec.");
			return -1;
		}
		//获取视频的宽、高
		int videoWidth = pCodecCtx->width;
		int videoHeight = pCodecCtx->height;
		//申请一块空间，用于保存原始数据
		pFrame = av_frame_alloc();
		//申请一块空间，用于保存转换为RGB后的数据,流Stream是由帧Frame组成的
		pFrameRGB = av_frame_alloc();
		if (pFrameRGB == NULL)
		{
			LOGE("Could not allocate video frame");
			return -1;
		}
		//计算机的图片是以RGB格式存储的，所以我们要把Frame从它本地格式转换会24位的RGB格式
		int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, pCodecCtx->width, pCodecCtx->height, 1);
		buffer = (uint8_t*) av_malloc(numBytes * sizeof(uint8_t));
		//这个函数并没有真正的转换，我理解只是将一些图片的信息写到新的buffer中，然后将pFrameRGB的一些字段指向部分buffer的空间，为后面转换做准备,不然转换的时候不知道数据往哪里填。
		//pFrameRGB仅仅只是分配了一个结构体的空间，这里buff才是真正存储转换后的数据，也就是说pFrameRGB是指向buff的
		//英语原文是：Assign appropriate parts of buffer to image planes in pFrameRGB
		//翻译一下就是：为PFrameRGB中的图片分配适当的部分空间
		av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer, AV_PIX_FMT_RGBA, pCodecCtx->width, pCodecCtx->height, 1);
		//下面我们终于可以从流里面拿到数据了，注意每次循环读取一个包Package,帧是逻辑上的概念，实际数据是安照Packet的格式来存储的。类似于IP协议中的分包,ip包是逻辑概念，实际发送的是mac包
		//先获取一个转换用的上下文
		sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
		//删除局部引用
		(*env)->DeleteLocalRef(env, cls);
		(*env)->DeleteLocalRef(env, file_path);
}

/*
 * Class:     com_fuchao_ffmpegandroidplayer_GlVideoView
 * Method:    onNdkGetFrame
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_fuchao_ffmpegandroidplayer_GlVideoView_onNdkGetFrame(JNIEnv *env, jobject obj)
{
	// keep reading packets until we hit the end or find a video packet
		while (av_read_frame(pFormatCtx, &packet) >= 0)
		{
			static struct SwsContext *img_convert_ctx;
			// Is this a packet from the video stream?
			if (packet.stream_index == videoStream)
			{
				// Decode video frame
				avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
				// Did we get a video frame?
				if (frameFinished)
				{
					if (img_convert_ctx == NULL)
					{
						/* get/set the scaling context */
						int w = pCodecCtx->width;
						int h = pCodecCtx->height;
						img_convert_ctx = sws_getContext(w, h, //source
								pCodecCtx->pix_fmt, textureWidth, textureHeight,
								//w, h, //destination
								textureFormat,
								//SWS_BICUBIC,
								//SWS_POINT,
								//SWS_X,
								//SWS_CPU_CAPS_MMX2,
								SWS_FAST_BILINEAR, NULL, NULL, NULL);
						if (img_convert_ctx == NULL)
						{
							/* __android_log_print(ANDROID_LOG_DEBUG,  */
							/* 			"video.c",  */
							/* 			"NDK: Cannot initialize the conversion context!" */
							/* 			); */
							return;
						}
					} /* if img convert null */
					/* finally scale the image */
					/* __android_log_print(ANDROID_LOG_DEBUG,  */
					/* 			"video.c",  */
					/* 			"getFrame: Try to scale the image" */
					/* 			); */
					sws_scale(img_convert_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameConverted->data, pFrameConverted->linesize);
					/* do something with pFrameConverted */
					/* ... see drawFrame() */
					/* We found a video frame, did something with it, no free up
					 packet and return */
					av_free_packet(&packet);
					return;
				} /* if frame finished */
			} /* if packet video stream */
			// Free the packet that was allocated by av_read_frame
			av_free_packet(&packet);
		} /* while */
		//reload video when you get to the end
		av_seek_frame(pFormatCtx, videoStream, 0, AVSEEK_FLAG_ANY);
}

/*
 * Class:     com_fuchao_ffmpegandroidplayer_GlVideoView
 * Method:    onNdkDrawFrame
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_fuchao_ffmpegandroidplayer_GlVideoView_onNdkDrawFrame(JNIEnv *env, jobject obj)
{
	LOGI("onNdkDrawFrame()");
	if(av_read_frame(pFormatCtx, &packet) >= 0)
	{
				//判断这个包是不是视屏流的包,videoStream就是我们上面获取的流在结构体中的顺序
				if (packet.stream_index == videoStream)
				{
					LOGI("获得一个视频包");
					//真正的获取原始数据,获取的原始数据放在pFrame所指向的缓冲区中
					//注意：解码函数就一个，没有avcodec_decode_video(),也没有avcodec_decode_video1(),这个函数名字有点坑
					avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
					//这里有可能一个Frame不止一个package,所以用frameFinished这个变量来标示一个frame是否已经全部取到了.英文原文如下:
					//However, we might not have all the information we need for a frame after decoding a packet, so avcodec_decode_video() sets frameFinished for us when we have the next frame
					if (frameFinished)
					{
						LOGI("获得一个完整包，count = %d",count++);
						//真正的将frame从原始格式转换为RGB格式.这里也可以看到pFrameRGB->data,pFrameRGB->linesize这些数据我们之前已经用av_image_fill_arrays()给分配好了
						sws_scale(sws_ctx, (uint8_t const * const *) pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);
						/*
						//到这里我们已经拿到一个frame的视频数据，这个frame就是pFrameRGB,数据就存在pFrameRGB->data字段中,接下来就是把pFrameRGB中的数据复制到surfaceView的缓冲区windowBuffer中
						//但是这里要注意.复制的时候要一行一行的复制，因为当屏幕比图片大或小的时候，复制过去的数据就会乱
						uint8_t *dst = windowBuffer.bits;
						int dstStride = windowBuffer.stride * 4; //windowBUffer.strid是一行的像素数目,然后我们设置的是RGB_8888，所以一个像素占了4个字节，其中最高的一个字节表示透明度
						uint8_t *src = (uint8_t*) pFrameRGB->data[0]; //data是个数组指针,也就是个二维数组
						int srcStride = pFrameRGB->linesize[0]; //pFrameRGB->linesize保存的是frame每一行的宽度，单位是字节
						//开始一行一行的复制
						//LOGI("start copy data");
						int h;
						for (h = 0; h < pCodecCtx->height; h++)
						{
							memcpy(dst + h * dstStride, src + h * srcStride, srcStride);
						}
						*/
						glGenTextures(1, &texture[0]);
						glBindTexture(GL_TEXTURE_2D, texture[0]);
						//glClear(GL_COLOR_BUFFER_BIT);
						glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pCodecCtx->width, pCodecCtx->height, GL_RGB, GL_UNSIGNED_BYTE, pFrameRGB->data[0]);
						glDrawTexiOES(0, 0, 0, pCodecCtx->width, pCodecCtx->height);
					}
				}
				//释放掉这个packet
				av_packet_unref(&packet);
			}
}

/*
 * Class:     com_fuchao_ffmpegandroidplayer_GlVideoView
 * Method:    onNdkDestory
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_fuchao_ffmpegandroidplayer_GlVideoView_onNdkDestory(JNIEnv *env, jobject obj)
{
		//释放用于存储转换后的数据缓冲区
		av_free(buffer);
		//释放指向转换后数据的结构体
		av_free(pFrameRGB);
		//释放原始数据
		av_free(pFrame);
		//关闭解码器
		avcodec_close(pCodecCtx);
		//关闭视频文件
		avformat_close_input(&pFormatCtx);
}

//初始化OpenGL
void initOpenGL()
{
		//This is actually the oncreate code
		glDeleteTextures(1, &texturesConverted[len]);
		//Disable stuff
		GLuint *start = s_disable_options;
		while (*start)
		{
			glDisable(*start++);
		}
		//setup textures
		glEnable(GL_TEXTURE_2D);
		len = (len == 0) ? 1 : 0;
		glGenTextures(1, &texturesConverted[len]);
		glBindTexture(GL_TEXTURE_2D, texturesConverted[len]);
		//Create Nearest Filtered Texture
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		//Different possible texture parameters, e.g. GL10.GL_CLAMP_TO_EDGE
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		//GL_REPEAT);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		//GL_REPEAT);
		glTexImage2D(GL_TEXTURE_2D, /* target */
		0, /* level */
		GL_RGBA, /* internal format */
		textureWidth, /* width */
		textureHeight, /* height */
		0, /* border */
		GL_RGBA, /* format */
		GL_UNSIGNED_BYTE,/* type */
		NULL);
		//setup simple shading
		glShadeModel(GL_FLAT);
		//check_gl_error("glShadeModel");
		glColor4x(0x10000, 0x10000, 0x10000, 0x10000);
}
