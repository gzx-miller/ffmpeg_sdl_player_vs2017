

#include <stdio.h>
#include <intsafe.h>

#include <stdlib.h>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "SDL.h"
}

#define INBUF_SIZE 4096
#define DECODE_BUF_SIZE 1024
using namespace std;

const char filePath[] = "a.h265";
bool GetVideoIndex(AVFormatContext * pFormatCtx, int &i)
{
    int videoIndex = -1;
    for (; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIndex = i;
            break;
        }
    }
    return videoIndex;
}

static void CopyImageBuf(AVFrame * frame, uint8_t* imgBuf) {
    int xsize = frame->width;
    int ysize = frame->height;
    int offset = 0;
    int wrap = frame->linesize[0];
    uint8_t* buf = frame->data[0];
    for (int i = 0; i < ysize; i++) {
        memcpy(imgBuf + offset, buf + i * wrap, xsize);
        offset += xsize;
    }
}

static int DecodePacket(AVCodecContext *dec_ctx, AVPacket *pkt, AVFrame *frame) {
    char buf[DECODE_BUF_SIZE];
    int ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "sending a packet for decoding failed! \n");
        return -1;
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        }
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            return -1;
        }
    }
    return 1;
}

int main(int argc, char* argv[]) {
    AVFormatContext	*pFormatCtx = avformat_alloc_context();
    if (avformat_open_input(&pFormatCtx, filePath, NULL, NULL) != 0) {
        fprintf(stderr, "open file %s failed! \n", filePath);
        return -1;
    }
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        fprintf(stderr, "find stream info failed! \n");
        return -1;
    }

    int i = 0;
    int videoIndex = GetVideoIndex(pFormatCtx, i);
    if (videoIndex < 0) {
        fprintf(stderr, "get video stream %d failed! \n", i);
        return -1;
    }

    AVCodecParameters *pCodecParam = pFormatCtx->streams[videoIndex]->codecpar;
    AVCodec *pCodec = avcodec_find_decoder(pCodecParam->codec_id);
    if (pCodec == NULL) {
        fprintf(stderr, "[Err] find decoder of videoIndex %d failed! \n", i);
        return -1;
    }
    AVCodecParserContext *parser = av_parser_init(pCodec->id);
    if (!parser) {
        fprintf(stderr, "parser init failed! \n");
        return -1;
    }
    AVCodecContext *c = avcodec_alloc_context3(pCodec);
    if (!c) {
        fprintf(stderr, "alloc context failed! \n");
        return -1;
    }

    if (avcodec_open2(c, pCodec, NULL) < 0) {
        fprintf(stderr, "open decoder failed! \n");
        return -1;
    }

    AVFrame *pFrame = av_frame_alloc();
    AVFrame * pFrameYUV = av_frame_alloc();
    int bufSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, 
        c->width, c->height, 1);
    uint8_t *out_buffer = (uint8_t *)av_malloc(bufSize);
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "alloc packet failed! \n");
    }

    av_dump_format(pFormatCtx, 0, filePath, 0);
    struct SwsContext *img_convert_ctx = sws_getContext(
        c->width, c->height, c->pix_fmt,
        c->width, c->height, AV_PIX_FMT_YUV420P, 
        SWS_BICUBIC, NULL, NULL, NULL);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        fprintf(stderr, "init sdl failed! \n");
        return -1;
    }

    int screen_w = c->width;
    int screen_h = c->height;

    SDL_Window *screen = SDL_CreateWindow("fd-player",        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
          screen_w, screen_h, SDL_WINDOW_OPENGL);
    if (!screen) {
        fprintf(stderr, "sdl create window failed! \n");
        return -1;
    }

    SDL_Renderer* sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
    SDL_Texture* sdlTexture = SDL_CreateTexture(sdlRenderer,
        SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, 
        c->width, c->height);

    SDL_Rect sdlRect;
    sdlRect.x = 0;
    sdlRect.y = 0;
    sdlRect.w = screen_w;
    sdlRect.h = screen_h;

    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    uint8_t *data;
    FILE* f = fopen(filePath, "rb");
    if (!f) {
        fprintf(stderr, "open file failed failed! \n");
        return -1;
    }

    while (!feof(f)) {
        int data_size = fread(inbuf, 1, INBUF_SIZE, f);
        if (!data_size){
            break;
        }
        data = inbuf;
        while (data_size > 0) {
            int ret = av_parser_parse2(parser, c, &pkt->data, &pkt->size,
                data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (ret < 0) {
                fprintf(stderr, "av parse failed! \n");
                return -1;
            }
            data += ret;
            data_size -= ret;
            if (pkt->size) {
                if (pkt->stream_index == videoIndex) {
                    int retd = DecodePacket(c, pkt, pFrame);
                    if (retd == 0) continue;
                    if (retd == -1) return -1;
                    sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, c->height,
                        pFrameYUV->data, pFrameYUV->linesize);
                    SDL_UpdateYUVTexture(sdlTexture, &sdlRect,
                        pFrameYUV->data[0], pFrameYUV->linesize[0],
                        pFrameYUV->data[1], pFrameYUV->linesize[1],
                        pFrameYUV->data[2], pFrameYUV->linesize[2]);
                    SDL_RenderClear(sdlRenderer);
                    SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);
                    SDL_RenderPresent(sdlRenderer);
                    SDL_Delay(40);
                }
            }
        }       
    }

    av_parser_close(parser);
    sws_freeContext(img_convert_ctx);
    SDL_Quit();
    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avcodec_free_context(&c);
    av_packet_free(&pkt);
    avformat_close_input(&pFormatCtx);

    return 0;
}
