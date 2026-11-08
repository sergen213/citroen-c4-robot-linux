#include "MenuVideo.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace c4::ui {
namespace {

[[noreturn]] void invalid(const char* message) {
    throw std::runtime_error(std::string("menu video: ") + message);
}

void require_av(int result, const char* operation) {
    if (result >= 0) return;
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(result, message, sizeof(message));
    throw std::runtime_error(std::string("menu video: ") + operation + ": " + message);
}

} // namespace

struct MenuVideo::Impl {
    AVFormatContext* format{};
    AVCodecContext* codec{};
    AVPacket* packet{};
    AVFrame* current{};
    AVFrame* next{};
    SwsContext* scaler{};
    int stream_index{};
    std::uint32_t width{}, height{};
    std::size_t byte_count{};
    AVRational time_base{};
    std::int64_t origin{}, current_pts{}, next_pts{}, decoded_pts{};
    std::int64_t uploaded_pts{AV_NOPTS_VALUE};
    double duration{}, cycle{}, last_elapsed{-1};
    bool draining{}, eof{}, have_next{}, have_decoded{};
    bool destination_initialized{};
    assets::Image* output{};

    ~Impl() {
        sws_freeContext(scaler);
        av_frame_free(&next);
        av_frame_free(&current);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }

    void open(const std::filesystem::path& path) {
        require_av(avformat_open_input(&format, path.c_str(), nullptr, nullptr), "open source");
        require_av(avformat_find_stream_info(format, nullptr), "read stream information");
        const AVCodec* decoder{};
        stream_index = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
        require_av(stream_index, "find supported video stream");
        const auto* stream = format->streams[stream_index];
        time_base = stream->time_base;
        if (time_base.num <= 0 || time_base.den <= 0) invalid("invalid presentation time base");
        if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0)
            duration = stream->duration * av_q2d(time_base);
        else if (format->duration != AV_NOPTS_VALUE && format->duration > 0)
            duration = double(format->duration) / AV_TIME_BASE;
        if (!std::isfinite(duration) || duration <= 0) invalid("source has no finite loop duration");

