#include "ffmpegtool.h"
#include "mainwindow.h"
#include <QDebug>

extern "C"
{
#include "libavformat/avformat.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/timestamp.h"
}

typedef struct InputStream {
    AVStream *st;

    AVCodecContext *dec_ctx;
} InputStream;

typedef struct InputFile {
    AVFormatContext *fmt_ctx;

    InputStream *streams;
    int       nb_streams;
} InputFile;

typedef struct ReadInterval {
    int id;             ///< identifier
    int64_t start, end; ///< start, end in second/AV_TIME_BASE units
    int has_start, has_end;
    int start_is_offset, end_is_offset;
    int duration_frames;
} ReadInterval;

AVDictionary *format_opts, *codec_opts;
static AVInputFormat *iformat = NULL;
static int find_stream_info  = 1;
static int do_show_log = 0;
static int nb_streams;
static uint64_t *nb_streams_packets;
static uint64_t *nb_streams_frames;
static int *selected_streams;
typedef struct WriterContext WriterContext;

#define REALLOCZ_ARRAY_STREAM(ptr, cur_n, new_n)                        \
{                                                                       \
    ret = av_reallocp_array(&(ptr), (new_n), sizeof(*(ptr)));           \
    if (ret < 0)                                                        \
        goto end;                                                       \
    memset( (ptr) + (cur_n), 0, ((new_n) - (cur_n)) * sizeof(*(ptr)) ); \
}


ffmpegTool::ffmpegTool()
{

}

int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec)
{
    int ret = avformat_match_stream_specifier(s, st, spec);
    if (ret < 0)
        av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
    return ret;
}

AVDictionary *filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id,
                                AVFormatContext *s, AVStream *st, AVCodec *codec)
{
    AVDictionary    *ret = NULL;
    AVDictionaryEntry *t = NULL;
    int            flags = s->oformat ? AV_OPT_FLAG_ENCODING_PARAM
                                      : AV_OPT_FLAG_DECODING_PARAM;
    char          prefix = 0;
    const AVClass    *cc = avcodec_get_class();

    if (!codec)
        codec            = s->oformat ? avcodec_find_encoder(codec_id)
                                      : avcodec_find_decoder(codec_id);

    switch (st->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        prefix  = 'v';
        flags  |= AV_OPT_FLAG_VIDEO_PARAM;
        break;
    case AVMEDIA_TYPE_AUDIO:
        prefix  = 'a';
        flags  |= AV_OPT_FLAG_AUDIO_PARAM;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        prefix  = 's';
        flags  |= AV_OPT_FLAG_SUBTITLE_PARAM;
        break;
    }

    while (t = av_dict_get(opts, "", t, AV_DICT_IGNORE_SUFFIX)) {
        char *p = strchr(t->key, ':');

        /* check stream specification in opt name */
        if (p)
            switch (check_stream_specifier(s, st, p + 1)) {
            case  1: *p = 0; break;
            case  0:         continue;
            default:         exit(1);
            }

        if (av_opt_find(&cc, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ) ||
            !codec ||
            (codec->priv_class &&
             av_opt_find(&codec->priv_class, t->key, NULL, flags,
                         AV_OPT_SEARCH_FAKE_OBJ)))
            av_dict_set(&ret, t->key, t->value, 0);
        else if (t->key[0] == prefix &&
                 av_opt_find(&cc, t->key + 1, NULL, flags,
                             AV_OPT_SEARCH_FAKE_OBJ))
            av_dict_set(&ret, t->key + 1, t->value, 0);

        if (p)
            *p = ':';
    }
    return ret;
}

AVDictionary **setup_find_stream_info_opts(AVFormatContext *s,
                                           AVDictionary *codec_opts)
{
    unsigned int i;
    AVDictionary **opts;

    if (!s->nb_streams)
        return NULL;
    opts = (AVDictionary **)av_mallocz_array(s->nb_streams, sizeof(*opts));
    if (!opts) {
        av_log(NULL, AV_LOG_ERROR,
               "Could not alloc memory for stream options.\n");
        return NULL;
    }
    for (i = 0; i < s->nb_streams; i++)
        opts[i] = filter_codec_opts(codec_opts, s->streams[i]->codecpar->codec_id,
                                    s, s->streams[i], NULL);
    return opts;
}

