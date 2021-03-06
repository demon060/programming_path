/*
 * jpg_crop.c
 *
 *  Created on: 2019年6月11日
 *      Author: cc
 */
#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libavcodec/avcodec.h"
#include "libavfilter/avfilter.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libswscale/swscale.h"

int file_read(const char* path, void** data, int* dlen) {
	int fd = 0;
	fd = open(path, O_RDONLY, S_IRUSR);
	if (fd < 0) {
		perror("file not open!");
		return -1;
	}

	int len = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);

	printf("fd=%d,len=%d\n", fd, len);

	*dlen = len;
	*data = malloc(len);
	read(fd, *data, len);
	close(fd);
	return 0;
}

void av_frame_save_jpg(const char *filename, AVFrame *frame) {
	AVCodec* pCodec;
	AVCodecContext* pCodecCtx;

	pCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
	pCodecCtx = avcodec_alloc_context3(pCodec);

	pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	pCodecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P; //pFrame->format; ///AV_PIX_FMT_YUVJ420P
	pCodecCtx->width = frame->width;
	pCodecCtx->height = frame->height;
	pCodecCtx->time_base.num = 1;
	pCodecCtx->time_base.den = 25;

	avcodec_open2(pCodecCtx, pCodec, NULL);

	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;

	avcodec_send_frame(pCodecCtx, frame);

	while (1) {
		if (avcodec_receive_packet(pCodecCtx, &pkt) == AVERROR(EAGAIN)) {
			break;
		}

		{
			FILE* pFile;
			pFile = fopen(filename, "wb");
			fwrite(pkt.data, 1, pkt.size, pFile);
			fclose(pFile);
		}
	}
	av_packet_unref(&pkt);
	avcodec_close(pCodecCtx);
	avcodec_free_context(&pCodecCtx);
}

//nv12裁剪成yuv420p格式
//@cropx cropy 裁剪起点，必须为偶数，否则UV和Y会有偏差
//@dscw dsch 裁剪出来的照片宽高，必须为偶数，否则UV和Y会有偏差
//注意点，linesize的对其为1，如果目标为ffmpeg的avframe，必须加linesize，NV12数据格式如 4*4
//Y  Y  Y  Y
//Y  Y  Y  Y
//Y  Y  Y  Y
//Y  Y  Y  Y
//U  V  U  V
//U  V  U  V
int image_crop_nv12to420p(uint8_t *nv12data, int width, int height, int cropx, int cropy,
		uint8_t *yuv420pdata, int dscw, int dsch) {
	uint8_t *ptry = yuv420pdata, *ptru, *ptrv;
	uint8_t *nvptr = nv12data + width * height;

	ptry = yuv420pdata;
	ptru = yuv420pdata + dscw * dsch; //u
	ptrv = ptru + (dscw * dsch) / 4; //v (420P的U分量1字节对其为(w*h)/4)

	for (int y = cropy; y < dsch + cropy /*pFrame->height*/; y++) {
		memcpy(ptry, nv12data + y * width + cropx, dscw);
		ptry += dscw;
	}

	for (int nvy = cropy / 2; nvy < dsch / 2 + cropy / 2 /*height / 2*/; nvy++) {
		for (int nvx = cropx; nvx < dscw + cropx; nvx++) {
			//如果目标是ffmpeg的avframe则需要注意的是linesize
			if (nvx % 2 == 0) {
				*ptru++ = nvptr[nvy * width + nvx]; //u
			} else {
				*ptrv++ = nvptr[nvy * width + nvx]; //v
			}
		}
	}
	return 0;
}

/**
 * x y dscFrame->width dscFrame->height is %2==0
 */
int image_crop_yuv420p(AVFrame *srcFrame, AVFrame *dscFrame, int x, int y) {
	int yuv_y = 0;
	int yuv_uv = 0;
	void *ptr_y, *ptr_u, *ptr_v;

	ptr_y = dscFrame->data[0];

	for (yuv_y = y; yuv_y < y + dscFrame->height; yuv_y++) {
		memcpy(ptr_y, srcFrame->data[0] + yuv_y * srcFrame->linesize[0] + x, dscFrame->width);

		ptr_y += dscFrame->linesize[0];
	}

	ptr_u = dscFrame->data[1];
	ptr_v = dscFrame->data[2];

	for (yuv_uv = y / 2; yuv_uv < (y / 2 + dscFrame->height / 2); yuv_uv++) {
		memcpy(ptr_u, srcFrame->data[1] + yuv_uv * srcFrame->linesize[1] + x / 2,
				dscFrame->width / 2);

		memcpy(ptr_v, srcFrame->data[2] + yuv_uv * srcFrame->linesize[2] + x / 2,
				dscFrame->width / 2);

		ptr_u += dscFrame->linesize[1];
		ptr_v += dscFrame->linesize[2];
	}
	return 0;
}

void jpg_decode_data(void *data, int size) {
	AVCodec* pCodec;
	AVCodecContext* pCodecCtx;
	AVFrame* pAvFrame;
	int ret = 0;

	pCodec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
	pCodecCtx = avcodec_alloc_context3(pCodec);
	avcodec_open2(pCodecCtx, pCodec, NULL);

	pAvFrame = av_frame_alloc();
	{
		AVPacket picPacket;
		av_init_packet(&picPacket);
		picPacket.data = (uint8_t*) data;
		picPacket.size = size;

		ret = avcodec_send_packet(pCodecCtx, &picPacket);
		ret = avcodec_receive_frame(pCodecCtx, pAvFrame);
		printf("decode=%s WH:%dx%d\n",
				av_pix_fmt_desc_get((enum AVPixelFormat) pAvFrame->format)->name, pAvFrame->width,
				pAvFrame->height); //解码
		//
		{
			AVFrame *dscFrame = av_frame_alloc();
			dscFrame->format = AV_PIX_FMT_YUVJ420P;
			dscFrame->width = 900;
			dscFrame->height = 200;

			ret = av_frame_get_buffer(dscFrame, 32);
			printf("ret=%d\n", ret);

			ret = av_frame_make_writable(dscFrame);
			printf("ret=%d\n", ret);

			av_frame_save_jpg("/tmp/0.jpg", pAvFrame);

			image_crop_yuv420p(pAvFrame, dscFrame, 10, 600);

			printf("start save...\n");
			av_frame_save_jpg("/tmp/crop-0.jpg", dscFrame);
			av_frame_free(&dscFrame);
		}
	}
	avcodec_free_context(&pCodecCtx);
}

int main(int argc, char **argv) {
	void *data;
	int len;
	int ret = 0;

	ret = file_read("/home/cc/下载/aa.jpg", &data, &len);
	if (ret) {
		perror("open file err\n");
		return -1;
	}
	//avcodec_register_all();

	jpg_decode_data(data, len);
	return 0;
}

