#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// #ifdef __cplusplus
// extern "C" {
// #endif

// #include <libavformat/avformat.h>
// #include <libavcodec/avcodec.h>
// #include <libswscale/swscale.h>
// #include <libavutil/imgutils.h>

// #ifdef __cplusplus
// }
// #endif
#ifdef __cplusplus
extern "C" {
#endif
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#ifdef __cplusplus
}
#endif

#define PORT 8080
#define VIDEO_FILE "sample.mp4"
#define BOUNDARY "boundarydonotcross"

int clients[1024];
int client_count = 0;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_info(const char *msg) {
    printf("[INFO] %s\n", msg);
}

void log_error(const char *msg) {
    perror(msg);
}

void *video_streamer(void *arg);
void *client_handler(void *arg);

int main() {
    signal(SIGPIPE, SIG_IGN);  // 忽略 SIGPIPE 防止客户端断开时报错

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_error("socket creation failed");
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("bind failed");
        return -1;
    }

    if (listen(server_fd, 10) < 0) {
        log_error("listen failed");
        return -1;
    }

    log_info("MJPEG stream server started at http://localhost:8080");

    pthread_t video_thread;
    pthread_create(&video_thread, NULL, video_streamer, NULL);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &len);
        if (client_fd < 0) {
            log_error("accept failed");
            continue;
        }

        log_info("New client connected");

        pthread_mutex_lock(&client_mutex);
        clients[client_count++] = client_fd;
        pthread_mutex_unlock(&client_mutex);
    }

    close(server_fd);
    return 0;
}

void *video_streamer(void *arg) {
    avformat_network_init();
    int *test = nullptr;
    AVFormatContext *fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, VIDEO_FILE, nullptr, nullptr) < 0) {
        log_error("Failed to open video file");
        return nullptr;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        log_error("Failed to get stream info");
        return nullptr;
    }

    int video_index = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = i;
            break;
        }
    }

    if (video_index == -1) {
        log_error("No video stream found");
        return nullptr;
    }

    const AVCodec *decoder = avcodec_find_decoder(fmt_ctx->streams[video_index]->codecpar->codec_id);
    AVCodecContext *decoder_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decoder_ctx, fmt_ctx->streams[video_index]->codecpar);
    avcodec_open2(decoder_ctx, decoder, nullptr);

    const AVCodec *jpeg_encoder = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    AVCodecContext *encoder_ctx = avcodec_alloc_context3(jpeg_encoder);
    encoder_ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    encoder_ctx->height = decoder_ctx->height;
    encoder_ctx->width = decoder_ctx->width;
    encoder_ctx->time_base = {1, 25};
    avcodec_open2(encoder_ctx, jpeg_encoder, nullptr);

    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    AVPacket *jpg_pkt = av_packet_alloc();
    struct SwsContext *sws_ctx = sws_getContext(
        decoder_ctx->width, decoder_ctx->height, decoder_ctx->pix_fmt,
        encoder_ctx->width, encoder_ctx->height, encoder_ctx->pix_fmt,
        SWS_BICUBIC, nullptr, nullptr, nullptr
    );

    AVFrame *yuv_frame = av_frame_alloc();
    yuv_frame->format = encoder_ctx->pix_fmt;
    yuv_frame->width = encoder_ctx->width;
    yuv_frame->height = encoder_ctx->height;
    av_frame_get_buffer(yuv_frame, 32);

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index != video_index) {
            av_packet_unref(pkt);
            continue;
        }

        if (avcodec_send_packet(decoder_ctx, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }

        while (avcodec_receive_frame(decoder_ctx, frame) == 0) {
            sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height, yuv_frame->data, yuv_frame->linesize);

            if (avcodec_send_frame(encoder_ctx, yuv_frame) < 0) continue;
            if (avcodec_receive_packet(encoder_ctx, jpg_pkt) < 0) continue;

            pthread_mutex_lock(&client_mutex);
            for (int i = 0; i < client_count; ++i) {
                char header[512];
                snprintf(header, sizeof(header),
                         "HTTP/1.0 200 OK\r\n"
                         "Server: MJPEGStreamer\r\n"
                         "Content-Type: multipart/x-mixed-replace;boundary=" BOUNDARY "\r\n\r\n");
                send(clients[i], header, strlen(header), MSG_NOSIGNAL);

                snprintf(header, sizeof(header),
                         "--" BOUNDARY "\r\n"
                         "Content-Type: image/jpeg\r\n"
                         "Content-Length: %d\r\n\r\n",
                         jpg_pkt->size);
                send(clients[i], header, strlen(header), MSG_NOSIGNAL);
                send(clients[i], jpg_pkt->data, jpg_pkt->size, MSG_NOSIGNAL);
                send(clients[i], "\r\n", 2, MSG_NOSIGNAL);
            }
            pthread_mutex_unlock(&client_mutex);

            av_packet_unref(jpg_pkt);
            usleep(1000000 / 25); // 25 FPS
        }

        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_frame_free(&yuv_frame);
    av_packet_free(&pkt);
    av_packet_free(&jpg_pkt);
    avcodec_free_context(&decoder_ctx);
    avcodec_free_context(&encoder_ctx);
    avformat_close_input(&fmt_ctx);
    return nullptr;
}