static int open_input_file(InputFile *ifile, const char *filename)
{
    int err, i;
    AVFormatContext *fmt_ctx = NULL;
    AVDictionaryEntry *t;
    int scan_all_pmts_set = 0;

    fmt_ctx = avformat_alloc_context();
    if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }
    if ((err = avformat_open_input(&fmt_ctx, filename,
                                   iformat, &format_opts)) < 0) {
        return err;
    }
    ifile->fmt_ctx = fmt_ctx;
    if (scan_all_pmts_set)
        av_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);
    if ((t = av_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        return AVERROR_OPTION_NOT_FOUND;
    }

    if (find_stream_info) {
        AVDictionary **opts = setup_find_stream_info_opts(fmt_ctx, codec_opts);
        int orig_nb_streams = fmt_ctx->nb_streams;

        err = avformat_find_stream_info(fmt_ctx, opts);

        for (i = 0; i < orig_nb_streams; i++)
            av_dict_free(&opts[i]);
        av_freep(&opts);

        if (err < 0) {
            return err;
        }
    }

    av_dump_format(fmt_ctx, 0, filename, 0);

    ifile->streams = (InputStream *)av_mallocz_array(fmt_ctx->nb_streams,
                                      sizeof(*ifile->streams));
    if (!ifile->streams)
        exit(1);
    ifile->nb_streams = fmt_ctx->nb_streams;

    /* bind a decoder to each input stream */
    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        InputStream *ist = &ifile->streams[i];
        AVStream *stream = fmt_ctx->streams[i];
        AVCodec *codec;

        ist->st = stream;

        if (stream->codecpar->codec_id == AV_CODEC_ID_PROBE) {
            av_log(NULL, AV_LOG_WARNING,
                   "Failed to probe codec for input stream %d\n",
                    stream->index);
            continue;
        }

        codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            av_log(NULL, AV_LOG_WARNING,
                    "Unsupported codec with id %d for input stream %d\n",
                    stream->codecpar->codec_id, stream->index);
            continue;
        }
        {
            AVDictionary *opts = filter_codec_opts(codec_opts, stream->codecpar->codec_id,
                                                   fmt_ctx, stream, codec);

            ist->dec_ctx = avcodec_alloc_context3(codec);
            if (!ist->dec_ctx)
                exit(1);

            err = avcodec_parameters_to_context(ist->dec_ctx, stream->codecpar);
            if (err < 0)
                exit(1);

            if (do_show_log) {
                // For loging it is needed to disable at least frame threads as otherwise
                // the log information would need to be reordered and matches up to contexts and frames
                // That is in fact possible but not trivial
                av_dict_set(&codec_opts, "threads", "1", 0);
            }

            ist->dec_ctx->pkt_timebase = stream->time_base;
            ist->dec_ctx->framerate = stream->avg_frame_rate;
#if FF_API_LAVF_AVCTX
            ist->dec_ctx->coded_width = stream->codec->coded_width;
            ist->dec_ctx->coded_height = stream->codec->coded_height;
#endif

            if (avcodec_open2(ist->dec_ctx, codec, &opts) < 0) {
                av_log(NULL, AV_LOG_WARNING, "Could not open codec for input stream %d\n",
                       stream->index);
                exit(1);
            }

            if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
                av_log(NULL, AV_LOG_ERROR, "Option %s for input stream %d not found\n",
                       t->key, stream->index);
                return AVERROR_OPTION_NOT_FOUND;
            }
        }
    }

    ifile->fmt_ctx = fmt_ctx;
    return 0;
}

static av_always_inline int process_frame(InputFile *ifile,
                                          AVFrame *frame, AVPacket *pkt,
                                          int *packet_new,
                                          vector<StreamFrames> *PacketList)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    AVCodecContext *dec_ctx = ifile->streams[pkt->stream_index].dec_ctx;
    AVCodecParameters *par = ifile->streams[pkt->stream_index].st->codecpar;
    AVSubtitle sub;
    int ret = 0, got_frame = 0;

    if (dec_ctx && dec_ctx->codec) {
        switch (par->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
        case AVMEDIA_TYPE_AUDIO:
            if (*packet_new) {
                ret = avcodec_send_packet(dec_ctx, pkt);
                if (ret == AVERROR(EAGAIN)) {
                    ret = 0;
                } else if (ret >= 0 || ret == AVERROR_EOF) {
                    ret = 0;
                    *packet_new = 0;
                }
            }
            if (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret >= 0) {
                    got_frame = 1;
                } else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    ret = 0;
                }
            }
            break;

        case AVMEDIA_TYPE_SUBTITLE:
            if (*packet_new)
                ret = avcodec_decode_subtitle2(dec_ctx, &sub, &got_frame, pkt);
            *packet_new = 0;
            break;
        default:
            *packet_new = 0;
        }
    } else {
        *packet_new = 0;
    }

    if (ret < 0)
        return ret;
    if (got_frame) {
        int is_sub = (par->codec_type == AVMEDIA_TYPE_SUBTITLE);
        nb_streams_frames[pkt->stream_index]++;

        StreamFrames tmp;
        tmp.pkt_size = frame->pkt_size;
        tmp.pict_type = av_get_picture_type_char(frame->pict_type);
        PacketList->push_back(tmp);
        if (is_sub)
            avsubtitle_free(&sub);

    }
    return got_frame || *packet_new;
}