        codec = avcodec_alloc_context3(decoder);
        if (!codec) invalid("cannot allocate video decoder");
        require_av(avcodec_parameters_to_context(codec, stream->codecpar), "read codec parameters");
        codec->thread_count = 1;
        codec->err_recognition = AV_EF_CRCCHECK | AV_EF_EXPLODE;
        codec->max_pixels = 64ll * 1024 * 1024;
        require_av(avcodec_open2(codec, decoder, nullptr), "open video decoder");
        packet = av_packet_alloc();
        current = av_frame_alloc();
        next = av_frame_alloc();
        if (!packet || !current || !next) invalid("cannot allocate video frame or packet");
        if (!decode(current)) invalid("source contains no decoded video frames");
        if (current->width <= 0 || current->height <= 0 || current->width > 16384 ||
            current->height > 16384 ||
            std::uint64_t(current->width) * current->height > 64ull * 1024 * 1024)
            invalid("video dimensions exceed native image limits");
        width = static_cast<std::uint32_t>(current->width);
        height = static_cast<std::uint32_t>(current->height);
        const auto bytes = std::uint64_t(width) * height * 4;
        if (bytes > std::numeric_limits<std::size_t>::max()) invalid("RGBA image is too large");
        byte_count = static_cast<std::size_t>(bytes);
        origin = current_pts = current->best_effort_timestamp;
    }

    // Receive before sending: an EAGAIN packet is never discarded, and EOF
    // sends one null packet so delayed presentation frames are drained.
    bool decode(AVFrame* frame) {
        av_frame_unref(frame);
        if (eof) return false;
        for (;;) {
            const int received = avcodec_receive_frame(codec, frame);
            if (received == 0) {
                if ((frame->flags & AV_FRAME_FLAG_CORRUPT) || frame->decode_error_flags)
                    invalid("corrupt decoded video frame");
                const auto pts = frame->best_effort_timestamp;
                if (pts == AV_NOPTS_VALUE) invalid("video frame has no presentation timestamp");
                if (have_decoded && pts < decoded_pts) invalid("video presentation timestamps go backwards");
                decoded_pts = pts;
                have_decoded = true;
                return true;
            }
            if (received == AVERROR_EOF) {
                eof = true;
                return false;
            }
            if (received != AVERROR(EAGAIN)) require_av(received, "receive video frame");
            if (draining) invalid("decoder requests packets after end-of-source drain");

            int read;
            do {
                av_packet_unref(packet);
                read = av_read_frame(format, packet);
                if (read == AVERROR_EOF) break;
                require_av(read, "read video packet");
            } while (packet->stream_index != stream_index);
            if (read == AVERROR_EOF) {
                av_packet_unref(packet);
                require_av(avcodec_send_packet(codec, nullptr), "drain video decoder");
                draining = true;
            } else {
                if (packet->flags & AV_PKT_FLAG_CORRUPT) invalid("corrupt source video packet");
                // libavcodec forbids send and receive both returning EAGAIN.
                // Sending only after receive EAGAIN preserves the packet until
                // it is accepted; a violation is an explicit decoder error.
                require_av(avcodec_send_packet(codec, packet), "send video packet");
                av_packet_unref(packet);
            }
        }
    }

    double seconds(std::int64_t pts) const {
        return (double(pts) - double(origin)) * av_q2d(time_base);
    }

    void restart(double target) {
        const double timestamp = double(origin) + target / av_q2d(time_base);
        if (timestamp < double(std::numeric_limits<std::int64_t>::min()) ||
            timestamp >= double(std::numeric_limits<std::int64_t>::max()))
            invalid("seek timestamp exceeds source limits");
        require_av(av_seek_frame(format, stream_index, static_cast<std::int64_t>(timestamp),
                                 AVSEEK_FLAG_BACKWARD), "seek looping source");
        avcodec_flush_buffers(codec);
        av_packet_unref(packet);
        av_frame_unref(current);
        av_frame_unref(next);
        draining = eof = have_next = have_decoded = false;
        if (!decode(current)) invalid("loop seek produced no video frame");
        current_pts = current->best_effort_timestamp;
        if (seconds(current_pts) > target) invalid("loop seek skipped the requested presentation frame");
    }

    void restart() {
        restart(0);
        last_elapsed = -1;
        cycle = 0;
        uploaded_pts = AV_NOPTS_VALUE;
    }

    bool update(double elapsed, assets::Image& destination) {
        if (!std::isfinite(elapsed) || elapsed < 0 || elapsed < last_elapsed)
            invalid("elapsed time must be finite, nonnegative and monotonic");
        last_elapsed = elapsed;
        const double next_cycle = std::floor(elapsed / duration);
        const double target = std::fmod(elapsed, duration);
        if (next_cycle != cycle) {
            restart(target);
            cycle = next_cycle;
        }
        for (;;) {
            if (!have_next) {
                if (!decode(next)) break;
                next_pts = next->best_effort_timestamp;
                have_next = true;
            }
            if (seconds(next_pts) > target) break;
            std::swap(current, next);
            current_pts = next_pts;
            have_next = false;
        }
        if (current->width != static_cast<int>(width) || current->height != static_cast<int>(height))
            invalid("video changes dimensions during playback");
        if (!destination_initialized) {
            if (!destination.width && !destination.height && destination.rgba.empty()) {
                destination.width = width;
                destination.height = height;
                destination.rgba.resize(byte_count);
            }
            output = &destination;
            destination_initialized = true;
        }
        if (output != &destination || destination.width != width || destination.height != height ||
            destination.rgba.size() != byte_count)
            invalid("destination must retain its native-size RGBA image allocation");
        if (uploaded_pts == current_pts) return false;
        scaler = sws_getCachedContext(scaler, current->width, current->height,
                                     static_cast<AVPixelFormat>(current->format),
                                     current->width, current->height, AV_PIX_FMT_RGBA,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scaler) invalid("unsupported video pixel format or RGBA conversion");
        std::uint8_t* planes[]{destination.rgba.data(), nullptr, nullptr, nullptr};
        const int strides[]{static_cast<int>(width * 4), 0, 0, 0};
        const int rows = sws_scale(scaler, current->data, current->linesize, 0,
                                   current->height, planes, strides);
        if (rows != current->height) invalid("incomplete RGBA frame conversion");
        uploaded_pts = current_pts;
        return true;
    }
};

MenuVideo::MenuVideo(const std::filesystem::path& path) : impl_(std::make_unique<Impl>()) {
    impl_->open(path);
}
MenuVideo::~MenuVideo() = default;
std::uint32_t MenuVideo::width() const noexcept { return impl_->width; }
std::uint32_t MenuVideo::height() const noexcept { return impl_->height; }
void MenuVideo::restart() { impl_->restart(); }
bool MenuVideo::update(double elapsed_seconds, assets::Image& destination) {
    return impl_->update(elapsed_seconds, destination);
}

} // namespace c4::ui
