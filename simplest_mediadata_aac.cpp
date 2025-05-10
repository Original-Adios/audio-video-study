/**
 * 本项目包含如下几种视音频测试示例：
 *  (1)像素数据处理程序。包含RGB和YUV像素格式处理的函数。
 *  (2)音频采样数据处理程序。包含PCM音频采样格式处理的函数。
 *  (3)H.264码流分析程序。可以分离并解析NALU。
 *  (4)AAC码流分析程序。可以分离并解析ADTS帧。
 *  (5)FLV封装格式分析程序。可以将FLV中的MP3音频码流分离出来。
 *  (6)UDP-RTP协议分析程序。可以将分析UDP/RTP/MPEG-TS数据包。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * 提取 ADTS 一帧数据
 * buffer: 输入的缓冲区
 * buf_size: 缓冲区大小
 * data: 输出帧数据
 * data_size: 输出帧大小
 * 返回值：
 *   0 表示成功提取一帧
 *   1 表示数据不足，需要更多数据
 *  -1 表示错误
 */
int getADTSframe(unsigned char* buffer, int buf_size, unsigned char* data ,int* data_size)
{
    int size = 0;

    if(!buffer || !data || !data_size){
        return -1;  // 空指针检测
    }

    while(1){
        // 至少要 7 字节才可能构成一个 ADTS 帧头
        if(buf_size < 7){
            return -1;
        }

        // 检测同步字 0xFFF（12 位），buffer[0]=0xFF，buffer[1]高4位=0xF
        if((buffer[0] == 0xff) && ((buffer[1] & 0xf0) == 0xf0)){
            // 帧长度由 buffer[3]~buffer[5] 组成，总共13位
            size |= ((buffer[3] & 0x03) << 11);      // 第 3 字节低2位：高位
            size |= (buffer[4] << 3);                // 第 4 字节：中间8位
            size |= ((buffer[5] & 0xe0) >> 5);       // 第 5 字节高3位：低位
            break;
        }

        // 未找到同步字，继续向后找
        --buf_size;
        ++buffer;
    }

    if(buf_size < size){
        return 1;  // 剩余数据不足一帧，留待下次处理
    }

    // 复制完整帧数据
    memcpy(data, buffer, size);
    *data_size = size;

    return 0;
}

/**
 * 最简单的 AAC 解析器：逐帧读取并输出帧信息
 */
int simplest_aac_parser(char *url)
{
    int data_size = 0;
    int size = 0;
    int cnt = 0;       // 帧计数
    int offset = 0;    // 残留数据大小

    FILE *myout = stdout;  // 输出到终端

    // 用于存储每一帧的数据
    unsigned char *aacframe = (unsigned char *)malloc(1024*5);
    // 缓冲区：最大 1MB
    unsigned char *aacbuffer = (unsigned char *)malloc(1024*1024);

    FILE *ifile = fopen(url, "rb");
    if(!ifile){
        printf("Open file error\n");
        return -1;
    }

    // 打印表头
    printf("-----+- ADTS Frame Table -+------+\n");
    printf(" NUM | Profile | Frequency| Size |\n");
    printf("-----+---------+----------+------+\n");

    while(!feof(ifile)){
        // 读入数据到缓冲区（注意保留上次 offset 的残留数据）
        data_size = fread(aacbuffer + offset, 1, 1024*1024 - offset, ifile);
        if(data_size <= 0) break;

        data_size += offset;                  // 加上上次遗留的残留数据
        unsigned char* input_data = aacbuffer;

        while(1){
            int ret = getADTSframe(input_data, data_size, aacframe, &size);
            if(ret == -1){
                // 错误或无效数据，退出
                break;
            } else if(ret == 1){
                // 数据不完整，留下次继续处理
                memcpy(aacbuffer, input_data, data_size);
                offset = data_size;
                break;
            }

            // 解析 Profile
            unsigned char profile = (aacframe[2] & 0xC0) >> 6;
            const char* profile_str = "Unknown";
            switch(profile){
                case 0: profile_str = "Main"; break;
                case 1: profile_str = "LC"; break;
                case 2: profile_str = "SSR"; break;
                default: break;
            }

            // 解析采样率 index
            unsigned char sampling_frequency_index = (aacframe[2] & 0x3C) >> 2;
            const char* frequence_str = "Unknown";
            switch(sampling_frequency_index){
                case 0: frequence_str = "96000Hz"; break;
                case 1: frequence_str = "88200Hz"; break;
                case 2: frequence_str = "64000Hz"; break;
                case 3: frequence_str = "48000Hz"; break;
                case 4: frequence_str = "44100Hz"; break;
                case 5: frequence_str = "32000Hz"; break;
                case 6: frequence_str = "24000Hz"; break;
                case 7: frequence_str = "22050Hz"; break;
                case 8: frequence_str = "16000Hz"; break;
                case 9: frequence_str = "12000Hz"; break;
                case 10: frequence_str = "11025Hz"; break;
                case 11: frequence_str = "8000Hz"; break;
                default: break;
            }

            // 输出帧信息
            fprintf(myout, "%5d| %8s|  %8s| %5d|\n", cnt, profile_str, frequence_str, size);

            // 移动指针处理下一帧
            data_size -= size;
            input_data += size;
            cnt++;
        }
    }

    fclose(ifile);
    free(aacbuffer);
    free(aacframe);

    return 0;
}

int main()
{
    simplest_aac_parser((char*)"./nocturne.aac");
    return 0;
}