static void read_interval_packets(InputFile *ifile,const ReadInterval *interval,
                                 int64_t *cur_ts, vector<StreamFrames> *PacketList)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    AVPacket pkt;
    AVFrame *frame = NULL;
    int ret = 0, i = 0, frame_count = 0;
    int64_t start = -INT64_MAX, end = interval->end;
    int has_start = 0, has_end = interval->has_end && !interval->end_is_offset;

    av_init_packet(&pkt);

    av_log(NULL, AV_LOG_VERBOSE, "Processing read interval ");

    if (interval->has_start) {
        int64_t target;
        if (interval->start_is_offset) {
            if (*cur_ts == AV_NOPTS_VALUE) {
                av_log(NULL, AV_LOG_ERROR,
                       "Could not seek to relative position since current "
                       "timestamp is not defined\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
            target = *cur_ts + interval->start;
        } else {
            target = interval->start;
        }

        if ((ret = avformat_seek_file(fmt_ctx, -1, -INT64_MAX, target, INT64_MAX, 0)) < 0) {
            goto end;
        }
    }

    frame = av_frame_alloc();
    if (!frame) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    while (!av_read_frame(fmt_ctx, &pkt)) {
        if (fmt_ctx->nb_streams > nb_streams) {
            REALLOCZ_ARRAY_STREAM(nb_streams_frames,  nb_streams, fmt_ctx->nb_streams);
            REALLOCZ_ARRAY_STREAM(nb_streams_packets, nb_streams, fmt_ctx->nb_streams);
            REALLOCZ_ARRAY_STREAM(selected_streams,   nb_streams, fmt_ctx->nb_streams);
            nb_streams = fmt_ctx->nb_streams;

        }


        AVRational tb = ifile->streams[pkt.stream_index].st->time_base;

        if (pkt.pts != AV_NOPTS_VALUE)
            *cur_ts = av_rescale_q(pkt.pts, tb, AV_TIME_BASE_Q);

        if (!has_start && *cur_ts != AV_NOPTS_VALUE) {
            start = *cur_ts;
            has_start = 1;
        }

        if (has_start && !has_end && interval->end_is_offset) {
            end = start + interval->end;
            has_end = 1;
        }

        if (interval->end_is_offset && interval->duration_frames) {
            if (frame_count >= interval->end)
                break;
        } else if (has_end && *cur_ts != AV_NOPTS_VALUE && *cur_ts >= end) {
            break;
        }
        int packet_new = 1;
        while (process_frame(ifile, frame, &pkt, &packet_new, PacketList) > 0);

        av_packet_unref(&pkt);
    }
    av_packet_unref(&pkt);

end:
    av_frame_free(&frame);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not read packets in interval ");
    }
}



bool ffmpegTool::probe_get_stream_info(StreamInfo *info, vector<StreamFrames> *PacketList, QString filename)
{
    InputFile ifile;

    AVCodecParameters *par;
    AVCodecContext *dec_ctx;

    const AVCodecDescriptor *cd = nullptr;
    const char *s = nullptr;
    ReadInterval interval = (ReadInterval) { .has_start = 0, .has_end = 0 };

    open_input_file(&ifile, filename.toLatin1());
    AVStream *stream = ifile.streams->st;
    AVFormatContext *fmt_ctx = ifile.fmt_ctx;
    int64_t cur_ts = fmt_ctx->start_time;
    par     = stream->codecpar;
    dec_ctx = ifile.streams->dec_ctx;
    cd = avcodec_descriptor_get(par->codec_id);
    if (cd != nullptr) info->codec_name = cd->name;
    else info->codec_name = "unknown";
    switch (par->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            info->width = par->width;
            info->height = par->height;

            s = av_get_pix_fmt_name((AVPixelFormat)par->format);
            if (s) info->pix_fmt = s;
            else   info->pix_fmt = "unknown";
            info->level = par->level;

            info->fps = stream->r_frame_rate.num / stream->r_frame_rate.den ;
    }
    read_interval_packets(&ifile, &interval, &cur_ts, PacketList);
    info->frame_count = PacketList->size();
    unsigned int index = 0;
    unsigned int gopsize_tmp_flag = 0;
    for(vector<StreamFrames>::iterator it=PacketList->begin();it!=PacketList->end();it++) {
        // get gop size
        if (it->pict_type == 'I' && index != 0 && gopsize_tmp_flag == 0) {
            info->gop_size = index;
            gopsize_tmp_flag++;
        }
        it->frame_index = index;

        it->bitrate = it->pkt_size * 8 * info->fps / 1000;
        // max bitrate and min bitrate
        if (index == 0) {
            info->max_bitrate = it->bitrate;
            info->min_bitrate = it->bitrate;
        }
        if (info->max_bitrate < it->bitrate) info->max_bitrate = it->bitrate;
        if (info->min_bitrate > it->bitrate) info->min_bitrate = it->bitrate;
        index++;
    }

    return true;
}
