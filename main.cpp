// MyVideoAndAudioPlayer.cpp: 定义应用程序的入口点。
//

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
using namespace std;

#define _STDC_CONSTANT_MACROS
#define SDL_MAIN_HANDLED
extern "C" {
#include "SDL/SDL.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
};
const int MAXSIZE = 3;
const int VIDEOTYPE = 1;
const int AUDIOTYPE = 2;

AVFormatContext* pFormatCtx;
AVCodecParameters* parser4video;
AVCodecParameters* parser4audio;
AVCodecContext* pCodecCtx4video;
AVCodecContext* pCodecCtx4audio;
AVCodec* pCodec4video;
AVCodec* pCodec4audio;
int videoIndex = -1, audioIndex = -1;
std::mutex pkMutex4Video;		//队列同步锁
std::mutex pkMutex4Audio;
queue<AVPacket*> packetVideoQueue;
queue<AVPacket*> packetAudioQueue;
AVFrame* frame4Video = av_frame_alloc();
AVFrame* frame4Audio = av_frame_alloc();

//存储时间戳信息，用于音画同步
AVRational streamTimeBase4Video{ 1,0 };
AVRational streamTimeBase4Audio{ 1,0 };
std::atomic<uint64_t> currentTimestamp4Audio{ 0 };

#define REFRESH_EVENT (SDL_USEREVENT + 1)

#define BREAK_EVENT (SDL_USEREVENT + 2)

string filePath = "D:/迅雷下载/大鱼海棠.mkv";

void refreshPic(int timeInterval, bool& exitRefresh, bool& faster) {
	cout << "picRefresher timeInterval[" << timeInterval << "]" << endl;
	while (!exitRefresh) {
		SDL_Event event;
		event.type = REFRESH_EVENT;
		SDL_PushEvent(&event);
		if (faster) {
			std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval / 2));
		}
		else {
			std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval));
		}
	}
	cout << "[THREAD] picRefresher thread finished." << endl;
}

int initAVCodecContext() {
	pFormatCtx = avformat_alloc_context();
	if (avformat_open_input(&pFormatCtx, filePath.c_str(), nullptr, nullptr) != 0) {
		cout << "Couldn't open input stream" << endl;
		return -1;
	}
	if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {  //获取视频文件信息
		cout << "Couldn't find stream information" << endl;
		return -1;
	}
	for (int i = 0; i < pFormatCtx->nb_streams; i++) {
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoIndex = i;
			cout << "get video index " << videoIndex << endl;
			streamTimeBase4Video = pFormatCtx->streams[i]->time_base;
		}
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioIndex = i;
			cout << "get audio index " << audioIndex << endl;
			streamTimeBase4Audio = pFormatCtx->streams[i]->time_base;
		}
	}
	if (videoIndex == -1) {
		cout << "Didn't find a video stream" << endl;
		return -1;
	}
	if (audioIndex == -1) {
		cout << "Didn't find a audio stream" << endl;
		return -1;
	}

	parser4video = pFormatCtx->streams[videoIndex]->codecpar;
	parser4audio = pFormatCtx->streams[audioIndex]->codecpar;
	pCodec4video = avcodec_find_decoder(parser4video->codec_id);  //视频编解码器
	pCodec4audio = avcodec_find_decoder(parser4audio->codec_id);
	if (pCodec4video == nullptr || pCodec4audio == nullptr) {
		cout << "Codec not found." << endl;
		return -1;
	}
	pCodecCtx4video = avcodec_alloc_context3(pCodec4video);
	pCodecCtx4audio = avcodec_alloc_context3(pCodec4audio);
	if (!pCodecCtx4video || !pCodecCtx4audio) {
		cout << "Could not allocate video codec context" << endl;
		return -1;
	}
	if (avcodec_parameters_to_context(pCodecCtx4video, parser4video) != 0
		|| avcodec_parameters_to_context(pCodecCtx4audio, parser4audio) != 0) {
		cout << "Could not copy codec context" << endl;
		return -1;
	}
	if (avcodec_open2(pCodecCtx4video, pCodec4video, nullptr) < 0
		|| avcodec_open2(pCodecCtx4audio, pCodec4audio, nullptr) < 0) {  //打开解码器
		cout << "Could not open codec" << endl;
		return -1;
	}
	return 0;
}

