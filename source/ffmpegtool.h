#ifndef FFMPEGTOOL_H
#define FFMPEGTOOL_H

#include <QString>
#include <vector>

using namespace std;

typedef struct StreamInfo {
    const char * codec_name;
    const char *  pix_fmt;
    int level;
    unsigned int frame_count;
    unsigned int width;
    unsigned int height;
    unsigned int gop_size;
    float max_bitrate;
    float min_bitrate;
    float fps;
} StreamInfo;

typedef struct StreamFrames {
    unsigned int frame_index;
    unsigned int pkt_size;
    float bitrate;
    char pict_type;
} StreamFrames;

class ffmpegTool
{
public:
    ffmpegTool();
    ~ffmpegTool();

    static bool probe_get_stream_info(StreamInfo *info, vector<StreamFrames> *PacketList, QString filename);
private:


};

#endif // FFMPEGTOOL_H
