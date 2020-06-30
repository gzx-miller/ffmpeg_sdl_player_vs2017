
#include <iostream>

extern "C" {
#include "sdl.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil\log.h"
#include "libavutil\error.h"
}

int main(int argc, char* argv[]) {
    av_log_set_level(AV_LOG_INFO);
    AVFormatContext *fmt_ctx = NULL;

    int ret;
    ret = avformat_open_input(&fmt_ctx, "1.mp4", NULL, NULL);
    if (ret < 0) {
        printf("avformat_open_input failed: %d\n", ret);
        return -1;
    }
    av_dump_format(fmt_ctx, 0, "1.mp4", 0);
    avformat_close_input(&fmt_ctx);
    return 0;
}