int getAqSize() {
	std::lock_guard<std::mutex> lk(pkMutex4Audio);
	int size = packetAudioQueue.size();
//	cout << "AqSize=" << size << endl;
	return size;
}

int getVqSize() {
	std::lock_guard<std::mutex> lk(pkMutex4Video);
	int size = packetVideoQueue.size();
//	cout << "VqSize:" << size << endl;
	return size;
}

void grabPacketToQueue() {
	//TODO 结束标志，暂时死循环读取
	while (true) {
		while (getAqSize() < MAXSIZE || getVqSize() < MAXSIZE) {
			AVPacket* packet = av_packet_alloc();
			int ret = av_read_frame(pFormatCtx, packet);
			if (ret < 0) {
				//TODO 文件结束的处理

				cout << "file finish or error" << endl;
				break;
			}
			else if (packet->stream_index == audioIndex) {
				std::lock_guard<std::mutex> lk(pkMutex4Audio);
				packetAudioQueue.push(packet);
			}
			else if (packet->stream_index == videoIndex) {
				std::lock_guard<std::mutex> lk(pkMutex4Video);
				packetVideoQueue.push(packet);
			}
			else {
				av_packet_free(&packet);
				cout << "WARN: unknown streamIndex: [" << packet->stream_index << "]" << endl;
			}
		}

	}
}

AVPacket* getPacketFromQueue(int type) {

	if (type == VIDEOTYPE) {
		std::lock_guard<std::mutex> lk(pkMutex4Video);
		if (packetVideoQueue.empty() || packetVideoQueue.front() == nullptr) {
			return nullptr;
		}
		else {
			AVPacket* front = packetVideoQueue.front();
			packetVideoQueue.pop();
			return front;
		}
	}
	else if (type == AUDIOTYPE) {
		std::lock_guard<std::mutex> lk(pkMutex4Audio);
		if (packetAudioQueue.empty() || packetAudioQueue.front() == nullptr) {
			return nullptr;
		}
		else {
			AVPacket* front = packetAudioQueue.front();
			packetAudioQueue.pop();
			return front;
		}
	}
}

void startVideoBySDL() {
	int screen_w, screen_h;
	SDL_Window* screen;
	SDL_Renderer* sdlRenderer;
	SDL_Texture* sdlTexture;
	SDL_Rect sdlRect;
	SDL_Thread* video_tid;
	SDL_Event event;

	screen_w = parser4video->width;
	screen_h = parser4video->height;

	screen = SDL_CreateWindow("cq player",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h, SDL_WINDOW_OPENGL);
	if (!screen) {
		printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
	}
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV,
		SDL_TEXTUREACCESS_STREAMING, parser4video->width,
		parser4video->height);
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;



	bool faster = false;
	bool exitRefresh = false;
	cout << av_q2d(pCodecCtx4video->framerate) << endl;
	AVRational frame_rate =
		av_guess_frame_rate(pFormatCtx, pFormatCtx->streams[videoIndex], nullptr);
	cout << av_q2d(frame_rate) << endl;
	double frameRate = av_q2d(frame_rate);
	std::thread refreshThread{ refreshPic, (int)(1000 / frameRate), ref(exitRefresh), ref(faster) };

	AVPacket* packet;
	bool skip = false;
	int ret = -1;
	//TODO 结束标志未添加
	while (1) {
		SDL_WaitEvent(&event);
		if (event.type == REFRESH_EVENT) {
			if (!skip) {
				packet = getPacketFromQueue(VIDEOTYPE);
				if (packet == nullptr) {
					cout << "error occured in get packet or finished" << endl;
				}
				
				ret = avcodec_send_packet(pCodecCtx4video, packet);
				if (ret == 0) {
					av_packet_free(&packet);
					packet = nullptr;
				}
				else if (ret == AVERROR(EAGAIN)) {
					// buff full, can not decode any more, nothing need to do.
					// keep the packet for next time decode.
				}
				else if (ret == AVERROR_EOF) {
					cout << "[WARN]  no new VIDEO packets can be sent to it." << endl;
				}
				else {
					string errorMsg = "+++++++++ ERROR avcodec_send_packet error: ";
					errorMsg += ret;
					cout << errorMsg << endl;
					throw std::runtime_error(errorMsg);
				}

				ret = avcodec_receive_frame(pCodecCtx4video, frame4Video);
			}
			
			if (ret == 0) {
				auto ats = currentTimestamp4Audio.load();
				auto vts =(uint64_t)( frame4Video->pts * av_q2d(streamTimeBase4Video) * 1000);
			//	cout << ats << ":::::" << vts << endl;
				if (vts > ats&& vts - ats > 30) {
					cout << "VIDEO FASTER ================= vTs - aTs [" << (vts - ats)
						<< "]ms, SKIP A EVENT" << endl;
					skip = true;
					faster = false;
					continue;
				}
				else if (vts < ats && ats - vts>30) {
					cout << "VIDEO SLOWER ================= aTs - vTs =[" << (ats - vts) << "]ms, Faster"
						<< endl;
					skip = false;
					faster = true;
				}
				else {
					skip = false;
					faster = false;
				}
				// cout << "avcodec_receive_frame success." << endl;
				// success.
				//TODO 暂时对视频帧处理未用sws_scale处理
				SDL_UpdateYUVTexture(sdlTexture, NULL, frame4Video->data[0],
					frame4Video->linesize[0], frame4Video->data[1],
					frame4Video->linesize[1], frame4Video->data[2],
					frame4Video->linesize[2]);
				SDL_RenderClear(sdlRenderer);
				// SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );
				SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
				SDL_RenderPresent(sdlRenderer);
			}
			else if (ret == AVERROR_EOF) {
				cout << "+++++++++++++++++++++++++++++ MediaProcessor no more output frames. video" << endl;
			}
			else if (ret == AVERROR(EAGAIN)) {
				// need more packet.
			}
			else {
				string errorMsg = "avcodec_receive_frame error: ";
				errorMsg += ret;
				cout << errorMsg << endl;
				throw std::runtime_error(errorMsg);
			}
		}
		else if (event.type == SDL_WINDOWEVENT) {
			SDL_GetWindowSize(screen, &screen_w, &screen_h);
		}
		else if (event.type == SDL_QUIT) {
			exitRefresh = true;
			break;
		}
		else if (event.type == BREAK_EVENT) {
			break;
		}

	}


}

//start audio
//存储音频信息结构体（主要用于输入音频和输出音频的信息保存）
struct AudioInfo {
	int64_t layout;
	int sampleRate;
	int channels;
	AVSampleFormat format;

	AudioInfo() {
		layout = -1;
		sampleRate = -1;
		channels = -1;
		format = AV_SAMPLE_FMT_S16;
	}

	AudioInfo(int64_t l, int rate, int c, AVSampleFormat f)
		: layout(l), sampleRate(rate), channels(c), format(f) {}
};

AudioInfo in;
AudioInfo out;

int audio_samples = -1;
//用于申请音频buffer存储空间
int allocDataBuf(AudioInfo in, AudioInfo out, uint8_t** outData, int inputSamples) {
	int bytePerOutSample = -1;
	switch (out.format) {
	case AV_SAMPLE_FMT_U8:
		bytePerOutSample = 1;
		break;
	case AV_SAMPLE_FMT_S16P:
	case AV_SAMPLE_FMT_S16:
		bytePerOutSample = 2;
		break;
	case AV_SAMPLE_FMT_S32:
	case AV_SAMPLE_FMT_S32P:
	case AV_SAMPLE_FMT_FLT:
	case AV_SAMPLE_FMT_FLTP:
		bytePerOutSample = 4;
		break;
	case AV_SAMPLE_FMT_DBL:
	case AV_SAMPLE_FMT_DBLP:
	case AV_SAMPLE_FMT_S64:
	case AV_SAMPLE_FMT_S64P:
		bytePerOutSample = 8;
		break;
	default:
		bytePerOutSample = 2;
		break;
	}

	int guessOutSamplesPerChannel =
		av_rescale_rnd(inputSamples, out.sampleRate, in.sampleRate, AV_ROUND_UP);
	int guessOutSize = guessOutSamplesPerChannel * out.channels * bytePerOutSample;

	//std::cout << "GuessOutSamplesPerChannel: " << guessOutSamplesPerChannel << std::endl;
	//std::cout << "GuessOutSize: " << guessOutSize << std::endl;

	guessOutSize *= 1.2;  // just make sure.

	*outData = (uint8_t*)av_malloc(sizeof(uint8_t) * guessOutSize);
	// av_samples_alloc(&outData, NULL, outChannels, guessOutSamplesPerChannel,
	// AV_SAMPLE_FMT_S16, 0);
	return guessOutSize;
}


//将ffmpeg抓取到的音频frame存储至databuffer中
tuple<int, int> reSample( uint8_t* dataBuffer, int dataBufferSize,
	const AVFrame* frame) {
	SwrContext* swr = swr_alloc_set_opts(nullptr, out.layout, out.format, out.sampleRate,
		in.layout, in.format, in.sampleRate, 0, nullptr);
	//cout << out.layout << "," << out.format << "," << out.sampleRate << "," << in.layout << ","
	//	<< in.format << "," << in.sampleRate << endl;
	if (swr_init(swr)) {
		cout << "swr_init error." << endl;
		throw std::runtime_error("swr_init error.");
	}
	int outSamples = swr_convert(swr, &dataBuffer, dataBufferSize,
		(const uint8_t**)&frame->data[0], frame->nb_samples);
//	cout << "reSample: nb_samples=" << frame->nb_samples
//		<< ", sample_rate = " << frame->sample_rate << ", outSamples=" << outSamples << endl;
	if (outSamples <= 0) {
		throw std::runtime_error("error: outSamples=" + outSamples);
	}
	int outDataSize = av_samples_get_buffer_size(NULL, out.channels, outSamples, out.format, 1);

	if (outDataSize <= 0) {
		throw std::runtime_error("error: outDataSize=" + outDataSize);
	}
	return { outSamples, outDataSize };
}

void audio_callback(void* userdata, Uint8* stream, int len) {
	
	AVPacket* packet;
	//TODO 对结束的处理
	while (true) {
		
		packet = getPacketFromQueue(AUDIOTYPE);
		if (packet == nullptr) {
			cout << "audio finished or get some error" << endl;
		}
		int ret = -1;
		ret = avcodec_send_packet(pCodecCtx4audio, packet);
		if (ret == 0) {
			av_packet_free(&packet);
			packet = nullptr;
		}
		else if (ret == AVERROR(EAGAIN)) {
			// buff full, can not decode any more, nothing need to do.
			// keep the packet for next time decode.
		}
		else if (ret == AVERROR_EOF) {
			cout << "[WARN]  no new AUDIO packets can be sent to it." << endl;
		}
		else {
			string errorMsg = "+++++++++ ERROR avcodec_send_packet error: ";
			errorMsg += ret;
			cout << errorMsg << endl;
			cout << ret << endl;
			throw std::runtime_error(errorMsg);
		}

		ret = avcodec_receive_frame(pCodecCtx4audio, frame4Audio);
		if (ret >= 0) {
			break;
		}
		else if (ret == AVERROR(EAGAIN)) {
			continue;
		}
		else {
			cout << "can't get frame" << endl;
			throw std::runtime_error("can't get frame");
		}
	}
	auto t = frame4Audio->pts * av_q2d(streamTimeBase4Audio) * 1000;
	currentTimestamp4Audio.store((uint64_t)t);
	static uint8_t* outBuffer = nullptr;
	static int outBufferSize = 0;

	if (outBuffer == nullptr) {
		outBufferSize = allocDataBuf(in, out, &outBuffer, frame4Audio->nb_samples);
	//	cout << " --------- audio samples: " << frame4Audio->nb_samples << endl;
	}
	else {
		memset(outBuffer, 0, outBufferSize);
	}

	int outSamples;
	int outDataSize;
	std::tie(outSamples, outDataSize) =
		reSample( outBuffer, outBufferSize, frame4Audio);
	audio_samples =outSamples;
	if (outDataSize != len) {
		cout << "WARNING: outDataSize[" << outDataSize << "] != len[" << len << "]" << endl;
	}

	std::memcpy(stream, outBuffer, outDataSize);
	av_freep(&outBuffer);
	outBuffer = nullptr;
}

void getsamples() {
	AVPacket* packet;
	//TODO 对结束的处理
	while (true) {

		packet = getPacketFromQueue(AUDIOTYPE);
		if (packet == nullptr) {
			cout << "audio finished or get some error" << endl;
		}
		int ret = -1;
		ret = avcodec_send_packet(pCodecCtx4audio, packet);
		if (ret == 0) {
			av_packet_free(&packet);
			packet = nullptr;
		}
		else if (ret == AVERROR(EAGAIN)) {
			// buff full, can not decode any more, nothing need to do.
			// keep the packet for next time decode.
		}
		else if (ret == AVERROR_EOF) {
			cout << "[WARN]  no new AUDIO packets can be sent to it." << endl;
		}
		else {
			string errorMsg = "+++++++++ ERROR avcodec_send_packet error: ";
			errorMsg += ret;
			cout << errorMsg << endl;
			cout << ret << endl;
			throw std::runtime_error(errorMsg);
		}

		ret = avcodec_receive_frame(pCodecCtx4audio, frame4Audio);
		if (ret >= 0) {
			break;
		}
		else if (ret == AVERROR(EAGAIN)) {
			continue;
		}
		else {
			cout << "can't get frame" << endl;
			throw std::runtime_error("can't get frame");
		}
	}

	static uint8_t* outBuffer = nullptr;
	static int outBufferSize = 0;

	if (outBuffer == nullptr) {
		outBufferSize = allocDataBuf(in, out, &outBuffer, frame4Audio->nb_samples);
	//	cout << " --------- audio samples: " << frame4Audio->nb_samples << endl;
	}
	else {
		memset(outBuffer, 0, outBufferSize);
	}

	int outSamples;
	int outDataSize;
	std::tie(outSamples, outDataSize) =
		reSample(outBuffer, outBufferSize, frame4Audio);
	audio_samples = outSamples;
}

void startAudioBySDL() {
	SDL_AudioSpec wanted_spec;
	SDL_AudioSpec specs;

	
	int64_t inLayout = parser4audio->channel_layout;
	int inChannels = parser4audio->channels;
	int inSampleRate = parser4audio->sample_rate;
	AVSampleFormat inFormate = AVSampleFormat(pCodecCtx4audio->sample_fmt);
	in = AudioInfo(inLayout, inSampleRate, inChannels, inFormate);
	out = AudioInfo(AV_CH_LAYOUT_STEREO, inSampleRate, 2, AV_SAMPLE_FMT_S16);
	while (audio_samples <= 0) {
		getsamples();
	}
	wanted_spec.freq = parser4audio->sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = parser4audio->channels;
	wanted_spec.samples = audio_samples;  // set by output samples
	wanted_spec.callback = audio_callback;
	wanted_spec.userdata = nullptr;
	SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);

	SDL_AudioDeviceID audioDeviceId =
		SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &specs, 0);  //[1]
	if (audioDeviceId == 0) {
		cout << "Failed to open audio device:" << SDL_GetError() << endl;
	}
	cout << "wanted_specs.freq:" << wanted_spec.freq << endl;
	// cout << "wanted_specs.format:" << wanted_specs.format << endl;
	std::printf("wanted_specs.format: Ox%X\n", wanted_spec.format);
	cout << "wanted_specs.channels:" << (int)wanted_spec.channels << endl;
	cout << "wanted_specs.samples:" << (int)wanted_spec.samples << endl;

	cout << "------------------------------------------------" << endl;
	cout << "specs.freq:" << specs.freq << endl;
	// cout << "specs.format:" << specs.format << endl;
	std::printf("specs.format: Ox%X\n", specs.format);
	cout << "specs.channels:" << (int)specs.channels << endl;
	cout << "specs.silence:" << (int)specs.silence << endl;
	cout << "specs.samples:" << (int)specs.samples << endl;

	cout << "waiting audio play..." << endl;

	SDL_PauseAudioDevice(audioDeviceId, 0);  // [2]

}
int main()
{
	initAVCodecContext();					//初始化ffmpeg相关组件

	thread grabPacket{ grabPacketToQueue };	//开启线程往队列中喂食packet
	grabPacket.detach();
	//初始化sdl
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		string errMsg = "Could not initialize SDL -";
		errMsg += SDL_GetError();
		cout << errMsg << endl;
		throw std::runtime_error(errMsg);
	}
	std::thread audioThread{ startAudioBySDL };
	audioThread.join();
	std::thread videoThread{ startVideoBySDL };
	videoThread.join();
	return 0;
}
